#include "krnl_proj.h"
#include "scanner.h"
#include <ap_int.h>
#include <hls_stream.h>

static const int EVENT_W = 128;
static const int SLOTS_PER_BEAT = DWIDTH / EVENT_W;

// 关键优化：使用更小的结构，减少 FIFO 带宽
struct match_event {
    ap_uint<64> byte_index;
    ap_uint<16> pattern_id;
    ap_uint<8> lane;
    ap_uint<2> control;  // 0=valid, 1=beat_end, 2=packet_end
};

// ============================================================
// 进程 1：处理输入并发送事件（优化版）
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

            pkt v_in = n2k.read();
            bool is_last = (v_in.last == 1);
            
            // 优化 1：提前计算，避免在循环内重复计算
            ap_uint<64> base_idx = global_byte_idx;

            // 处理字节
        byte_loop:
            for (int i = 0; i < DATA_WIDTH_BYTES; i += DCAM_P)
            {
#pragma HLS PIPELINE II=1

                // 优化 2：一次性提取，避免多次 range 操作
                unsigned char byte_val = (unsigned char)v_in.data.range((i + 1) * 8 - 1, i * 8);
                
                unsigned char bytes[DCAM_P];
#pragma HLS ARRAY_PARTITION variable=bytes complete
                bytes[0] = byte_val;

                ap_uint<16> out_ids[DCAM_P];
#pragma HLS ARRAY_PARTITION variable=out_ids complete

                bool reset = (first_beat && (i == 0));
                dcam_step_multi(bytes, reset, out_ids);

                // 优化 3：简化结构，减少位操作
                ap_uint<16> id = out_ids[0];
                
                if (id != 0)
                {
#pragma HLS OCCURRENCE cycle=4
                    match_event me;
                    me.byte_index = base_idx + i;
                    me.pattern_id = id;
                    me.lane = 0;
                    me.control = 0;  // valid event
                    match_stream.write(me);
                }
            }

            // Beat 结束标记
            match_event end_marker;
            end_marker.byte_index = 0;
            end_marker.pattern_id = 0;
            end_marker.lane = 0;
            end_marker.control = is_last ? 2 : 1;  // 1=beat_end, 2=packet_end
            match_stream.write(end_marker);

            global_byte_idx += DATA_WIDTH_BYTES;
            first_beat = false;

            if (is_last)
                break;
        }
    }
}

// ============================================================
// 进程 2：接收并打包输出（延迟 packing，减轻时序压力）
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
#pragma HLS LOOP_TRIPCOUNT min=1 max=1024

            match_event me = match_stream.read();

            // 在这里做 packing，减轻 process_and_stream 的时序压力
            if (me.control == 0)  // valid event
            {
                ap_uint<EVENT_W> e = 0;
                e.range(127, 64) = me.byte_index;
                e.range(63, 48) = me.pattern_id;
                e.range(47, 40) = me.lane;
                
                batch[batch_count] = e;
                batch_count++;

                if (batch_count == SLOTS_PER_BEAT)
                {
                    pkt o;
                    o.dest = dest;
                    o.user = 0;
                    o.id = 0;
                    o.last = 0;
                    o.data = 0;

                    for (int s = 0; s < SLOTS_PER_BEAT; s++)
                    {
#pragma HLS UNROLL
                        o.data.range((s + 1) * EVENT_W - 1, s * EVENT_W) = batch[s];
                    }
                    o.keep = 0xFFFFFFFFFFFFFFFFULL;
                    k2n.write(o);
                    batch_count = 0;
                }
            }
            else  // beat_end or packet_end
            {
                if (batch_count > 0)
                {
                    pkt o;
                    o.dest = dest;
                    o.user = 0;
                    o.id = 0;
                    o.last = 0;
                    o.data = 0;

                    for (int s = 0; s < SLOTS_PER_BEAT; s++)
                    {
#pragma HLS UNROLL
                        if (s < batch_count)
                        {
                            o.data.range((s + 1) * EVENT_W - 1, s * EVENT_W) = batch[s];
                        }
                    }

                    if (batch_count == 1)
                        o.keep = 0x000000000000FFFFULL;
                    else if (batch_count == 2)
                        o.keep = 0x00000000FFFFFFFFULL;
                    else if (batch_count == 3)
                        o.keep = 0x0000FFFFFFFFFFFFULL;
                    else
                        o.keep = 0xFFFFFFFFFFFFFFFFULL;

                    k2n.write(o);
                    batch_count = 0;
                }

                if (me.control == 2)  // packet_end
                {
                    pkt end_pkt;
                    end_pkt.data = 0;
                    end_pkt.data.range(7, 0) = 0xEE;
                    end_pkt.keep = 0x00000000000000FFULL;
                    end_pkt.dest = dest;
                    end_pkt.user = 0;
                    end_pkt.id = 0;
                    end_pkt.last = 1;
                    k2n.write(end_pkt);
                    break;
                }
            }
        }
    }
}

// ============================================================
// 顶层函数
// ============================================================
void krnl_proj(
    hls::stream<pkt> &n2k,
    hls::stream<pkt> &k2n,
    unsigned int dest,
    unsigned int num_packets)
{
#pragma HLS INTERFACE axis port=n2k
#pragma HLS INTERFACE axis port=k2n depth=1024
#pragma HLS INTERFACE s_axilite port=dest bundle=control
#pragma HLS INTERFACE s_axilite port=num_packets bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

#pragma HLS DATAFLOW

    hls::stream<match_event> match_fifo;
#pragma HLS STREAM variable=match_fifo depth=64

    process_and_stream(n2k, match_fifo, num_packets);
    collect_and_output(match_fifo, k2n, dest, num_packets);
}