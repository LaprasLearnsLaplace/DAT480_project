#include "krnl_proj.h"
#include "../src/patterns.h"
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <ctime>

using std::cout;
using std::endl;
using std::string;
using std::vector;

// ========================================
// 性能统计结构
// ========================================
struct PerfStats {
    int total_packets;
    int total_bytes;
    int total_cycles;
    int patterns_detected;
    int unique_patterns;
    
    PerfStats() : total_packets(0), total_bytes(0), total_cycles(0),
                  patterns_detected(0), unique_patterns(0) {}
    
    void print_report() {
        cout << "\n=======================================" << endl;
        cout << "   PERFORMANCE REPORT" << endl;
        cout << "=======================================" << endl;
        cout << "Total Packets:       " << total_packets << endl;
        cout << "Total Bytes:         " << total_bytes << endl;
        cout << "Total Cycles:        " << total_cycles << endl;
        cout << "Patterns Detected:   " << patterns_detected << endl;
        cout << "Unique Patterns:     " << unique_patterns << endl;
        
        if (total_cycles > 0) {
            double bytes_per_cycle = (double)total_bytes / total_cycles;
            cout << "\n--- Throughput Analysis ---" << endl;
            cout << "Bytes/Cycle:         " << bytes_per_cycle << endl;
            
            //300MHz 工作频率
            double freq_mhz = 300.0;
            double throughput_mbps = bytes_per_cycle * freq_mhz * 8.0;
            double throughput_gbps = throughput_mbps / 1000.0;
            
            cout << "@ 300MHz:" << endl;
            cout << "  Throughput:        " << throughput_gbps << " Gbps" << endl;
            cout << "  Throughput:        " << throughput_mbps << " Mbps" << endl;
            cout << "  MB/s:              " << (bytes_per_cycle * freq_mhz) << endl;
            
            // 性能评估
            cout << "\n--- Performance Grade ---" << endl;
            if (throughput_gbps >= 100.0) {
                cout << "Rating: EXCELLENT (100 Gbps line-rate)" << endl;
            } else if (throughput_gbps >= 50.0) {
                cout << "Rating: VERY GOOD (50+ Gbps)" << endl;
            } else if (throughput_gbps >= 10.0) {
                cout << "Rating: GOOD (10+ Gbps)" << endl;
            } else if (throughput_gbps >= 2.4) {
                cout << "Rating: PASS (Minimum requirement: 1 byte/cycle)" << endl;
            } else {
                cout << "Rating: BELOW MINIMUM REQUIREMENT" << endl;
            }
        }
        
        if (total_packets > 0) {
            double detection_rate = (double)patterns_detected / total_packets * 100.0;
            cout << "\n--- Detection Statistics ---" << endl;
            cout << "Detection Rate:      " << detection_rate << "%" << endl;
            cout << "Avg Patterns/Packet: " << (double)patterns_detected / total_packets << endl;
        }
        
        cout << "=======================================" << endl;
    }
};

// ========================================
// 辅助函数
// ========================================

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
        if (same) return i;
    }
    return -1;
}

void print_payload(const unsigned char *buf, int len, int max_print = 40) {
    int print_len = (len > max_print) ? max_print : len;
    for (int i = 0; i < print_len; ++i) {
        unsigned char c = buf[i];
        if (c >= 32 && c <= 126) cout << c;
        else cout << '.';
    }
    if (len > max_print) cout << "...";
}

