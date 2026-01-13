#include "krnl_proj.h"
#include "scanner.h"
#include <ap_int.h>
#include <hls_stream.h>

static const int EVENT_W = 128;
static const int SLOTS_PER_BEAT = DWIDTH / EVENT_W;

struct match_event {
    ap_uint<64> byte_index;
    ap_uint<16> pattern_id;
    ap_uint<8> lane;
    ap_uint<2> control;  // 0=valid, 1=beat_end, 2=packet_end
};

// ============================================================
// 进程 1：处理输入并发送事件（支持并行 DCAM_P）
// ============================================================
void process_and_stream(
    hls::stream<pkt> &n2k,
    hls::stream<match_event> &match_stream,
    unsigned int num_packets)
{
    uint64_t global_byte_idx = 0;

packet_loop:
    for (unsigned pkt_idx = 0; pkt_idx < num_packets; pkt_idx++)
    {
        bool first_beat = true;

    beat_loop:
        while (true)
        {
#pragma HLS LOOP_TRIPCOUNT min=1 max=22
            pkt v_in = n2k.read(); // 定义在 beat_loop 内，确保 byte_loop 可访问
            bool is_last = (v_in.last == 1);
            ap_uint<64> base_idx = global_byte_idx; // 定义 base_idx

        byte_loop:
            for (int i = 0; i < DATA_WIDTH_BYTES; i += DCAM_P)
            {
#pragma HLS PIPELINE II=1

                unsigned char bytes[DCAM_P];
#pragma HLS ARRAY_PARTITION variable=bytes complete
                
                // 正确提取 DCAM_P 个字节
                for (int p = 0; p < DCAM_P; p++) {
#pragma HLS UNROLL
                    bytes[p] = (unsigned char)v_in.data.range((i + p + 1) * 8 - 1, (i + p) * 8);
                }

                ap_uint<16> out_ids[DCAM_P];
#pragma HLS ARRAY_PARTITION variable=out_ids complete

                bool reset = (first_beat && (i == 0));
                dcam_step_multi(bytes, reset, out_ids);

                // 检查这组并行字节中的匹配
                for (int p = 0; p < DCAM_P; p++) {
#pragma HLS UNROLL
                    if (out_ids[p] != 0) {
                        match_event me;
                        me.byte_index = base_idx + i + p;
                        me.pattern_id = out_ids[p];
                        me.lane = p;
                        me.control = 0;
                        match_stream.write(me);
                    }
                }
            }

            match_event end_marker;
            end_marker.byte_index = 0;
            end_marker.pattern_id = 0;
            end_marker.lane = 0;
            end_marker.control = is_last ? 2 : 1; 
            match_stream.write(end_marker);

            global_byte_idx += DATA_WIDTH_BYTES;
            first_beat = false;

            if (is_last) break;
        }
    }
}

// ============================================================
// 进程 2：打包输出（保持与 TB 的解析逻辑对齐）
// ============================================================
void collect_and_output(
    hls::stream<match_event> &match_stream,
    hls::stream<pkt> &k2n,
    unsigned int dest,
    unsigned int num_packets)
{
    ap_uint<EVENT_W> batch[SLOTS_PER_BEAT];
#pragma HLS ARRAY_PARTITION variable=batch complete
    int batch_count = 0;

packet_loop:
    for (unsigned pkt_idx = 0; pkt_idx < num_packets; pkt_idx++)
    {
    event_loop:
        while (true)
        {
#pragma HLS PIPELINE II=1
            match_event me = match_stream.read();

            if (me.control == 0) {
                ap_uint<EVENT_W> e = 0;
                // 按照 TB 修正后的逻辑进行 Packing
                e.range(127, 64) = me.byte_index; // 字节 8-15
                e.range(63, 48)  = me.pattern_id; // 字节 6-7
                e.range(47, 40)  = me.lane;       // 字节 5
                
                batch[batch_count] = e;
                batch_count++;

                if (batch_count == SLOTS_PER_BEAT) {
                    pkt o;
                    o.data = 0;
                    for (int s = 0; s < SLOTS_PER_BEAT; s++) {
#pragma HLS UNROLL
                        o.data.range((s + 1) * EVENT_W - 1, s * EVENT_W) = batch[s];
                    }
                    o.keep = 0xFFFFFFFFFFFFFFFFULL;
                    o.last = 0; o.dest = dest; o.user = 0; o.id = 0;
                    k2n.write(o);
                    batch_count = 0;
                }
            } else {
                if (batch_count > 0) {
                    pkt o; o.data = 0;
                    for (int s = 0; s < SLOTS_PER_BEAT; s++) {
#pragma HLS UNROLL
                        if (s < batch_count) o.data.range((s + 1) * EVENT_W - 1, s * EVENT_W) = batch[s];
                    }
                    // 根据 count 设置 keep
                    if (batch_count == 1) o.keep = 0x000000000000FFFFULL;
                    else if (batch_count == 2) o.keep = 0x00000000FFFFFFFFULL;
                    else if (batch_count == 3) o.keep = 0x0000FFFFFFFFFFFFULL;
                    else o.keep = 0xFFFFFFFFFFFFFFFFULL;
                    o.last = 0; o.dest = dest; o.user = 0; o.id = 0;
                    k2n.write(o);
                    batch_count = 0;
                }
                if (me.control == 2) {
                    pkt end_pkt;
                    end_pkt.data = 0; end_pkt.data.range(7, 0) = 0xEE;
                    end_pkt.keep = 0xFF; end_pkt.last = 1; 
                    end_pkt.dest = dest; end_pkt.user = 0; end_pkt.id = 0;
                    k2n.write(end_pkt);
                    break;
                }
            }
        }
    }
}

void krnl_proj(hls::stream<pkt> &n2k, hls::stream<pkt> &k2n, unsigned int dest, unsigned int num_packets) {
#pragma HLS INTERFACE axis port=n2k
#pragma HLS INTERFACE axis port=k2n
#pragma HLS INTERFACE s_axilite port=dest bundle=control
#pragma HLS INTERFACE s_axilite port=num_packets bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control
#pragma HLS DATAFLOW
    hls::stream<match_event> match_fifo;
#pragma HLS STREAM variable=match_fifo depth=64
    process_and_stream(n2k, match_fifo, num_packets);
    collect_and_output(match_fifo, k2n, dest, num_packets);
}