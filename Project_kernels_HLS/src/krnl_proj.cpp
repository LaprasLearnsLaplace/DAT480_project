#include "krnl_proj.h"
#include "scanner.h"
#include <iostream>

void krnl_proj(
    hls::stream<pkt> &n2k,
    hls::stream<pkt> &k2n,
    unsigned int dest)
{
#pragma HLS INTERFACE axis port = n2k
#pragma HLS INTERFACE axis port = k2n
#pragma HLS INTERFACE s_axilite port = dest bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

    static bool start_new_packet = true;

process_loop:
    while (1)
    {
        #pragma HLS LOOP_FLATTEN off 
#ifndef __SYNTHESIS__
        if (n2k.empty())
            break;
#endif

        pkt v_in;
        n2k.read(v_in);

        ap_uint<DWIDTH> data = v_in.data;

        unsigned char data_buffer[DATA_WIDTH_BYTES];
        #pragma HLS ARRAY_PARTITION variable=data_buffer complete

    fill_buffer_loop:
        for (int j = 0; j < DATA_WIDTH_BYTES; ++j) {
            #pragma HLS UNROLL 
            data_buffer[j] = data(j * 8 + 7, j * 8);
        }

        unsigned char next_byte; 
        unsigned char curr_byte; 

        bool next_reset; 
        bool curr_reset;

    byte_loop:
        for (int i = 0; i < DATA_WIDTH_BYTES + 1; ++i)
        {
            #pragma HLS PIPELINE II=1

            // --- 阶段 A: 预取 (Fetch & Pre-calculate) ---
            if (i < DATA_WIDTH_BYTES) {
                // 1. 读数据
                next_byte = data_buffer[i];
                
                // [关键修改] 2. 提前计算 Reset
                // 我们现在读的是 data_buffer[i]，它将在下一拍被处理。
                // 所以我们判断当前的 i 是否为 0 即可。
                // 这个比较操作 (icmp) 现在发生在 Fetch 阶段，不占用 Execute 阶段的时间！
                next_reset = start_new_packet && (i == 0);
            }

            // --- 阶段 B: 执行 (Execute) ---
            if (i > 0) {
                // 现在的 curr_reset 是直接从寄存器出来的
                // 延迟 ≈ 0ns，不再是 0.8ns！
                // 这一招直接为你抢回了将近 1ns 的时间。
                dcam_step(curr_byte, curr_reset, v_in.dest);
            }

            // --- 阶段 C: 移位 (Shift) ---
            curr_byte = next_byte;
            curr_reset = next_reset; // 传递 Reset 信号
        }

        if (v_in.last)
        {
            start_new_packet = true;
        }
        else
        {
            start_new_packet = false;
        }

        k2n.write(v_in);

#ifndef __SYNTHESIS__
        if (v_in.last)
            break;
#endif
    }
}