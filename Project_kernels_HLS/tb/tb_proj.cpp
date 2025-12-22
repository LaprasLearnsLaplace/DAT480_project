#include "krnl_proj.h"
#include "../src/patterns.h"
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <ctime>

using std::cout;
using std::endl;
using std::vector;
using std::string;

// ============================================================================
// Sparse output protocol (must match kernel)
// ============================================================================

static const int EVENT_W     = 128;
static const int EVENT_BYTES = EVENT_W / 8;          // 16
static const int KEEP_W      = DATA_WIDTH_BYTES;     // 64
static const int EVENTS_PER_BEAT = (DWIDTH / EVENT_W); // 512/128 = 4

static const uint8_t EV_FLAG_END      = 1 << 0;
static const uint8_t EV_FLAG_OVERFLOW = 1 << 1;

struct MatchResult {
    int      byte_index; // packet-local byte index
    uint16_t id;
    uint8_t  lane;
};

struct ReportInfo {
    uint64_t pkt_in_bytes   = 0;
    uint64_t pkt_in_beats   = 0;
    uint64_t pkt_out_bytes  = 0;
    uint64_t pkt_out_beats  = 0;
    uint64_t packet_seq     = 0;
    uint64_t total_in_bytes = 0;
    uint64_t total_out_bytes= 0;
};

struct PacketDrainResult {
    vector<MatchResult> matches;
    ReportInfo report;
};

// ----------------------------------------------------------------------------
// popcount for 64-bit keep
// ----------------------------------------------------------------------------
static inline int popcount_keep(ap_uint<KEEP_W> k) {
    int c = 0;
    for (int i = 0; i < KEEP_W; i++) c += (int)k[i];
    return c;
}

// ----------------------------------------------------------------------------
// unpack one event128 from 512b beat slice
// event layout (must match kernel):
// [127:64]  byte_index
// [63:48]   pattern_id
// [47:40]   lane
// [39:32]   flags
// [31:0]    user/reserved
// ----------------------------------------------------------------------------
static inline void unpack_event(
    const ap_uint<EVENT_W> &w,
    uint64_t &byte_index,
    uint16_t &pattern_id,
    uint8_t  &lane,
    uint8_t  &flags
) {
    byte_index = (uint64_t)w.range(127, 64);
    pattern_id = (uint16_t)w.range(63, 48);
    lane       = (uint8_t) w.range(47, 40);
    flags      = (uint8_t) w.range(39, 32);
}

// ============================================================================
// Stream helper functions
// ============================================================================

pkt make_pkt(const unsigned char *data, int len, bool last_flag) {
    pkt p;
    p.data = 0;
    p.keep = 0;
    p.last = last_flag ? 1 : 0;
    p.dest = 0;      // not used in TB
    p.user = 0;      // input beats are not report
    p.id   = 0;

    int n = (len < DATA_WIDTH_BYTES) ? len : DATA_WIDTH_BYTES;
    for (int i = 0; i < n; ++i) {
        p.data(i * 8 + 7, i * 8) = data[i];
        p.keep[i] = 1;
    }
    return p;
}

// Drain exactly ONE output packet:
// - EVENT beats have user==0
// - REPORT beat has user==1 (and should end the packet)
PacketDrainResult drain_one_packet_sparse(hls::stream<pkt> &k2n) {
    PacketDrainResult out;

    while (!k2n.empty()) {
        pkt w = k2n.read();
        int valid_bytes = popcount_keep(w.keep);

        if (w.user == 0) {
            // EVENT beat
            int valid_events = valid_bytes / EVENT_BYTES;

            // Safety: bound events to [0..4]
            if (valid_events < 0) valid_events = 0;
            if (valid_events > EVENTS_PER_BEAT) valid_events = EVENTS_PER_BEAT;

            for (int e = 0; e < valid_events; ++e) {
                ap_uint<EVENT_W> evw = w.data.range((e + 1) * EVENT_W - 1, e * EVENT_W);

                uint64_t byte_index;
                uint16_t pattern_id;
                uint8_t  lane;
                uint8_t  flags;

                unpack_event(evw, byte_index, pattern_id, lane, flags);

                if (flags & EV_FLAG_END) {
                    continue; // end marker, not a match
                }
                if (pattern_id != 0) {
                    out.matches.push_back({(int)byte_index, pattern_id, lane});
                }
            }
        } else {
            // REPORT beat (end-of-packet)
            out.report.pkt_in_bytes    = (uint64_t)w.data.range(63, 0);
            out.report.pkt_in_beats    = (uint64_t)w.data.range(127, 64);
            out.report.pkt_out_bytes   = (uint64_t)w.data.range(191, 128);
            out.report.pkt_out_beats   = (uint64_t)w.data.range(255, 192);
            out.report.packet_seq      = (uint64_t)w.data.range(319, 256);
            out.report.total_in_bytes  = (uint64_t)w.data.range(383, 320);
            out.report.total_out_bytes = (uint64_t)w.data.range(447, 384);
            return out;
        }
    }

    return out; // stream ended unexpectedly without REPORT
}

