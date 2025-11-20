// tb/tb_proj.cpp

#include "krnl_proj.h"
#include "../src/patterns.h"
#include <iostream>
#include <string>
#include <vector>

// 方便一点的别名
using std::cout;
using std::endl;
using std::string;
using std::vector;

// 在 patterns.h 里按字符串内容查规则下标
int find_rule_by_string(const std::string &pat) {
  for (int i = 0; i < NUM_PATTERNS; ++i) {
    if (rules[i].len != (int)pat.size()) continue;
    bool same = true;
    for (int k = 0; k < rules[i].len; ++k) {
      if (rules[i].data[k] != (unsigned char)pat[k]) {
        same = false;
        break;
      }
    }
    if (same) return i; // 找到就返回 index
  }
  return -1; // 没找到
}

// 打印一段 payload（可视字符直接打，不可视的用 '.')
void print_payload(const unsigned char *buf, int len) {
  for (int i = 0; i < len; ++i) {
    unsigned char c = buf[i];
    if (c >= 32 && c <= 126) cout << c;
    else cout << '.';
  }
}

// 构造一个 512 bit 的 pkt（最多写 64 字节）
pkt make_pkt(const unsigned char *data, int len,
             bool last_flag, unsigned dest_init = 0) {
  pkt p;
  p.data = 0;
  p.keep = -1;
  p.strb = -1;
  p.dest = dest_init;
  p.last = last_flag ? 1 : 0;

  for (int i = 0; i < len && i < 64; ++i) {
    p.data(i * 8 + 7, i * 8) = data[i];
  }
  return p;
}

// 读取输出流，打印每个包的 dest，并返回所有非零 ID
vector<unsigned> drain_output(hls::stream<pkt> &k2n) {
  vector<unsigned> ids;
  int idx = 0;
  while (!k2n.empty()) {
    pkt out_p = k2n.read();
    ++idx;
    cout << "  [OUT pkt " << idx << "] dest = " << out_p.dest << endl;
    if (out_p.dest != 0) ids.push_back((unsigned)out_p.dest);
  }
  return ids;
}

//---------------------- Test 1 --------------------------
// 1. 一个 pattern 在两个包中间被夹断
//    设计：pattern 的前半段放在包2末尾，后半段放在包3开头
//-------------------------------------------------------
void test_pattern_split_across_two_packets() {
  cout << "========================================" << endl;
  cout << "Test 1: Pattern split across two packets" << endl;

  // 用一个你肯定存在的 pattern（示例: "/bnbform.cgi"）
  std::string pat = "/bnbform.cgi";
  int rule_idx = find_rule_by_string(pat);
  if (rule_idx < 0) {
    cout << "  [WARN] pattern \"" << pat
         << "\" not found in rules, skip this test." << endl;
    return;
  }
  unsigned expected_id = (unsigned)(rule_idx + 1);
  int L = (int)pat.size();

  // 让前半段在第二个包的末尾，后半段在第三个包开头
  const int packet_len = 64;
  unsigned char pkt1_buf[packet_len] = {0};
  unsigned char pkt2_buf[packet_len] = {0};
  unsigned char pkt3_buf[packet_len] = {0};

  // pkt1: 完全无关的预热包
  for (int i = 0; i < packet_len; ++i) pkt1_buf[i] = 0;

  // pkt2: pattern 前 half_len 个字节放在末尾
  int half_len = L / 2;
  for (int i = 0; i < packet_len; ++i) pkt2_buf[i] = 0;
  for (int i = 0; i < half_len; ++i) {
    int pos = packet_len - half_len + i; // 放到末尾
    pkt2_buf[pos] = (unsigned char)pat[i];
  }

  // pkt3: pattern 剩余部分从开头开始
  for (int i = 0; i < packet_len; ++i) pkt3_buf[i] = 0;
  for (int i = half_len; i < L; ++i) {
    int pos = i - half_len; // 从 0 开始排
    pkt3_buf[pos] = (unsigned char)pat[i];
  }

  cout << "  Pattern: \"" << pat << "\"  (rule ID = " << expected_id << ")" << endl;

  // 打印一下三个包的末尾/开头片段，方便肉眼确认
  cout << "  pkt2 tail: \"";
  print_payload(pkt2_buf + (packet_len - half_len), half_len);
  cout << "\"" << endl;
  cout << "  pkt3 head: \"";
  print_payload(pkt3_buf, L - half_len);
  cout << "\"" << endl;

  // 准备流
  hls::stream<pkt> n2k("in1");
  hls::stream<pkt> k2n("out1");

  // 写入三个包（TLAST 只在第三个包上拉高）
  n2k.write(make_pkt(pkt1_buf, packet_len, false));
  n2k.write(make_pkt(pkt2_buf, packet_len, false));
  n2k.write(make_pkt(pkt3_buf, packet_len, true));

  unsigned dest_reg = 0;
  krnl_proj(n2k, k2n, dest_reg);

  auto ids = drain_output(k2n);

  bool ok = false;
  for (auto id : ids) {
    if (id == expected_id) ok = true;
  }
  if (ok)
    cout << "  [PASS] split pattern detected with ID " << expected_id << endl;
  else
    cout << "  [FAIL] split pattern NOT detected." << endl;
}

