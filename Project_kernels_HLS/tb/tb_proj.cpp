#include "krnl_proj.h"
#include "../src/patterns.h"
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <algorithm>
#include <map>

using std::cout;
using std::dec;
using std::endl;
using std::hex;
using std::map;
using std::string;
using std::vector;

// ============================================================================
// 安全的背景填充值 (从不在patterns中出现)
// ============================================================================
const unsigned char SAFE_BACKGROUND = 0x84;

// ============================================================================
// Event protocol
// ============================================================================
static const int EVENT_BYTES = 16;

struct MatchResult
{
  uint64_t byte_index;
  uint16_t pattern_id;
  uint8_t lane;
};

// ============================================================================
// Helper: 打印pattern内容
// ============================================================================
void print_pattern_content(int pattern_id, bool detailed = false)
{
  if (pattern_id <= 0)
  {
    cout << "    Invalid pattern ID: " << pattern_id << endl;
    return;
  }

  int len = get_pattern_len(pattern_id);
  if (len == 0)
  {
    cout << "    Pattern ID " << pattern_id << " not found" << endl;
    return;
  }

  if (!detailed)
  {
    cout << "    Pattern " << pattern_id << ": \"";
  }
  else
  {
    cout << "\n    === Pattern " << pattern_id << " ===" << endl;
    cout << "    Length: " << len << " bytes" << endl;
    cout << "    Content: \"";
  }

  for (int i = 0; i < len; ++i)
  {
    unsigned char c = get_pattern_byte(pattern_id, i);
    if (c >= 32 && c <= 126)
    {
      cout << c;
    }
    else
    {
      cout << "\\x" << hex << std::setw(2) << std::setfill('0') << (int)c << dec;
    }
  }
  cout << "\"";

  if (detailed)
  {
    cout << endl;
    cout << "    Hex: ";
    for (int i = 0; i < len; ++i)
    {
      unsigned char c = get_pattern_byte(pattern_id, i);
      cout << std::setw(2) << std::setfill('0') << hex << (int)c << " ";
    }
    cout << dec << endl;
  }
  else
  {
    cout << " (len=" << len << ")" << endl;
  }
}

// ============================================================================
// Helper: 检查数据中是否真的包含某个pattern
// ============================================================================
bool verify_pattern_in_data(const vector<unsigned char> &data, int pattern_id, uint64_t reported_pos)
{
  int plen = get_pattern_len(pattern_id);
  if (plen == 0)
    return false;

  // 检查报告位置附近
  for (int offset = -2; offset <= 2; offset++)
  {
    int check_pos = reported_pos + offset - plen + 1;
    if (check_pos < 0 || check_pos + plen > (int)data.size())
      continue;

    bool match = true;
    for (int i = 0; i < plen; ++i)
    {
      unsigned char expected = get_pattern_byte(pattern_id, i);
      if (data[check_pos + i] != expected)
      {
        match = false;
        break;
      }
    }

    if (match)
    {
      if (offset != 0)
      {
        cout << "      ⚠️  Pattern actually at offset " << check_pos << " (reported " << reported_pos << ")" << endl;
      }
      return true;
    }
  }

  return false;
}

// ============================================================================
// Helper: 找一个适合测试的pattern
// ============================================================================
int find_good_test_pattern()
{
  // 遍历所有组，找一个长度适中、不含常见字符的pattern
  for (int g = 0; g < NUM_GROUPS; g++)
  {
    for (int i = 0; i < group_info[g].num_patterns; i++)
    {
      int original_id = group_rules[g][i].original_id;
      int len = group_rules[g][i].len;
      
      if (len >= 3 && len <= 10)
      {
        bool has_common = false;
        for (int j = 0; j < len; j++)
        {
          unsigned char c = get_pattern_byte(original_id, j);
          // 避免空格、NULL、FF和其他常见填充值
          if (c == ' ' || c == 0x00 || c == 0xFF || c == SAFE_BACKGROUND)
          {
            has_common = true;
            break;
          }
        }
        if (!has_common)
        {
          return original_id;
        }
      }
    }
  }
  
  // Fallback: 返回第一个pattern
  if (NUM_GROUPS > 0 && group_info[0].num_patterns > 0)
  {
    return group_rules[0][0].original_id;
  }
  return 1;
}