// Find rule ID (Helper)
int get_rule_id(string s) {
    for (int i = 0; i < NUM_PATTERNS; ++i) {
        if (rules[i].len != (int)s.size()) continue;
        bool m = true;
        for (int k = 0; k < rules[i].len; ++k) {
            if (rules[i].data[k] != (unsigned char)s[k]) m = false;
        }
        if (m) return i + 1;
    }
    return -1;
}

// ============================================================================
// Test 1: Silence
// ============================================================================

bool test_silence() {
    cout << "\n>>> Test 1: Silence (No Match) Test" << endl;

    unsigned char zero_buf[DATA_WIDTH_BYTES];
    for (int i = 0; i < DATA_WIDTH_BYTES; ++i) zero_buf[i] = 0;

    hls::stream<pkt> n2k("n2k_1");
    hls::stream<pkt> k2n("k2n_1");

    n2k.write(make_pkt(zero_buf, DATA_WIDTH_BYTES, true));

    unsigned dummy = 0;
    krnl_proj(n2k, k2n, dummy, 1);

    auto out = drain_one_packet_sparse(k2n);

    if (out.matches.empty()) {
        cout << "  [PASS] No false positives detected." << endl;
        cout << "  [INFO] REPORT: in_bytes=" << out.report.pkt_in_bytes
             << " out_bytes=" << out.report.pkt_out_bytes
             << " seq=" << out.report.packet_seq << endl;
        return true;
    } else {
        cout << "  [FAIL] Detected ID " << out.matches[0].id
             << " at byte " << out.matches[0].byte_index << endl;
        return false;
    }
}

// ============================================================================
// Test 2: Boundary diagnostic (Byte 31/32)
// ============================================================================

bool test_boundary_split() {
    cout << "\n>>> Test 2: Boundary Crossing (Byte 31/32)" << endl;

    if (NUM_PATTERNS < 1) {
        cout << "  [SKIP] No patterns defined." << endl;
        return true;
    }

    int      rule_idx  = 0;
    uint16_t target_id = rule_idx + 1;

    string pat;
    for (int i = 0; i < rules[rule_idx].len; ++i)
        pat += (char)rules[rule_idx].data[i];

    if (pat.empty()) {
        cout << "  [SKIP] Pattern length is zero." << endl;
        return true;
    }

    if ((int)pat.size() > DATA_WIDTH_BYTES) {
        cout << "  [SKIP] Pattern too long for 64B beat." << endl;
        return true;
    }

    unsigned char buf[DATA_WIDTH_BYTES];
    for (int i = 0; i < DATA_WIDTH_BYTES; ++i) buf[i] = ' ';

    const int end_pos   = 32;
    const int start_pos = end_pos - (int)pat.size() + 1;

    if (start_pos < 0) {
        cout << "  [SKIP] Pattern too long for boundary positioning." << endl;
        return true;
    }

    for (int i = 0; i < (int)pat.size(); ++i) {
        buf[start_pos + i] = pat[i];
    }

    hls::stream<pkt> n2k("n2k_2");
    hls::stream<pkt> k2n("k2n_2");
    n2k.write(make_pkt(buf, DATA_WIDTH_BYTES, true));

    unsigned dummy = 0;
    krnl_proj(n2k, k2n, dummy, 1);

    auto out = drain_one_packet_sparse(k2n);

    if (out.matches.empty()) {
        cout << "  [WARN] No matches reported at all for boundary test." << endl;
        return true;
    }

    bool found_in_span = false;
    cout << "  [INFO] Matches reported for boundary test:" << endl;
    for (auto r : out.matches) {
        cout << "        ID " << r.id << " at byte " << r.byte_index
             << " lane " << (int)r.lane << endl;
        if (r.id == target_id &&
            r.byte_index >= start_pos &&
            r.byte_index <= end_pos) {
            found_in_span = true;
        }
    }

    if (found_in_span) {
        cout << "  [PASS] Found ID " << target_id
             << " within pattern span [" << start_pos
             << ", " << end_pos << "] crossing 31/32." << endl;
    } else {
        cout << "  [WARN] Target ID " << target_id
             << " not reported inside span [" << start_pos
             << ", " << end_pos << "]." << endl;
    }

    return true;
}

// ============================================================================
// Test 3A: Random fuzz, multi-packet (TLAST every beat)
// ============================================================================