//---------------------- Test 2 --------------------------
// 2. 一个包中包含两个不同的 pattern
//-------------------------------------------------------
void test_two_patterns_in_one_packet() {
  cout << "========================================" << endl;
  cout << "Test 2: Two patterns in one packet" << endl;

  // 选两个明显存在又比较短的 pattern
  string pat1 = "/bnbform.cgi";
  string pat2 = "/bb/index.php";

  int idx1 = find_rule_by_string(pat1);
  int idx2 = find_rule_by_string(pat2);

  if (idx1 < 0 || idx2 < 0) {
    cout << "  [WARN] pat1 or pat2 not found in rules, skip this test." << endl;
    return;
  }
  unsigned id1 = (unsigned)(idx1 + 1);
  unsigned id2 = (unsigned)(idx2 + 1);

  cout << "  Pattern1: \"" << pat1 << "\" (ID = " << id1 << ")" << endl;
  cout << "  Pattern2: \"" << pat2 << "\" (ID = " << id2 << ")" << endl;

  const int packet_len = 64;
  unsigned char buf[packet_len] = {0};

  // 在同一个包里依次塞 pat1、一些空格、再塞 pat2
  int pos = 0;
  for (int i = 0; i < (int)pat1.size() && pos < packet_len; ++i, ++pos)
    buf[pos] = (unsigned char)pat1[i];

  // 隔几个空
  if (pos < packet_len) buf[pos++] = ' ';
  if (pos < packet_len) buf[pos++] = ' ';

  for (int i = 0; i < (int)pat2.size() && pos < packet_len; ++i, ++pos)
    buf[pos] = (unsigned char)pat2[i];

  cout << "  Packet payload: \"";
  print_payload(buf, pos);
  cout << "\"" << endl;

  hls::stream<pkt> n2k("in2");
  hls::stream<pkt> k2n("out2");

  n2k.write(make_pkt(buf, packet_len, true));

  unsigned dest_reg = 0;
  krnl_proj(n2k, k2n, dest_reg);

  auto ids = drain_output(k2n);

  bool seen1 = false, seen2 = false;
  for (auto id : ids) {
    if (id == id1) seen1 = true;
    if (id == id2) seen2 = true;
  }

  if (seen1 || seen2) {
    cout << "  [INFO] at least one pattern detected." << endl;
    if (seen1) cout << "    - ID " << id1 << " (" << pat1 << ")" << endl;
    if (seen2) cout << "    - ID " << id2 << " (" << pat2 << ")" << endl;
    // kernel 逻辑是“取最小 ID 为优先级最高”
    cout << "  [NOTE] kernel will keep the smallest ID as final dest." << endl;
  } else {
    cout << "  [FAIL] no pattern detected in single packet." << endl;
  }
}