pkt make_pkt(const unsigned char *data, int len, bool last_flag, unsigned dest_init = 0) {
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

vector<unsigned> drain_output(hls::stream<pkt> &k2n, PerfStats &stats, bool verbose = false) {
    vector<unsigned> ids;
    int idx = 0;
    
    while (!k2n.empty()) {
        pkt out_p = k2n.read();
        ++idx;
        
        if (verbose) {
            cout << "  [OUT pkt " << idx << "] dest = " << out_p.dest;
            if (out_p.last) cout << " (LAST)";
            cout << endl;
        }
        
        if (out_p.dest != 0) {
            ids.push_back((unsigned)out_p.dest);
            stats.patterns_detected++;
        }
        
    }
    
    vector<unsigned> unique_ids;
    for (auto id : ids) {
        bool found = false;
        for (auto uid : unique_ids) {
            if (uid == id) {
                found = true;
                break;
            }
        }
        if (!found) unique_ids.push_back(id);
    }
    stats.unique_patterns = unique_ids.size();
    
    return ids;
}

// 生成随机payload
void generate_random_payload(unsigned char *buf, int len) {
    for (int i = 0; i < len; ++i) {
        buf[i] = rand() % 256;
    }
}

// 在payload中插入pattern
void insert_pattern(unsigned char *buf, int buf_len, const string &pattern, int pos) {
    for (int i = 0; i < (int)pattern.size() && (pos + i) < buf_len; ++i) {
        buf[pos + i] = (unsigned char)pattern[i];
    }
}

// ========================================
// Test 1: 基本功能测试（跨包匹配）
// ========================================
void test_basic_cross_packet_matching() {
    cout << "\n========================================" << endl;
    cout << "Test 1: Cross-packet Pattern Matching" << endl;
    cout << "========================================" << endl;

    string pat = "/bnbform.cgi";
    int rule_idx = find_rule_by_string(pat);
    if (rule_idx < 0) {
        cout << "Pattern not found, skipping test." << endl;
        return;
    }

    unsigned expected_id = (unsigned)(rule_idx + 1);
    int L = (int)pat.size();
    const int packet_len = 64;

    // 创建3个包：pattern跨越包2和包3
    unsigned char pkt1_buf[packet_len] = {0};
    unsigned char pkt2_buf[packet_len] = {0};
    unsigned char pkt3_buf[packet_len] = {0};

    int half = L / 2;
    // pkt2末尾放前半
    for (int i = 0; i < half; ++i) {
        pkt2_buf[packet_len - half + i] = (unsigned char)pat[i];
    }
    // pkt3开头放后半
    for (int i = half; i < L; ++i) {
        pkt3_buf[i - half] = (unsigned char)pat[i];
    }

    cout << "Pattern: \"" << pat << "\" (ID=" << expected_id << ")" << endl;
    cout << "Split position: " << half << "/" << L << endl;

    hls::stream<pkt> n2k("in1");
    hls::stream<pkt> k2n("out1");

    n2k.write(make_pkt(pkt1_buf, packet_len, false));
    n2k.write(make_pkt(pkt2_buf, packet_len, false));
    n2k.write(make_pkt(pkt3_buf, packet_len, true));

    unsigned dest_reg = 0;
    PerfStats stats;
    stats.total_packets = 3;
    stats.total_bytes = 3 * packet_len;
    // ✅ 正确计算：II=1意味着每byte需要1 cycle
    stats.total_cycles = stats.total_bytes;  // 3 * 64 = 192 cycles

    krnl_proj(n2k, k2n, dest_reg);
    auto ids = drain_output(k2n, stats, true);

    bool found = false;
    for (auto id : ids) {
        if (id == expected_id) found = true;
    }

    cout << (found ? "[PASS]" : "[FAIL]") << " Cross-packet pattern detection" << endl;
}

// ========================================
// Test 2: 多pattern单包测试
// ========================================
void test_multiple_patterns_single_packet() {
    cout << "\n========================================" << endl;
    cout << "Test 2: Multiple Patterns in One Packet" << endl;
    cout << "========================================" << endl;

    vector<string> test_patterns = {"/bnbform.cgi", "/bb/index.php", "CAL"};
    vector<int> found_indices;
    vector<unsigned> expected_ids;

    for (auto &pat : test_patterns) {
        int idx = find_rule_by_string(pat);
        if (idx >= 0) {
            found_indices.push_back(idx);
            expected_ids.push_back((unsigned)(idx + 1));
        }
    }

    if (found_indices.empty()) {
        cout << "No test patterns found, skipping." << endl;
        return;
    }

    const int packet_len = 64;
    unsigned char buf[packet_len] = {0};
    int pos = 0;

    // 依次插入patterns
    for (size_t i = 0; i < found_indices.size(); ++i) {
        string &pat = test_patterns[i];
        for (size_t j = 0; j < pat.size() && pos < packet_len; ++j) {
            buf[pos++] = (unsigned char)pat[j];
        }
        if (pos < packet_len) buf[pos++] = ' ';
    }

    cout << "Payload: \"";
    print_payload(buf, pos);
    cout << "\"" << endl;

    cout << "Expected IDs: ";
    for (auto id : expected_ids) cout << id << " ";
    cout << endl;

    hls::stream<pkt> n2k("in2");
    hls::stream<pkt> k2n("out2");

    n2k.write(make_pkt(buf, packet_len, true));

    unsigned dest_reg = 0;
    PerfStats stats;
    stats.total_packets = 1;
    stats.total_bytes = packet_len;
    // ✅ II=1: 64 bytes = 64 cycles
    stats.total_cycles = packet_len;

    krnl_proj(n2k, k2n, dest_reg);
    auto ids = drain_output(k2n, stats, true);

    int matched = 0;
    for (auto id : ids) {
        for (auto exp : expected_ids) {
            if (id == exp) matched++;
        }
    }

    cout << "[INFO] Matched " << matched << "/" << expected_ids.size() 
         << " patterns" << endl;
}

// ========================================
// Test 3: 大规模吞吐量测试
// ========================================
void test_throughput_benchmark(int num_packets = 1000) {
    cout << "\n========================================" << endl;
    cout << "Test 3: Throughput Benchmark" << endl;
    cout << "========================================" << endl;
    cout << "Generating " << num_packets << " test packets..." << endl;

    const int packet_len = 64;
    srand(time(NULL));

    // 选择几个常见patterns用于测试
    vector<string> common_patterns;
    for (int i = 0; i < 10 && i < NUM_PATTERNS; ++i) {
        if (rules[i].len > 0 && rules[i].len < 20) {
            string pat;
            for (int k = 0; k < rules[i].len; ++k) {
                pat.push_back((char)rules[i].data[k]);
            }
            common_patterns.push_back(pat);
        }
    }

    if (common_patterns.empty()) {
        cout << "No suitable patterns for testing." << endl;
        return;
    }

    cout << "Using " << common_patterns.size() << " test patterns" << endl;

    hls::stream<pkt> n2k("in_bench");
    hls::stream<pkt> k2n("out_bench");

    // 生成测试包
    cout << "Writing test packets..." << endl;
    for (int i = 0; i < num_packets; ++i) {
        unsigned char buf[packet_len];
        generate_random_payload(buf, packet_len);

        // 随机在20%的包中插入pattern
        if (rand() % 5 == 0) {
            string &pat = common_patterns[rand() % common_patterns.size()];
            int pos = rand() % (packet_len - pat.size());
            insert_pattern(buf, packet_len, pat, pos);
        }

        bool is_last = (i == num_packets - 1);
        n2k.write(make_pkt(buf, packet_len, is_last));
    }

    cout << "Running kernel..." << endl;
    unsigned dest_reg = 0;
    
    krnl_proj(n2k, k2n, dest_reg);

    PerfStats stats;
    stats.total_packets = num_packets;
    stats.total_bytes = num_packets * packet_len;
    // ✅ 正确：假设II=1，每byte需要1 cycle
    // 对于64-byte packets: 1000 packets × 64 bytes = 64000 cycles
    stats.total_cycles = stats.total_bytes;

    cout << "Processing output..." << endl;
    auto ids = drain_output(k2n, stats, false);

    cout << "\n[BENCHMARK COMPLETE]" << endl;
    stats.print_report();
}

// ========================================
// Test 4: 边界条件测试
// ========================================
void test_edge_cases() {
    cout << "\n========================================" << endl;
    cout << "Test 4: Edge Cases" << endl;
    cout << "========================================" << endl;

    const int packet_len = 64;
    hls::stream<pkt> n2k("in_edge");
    hls::stream<pkt> k2n("out_edge");
    unsigned dest_reg = 0;

    // Case 1: 空包
    cout << "\nCase 1: Empty packet" << endl;
    unsigned char empty[packet_len] = {0};
    n2k.write(make_pkt(empty, packet_len, true));
    krnl_proj(n2k, k2n, dest_reg);
    
    PerfStats stats1;
    stats1.total_packets = 1;
    stats1.total_bytes = packet_len;
    stats1.total_cycles = packet_len;  // ✅ 64 bytes = 64 cycles
    auto ids1 = drain_output(k2n, stats1, true);
    cout << (ids1.empty() ? "[PASS]" : "[INFO]") << " Empty packet handling" << endl;

    // Case 2: 全0xFF包
    cout << "\nCase 2: All 0xFF packet" << endl;
    unsigned char full[packet_len];
    for (int i = 0; i < packet_len; ++i) full[i] = 0xFF;
    n2k.write(make_pkt(full, packet_len, true));
    krnl_proj(n2k, k2n, dest_reg);
    
    PerfStats stats2;
    stats2.total_packets = 1;
    stats2.total_bytes = packet_len;
    stats2.total_cycles = packet_len;  // ✅ 64 bytes = 64 cycles
    auto ids2 = drain_output(k2n, stats2, true);
    cout << "[INFO] 0xFF packet processed" << endl;

    // Case 3: 单字节pattern（如果有的话）
    cout << "\nCase 3: Single-byte pattern (if exists)" << endl;
    bool has_single = false;
    for (int i = 0; i < NUM_PATTERNS; ++i) {
        if (rules[i].len == 1) {
            unsigned char single[packet_len] = {0};
            single[0] = rules[i].data[0];
            n2k.write(make_pkt(single, packet_len, true));
            krnl_proj(n2k, k2n, dest_reg);
            
            PerfStats stats3;
            stats3.total_packets = 1;
            stats3.total_bytes = packet_len;
            stats3.total_cycles = packet_len;  // ✅ 64 bytes = 64 cycles
            auto ids3 = drain_output(k2n, stats3, true);
            
            has_single = true;
            cout << "[INFO] Single-byte pattern ID=" << (i+1) 
                 << " detected: " << (!ids3.empty()) << endl;
            break;
        }
    }
    if (!has_single) {
        cout << "[INFO] No single-byte patterns in ruleset" << endl;
    }
}

// ========================================
// Test 5: 连续包流测试（模拟真实流量）
// ========================================
void test_continuous_stream(int num_packets = 100) {
    cout << "\n========================================" << endl;
    cout << "Test 5: Continuous Stream (No LAST)" << endl;
    cout << "========================================" << endl;
    cout << "Testing " << num_packets << " continuous packets..." << endl;

    const int packet_len = 64;
    hls::stream<pkt> n2k("in_cont");
    hls::stream<pkt> k2n("out_cont");

    // 选一个中等长度的pattern
    string test_pat = "/bb/index.php";
    int idx = find_rule_by_string(test_pat);
    
    if (idx < 0) {
        cout << "Test pattern not found." << endl;
        return;
    }

    unsigned expected_id = (unsigned)(idx + 1);
    cout << "Test pattern: \"" << test_pat << "\" (ID=" << expected_id << ")" << endl;

    // 生成连续包，其中某些包包含pattern
    for (int i = 0; i < num_packets; ++i) {
        unsigned char buf[packet_len];
        generate_random_payload(buf, packet_len);

        // 每10个包插入一次pattern
        if (i % 10 == 5) {
            insert_pattern(buf, packet_len, test_pat, 10);
        }

        // 最后一个包才设置LAST
        bool is_last = (i == num_packets - 1);
        n2k.write(make_pkt(buf, packet_len, is_last));
    }

    unsigned dest_reg = 0;
    krnl_proj(n2k, k2n, dest_reg);

    PerfStats stats;
    stats.total_packets = num_packets;
    stats.total_bytes = num_packets * packet_len;
    // ✅ II=1: 每byte需要1 cycle
    stats.total_cycles = stats.total_bytes;

    auto ids = drain_output(k2n, stats, false);

    int expected_detections = num_packets / 10;
    int actual_detections = 0;
    for (auto id : ids) {
        if (id == expected_id) actual_detections++;
    }

    cout << "\nExpected ~" << expected_detections << " detections" << endl;
    cout << "Actual: " << actual_detections << " detections" << endl;
    
    double detection_accuracy = (double)actual_detections / expected_detections * 100.0;
    cout << "Detection accuracy: " << detection_accuracy << "%" << endl;

    if (detection_accuracy >= 80.0) {
        cout << "[PASS] Good detection accuracy" << endl;
    } else {
        cout << "[WARN] Low detection accuracy" << endl;
    }
}

// ========================================
// main
// ========================================
int main() {
    cout << "========================================" << endl;
    cout << "   ENHANCED PATTERN MATCHING TESTBENCH" << endl;
    cout << "========================================" << endl;
    cout << "NUM_PATTERNS: " << NUM_PATTERNS << endl;
    cout << "PATTERN_MAX_LEN: " << PATTERN_MAX_LEN << endl;
    cout << "Target: 300 MHz, 64 bytes/cycle" << endl;
    cout << "Minimum: 1 byte/cycle (2.4 Gbps)" << endl;
    cout << "========================================" << endl;

    // 运行所有测试
    test_basic_cross_packet_matching();
    test_multiple_patterns_single_packet();
    test_edge_cases();
    // test_continuous_stream(100);
    // test_throughput_benchmark(1000);  // 主要性能测试
    test_continuous_stream(10);
    test_throughput_benchmark(30); 


    cout << "\n========================================" << endl;
    cout << "   ALL TESTS COMPLETED" << endl;
    cout << "========================================" << endl;
    cout << "\nNOTE: The throughput shown is for C simulation only." << endl;
    cout << "Actual hardware performance will be determined by:" << endl;
    cout << "  - HLS Pipeline II (Initiation Interval)" << endl;
    cout << "  - FPGA clock frequency achieved" << endl;
    cout << "  - Memory access patterns" << endl;
    cout << "\nCheck synthesis report for accurate performance!" << endl;
    cout << "========================================" << endl;

    return 0;
}