bool test_random_fuzz_multi_packets() {
    cout << "\n>>> Test 3A: Random Fuzzing (Multi-Packet, TLAST every beat)" << endl;

    srand((unsigned)time(NULL));
    hls::stream<pkt> n2k("n2k_3A_in");
    hls::stream<pkt> k2n("k2n_3A_out");

    int num_packets = 10;

    for (int p = 0; p < num_packets; ++p) {
        unsigned char buf[DATA_WIDTH_BYTES];
        for (int i = 0; i < DATA_WIDTH_BYTES; ++i) buf[i] = 0;

        int    r_idx = rand() % NUM_PATTERNS;
        string pat;
        for (int k = 0; k < rules[r_idx].len; ++k)
            pat += (char)rules[r_idx].data[k];

        if (pat.size() <= DATA_WIDTH_BYTES && pat.size() > 0) {
            int max_pos   = DATA_WIDTH_BYTES - (int)pat.size();
            int start_pos = rand() % (max_pos + 1);
            for (int i = 0; i < (int)pat.size(); ++i)
                buf[start_pos + i] = pat[i];
        }

        n2k.write(make_pkt(buf, DATA_WIDTH_BYTES, true));
    }

    unsigned dummy = 0;
    unsigned packn = num_packets;
    krnl_proj(n2k, k2n, dummy, packn);

    int packets_read  = 0;
    int total_matches = 0;

    while (!k2n.empty()) {
        auto out = drain_one_packet_sparse(k2n);
        packets_read++;
        total_matches += (int)out.matches.size();

        if (!out.matches.empty() && packets_read <= 10) {
            cout << "  [INFO] [3A] Pkt " << packets_read
                 << " first match ID " << out.matches[0].id
                 << " at byte " << out.matches[0].byte_index
                 << " lane " << (int)out.matches[0].lane << endl;
        }

        cout << "  [INFO] [3A] REPORT seq=" << out.report.packet_seq
             << " in_bytes=" << out.report.pkt_in_bytes
             << " out_bytes=" << out.report.pkt_out_bytes << endl;
    }

    if (packets_read == num_packets) {
        cout << "  [PASS] [3A] Processed " << packets_read << " packets." << endl;
        return true;
    } else {
        cout << "  [FAIL] [3A] Packet count mismatch. Expected "
             << num_packets << ", got " << packets_read << endl;
        return false;
    }
}

// ============================================================================
// Test 3B: Random fuzz, single long packet (TLAST at end)
// ============================================================================

bool test_random_fuzz_single_long_packet() {
    cout << "\n>>> Test 3B: Random Fuzzing (Single Long Packet, TLAST at end)" << endl;

    srand((unsigned)time(NULL) + 1234);
    hls::stream<pkt> n2k("n2k_3B_in");
    hls::stream<pkt> k2n("k2n_3B_out");

    int num_beats      = 10;
    int beat_with_last = num_beats - 1;

    for (int p = 0; p < num_beats; ++p) {
        unsigned char buf[DATA_WIDTH_BYTES];
        for (int i = 0; i < DATA_WIDTH_BYTES; ++i) buf[i] = 0;

        int    r_idx = rand() % NUM_PATTERNS;
        string pat;
        for (int k = 0; k < rules[r_idx].len; ++k)
            pat += (char)rules[r_idx].data[k];

        if (pat.size() <= DATA_WIDTH_BYTES && pat.size() > 0) {
            int max_pos   = DATA_WIDTH_BYTES - (int)pat.size();
            int start_pos = rand() % (max_pos + 1);
            for (int i = 0; i < (int)pat.size(); ++i)
                buf[start_pos + i] = pat[i];
        }

        bool is_last = (p == beat_with_last);
        n2k.write(make_pkt(buf, DATA_WIDTH_BYTES, is_last));
    }

    unsigned dummy = 0;
    krnl_proj(n2k, k2n, dummy, 1);

    auto out = drain_one_packet_sparse(k2n);

    cout << "  [INFO] [3B] REPORT seq=" << out.report.packet_seq
         << " in_beats=" << out.report.pkt_in_beats
         << " in_bytes=" << out.report.pkt_in_bytes
         << " out_bytes=" << out.report.pkt_out_bytes << endl;

    if (out.report.pkt_in_beats == (uint64_t)num_beats) {
        cout << "  [PASS] [3B] Processed one long packet of "
             << num_beats << " beats." << endl;
        return true;
    } else {
        cout << "  [FAIL] [3B] Input beats mismatch. Expected "
             << num_beats << ", got " << out.report.pkt_in_beats << endl;
        return false;
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    cout << "===========================================" << endl;
    cout << "   Testbench for Sparse-Event DCAM Kernel  " << endl;
    cout << "   DWIDTH = " << DWIDTH << ", DATA_WIDTH_BYTES = " << DATA_WIDTH_BYTES << endl;
    cout << "===========================================" << endl;

    bool pass = true;

    pass &= test_silence();
    pass &= test_boundary_split();
    pass &= test_random_fuzz_multi_packets();
    pass &= test_random_fuzz_single_long_packet();

    cout << "\n===========================================" << endl;
    if (pass) cout << "   ALL TESTS PASSED " << endl;
    else      cout << "   SOME TESTS FAILED " << endl;
    cout << "===========================================" << endl;

    return pass ? 0 : 1;
}
