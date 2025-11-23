#include "scanner.h"
#include "patterns.h"
#include <ap_int.h>

void dcam_step(
    unsigned char in_byte,
    bool reset,
    ap_uint<TDWIDTH> &dest_signal)
{
#pragma HLS INLINE

    ap_uint<TDWIDTH> current_best = reset ? (ap_uint<TDWIDTH>)0 : dest_signal;

    static ap_uint<PATTERN_MAX_LEN> history[NUM_PATTERNS];
#pragma HLS ARRAY_PARTITION variable=history complete

    ap_uint<NUM_PATTERNS> byte_match;
#pragma HLS ARRAY_PARTITION variable=byte_match complete
    
decode_loop:
    for (int b = 0; b < NUM_PATTERNS; ++b)
    {
#pragma HLS UNROLL
        byte_match[b] = (in_byte == (unsigned char)b);
    }


update_history:
    for (int b = 0; b < NUM_PATTERNS; ++b)
    {
#pragma HLS UNROLL
        ap_uint<PATTERN_MAX_LEN> reg = reset ? (ap_uint<PATTERN_MAX_LEN>)0 : history[b];
        reg <<= 1;
        reg[0] = byte_match[b];
        history[b] = reg;
    }

    ap_uint<TDWIDTH> local_best = (ap_uint<TDWIDTH>)0xFFFF;


rule_loop:
    for (int r = 0; r < NUM_PATTERNS; ++r)
    {
#pragma HLS UNROLL 
        int len = rules[r].len;
        if (len <= 0)
            continue;

        bool match = true;

    // 内部字节检查 (并行 AND 树)
    byte_check_loop: 
        for (int k = 0; k < PATTERN_MAX_LEN; ++k)
        {
#pragma HLS UNROLL
            if (k < len) {
                unsigned char pb = rules[r].data[k];
                unsigned char tap = rules[r].tap_idx[k];

                match &= (history[pb][tap] != 0);
            }
        }

        
        if (match)
        {
            ap_uint<TDWIDTH> pattern_id = (ap_uint<TDWIDTH>)(r + 1);
            if (pattern_id < local_best)
            {
                local_best = pattern_id;
            }
        }
    }

    
    bool has_match = (local_best != (ap_uint<TDWIDTH>)0xFFFF);
    
    bool current_is_zero = (current_best == 0);
    bool new_is_better = (local_best < current_best);

    bool should_update = has_match && (current_is_zero || new_is_better);

    dest_signal = should_update ? local_best : current_best;
}