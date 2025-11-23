#include "scanner.h"
#include "patterns.h"
#include <ap_int.h>

// SRL-based streaming DCAM, 1 byte per call
void dcam_step(
    unsigned char in_byte,
    bool reset,
    ap_uint<TDWIDTH> &dest_signal)
{
#pragma HLS INLINE  // 让它被内联到 krnl_proj 的 byte pipeline 里
    if (reset) {
        dest_signal = 0;   
    }

    // 对每个可能的字节值（0..255）维护一条长度 PATTERN_MAX_LEN 的 SRL 线
    // history[b][0]  = 当前时刻是否看到字节 b
    // history[b][1]  = 上一个时刻是否看到字节 b
    // ...
    // history[b][k]  = k 个 cycle 之前是否看到字节 b
    static ap_uint<PATTERN_MAX_LEN> history[NUM_PATTERNS];
#pragma HLS ARRAY_PARTITION variable=history complete

    // =========================================================
    // 1) 更新所有 256 条 SRL 线（decoder + shift register）
    //    reset = true 时，相当于从全 0 的历史开始（包之间不跨越）
    // =========================================================
update_history:
    for (int b = 0; b < NUM_PATTERNS; ++b)
    {
#pragma HLS UNROLL
        // reset 时忽略之前的内容，相当于 history[b] = 0
        ap_uint<PATTERN_MAX_LEN> reg = reset ? (ap_uint<PATTERN_MAX_LEN>)0 : history[b];

        // 左移一位，腾出 bit0 放“当前 byte 是否等于 b”
        reg <<= 1;

        bool hit = (in_byte == (unsigned char)b);
        reg[0] = hit ? 1 : 0;

        history[b] = reg;
    }

    // =========================================================
    // 2) 在当前时刻，用 history 做 DCAM 匹配
    //
    //    pattern 的字节序：rules[r].data[0 .. len-1]
    //    表示序列 P0 P1 ... P(len-1)
    //
    //    当前 cycle 时，如果最近 len 个字节等于这个序列：
    //      time t-len+1: P0
    //      ...
    //      time t-1    : P(len-2)
    //      time t      : P(len-1)
    //
    //    则各个字符对应的 tap：
    //      history[P(len-1)][0]      == 1
    //      history[P(len-2)][1]      == 1
    //      ...
    //      history[P0][len-1]        == 1
    //
    //    一般公式：第 k 个字符 Pk 需要 history[Pk][len-1-k] == 1
    // =========================================================
    ap_uint<TDWIDTH> local_best = (ap_uint<TDWIDTH>)0xFFFF;

rule_loop:
    for (int r = 0; r < NUM_PATTERNS; ++r)
    {
#pragma HLS UNROLL factor=16

        int len = rules[r].len;
        if (len <= 0)
            continue;

        bool match = true;

    byte_loop:
        for (int k = 0; k < PATTERN_MAX_LEN; ++k)
        {
#pragma HLS UNROLL
            if (k >= len)
                break;

            unsigned char pb = rules[r].data[k];
            int tap = len - 1 - k;   // 看上面注释：Pk 对应 history[pb][len-1-k]

            if (history[pb][tap] == 0)
            {
                match = false;
                break;
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

    // =========================================================
    // 3) 更新 AXI dest 字段（和你之前逻辑一致：取最小 ID）
    // =========================================================
    if (local_best != (ap_uint<TDWIDTH>)0xFFFF)
    {
        if (dest_signal == 0 || local_best < dest_signal)
        {
            dest_signal = local_best;
        }
    }
}
