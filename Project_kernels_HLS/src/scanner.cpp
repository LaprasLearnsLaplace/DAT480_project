#include "scanner.h"
#include "patterns.h"
#include <ap_int.h>

/**
 * 符合论文架构的多字节 DCAM 实现
 * 参考: Lecture 3, Slides 29-35 (Sourdis & Pnevmatikatos, FCCM 2004)
 */
void dcam_step_multi(
    unsigned char        in_bytes[DCAM_P],
    bool                 reset,
    ap_uint<TDWIDTH>     out_ids[DCAM_P]
)
{
#pragma HLS INLINE off
#pragma HLS PIPELINE II=1

    // ================================================================
    // STAGE 1: DECODER
    // ================================================================
    // one_hot[byte_idx][phase] = 1 当且仅当 in_bytes[phase] == used_bytes[byte_idx]
    
    ap_uint<DCAM_P> one_hot[NUM_USED_BYTES];
#pragma HLS ARRAY_PARTITION variable=one_hot complete

decode_stage:
    for (int b = 0; b < NUM_USED_BYTES; b++) {
    #pragma HLS UNROLL
        unsigned char target = used_bytes[b];
        ap_uint<DCAM_P> match_bits = 0;
        
    decode_phases:
        for (int p = 0; p < DCAM_P; p++) {
        #pragma HLS UNROLL
            // in_bytes[0] 最早，存入 bit P-1
            // in_bytes[P-1] 最晚，存入 bit 0
            match_bits[DCAM_P - 1 - p] = (in_bytes[p] == target);
        }
        one_hot[b] = match_bits;
    }

    // ================================================================
    // STAGE 2: SHIFT REGISTER (SRL16)
    // ================================================================
    // 共享移位寄存器，每拍左移 P 位
    
    static ap_uint<HISTORY_LEN> history[NUM_USED_BYTES];
#pragma HLS ARRAY_PARTITION variable=history complete

shift_stage:
    for (int b = 0; b < NUM_USED_BYTES; b++) {
    #pragma HLS UNROLL
        ap_uint<HISTORY_LEN> reg;
        
        if (reset) {
            reg = 0;
        } else {
            reg = history[b] << DCAM_P;
        }
        
        // 将 one_hot 的 P 位填入低位
        reg(DCAM_P - 1, 0) = one_hot[b];
        
        history[b] = reg;
    }

    // ================================================================
    // STAGE 3: PATTERN MATCHER
    // ================================================================
    // P 个并行匹配器
    // Tap 公式: tap = (P - 1 - end_pos) + (len - 1 - k)
    
match_stage:
    for (int end_pos = 0; end_pos < DCAM_P; end_pos++) {
    #pragma HLS UNROLL
        ap_uint<TDWIDTH> best_match = 0;
        
    check_rules:
        for (int r = 0; r < NUM_PATTERNS; r++) {
        #pragma HLS UNROLL
            int len = rules[r].len;
            if (len <= 0) continue;
            
            bool match = true;
            
        check_chars:
            for (int k = 0; k < PATTERN_MAX_LEN; k++) {
            #pragma HLS UNROLL
                if (k < len) {
                    int byte_idx = rules[r].byte_index[k];
                    int tap = (DCAM_P - 1 - end_pos) + (len - 1 - k);
                    match &= (bool)history[byte_idx][tap];
                }
            }
            
            if (match && best_match == 0) {
                best_match = (ap_uint<TDWIDTH>)(r + 1);
            }
        }
        
        out_ids[end_pos] = best_match;
    }
}