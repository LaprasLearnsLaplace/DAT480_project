#include "krnl_proj.h"
#include "scanner.h"
#include <ap_int.h>
#include <hls_stream.h>

// ============================================================================
// 优化版本：byte_loop 内不做“动态地址写数组”，改为固定槽位写入 + 事后压缩
// 目标：让 byte_loop 更容易达到 II=1，并改善时序路径
// ============================================================================

static const int EVENT_W     = 128;
static const int EVENT_BYTES = EVENT_W / 8;            // 16
static const int SLOTS_PER_BEAT = DWIDTH / EVENT_W;    // 512/128 = 4

// 每个 beat 最多 64 bytes -> 最多 64 个事件槽位（按 byte 粒度）
static const int RAW_SLOTS_PER_BEAT = DATA_WIDTH_BYTES; // 64
static const int MAX_EVENTS_PER_BEAT = DATA_WIDTH_BYTES; // 64

// Pack event 到 128 位
static inline ap_uint<EVENT_W> pack_event(
    uint64_t byte_index,
    uint16_t pattern_id,
    uint8_t lane
) {
#pragma HLS INLINE
    ap_uint<EVENT_W> w = 0;
    w.range(127, 64) = byte_index;
    w.range(63, 48)  = pattern_id;
    w.range(47, 40)  = lane;
    // bits [39:0] reserved = 0
    return w;
}

// Generate keep mask (更小的组合逻辑版本)
static inline ap_uint<DATA_WIDTH_BYTES> keep_mask_bytes(int num_bytes) {
#pragma HLS INLINE
    ap_uint<DATA_WIDTH_BYTES> k = 0;
    if (num_bytes <= 0) return 0;
    if (num_bytes >= DATA_WIDTH_BYTES) return ~ap_uint<DATA_WIDTH_BYTES>(0);
    k = (ap_uint<DATA_WIDTH_BYTES>(1) << num_bytes) - 1;
    return k;
}