//---------------------- Test 3 --------------------------
// 3. 一个包内包含前缀关系的 pattern（A 和 AB）
//    在 rules 中自动搜索：rules[i] 是 rules[j] 的前缀
//-------------------------------------------------------
void test_prefix_patterns_in_one_packet() {
  cout << "========================================" << endl;
  cout << "Test 3: Prefix patterns (A and AB) in one packet" << endl;

  int idxA = -1, idxAB = -1;

  // 在规则表中寻找任意一对前缀关系：rules[i] 是 rules[j] 的前缀
  for (int i = 0; i < NUM_PATTERNS; ++i) {
    for (int j = 0; j < NUM_PATTERNS; ++j) {
      if (i == j) continue;
      if (rules[i].len >= rules[j].len) continue;

      bool prefix = true;
      for (int k = 0; k < rules[i].len; ++k) {
        if (rules[i].data[k] != rules[j].data[k]) {
          prefix = false;
          break;
        }
      }
      if (prefix) {
        idxA = i;
        idxAB = j;
        break;
      }
    }
    if (idxA >= 0) break;
  }

  if (idxA < 0 || idxAB < 0) {
    cout << "  [WARN] no prefix-pattern pair (A, AB) found in rules, skip this test." << endl;
    return;
  }

  unsigned idA  = (unsigned)(idxA + 1);
  unsigned idAB = (unsigned)(idxAB + 1);

  string patA, patAB;
  for (int k = 0; k < rules[idxA].len; ++k)
    patA.push_back((char)rules[idxA].data[k]);
  for (int k = 0; k < rules[idxAB].len; ++k)
    patAB.push_back((char)rules[idxAB].data[k]);

  cout << "  Found prefix pair:" << endl;
  cout << "    A  = \"" << patA  << "\" (ID = " << idA  << ")" << endl;
  cout << "    AB = \"" << patAB << "\" (ID = " << idAB << ")" << endl;

  const int packet_len = 64;
  unsigned char buf[packet_len] = {0};

  // 在同一个包里放 AB（长的那一个），这样理论上 A 和 AB 都会被匹配到，
  // 而 kernel 会保留 ID 更小的那个 (min)
  int pos = 0;
  for (int i = 0; i < (int)patAB.size() && pos < packet_len; ++i, ++pos)
    buf[pos] = (unsigned char)patAB[i];

  cout << "  Packet payload: \"";
  print_payload(buf, pos);
  cout << "\"" << endl;

  hls::stream<pkt> n2k("in3");
  hls::stream<pkt> k2n("out3");

  n2k.write(make_pkt(buf, packet_len, true));

  unsigned dest_reg = 0;
  krnl_proj(n2k, k2n, dest_reg);

  auto ids = drain_output(k2n);

  bool seenA = false, seenAB = false;
  for (auto id : ids) {
    if (id == idA)  seenA  = true;
    if (id == idAB) seenAB = true;
  }

  if (seenA || seenAB) {
    cout << "  [INFO] detected prefix-related patterns in one packet." << endl;
    if (seenA)  cout << "    - ID " << idA  << " (A)"  << endl;
    if (seenAB) cout << "    - ID " << idAB << " (AB)" << endl;
    cout << "  [NOTE] kernel will finally keep the smallest ID (highest priority)." << endl;
  } else {
    cout << "  [FAIL] no prefix pattern detected." << endl;
  }
}

//---------------------- main ----------------------------
int main() {
  cout << "========================================" << endl;
  cout << "Starting Custom Testbench" << endl;
  cout << "NUM_PATTERNS = " << NUM_PATTERNS << endl;
  cout << "========================================" << endl;

  test_pattern_split_across_two_packets();
  test_two_patterns_in_one_packet();
  test_prefix_patterns_in_one_packet();

  cout << "========================================" << endl;
  cout << "All tests finished (check logs above)." << endl;
  cout << "========================================" << endl;
  return 0;
}
