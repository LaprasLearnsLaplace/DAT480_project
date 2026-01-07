#include "krnl_proj.h"
#include "scanner.h"
#include <ap_int.h>
#include <hls_stream.h>

static const int EVENT_W = 128;
static const int EVENT_BYTES = EVENT_W / 8;
static const int SLOTS_PER_BEAT = DWIDTH / EVENT_W;
static const int RAW_SLOTS_PER_BEAT = DATA_WIDTH_BYTES;
static const int MAX_EVENTS_PER_BEAT = DATA_WIDTH_BYTES;

static inline ap_uint<EVENT_W> pack_event(
    uint64_t byte_index,
    uint16_t pattern_id,
    uint8_t lane)
{
#pragma HLS INLINE
  ap_uint<EVENT_W> w = 0;
  w.range(127, 64) = byte_index;
  w.range(63, 48) = pattern_id;
  w.range(47, 40) = lane;
  return w;
}

static inline ap_uint<DATA_WIDTH_BYTES> keep_mask_bytes(int num_bytes)
{
#pragma HLS INLINE
  if (num_bytes <= 0)
    return 0;
  if (num_bytes >= DATA_WIDTH_BYTES)
    return ~ap_uint<DATA_WIDTH_BYTES>(0);
  return (ap_uint<DATA_WIDTH_BYTES>(1) << num_bytes) - 1;
}

