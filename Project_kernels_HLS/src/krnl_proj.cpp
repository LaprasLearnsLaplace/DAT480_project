#include "krnl_proj.h"
#include "scanner.h"
#include <iostream>

/**
 * @brief 模式匹配内核
 * @param n2k         输入流 (来自网络或上一级)
 * @param k2n         输出流 (发送给 S2MM 写入内存)
 * @param dest        (保留参数) 目的地址或其他配置
 * @param num_packets 控制运行模式:
 *                     0 = 无限循环 (硬件)
 *                     N = 处理完 N 个 packet (TLAST) 后退出 (仿真)
 */
void krnl_proj(
    hls::stream<pkt> &n2k,
    hls::stream<pkt> &k2n,
    unsigned int dest,
    unsigned int num_packets
)
{
#pragma HLS INTERFACE axis port = n2k
#pragma HLS INTERFACE axis port = k2n depth=256
#pragma HLS INTERFACE s_axilite port = dest        bundle = control
#pragma HLS INTERFACE s_axilite port = num_packets bundle = control
#pragma HLS INTERFACE s_axilite port = return      bundle = control

    unsigned int packet_count = 0;

#ifndef __SYNTHESIS__
    // 仿真保护 避免死循环
    if (num_packets == 0)
        num_packets = 1;
#endif

packet_loop:
    while ( (num_packets == 0) || (packet_count < num_packets) )
    {
        bool start_new_packet = true;

    beat_loop:
        while (1)
        {
        #pragma HLS LOOP_FLATTEN off

            // ========= 1. 读取一个 512-bit beat =========
            pkt v_in;
            n2k.read(v_in);

            ap_uint<DWIDTH> data = v_in.data;

            // 将 512-bit 拆成 64 个 byte
            unsigned char data_buffer[DATA_WIDTH_BYTES];
        #pragma HLS ARRAY_PARTITION variable=data_buffer complete

        fill_buffer:
            for (int j = 0; j < DATA_WIDTH_BYTES; ++j) {
            #pragma HLS UNROLL
                data_buffer[j] = data(j * 8 + 7, j * 8);
            }

            // ========= 2. 为 64 个字节准备两个 512-bit 输出缓冲 =========
            ap_uint<512> packer_low  = 0; // 对应 byte 0..31
            ap_uint<512> packer_high = 0; // 对应 byte 32..63

            unsigned char      next_byte = 0;
            unsigned char      curr_byte = 0;
            ap_uint<TDWIDTH>   match_id  = 0;

        // 手动 N+1 pipeline（65 次），第 0 次只预取，不调 dcam
        byte_loop:
            for (int i = 0; i < DATA_WIDTH_BYTES + 1; ++i) {
            #pragma HLS PIPELINE II=1

                // Stage A: 预取下一个字节
                if (i < DATA_WIDTH_BYTES) {
                    next_byte = data_buffer[i];
                }

                // Stage B: 对上一个字节调用 dcam_step
                if (i > 0) {
                    int k = i - 1;
                    bool this_reset = start_new_packet && (k == 0);

                    match_id = 0;
                    dcam_step(curr_byte, this_reset, match_id);
                    if (k < 32) {
                        packer_low(k * 16 + 15, k * 16) = match_id;
                    } else {
                        int k_off = k - 32;
                        packer_high(k_off * 16 + 15, k_off * 16) = match_id;
                    }
                }

                // Stage C: 移位寄存器
                curr_byte = next_byte;
            }

            // ========= 3. 写出两个结果 beat =========
            pkt o1, o2;

            // 前 32 个字节的匹配结果
            o1.data = packer_low;
            o1.keep = v_in.keep;
            o1.dest = 0;
            o1.last = 0;          // 因为后面还有 high 部分
            k2n.write(o1);

            // 后 32 个字节的匹配结果
            o2.data = packer_high;
            o2.keep = v_in.keep;
            o2.dest = 0;
            o2.last = v_in.last;  // 将输入 TLAST 传递给最后的 high-beat
            k2n.write(o2);

            // ========= 4. packet 结束判断 =========
            if (v_in.last) {
                // 一个 AXI packet 结束
                break;
            } else {
                // 同一个 packet 中的后续 beat，start_new_packet 置 0
                start_new_packet = false;
            }
        } // end beat_loop

        packet_count++;
    } // end packet_loop
}
