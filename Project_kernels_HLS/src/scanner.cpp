#include "scanner.h"
#include "patterns.h"
#include <ap_int.h>

void dcam_step(
    unsigned char in_byte,
    bool reset,
    ap_uint<TDWIDTH> &dest_signal)
{
#pragma HLS INLINE

    // 历史记录寄存器
    static ap_uint<PATTERN_MAX_LEN> history[NUM_PATTERNS];
#pragma HLS ARRAY_PARTITION variable=history complete

    // 当前字节匹配结果
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
    // 初始化为 0xFFFF (代表本周期暂时无匹配)
    ap_uint<TDWIDTH> local_best = (ap_uint<TDWIDTH>)0xFFFF;

rule_loop:
    for (int r = 0; r < NUM_PATTERNS; ++r)
    {
#pragma HLS UNROLL
        // 强制 ID 为 16 位常量
        const ap_uint<TDWIDTH> pattern_id = r + 1;

        int len = rules[r].len;
        if (len <= 0) continue;

        bool match = true;

        // 检查规则的所有字节是否满足
    byte_check_loop:
        for (int k = 0; k < PATTERN_MAX_LEN; ++k) {
#pragma HLS UNROLL
            if (k < len) {
                unsigned char pb = rules[r].data[k];
                unsigned char tap = rules[r].tap_idx[k];
                match &= (history[pb][tap] != 0);
            }
        }

        // 如果当前规则在这一瞬间匹配成功
        if (match) {
            // 寻找当前并行匹配到的最小 ID
            if (pattern_id < local_best) {
                local_best = pattern_id;
            }
        }
    }

    // 4. 输出逻辑 (无闭锁/无状态保持)
    if (local_best == (ap_uint<TDWIDTH>)0xFFFF) {
        dest_signal = 0;
    } else {
        dest_signal = local_best;
    }

}