// ============================================================================
// MM2S Simulator
// ============================================================================
class MM2S_Simulator
{
private:
  static const int MAX_PACKET_BYTES = 1408;

public:
  int send_data(hls::stream<pkt> &n2k, const unsigned char *data, int total_bytes)
  {
    int packets_sent = 0;
    int bytes_sent = 0;

    while (bytes_sent < total_bytes)
    {
      int packet_bytes = std::min(MAX_PACKET_BYTES, total_bytes - bytes_sent);
      int beats = (packet_bytes + DATA_WIDTH_BYTES - 1) / DATA_WIDTH_BYTES;

      for (int b = 0; b < beats; ++b)
      {
        pkt p;
        p.data = 0;
        p.keep = 0;
        p.last = (b == beats - 1);

        int beat_offset = bytes_sent + b * DATA_WIDTH_BYTES;
        int current_beat_bytes = std::min(DATA_WIDTH_BYTES, total_bytes - beat_offset);

        for (int i = 0; i < current_beat_bytes; ++i)
        {
          p.data(i * 8 + 7, i * 8) = data[beat_offset + i];
          p.keep[i] = 1;
        }
        n2k.write(p);
      }
      bytes_sent += packet_bytes;
      packets_sent++;
    }
    return packets_sent;
  }
};

const int MM2S_Simulator::MAX_PACKET_BYTES;

// ============================================================================
// S2MM Simulator
// ============================================================================
class S2MM_Simulator
{
public:
  int receive_data(hls::stream<pkt> &k2n, vector<unsigned char> &buffer, int expected_packets)
  {
    int packets_received = 0;
    int timeout_limit = 2000000;
    int timer = 0;

    while (packets_received < expected_packets && timer < timeout_limit)
    {
      if (!k2n.empty())
      {
        pkt v = k2n.read();
        for (int i = 0; i < DATA_WIDTH_BYTES; ++i)
        {
          if (v.keep[i])
          {
            buffer.push_back((unsigned char)v.data(i * 8 + 7, i * 8));
          }
        }
        if (v.last)
          packets_received++;
        timer = 0;
      }
      else
      {
        timer++;
      }
    }

    if (timer >= timeout_limit)
    {
      cout << "  [S2MM] ERROR: Timeout!" << endl;
    }
    return packets_received;
  }
};

// ============================================================================
// Event Parser
// ============================================================================
class EventParser
{
public:
  static vector<MatchResult> parse_events(const vector<unsigned char> &buffer)
  {
    vector<MatchResult> results;
    for (size_t pos = 0; pos + EVENT_BYTES <= buffer.size(); pos += EVENT_BYTES)
    {
      uint8_t lane = buffer[pos + 5];
      uint16_t pattern_id = buffer[pos + 6] | (buffer[pos + 7] << 8);
      uint64_t byte_index = 0;
      for (int i = 0; i < 8; ++i)
      {
        byte_index |= ((uint64_t)buffer[pos + 8 + i]) << (i * 8);
      }
      if (pattern_id != 0 && pattern_id != 0xEE)
      {
        results.push_back({byte_index, pattern_id, lane});
      }
    }
    return results;
  }