void krnl_proj(
    hls::stream<pkt> &n2k,
    hls::stream<pkt> &k2n,
    unsigned int dest,
    unsigned int num_packets)
{
#pragma HLS INTERFACE axis port = n2k
#pragma HLS INTERFACE axis port = k2n depth = 1024
#pragma HLS INTERFACE s_axilite port = dest bundle = control
#pragma HLS INTERFACE s_axilite port = num_packets bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

  uint64_t global_byte_idx = 0;

packet_loop:
  for (unsigned pkt_idx = 0; pkt_idx < num_packets; pkt_idx++)
  {

    bool first_beat = true;

  read_beats:
    while (true)
    {
#pragma HLS LOOP_TRIPCOUNT min = 1 max = 22

      pkt v_in = n2k.read();
      bool is_last_input_beat = (v_in.last == 1);

      uint64_t base_idx = global_byte_idx;

      // 直接压缩到输出数组，边处理边写
      ap_uint<EVENT_W> beat_events[MAX_EVENTS_PER_BEAT];
#pragma HLS ARRAY_PARTITION variable = beat_events cyclic factor = 4 dim = 1

      ap_uint<7> beat_event_count = 0;

      // ============================================================
      // 融合的 byte_loop:  处理 + 即时压缩
      // ============================================================
    byte_loop:
      for (int i = 0; i < DATA_WIDTH_BYTES; i += DCAM_P)
      {
#pragma HLS PIPELINE II = 1
#pragma HLS LOOP_TRIPCOUNT min = 16 max = 16
#pragma HLS DEPENDENCE variable = beat_events inter false

        unsigned char bytes[DCAM_P];
#pragma HLS ARRAY_PARTITION variable = bytes complete dim = 1

        for (int p = 0; p < DCAM_P; p++)
        {
#pragma HLS UNROLL
          int byte_offset = i + p;
          bytes[p] = (unsigned char)v_in.data.range(
              (byte_offset + 1) * 8 - 1, byte_offset * 8);
        }

        ap_uint<16> out_ids[DCAM_P];
#pragma HLS ARRAY_PARTITION variable = out_ids complete dim = 1

        bool reset = (first_beat && (i == 0));
        dcam_step_multi(bytes, reset, out_ids);

        // 本次迭代的有效事件
        ap_uint<1> v0 = (out_ids[0] != 0);
        ap_uint<1> v1 = (out_ids[1] != 0);
        ap_uint<1> v2 = (out_ids[2] != 0);
        ap_uint<1> v3 = (out_ids[3] != 0);

        ap_uint<EVENT_W> e0 = pack_event(base_idx + i + 0, out_ids[0], 0);
        ap_uint<EVENT_W> e1 = pack_event(base_idx + i + 1, out_ids[1], 1);
        ap_uint<EVENT_W> e2 = pack_event(base_idx + i + 2, out_ids[2], 2);
        ap_uint<EVENT_W> e3 = pack_event(base_idx + i + 3, out_ids[3], 3);

        // 本次有几个有效
        ap_uint<3> cnt = (ap_uint<3>)v0 + (ap_uint<3>)v1 +
                         (ap_uint<3>)v2 + (ap_uint<3>)v3;

        // 4-to-N 压缩
        ap_uint<EVENT_W> c0, c1, c2, c3;

        c0 = v0 ? e0 : (v1 ? e1 : (v2 ? e2 : e3));

        if (v0 && v1)
          c1 = e1;
        else if (v0 && v2)
          c1 = e2;
        else if (v0 && v3)
          c1 = e3;
        else if (v1 && v2)
          c1 = e2;
        else if (v1 && v3)
          c1 = e3;
        else
          c1 = e3;

        if (v0 && v1 && v2)
          c2 = e2;
        else if (v0 && v1 && v3)
          c2 = e3;
        else if (v0 && v2 && v3)
          c2 = e3;
        else
          c2 = e3;

        c3 = e3;

        // 写入压缩后的事件
        ap_uint<7> wr_base = beat_event_count;
        if (cnt > 0)
          beat_events[wr_base] = c0;
        if (cnt > 1)
          beat_events[wr_base + 1] = c1;
        if (cnt > 2)
          beat_events[wr_base + 2] = c2;
        if (cnt > 3)
          beat_events[wr_base + 3] = c3;

        beat_event_count = beat_event_count + cnt;
      }

      global_byte_idx += DATA_WIDTH_BYTES;
      first_beat = false;

      // ============================================================
      // Output loop
      // ============================================================
      int evt_cnt = (int)beat_event_count;

      if (evt_cnt > 0)
      {
        int num_output_beats = (evt_cnt + SLOTS_PER_BEAT - 1) / SLOTS_PER_BEAT;

      output_loop:
        for (int ob = 0; ob < num_output_beats; ob++)
        {
#pragma HLS PIPELINE II = 1
#pragma HLS LOOP_TRIPCOUNT min = 1 max = 16

          pkt o;
          o.data = 0;
          o.dest = dest;
          o.user = 0;
          o.id = 0;
          o.last = 0;

          int base = ob * SLOTS_PER_BEAT;
          int left = evt_cnt - base;

          ap_uint<3> n = (left >= 4) ? (ap_uint<3>)4 : (ap_uint<3>)left;

          for (int s = 0; s < SLOTS_PER_BEAT; s++)
          {
#pragma HLS UNROLL
            int ev_idx = base + s;
            if (ev_idx < evt_cnt)
            {
              o.data.range((s + 1) * EVENT_W - 1, s * EVENT_W) =
                  beat_events[ev_idx];
            }
          }

          switch ((int)n)
          {
          case 1:
            o.keep = 0x000000000000FFFFULL;
            break;
          case 2:
            o.keep = 0x00000000FFFFFFFFULL;
            break;
          case 3:
            o.keep = 0x0000FFFFFFFFFFFFULL;
            break;
          default:
            o.keep = 0xFFFFFFFFFFFFFFFFULL;
            break;
          }

          k2n.write(o);
        }
      }

      if (is_last_input_beat)
        break;
    }

    pkt end_marker;
    end_marker.data = 0;
    end_marker.data.range(7, 0) = 0xEE;
    end_marker.keep = keep_mask_bytes(1);
    end_marker.dest = dest;
    end_marker.user = 0;
    end_marker.id = 0;
    end_marker.last = 1;
    k2n.write(end_marker);
  }
}