void krnl_proj(
    hls::stream<pkt> &n2k,
    hls::stream<pkt> &k2n,
    unsigned int dest,
    unsigned int num_packets
) {
#pragma HLS INTERFACE axis port = n2k
#pragma HLS INTERFACE axis port = k2n depth=1024
#pragma HLS INTERFACE s_axilite port = dest bundle = control
#pragma HLS INTERFACE s_axilite port = num_packets bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

    uint64_t global_byte_idx = 0;

    // =========== MAIN PACKET LOOP ===========
packet_loop:
    for (unsigned pkt_idx = 0; pkt_idx < num_packets; pkt_idx++) {

        bool first_beat = true;

        // =========== READ BEATS LOOP ===========
read_beats:
        while (true) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=22

            pkt v_in = n2k.read();
            bool is_last_input_beat = (v_in.last == 1);

            // ----------------------------------------------------------------
            // Phase A: byte_loop 内固定槽位写入 raw_events/raw_valid
            // ----------------------------------------------------------------

            ap_uint<EVENT_W> raw_events[RAW_SLOTS_PER_BEAT];
#pragma HLS ARRAY_PARTITION variable=raw_events cyclic factor=4 dim=1

            ap_uint<1> raw_valid[RAW_SLOTS_PER_BEAT];
#pragma HLS ARRAY_PARTITION variable=raw_valid cyclic factor=4 dim=1

            // 初始化 valid（完全展开，代价很小）
init_valid:
            for (int t = 0; t < RAW_SLOTS_PER_BEAT; t++) {
#pragma HLS UNROLL
                raw_valid[t] = 0;
            }

            // 记录本 beat 的全局 byte 基址，避免在 byte_loop 内更新 global_byte_idx
            uint64_t base_idx = global_byte_idx;

            // 处理 64 bytes，每次处理 DCAM_P 个 byte
byte_loop:
            for (int i = 0; i < DATA_WIDTH_BYTES; i += DCAM_P) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=16 max=16

                // Extract DCAM_P bytes
                unsigned char bytes[DCAM_P];
#pragma HLS ARRAY_PARTITION variable=bytes complete dim=1

                for (int p = 0; p < DCAM_P; p++) {
#pragma HLS UNROLL
                    int byte_offset = i + p;
                    bytes[p] = (unsigned char)v_in.data.range((byte_offset + 1) * 8 - 1,
                                                             byte_offset * 8);
                }

                // DCAM output ids
                ap_uint<16> out_ids[DCAM_P];
#pragma HLS ARRAY_PARTITION variable=out_ids complete dim=1

                bool reset = (first_beat && (i == 0));
                dcam_step_multi(bytes, reset, out_ids);

                // 固定槽位写入：slot = i + p
                // 这样 HLS 能证明写地址不冲突，byte_loop 更容易 II=1
                for (int p = 0; p < DCAM_P; p++) {
#pragma HLS UNROLL
                    int slot = i + p; // 0..63
                    if (out_ids[p] != 0) {
                        raw_events[slot] = pack_event(base_idx + (uint64_t)slot,
                                                      (uint16_t)out_ids[p],
                                                      (uint8_t)p);
                        raw_valid[slot] = 1;
                    }
                }
            }

            // 一个 input beat 处理完（固定 64 bytes）
            global_byte_idx += DATA_WIDTH_BYTES;
            first_beat = false;

            // ----------------------------------------------------------------
            // Phase B: 压缩 raw_events -> beat_events（连续存储）
            // ----------------------------------------------------------------

            ap_uint<EVENT_W> beat_events[MAX_EVENTS_PER_BEAT];
#pragma HLS ARRAY_PARTITION variable=beat_events cyclic factor=4 dim=1

            int beat_event_count = 0;

compact_loop:
            for (int t = 0; t < RAW_SLOTS_PER_BEAT; t++) {
#pragma HLS PIPELINE II=1
                if (raw_valid[t]) {
                    // 每拍最多写一次，地址单调递增，HLS 一般能处理
                    beat_events[beat_event_count] = raw_events[t];
                    beat_event_count++;
                }
            }

            // ----------------------------------------------------------------
            // Output loop: 打包输出到 k2n
            // ----------------------------------------------------------------
            if (beat_event_count > 0) {
                int num_output_beats =
                    (beat_event_count + SLOTS_PER_BEAT - 1) / SLOTS_PER_BEAT;

output_loop:
            for (int ob = 0; ob < num_output_beats; ob++) {
            #pragma HLS PIPELINE II=1
            #pragma HLS LOOP_TRIPCOUNT min=1 max=16

                pkt o;
                o.data = 0;
                o.dest = dest;
                o.user = 0;
                o.id   = 0;
                o.last = 0;

                // 计算本 beat 需要输出几个 event：n = clamp(beat_event_count - ob*4, 0..4)
                int base = ob * SLOTS_PER_BEAT;              // SLOTS_PER_BEAT=4
                int left = beat_event_count - base;

                ap_uint<3> n;                                // 0..4 fits in 3 bits
                if (left <= 0)      n = 0;
                else if (left >= 4) n = 4;
                else                n = (ap_uint<3>)left;

                // Pack events：仍然 UNROLL，但不再有 events_in_this_beat++ 链
                for (int s = 0; s < SLOTS_PER_BEAT; s++) {
            #pragma HLS UNROLL
                    int ev_idx = base + s;
                    if (ev_idx < beat_event_count) {
                        o.data.range((s + 1) * EVENT_W - 1, s * EVENT_W) = beat_events[ev_idx];
                    }
                }

                // keep 用 switch 常量表（最短组合逻辑），避免 variable shift/加法链
                // EVENT_BYTES=16, 所以 num_bytes = n*16: 0,16,32,48,64
                switch ((int)n) {
                    case 0: o.keep = 0x0000000000000000ULL; break;
                    case 1: o.keep = 0x000000000000FFFFULL; break;
                    case 2: o.keep = 0x00000000FFFFFFFFULL; break;
                    case 3: o.keep = 0x0000FFFFFFFFFFFFULL; break;
                    default:o.keep = 0xFFFFFFFFFFFFFFFFULL; break; // 4
                }

                k2n.write(o);
            }

            }

            if (is_last_input_beat) break;
        }

        // =========== FINALIZE PACKET ===========
        // 更 AXIS 友好：last beat 至少 1 byte 有效（避免 keep=0 的兼容性坑）
        pkt end_marker;
        end_marker.data = 0;
        end_marker.data.range(7, 0) = 0xEE;        // tag byte (可选)
        end_marker.keep = keep_mask_bytes(1);      // 1 byte valid
        end_marker.dest = dest;
        end_marker.user = 0;
        end_marker.id   = 0;
        end_marker.last = 1;
        k2n.write(end_marker);
    }
}