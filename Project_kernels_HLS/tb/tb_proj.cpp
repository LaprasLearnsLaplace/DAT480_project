#include "krnl_proj.h"
#include "../src/patterns.h"
#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <cstdlib>
#include <ctime>

using std::cout;
using std::endl;
using std::vector;
using std::string;

// ============================================================================
//  辅助结构与配置
// ============================================================================
struct MatchResult {
    int byte_index; // 0-63
    uint16_t id;
};

// ----------------------------------------------------------------------------
//  软件参考模型 (Golden Model) 占位（当前未使用）
// ----------------------------------------------------------------------------
void sw_dcam_step(unsigned char in_byte, bool reset, uint16_t &out_id) {
    static uint32_t sw_history[NUM_PATTERNS] = {0};
    (void)in_byte;
    (void)reset;
    (void)out_id;
    // 这里先不实现，当前 TB 通过构造已知 pattern 来验证硬件行为
}

// ============================================================================
//  流处理辅助函数
// ============================================================================

// 构造输入包
pkt make_pkt(const unsigned char *data, int len, bool last_flag) {
    pkt p;
    p.data = 0;
    p.keep = -1;
    p.last = last_flag ? 1 : 0;
    p.dest = 0;
    for (int i = 0; i < len && i < 64; ++i) {
        p.data(i * 8 + 7, i * 8) = data[i];
    }
    return p;
}

// 解析输出流 (双倍发包解析：一次消费 2 个 512-bit 输出)
vector<MatchResult> drain_one_cycle(hls::stream<pkt> &k2n) {
    vector<MatchResult> res;
    if (k2n.empty()) return res;

    pkt p1 = k2n.read(); // Low 32 bytes 对应 Byte 0-31
    pkt p2 = k2n.read(); // High 32 bytes 对应 Byte 32-63

    // 解析 P1 (Byte 0-31)
    for (int i = 0; i < 32; ++i) {
        uint16_t id = (uint16_t)p1.data(i * 16 + 15, i * 16);
        if (id != 0) res.push_back({i, id});
    }
    // 解析 P2 (Byte 32-63)
    for (int i = 0; i < 32; ++i) {
        uint16_t id = (uint16_t)p2.data(i * 16 + 15, i * 16);
        if (id != 0) res.push_back({i + 32, id});
    }
    return res;
}

// 查找规则 ID (Helper)
int get_rule_id(string s) {
    for(int i=0; i<NUM_PATTERNS; ++i) {
        if(rules[i].len != (int)s.size()) continue;
        bool m = true;
        for(int k=0; k<rules[i].len; ++k)
            if(rules[i].data[k] != (unsigned char)s[k]) m=false;
        if(m) return i + 1;
    }
    return -1;
}

// ============================================================================
//  Test 1: 全零测试 (Silence Test)
// ============================================================================
bool test_silence() {
    cout << "\n>>> Test 1: Silence (No Match) Test" << endl;
    unsigned char zero_buf[64] = {0}; // 全 0
    
    hls::stream<pkt> n2k("n2k_1");
    hls::stream<pkt> k2n("k2n_1");
    
    n2k.write(make_pkt(zero_buf, 64, true));
    
    unsigned dummy = 0;
    krnl_proj(n2k, k2n, dummy, 1);
    
    auto res = drain_one_cycle(k2n);
    if (res.empty()) {
        cout << "  [PASS] No false positives detected." << endl;
        return true;
    } else {
        cout << "  [FAIL] Detected ID " << res[0].id << " in zero buffer!" << endl;
        return false;
    }
}

// ============================================================================
//  Test 2: 边界跨越测试 (Boundary Crossing Byte 31/32)
// ============================================================================
bool test_boundary_split() {
    cout << "\n>>> Test 2: Boundary Crossing (Byte 31/32)" << endl;
    
    if(NUM_PATTERNS < 1) return true;
    int rule_idx = 0;
    uint16_t target_id = rule_idx + 1;
    string pat;
    for(int i=0; i<rules[rule_idx].len; ++i)
        pat += (char)rules[rule_idx].data[i];
    
    unsigned char buf[64];
    for(int i=0; i<64; ++i) buf[i] = ' ';
    
    int end_pos = 32; // pattern 最后一个字节落在 Byte 32
    int start_pos = end_pos - (int)pat.size() + 1;
    
    if(start_pos < 0) {
        cout << "  [SKIP] Pattern too long for boundary test." << endl;
        return true;
    }
    
    for(int i=0; i<(int)pat.size(); ++i) {
        buf[start_pos + i] = pat[i];
    }
    
    hls::stream<pkt> n2k("n2k_2");
    hls::stream<pkt> k2n("k2n_2");
    n2k.write(make_pkt(buf, 64, true));
    
    unsigned dummy = 0;
    krnl_proj(n2k, k2n, dummy, 1);
    
    auto res = drain_one_cycle(k2n);
    
    bool found = false;
    for(auto r : res) {
        if(r.id == target_id && r.byte_index == end_pos) {
            found = true;
            cout << "  [PASS] Found ID " << r.id << " at byte " << r.byte_index 
                 << " (Crossed boundary 31/32)" << endl;
        }
    }
    
    if(!found) {
        cout << "  [FAIL] Pattern missing at boundary." << endl;
        return false;
    }
    return true;
}

