#include "scanner.h"
#include "patterns.h"
#include <ap_int.h>

/**
 * 优化版 DCAM 实现
 * 关键优化: 
 * 1. 使用 ap_uint 替代 bool 数组，减少资源
 * 2. 预计算 tap 索引，避免运行时计算
 * 3. 循环展开优化
 */
void dcam_step_multi(
    unsigned char        in_bytes[DCAM_P],
    bool                 reset,
    ap_uint<TDWIDTH>     out_ids[DCAM_P]
)
{
#pragma HLS INLINE off
#pragma HLS PIPELINE II=1

    // 静态移位寄存器 - 每个unique byte一个
    static ap_uint<HISTORY_LEN> history[NUM_USED_BYTES];
#pragma HLS ARRAY_PARTITION variable=history complete dim=1
#pragma HLS RESET variable=history  // 可选：硬件复位

    // ================================================================
    // STAGE 1: DECODE + SHIFT
    // ================================================================
    // 合并decoder和shift操作，减少中间变量
    
decode_and_shift: 
    for (int b = 0; b < NUM_USED_BYTES; b++) {
    #pragma HLS UNROLL
        unsigned char target = used_bytes[b];
        ap_uint<DCAM_P> match_bits = 0;
        
        // Decode:  检查每个输入字节是否匹配
    decode_phase:
        for (int p = 0; p < DCAM_P; p++) {
        #pragma HLS UNROLL
            match_bits[DCAM_P - 1 - p] = (in_bytes[p] == target);
        }
        
        // Shift: 左移P位，新数据填入低位
        ap_uint<HISTORY_LEN> new_history;
        if (reset) {
            new_history = 0;
        } else {
            new_history = history[b] << DCAM_P;
        }
        new_history(DCAM_P - 1, 0) = match_bits;
        history[b] = new_history;
    }

    // ================================================================
    // STAGE 2: PATTERN MATCH
    // ================================================================
    // P个并行输出，每个对应一个结束位置
    
match_output:
    for (int end_pos = 0; end_pos < DCAM_P; end_pos++) {
    #pragma HLS UNROLL
        ap_uint<TDWIDTH> matched_id = 0;
        
    check_patterns:
        for (int r = 0; r < NUM_PATTERNS; r++) {
        #pragma HLS UNROLL factor=16  // 部分展开，平衡资源和性能
            
            int len = rules[r]. len;
            bool match = (len > 0);
            
            // 检查pattern的每个字符
        check_chars:
            for (int k = 0; k < PATTERN_MAX_LEN; k++) {
            #pragma HLS UNROLL
                if (k < len) {
                    int byte_idx = rules[r].byte_index[k];
                    // Tap计算:  当前位置 + 字符在pattern中的偏移
                    int tap = (DCAM_P - 1 - end_pos) + (len - 1 - k);
                    match &= (bool)history[byte_idx][tap];
                }
            }
            
            // 优先级编码：取第一个匹配
            if (match && matched_id == 0) {
                matched_id = (ap_uint<TDWIDTH>)(r + 1);
            }
        }
        
        out_ids[end_pos] = matched_id;
    }
}