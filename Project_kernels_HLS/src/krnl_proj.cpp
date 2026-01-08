#include "krnl_proj.h"
#include "scanner.h"
#include <ap_int.h>
#include <hls_stream.h>

// ==================== 常量定义 ====================
static const int EVENT_W = 128;
static const int EVENT_BYTES = EVENT_W / 8;

// ==================== 辅助函数 ====================

/**
 * @brief 打包事件到128位AXIS数据
 * 格式：[127:64] byte_index, [63:48] pattern_id, [47:40] lane
 * 优化：使用位拼接操作，减少中间变量
 */
static inline ap_uint<EVENT_W> pack_event(
    uint64_t byte_index,
    uint16_t pattern_id,
    uint8_t lane)
{
#pragma HLS INLINE
    // 使用位拼接，避免中间变量，优化时序
    ap_uint<EVENT_W> w;
    w(127, 64) = ap_uint<64>(byte_index);
    w(63, 48) = ap_uint<16>(pattern_id);
    w(47, 40) = ap_uint<8>(lane);
    w(39, 0) = 0;
    return w;
}

/**
 * @brief 生成keep掩码
 */
static inline ap_uint<DATA_WIDTH_BYTES> keep_mask_bytes(int num_bytes)
{
#pragma HLS INLINE
    if (num_bytes <= 0)
        return 0;
    if (num_bytes >= DATA_WIDTH_BYTES)
        return ~ap_uint<DATA_WIDTH_BYTES>(0);
    return (ap_uint<DATA_WIDTH_BYTES>(1) << num_bytes) - 1;
}

// ==================== 主Kernel ====================

void krnl_proj(
    hls::stream<pkt> &n2k,
    hls::stream<pkt> &k2n_payload,
    hls::stream<pkt> &k2n_events,
    unsigned int dest_payload,
    unsigned int dest_events,
    unsigned int num_packets)
{
#pragma HLS INTERFACE axis port = n2k
#pragma HLS INTERFACE axis port = k2n_payload depth = 16
#pragma HLS INTERFACE axis port = k2n_events depth = 1024
#pragma HLS INTERFACE s_axilite port = dest_payload bundle = control
#pragma HLS INTERFACE s_axilite port = dest_events bundle = control
#pragma HLS INTERFACE s_axilite port = num_packets bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

    uint64_t global_byte_idx = 0;

packet_loop:
    for (unsigned pkt_idx = 0; pkt_idx < num_packets; pkt_idx++)
    {
#pragma HLS LOOP_TRIPCOUNT min = 1 max = 1000

        bool first_beat = true;

    read_beats:
        while (true)
        {
#pragma HLS LOOP_TRIPCOUNT min = 1 max = 22
#pragma HLS PIPELINE II = 1

            // ============================================================
            // STAGE 1: 读取输入 + 立即透传到payload流
            // ============================================================
            pkt v_in = n2k.read();
            bool is_last_input_beat = (v_in.last == 1);

            // 🔹 立即透传payload（不做任何处理）
            pkt v_out_payload;
            v_out_payload.data = v_in.data;
            v_out_payload.keep = v_in.keep;
            v_out_payload.last = v_in.last;
            v_out_payload.dest = dest_payload;
            v_out_payload.user = 0;
            v_out_payload.id = 0;
            k2n_payload.write(v_out_payload);

            // ============================================================
            // STAGE 2: 并行做Pattern Matching + 事件输出
            // ============================================================
            uint64_t base_idx = global_byte_idx;

            // 注意：byte_loop必须保持PIPELINE，因为dcam_step_multi有状态依赖
            // dcam_step_multi内部的静态history变量需要在调用之间顺序更新
            // 这是carried dependence，必须顺序执行，无法UNROLL
            // 优化：将事件写入延迟到循环外，减少关键路径上的操作
            ap_uint<EVENT_W> event_buffer[DATA_WIDTH_BYTES];
            bool event_valid[DATA_WIDTH_BYTES];
#pragma HLS ARRAY_PARTITION variable = event_buffer cyclic factor = 8 dim = 1
#pragma HLS ARRAY_PARTITION variable = event_valid complete dim = 1

            // 初始化事件缓冲区
            for (int idx = 0; idx < DATA_WIDTH_BYTES; idx++)
            {
#pragma HLS UNROLL
                event_valid[idx] = false;
            }

        byte_loop:
            for (int i = 0; i < DATA_WIDTH_BYTES; i += DCAM_P)
            {
#pragma HLS PIPELINE II = 1
#pragma HLS LOOP_TRIPCOUNT min = 8 max = 8

                // 提取当前P字节
                unsigned char bytes[DCAM_P];
#pragma HLS ARRAY_PARTITION variable = bytes complete dim = 1

                for (int p = 0; p < DCAM_P; p++)
                {
#pragma HLS UNROLL
                    int byte_offset = i + p;
                    bytes[p] = (unsigned char)v_in.data.range(
                        (byte_offset + 1) * 8 - 1, byte_offset * 8);
                }

                // 调用DCAM匹配器（有状态依赖，必须顺序执行）
                ap_uint<16> out_ids[DCAM_P];
#pragma HLS ARRAY_PARTITION variable = out_ids complete dim = 1

                bool reset = (first_beat && (i == 0));
                dcam_step_multi(bytes, reset, out_ids);

                // 🔸 将匹配结果存储到缓冲区（延迟写入，减少关键路径）
                for (int p = 0; p < DCAM_P; p++)
                {
#pragma HLS UNROLL
                    int slot = i + p;
                    if (out_ids[p] != 0)
                    {
                        event_buffer[slot] = pack_event(base_idx + slot, out_ids[p], p);
                        event_valid[slot] = true;
                    }
                }
            }

            // 🔸 批量写入事件到流（在关键路径外，减少时序压力）
            // 注意：这个循环在read_beats pipeline内部，但使用独立pipeline
            // 这样可以减少byte_loop关键路径上的操作
            for (int idx = 0; idx < DATA_WIDTH_BYTES; idx++)
            {
#pragma HLS PIPELINE II = 1
                if (event_valid[idx])
                {
                    pkt evt;
                    evt.data = event_buffer[idx];
                    evt.keep = 0x000000000000FFFFULL; // 16字节有效
                    evt.dest = dest_events;
                    evt.user = 0;
                    evt.id = 0;
                    evt.last = 0;
                    k2n_events.write(evt);
                }
            }

            global_byte_idx += DATA_WIDTH_BYTES;
            first_beat = false;

            if (is_last_input_beat)
                break;
        }

        // ============================================================
        // STAGE 3: Packet结束标记（发送到event流）
        // ============================================================
        pkt end_marker;
        end_marker.data = 0;
        end_marker.data.range(7, 0) = 0xEE; // 结束标记
        end_marker.keep = keep_mask_bytes(1);
        end_marker.dest = dest_events;
        end_marker.user = 0;
        end_marker.id = 0;
        end_marker.last = 1;
        k2n_events.write(end_marker);
    }
}