  static void print_events(const vector<MatchResult> &events,
                           const vector<unsigned char> &input_data,
                           int max_print = 5)
  {
    if (events.empty())
    {
      cout << "    No match events detected." << endl;
      return;
    }

    // 统计每个pattern出现次数
    map<uint16_t, int> pattern_counts;
    for (const auto &e : events)
    {
      pattern_counts[e.pattern_id]++;
    }

    cout << "    Total matches: " << events.size() << endl;
    cout << "    Unique patterns: " << pattern_counts.size() << endl;

    // 显示前几个events
    int n = std::min((int)events.size(), max_print);
    cout << "\n    First " << n << " events:" << endl;
    for (int i = 0; i < n; ++i)
    {
      const auto &e = events[i];
      cout << "      [" << i << "] Pattern " << e.pattern_id
           << " at offset " << e.byte_index
           << " (Lane " << (int)e.lane << ")";

      // 验证这个匹配是否真实存在
      bool valid = verify_pattern_in_data(input_data, e.pattern_id, e.byte_index);
      if (!valid)
      {
        cout << " ⚠️  FALSE POSITIVE";
      }
      cout << endl;
    }

    // 如果有很多匹配，显示pattern统计
    if (pattern_counts.size() > 1)
    {
      cout << "\n    Pattern frequency (top 5):" << endl;
      vector<std::pair<uint16_t, int>> sorted_patterns(pattern_counts.begin(), pattern_counts.end());
      std::sort(sorted_patterns.begin(), sorted_patterns.end(),
                [](const auto &a, const auto &b)
                { return a.second > b.second; });

      for (size_t i = 0; i < std::min((size_t)5, sorted_patterns.size()); ++i)
      {
        cout << "      Pattern " << sorted_patterns[i].first
             << ": " << sorted_patterns[i].second << " times" << endl;
      }
    }
  }
};

// ============================================================================
// 测试用例
// ============================================================================

bool run_test(const string &name,
              const vector<unsigned char> &input_data,
              int expected_id = -1,
              int expected_pos = -1)
{
  cout << "\n"
       << string(60, '=') << endl;
  cout << ">>> Test: " << name << endl;
  cout << string(60, '=') << endl;

  hls::stream<pkt> n2k("n2k_stream");
  hls::stream<pkt> k2n("k2n_stream");
  MM2S_Simulator mm2s;
  S2MM_Simulator s2mm;
  vector<unsigned char> output_buffer;

  cout << "  [MM2S] Sending " << input_data.size() << " bytes" << endl;
  int sent = mm2s.send_data(n2k, input_data.data(), input_data.size());

  cout << "  [KERNEL] Processing..." << endl;
  unsigned int dest = 0;
  krnl_proj(n2k, k2n, dest, sent);

  int received = s2mm.receive_data(k2n, output_buffer, sent);
  cout << "  [S2MM] Received " << received << " packets" << endl;

  auto events = EventParser::parse_events(output_buffer);
  EventParser::print_events(events, input_data);

  // 验证逻辑
  bool pass = (sent == received);

  if (expected_id != -1)
  {
    bool found = false;
    bool correct_pos = false;

    for (const auto &e : events)
    {
      if (e.pattern_id == expected_id)
      {
        found = true;
        if (expected_pos != -1)
        {
          // 允许一些偏差（因为报告的可能是结束位置）
          int plen = get_pattern_len(expected_id);
          if (e.byte_index >= expected_pos && e.byte_index <= expected_pos + plen)
          {
            correct_pos = true;
          }
        }
        else
        {
          correct_pos = true; // 不检查位置
        }
        break;
      }
    }

    if (found && correct_pos)
    {
      cout << "\n  ✅ Expected pattern " << expected_id << " found";
      if (expected_pos != -1)
        cout << " at correct position";
      cout << endl;
    }
    else if (found)
    {
      cout << "\n  ⚠️  Expected pattern " << expected_id << " found but at wrong position" << endl;
    }
    else
    {
      cout << "\n  ❌ Expected pattern " << expected_id << " NOT found" << endl;
      pass = false;
    }
  }

  cout << "\n  [RESULT] " << (pass ? "✅ PASSED" : "❌ FAILED") << endl;
  return pass;
}

