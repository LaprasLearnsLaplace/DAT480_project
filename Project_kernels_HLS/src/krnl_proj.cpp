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

    // 标记下一次读取的字节是否为新的 packet 开始
    static bool start_new_packet = true;

process_loop:
    while (1)
    {
#ifndef __SYNTHESIS__
        if (n2k.empty())
            break;
#endif

        pkt v_in;
        n2k.read(v_in);

        ap_uint<DWIDTH> data = v_in.data;

    byte_loop:
        for (int i = 0; i < DATA_WIDTH_BYTES; ++i)
        {
#pragma HLS PIPELINE II=1

            // 从 512-bit data 中提取第 i 个字节
            ap_uint<8> tmp = (data >> (8 * i)) & 0xFF;
            unsigned char in_byte = (unsigned char)tmp;

            // 判断是否为 packet 的第一个字节
            bool reset = start_new_packet && (i == 0);

            // DCAM 单字节更新 + 匹配
            dcam_step(in_byte, reset, v_in.dest);
        }

        if (v_in.last)
        {
            start_new_packet = true;
        }
        else
        {
            start_new_packet = false;
        }

        // 输出数据包
        k2n.write(v_in);

#ifndef __SYNTHESIS__
        if (v_in.last)
            break;
#endif
    }
}