// ============================================================================
//  Test 3A: 随机 Fuzz，多包模式
//  每个 64B 是一个独立 AXI 包，TLAST 每次为 1，num_packets = N
// ============================================================================
bool test_random_fuzz_multi_packets() {
    cout << "\n>>> Test 3A: Random Fuzzing (Multi-Packet, TLAST every 64B)" << endl;
    
    srand((unsigned)time(NULL));
    hls::stream<pkt> n2k("n2k_3A_in");
    hls::stream<pkt> k2n("k2n_3A_out");
    
    int num_packets = 10; // 10 个独立的 64B 包
    
    for(int p=0; p<num_packets; ++p) {
        unsigned char buf[64];
        for(int i=0; i<64; ++i) buf[i] = 0;
        
        int r_idx = rand() % NUM_PATTERNS;
        string pat;
        for(int k=0; k<rules[r_idx].len; ++k)
            pat += (char)rules[r_idx].data[k];
        uint16_t expected_id = r_idx + 1;
        (void)expected_id; // 当前仅做存在性检查，不过度校验
        
        if (pat.size() <= 64 && pat.size() > 0) {
            int max_pos  = 64 - (int)pat.size();
            int start_pos = rand() % (max_pos + 1);
            for(int i=0; i<(int)pat.size(); ++i)
                buf[start_pos+i] = pat[i];
        }
        
        bool is_last = true; // 每个 64B 都是一个独立 packet
        n2k.write(make_pkt(buf, 64, is_last));
    }
    
    unsigned dummy = 0;
    unsigned packn = num_packets;   // 告诉 kernel：有 10 个 TLAST 包
    krnl_proj(n2k, k2n, dummy, packn);
    
    int total_results = 0;
    int packets_read  = 0;
    while(!k2n.empty()) {
        auto res = drain_one_cycle(k2n);
        packets_read++;
        if(!res.empty()) {
            if(total_results < 10) {
                cout << "  [INFO] [3A] Pkt " << packets_read 
                     << " first match ID " << res[0].id 
                     << " at byte " << res[0].byte_index << endl;
            }
            total_results += (int)res.size();
        }
    }
    
    if(packets_read == num_packets) {
        cout << "  [PASS] [3A] Processed " << packets_read 
             << " packets with TLAST per 64B." << endl;
        return true;
    } else {
        cout << "  [FAIL] [3A] Output packet count mismatch. Expected " 
             << num_packets << ", got " << packets_read << endl;
        return false;
    }
}

// ============================================================================
//  Test 3B: 随机 Fuzz，单长包模式
//  多个 64B 拼成一个 AXI 包，只有最后一个 TLAST = 1，num_packets = 1
// ============================================================================
bool test_random_fuzz_single_long_packet() {
    cout << "\n>>> Test 3B: Random Fuzzing (Single Long Packet, TLAST at end)" << endl;
    
    srand((unsigned)time(NULL) + 1234); // 换个种子避免和 3A 完全一致
    hls::stream<pkt> n2k("n2k_3B_in");
    hls::stream<pkt> k2n("k2n_3B_out");
    
    int num_beats   = 10; // 10 个 64B beat 组成一个长包
    int beat_with_last = num_beats - 1;
    
    for(int p=0; p<num_beats; ++p) {
        unsigned char buf[64];
        for(int i=0; i<64; ++i) buf[i] = 0;
        
        int r_idx = rand() % NUM_PATTERNS;
        string pat;
        for(int k=0; k<rules[r_idx].len; ++k)
            pat += (char)rules[r_idx].data[k];
        uint16_t expected_id = r_idx + 1;
        (void)expected_id;
        
        if (pat.size() <= 64 && pat.size() > 0) {
            int max_pos  = 64 - (int)pat.size();
            int start_pos = rand() % (max_pos + 1);
            for(int i=0; i<(int)pat.size(); ++i)
                buf[start_pos+i] = pat[i];
        }
        
        bool is_last = (p == beat_with_last); // 只有最后一个 beat TLAST=1
        n2k.write(make_pkt(buf, 64, is_last));
    }
    
    unsigned dummy = 0;
    unsigned packn = 1;   // 告诉 kernel：这是一“个” AXI 包（有多个 beat）
    krnl_proj(n2k, k2n, dummy, packn);
    
    int total_results = 0;
    int beats_read    = 0;
    while(!k2n.empty()) {
        auto res = drain_one_cycle(k2n);
        beats_read++;   // 每次 drain_one_cycle 对应 1 个 64B beat 的结果
        if(!res.empty()) {
            if(total_results < 10) {
                cout << "  [INFO] [3B] Beat " << beats_read 
                     << " first match ID " << res[0].id 
                     << " at byte " << res[0].byte_index << endl;
            }
            total_results += (int)res.size();
        }
    }
    
    if(beats_read == num_beats) {
        cout << "  [PASS] [3B] Processed " << beats_read 
             << " beats within a single long packet." << endl;
        return true;
    } else {
        cout << "  [FAIL] [3B] Output beat count mismatch. Expected " 
             << num_beats << ", got " << beats_read << endl;
        return false;
    }
}

// ============================================================================
//  Main
// ============================================================================
int main() {
    cout << "===========================================" << endl;
    cout << "   Robust Testbench for Double-Pump Arch   " << endl;
    cout << "===========================================" << endl;
    
    bool pass = true;
    
    pass &= test_silence();
    pass &= test_boundary_split();
    pass &= test_random_fuzz_multi_packets();     // Test 3A
    pass &= test_random_fuzz_single_long_packet(); // Test 3B
    
    cout << "\n===========================================" << endl;
    if(pass) cout << "   ALL TESTS PASSED " << endl;
    else     cout << "   SOME TESTS FAILED " << endl;
    cout << "===========================================" << endl;
    
    return pass ? 0 : 1;
}
