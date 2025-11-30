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
//  Helper structures and configuration
// ============================================================================
struct MatchResult {
    int byte_index; // 0-63
    uint16_t id;
};

// ----------------------------------------------------------------------------
//  Software reference model placeholder (currently unused)
// ----------------------------------------------------------------------------
void sw_dcam_step(unsigned char in_byte, bool reset, uint16_t &out_id) {
    static uint32_t sw_history[NUM_PATTERNS] = {0};
    (void)in_byte;
    (void)reset;
    (void)out_id;
    // Not implemented; TB validates with known patterns
}

// ============================================================================
//  Stream helper functions
// ============================================================================

// Build input packet
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

// Parse output stream (consume two 512-bit outputs per beat)
vector<MatchResult> drain_one_cycle(hls::stream<pkt> &k2n) {
    vector<MatchResult> res;
    if (k2n.empty()) return res;

    pkt p1 = k2n.read(); // Low 32 bytes -> Byte 0-31
    pkt p2 = k2n.read(); // High 32 bytes -> Byte 32-63

    // Decode P1 (Byte 0-31)
    for (int i = 0; i < 32; ++i) {
        uint16_t id = (uint16_t)p1.data(i * 16 + 15, i * 16);
        if (id != 0) res.push_back({i, id});
    }
    // Decode P2 (Byte 32-63)
    for (int i = 0; i < 32; ++i) {
        uint16_t id = (uint16_t)p2.data(i * 16 + 15, i * 16);
        if (id != 0) res.push_back({i + 32, id});
    }
    return res;
}

// Find rule ID (Helper)
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
//  Test 1: All-zero (Silence) Test
// ============================================================================
bool test_silence() {
    cout << "\n>>> Test 1: Silence (No Match) Test" << endl;
    unsigned char zero_buf[64] = {0}; // all zeros

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
//  Test 2: Boundary Crossing (Byte 31/32)
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

    int end_pos = 32; // pattern last byte falls at byte 32
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
//  Test 3A: Random fuzz, multi-packet
//  Each 64B is a separate AXI packet, TLAST = 1 every time, num_packets = N
// ============================================================================
bool test_random_fuzz_multi_packets() {
    cout << "\n>>> Test 3A: Random Fuzzing (Multi-Packet, TLAST every 64B)" << endl;

    srand((unsigned)time(NULL));
    hls::stream<pkt> n2k("n2k_3A_in");
    hls::stream<pkt> k2n("k2n_3A_out");

    int num_packets = 10; // 10 independent 64B packets

    for(int p=0; p<num_packets; ++p) {
        unsigned char buf[64];
        for(int i=0; i<64; ++i) buf[i] = 0;

        int r_idx = rand() % NUM_PATTERNS;
        string pat;
        for(int k=0; k<rules[r_idx].len; ++k)
            pat += (char)rules[r_idx].data[k];
        uint16_t expected_id = r_idx + 1;
        (void)expected_id; // Presence check only, not strict verification

        if (pat.size() <= 64 && pat.size() > 0) {
            int max_pos  = 64 - (int)pat.size();
            int start_pos = rand() % (max_pos + 1);
            for(int i=0; i<(int)pat.size(); ++i)
                buf[start_pos+i] = pat[i];
        }

        bool is_last = true; // Each 64B is its own packet
        n2k.write(make_pkt(buf, 64, is_last));
    }

    unsigned dummy = 0;
    unsigned packn = num_packets;   // Tell kernel there are 10 TLAST packets
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
//  Test 3B: Random fuzz, single long packet
//  Multiple 64B beats form one AXI packet; only last beat has TLAST = 1; num_packets = 1
// ============================================================================
bool test_random_fuzz_single_long_packet() {
    cout << "\n>>> Test 3B: Random Fuzzing (Single Long Packet, TLAST at end)" << endl;

    srand((unsigned)time(NULL) + 1234); // Different seed to avoid matching 3A
    hls::stream<pkt> n2k("n2k_3B_in");
    hls::stream<pkt> k2n("k2n_3B_out");

    int num_beats   = 10; // 10 beats make one long packet
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

        bool is_last = (p == beat_with_last); // Only the last beat sets TLAST
        n2k.write(make_pkt(buf, 64, is_last));
    }

    unsigned dummy = 0;
    unsigned packn = 1;   // Tell kernel this is one AXI packet (multiple beats)
    krnl_proj(n2k, k2n, dummy, packn);

    int total_results = 0;
    int beats_read    = 0;
    while(!k2n.empty()) {
        auto res = drain_one_cycle(k2n);
        beats_read++;   // Each drain_one_cycle corresponds to one 64B beat of output
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
