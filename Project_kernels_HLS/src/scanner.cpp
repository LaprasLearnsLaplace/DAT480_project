#include "scanner.h"
#include "patterns.h" // 假设 rules 和 used_bytes 在这里定义

void dcam_step_multi(
    unsigned char     in_bytes[DCAM_P],
    bool              reset,
    ap_uint<TDWIDTH>  out_ids[DCAM_P]
) {
#pragma HLS PIPELINE II=1
#pragma HLS INLINE off
// 增加流水线级数以改善时序：允许最多3级流水线
#pragma HLS LATENCY min=1 max=3

    // 延迟链：存储每个字符在过去时刻的匹配情况
    static ap_uint<HISTORY_LEN> history[NUM_USED_BYTES];
#pragma HLS ARRAY_PARTITION variable=history complete dim=1

    // 1. Decoder + Shift: 将输入字节与目标字符库比对并移入 history
decode_shift:
    for (int b = 0; b < NUM_USED_BYTES; b++) {
    #pragma HLS UNROLL
        ap_uint<DCAM_P> match_bits = 0;
        for (int p = 0; p < DCAM_P; p++) {
        #pragma HLS UNROLL
            match_bits[DCAM_P - 1 - p] = (in_bytes[p] == used_bytes[b]);
        }
        
        // 更新延迟链
        if (reset) history[b] = 0;
        else       history[b] = (history[b] << DCAM_P) | match_bits;
    }

    // 2. Parallel Match: 检查每个位置是否构成完整模式
    // 时序优化：使用寄存器暂存中间结果，减少组合逻辑路径
output:
    for (int end_pos = 0; end_pos < DCAM_P; end_pos++) {
    #pragma HLS UNROLL
        ap_uint<TDWIDTH> matched_id = 0;
        bool found = false;

    pattern_loop:
        for (int r = 0; r < NUM_PATTERNS; r++) {
            // 注意：若资源紧张，可将 factor 调小或取消 UNROLL
            // 时序优化：减少展开因子以降低关键路径
            #pragma HLS UNROLL factor=2 

            int len = rules[r].len;
            // 时序优化：提前退出条件检查
            if (found || len == 0) continue;

            // 时序优化：使用寄存器暂存匹配结果，分阶段计算
            bool match = true;
            bool history_bits[PATTERN_MAX_LEN];
        #pragma HLS ARRAY_PARTITION variable=history_bits complete
            
        char_loop:
            for (int k = 0; k < PATTERN_MAX_LEN; k++) {
            #pragma HLS UNROLL
                if (k < len) {
                    int byte_idx = rules[r].byte_index[k];
                    // 计算该字符对应的历史偏移量
                    int offset = (DCAM_P - 1 - end_pos) + (len - 1 - k);
                    
                    // 时序优化：先读取历史位到寄存器，再进行比较
                    bool bit_match = false;
                    if (offset < HISTORY_LEN && byte_idx < NUM_USED_BYTES) {
                        bit_match = (bool)history[byte_idx][offset];
                    }
                    // 使用寄存器暂存，减少组合逻辑深度
                    history_bits[k] = bit_match;
                } else {
                    history_bits[k] = true; // 超出长度的位置视为匹配
                }
            }
            
            // 时序优化：分阶段计算匹配结果
            bool pattern_match = true;
            for (int k = 0; k < PATTERN_MAX_LEN; k++) {
            #pragma HLS UNROLL
                if (k < len) {
                    pattern_match = pattern_match && history_bits[k];
                }
            }

            if (pattern_match) {
                matched_id = (ap_uint<TDWIDTH>)(r + 1);
                found = true;
            }
        }
        out_ids[end_pos] = matched_id;
    }
}