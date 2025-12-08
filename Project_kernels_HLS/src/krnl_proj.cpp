#include "krnl_proj.h"
#include "scanner.h"

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
    if (num_packets == 0) num_packets = 1;
#endif

packet_loop:
    while ((num_packets == 0) || (packet_count < num_packets))
    {
        bool start_new_packet = true;

    beat_loop:
        while (1)
        {
        #pragma HLS LOOP_FLATTEN off

            pkt v_in;
            n2k.read(v_in);

            ap_uint<DWIDTH> data = v_in.data;

            // 分割为 64 字节
            unsigned char data_buffer[DATA_WIDTH_BYTES];
        #pragma HLS ARRAY_PARTITION variable=data_buffer complete

        fill_buffer:
            for (int j = 0; j < DATA_WIDTH_BYTES; ++j) {
            #pragma HLS UNROLL
                data_buffer[j] = data(j * 8 + 7, j * 8);
            }

            // ============================================
            // 关键修改：使用临时数组存储结果
            // ============================================
            ap_uint<TDWIDTH> match_results[DATA_WIDTH_BYTES];
        #pragma HLS ARRAY_PARTITION variable=match_results complete

            const int P = DCAM_P;

        byte_loop:
            for (int i = 0; i < DATA_WIDTH_BYTES; i += P) {
            #pragma HLS PIPELINE II=1

                unsigned char    in_bytes[P];
                ap_uint<TDWIDTH> match_ids[P];
            #pragma HLS ARRAY_PARTITION variable=in_bytes  complete
            #pragma HLS ARRAY_PARTITION variable=match_ids complete

            prepare_inputs:
                for (int p = 0; p < P; ++p) {
                #pragma HLS UNROLL
                    in_bytes[p] = data_buffer[i + p];
                }

                bool reset = start_new_packet && (i == 0);

                dcam_step_multi(in_bytes, reset, match_ids);

                // 直接写入临时数组（固定偏移，无 MUX 链）
            store_results:
                for (int p = 0; p < P; ++p) {
                #pragma HLS UNROLL
                    match_results[i + p] = match_ids[p];
                }
            }

            // ============================================
            // 打包阶段：完全展开，固定索引
            // ============================================
            ap_uint<512> packer_low  = 0;
            ap_uint<512> packer_high = 0;

        pack_low:
            for (int k = 0; k < 32; ++k) {
            #pragma HLS UNROLL
                packer_low(k * 16 + 15, k * 16) = match_results[k];
            }

        pack_high:
            for (int k = 0; k < 32; ++k) {
            #pragma HLS UNROLL
                packer_high(k * 16 + 15, k * 16) = match_results[32 + k];
            }

            // 输出
            pkt o1, o2;

            o1.data = packer_low;
            o1.keep = v_in.keep;
            o1.dest = 0;
            o1.last = 0;
            k2n.write(o1);

            o2.data = packer_high;
            o2.keep = v_in.keep;
            o2.dest = 0;
            o2.last = v_in.last;
            k2n.write(o2);

            if (v_in.last) {
                break;
            } else {
                start_new_packet = false;
            }
        }

        packet_count++;
    }
}