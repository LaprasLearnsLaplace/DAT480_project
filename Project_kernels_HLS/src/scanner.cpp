#include "scanner.h"
#include "patterns.h"
#include <ap_int.h>

void dcam_step(
    unsigned char in_byte,
    bool reset,
    ap_uint<TDWIDTH> &dest_signal)
{
#pragma HLS INLINE

    // History registers
    static ap_uint<PATTERN_MAX_LEN> history[NUM_PATTERNS];
#pragma HLS ARRAY_PARTITION variable=history complete

    // Per-byte match vector
    ap_uint<NUM_PATTERNS> byte_match;
#pragma HLS ARRAY_PARTITION variable=byte_match complete

    // 1. Decode
decode_loop:
    for (int b = 0; b < NUM_PATTERNS; ++b) {
#pragma HLS UNROLL
        byte_match[b] = (in_byte == (unsigned char)b);
    }

    // 2. Update History
update_history:
    for (int b = 0; b < NUM_PATTERNS; ++b) {
#pragma HLS UNROLL
        ap_uint<PATTERN_MAX_LEN> reg = reset ? (ap_uint<PATTERN_MAX_LEN>)0 : history[b];
        reg <<= 1;
        reg[0] = byte_match[b];
        history[b] = reg;
    }

    // 3. Find Local Best
    // Init to 0xFFFF (means no match this cycle)
    ap_uint<TDWIDTH> local_best = (ap_uint<TDWIDTH>)0xFFFF;

rule_loop:
    for (int r = 0; r < NUM_PATTERNS; ++r)
    {
#pragma HLS UNROLL
        // Force ID to 16-bit constant
        const ap_uint<TDWIDTH> pattern_id = r + 1;

        int len = rules[r].len;
        if (len <= 0) continue;

        bool match = true;

        // Check all bytes of the rule
    byte_check_loop:
        for (int k = 0; k < PATTERN_MAX_LEN; ++k) {
#pragma HLS UNROLL
            if (k < len) {
                unsigned char pb = rules[r].data[k];
                unsigned char tap = rules[r].tap_idx[k];
                match &= (history[pb][tap] != 0);
            }
        }

        // If this rule matches at this moment
        if (match) {
            // Keep the smallest matching ID
            if (pattern_id < local_best) {
                local_best = pattern_id;
            }
        }
    }

    // 4. Output logic (stateless)
    if (local_best == (ap_uint<TDWIDTH>)0xFFFF) {
        dest_signal = 0;
    } else {
        dest_signal = local_best;
    }

}