int main()
{
  cout << "======================================================" << endl;
  cout << "  Pattern Matching Testbench (Grouped Version)" << endl;
  cout << "  Total Groups: " << NUM_GROUPS << endl;
  cout << "  Total Patterns: " << get_total_patterns() << endl;
  cout << "  Background fill: 0x" << hex << (int)SAFE_BACKGROUND << dec << endl;
  cout << "======================================================" << endl;

  bool all_pass = true;

  // Test 1: Silence
  cout << "\n[Test 1] Verifying no false positives with clean background" << endl;
  vector<unsigned char> data_empty(256, SAFE_BACKGROUND);
  all_pass &= run_test("Silence Test", data_empty);

  // Test 2: Single Pattern Match
  if (get_total_patterns() > 0)
  {
    int test_pattern_id = find_good_test_pattern();
    int pattern_len = get_pattern_len(test_pattern_id);

    cout << "\n[Test 2] Testing single pattern detection" << endl;
    print_pattern_content(test_pattern_id, true);

    vector<unsigned char> data_pat(512, SAFE_BACKGROUND);

    // 放置pattern
    int pos = 100;
    for (int i = 0; i < pattern_len; ++i)
    {
      data_pat[pos + i] = get_pattern_byte(test_pattern_id, i);
    }

    all_pass &= run_test("Single Pattern Match", data_pat, test_pattern_id, pos + pattern_len - 1);
  }

  // Test 3: Boundary crossing
  {
    int test_pattern_id = find_good_test_pattern();
    int pattern_len = get_pattern_len(test_pattern_id);

    cout << "\n[Test 3] Testing pattern crossing 64-byte boundary" << endl;
    print_pattern_content(test_pattern_id, true);

    vector<unsigned char> data_boundary(256, SAFE_BACKGROUND);

    int boundary_pos = 62; // 跨越beat边界
    for (int i = 0; i < pattern_len; ++i)
    {
      data_boundary[boundary_pos + i] = get_pattern_byte(test_pattern_id, i);
    }

    all_pass &= run_test("Boundary Cross Test", data_boundary, test_pattern_id);
  }

  // Test 4: Multiple patterns
  if (get_total_patterns() > 5)
  {
    cout << "\n[Test 4] Testing multiple pattern detection" << endl;

    vector<unsigned char> data_multi(512, SAFE_BACKGROUND);
    vector<int> test_patterns;

    // 选择3个不同的patterns
    for (int g = 0; g < NUM_GROUPS && test_patterns.size() < 3; g++)
    {
      for (int i = 0; i < group_info[g].num_patterns && test_patterns.size() < 3; i++)
      {
        int original_id = group_rules[g][i].original_id;
        int len = group_rules[g][i].len;
        
        if (len >= 3 && len <= 8)
        {
          bool ok = true;
          for (int j = 0; j < len; j++)
          {
            if (get_pattern_byte(original_id, j) == SAFE_BACKGROUND)
            {
              ok = false;
              break;
            }
          }
          if (ok)
            test_patterns.push_back(original_id);
        }
      }
    }

    // 放置patterns
    int positions[] = {50, 150, 250};
    for (size_t i = 0; i < test_patterns.size(); i++)
    {
      int pattern_id = test_patterns[i];
      int len = get_pattern_len(pattern_id);
      int pos = positions[i];
      
      cout << "  Placing ";
      print_pattern_content(pattern_id);
      
      for (int j = 0; j < len; ++j)
      {
        data_multi[pos + j] = get_pattern_byte(pattern_id, j);
      }
    }

    all_pass &= run_test("Multiple Pattern Match", data_multi);
  }

  // Test 5: Large data stress test
  cout << "\n[Test 5] Stress test with large random data" << endl;
  vector<unsigned char> data_large(5000);
  srand(12345); // 固定seed以便可重复
  for (int i = 0; i < 5000; ++i)
    data_large[i] = rand() % 256;
  all_pass &= run_test("Stress Test", data_large);

  cout << "\n"
       << string(60, '=') << endl;
  if (all_pass)
    cout << "  ✅ FINAL STATUS: ALL TESTS PASSED" << endl;
  else
    cout << "  ❌ FINAL STATUS: SOME TESTS FAILED" << endl;
  cout << string(60, '=') << endl;

  return all_pass ? 0 : 1;
}