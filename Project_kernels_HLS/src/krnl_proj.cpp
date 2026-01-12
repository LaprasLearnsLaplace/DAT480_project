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
      for (int i = 0; i < DATA_WIDTH_BYTES; i += DCAM_P) {
#pragma HLS PIPELINE II = 1
#pragma HLS DEPENDENCE variable = beat_events inter false

        unsigned char bytes[DCAM_P];
#pragma HLS ARRAY_PARTITION variable = bytes complete

        // 提取 4 字节
        for (int p = 0; p < DCAM_P; p++) {
#pragma HLS UNROLL
          bytes[p] = (unsigned char)v_in.data.range((i + p + 1) * 8 - 1, (i + p) * 8);
        }

        // 修复点：声明大小必须是 DCAM_P
        ap_uint<16> out_ids[DCAM_P];
#pragma HLS ARRAY_PARTITION variable = out_ids complete

        bool reset = (first_beat && (i == 0));
        dcam_step_multi(bytes, reset, out_ids);

        // 处理匹配结果并压缩到 beat_events
        for (int p = 0; p < DCAM_P; p++) {
#pragma HLS UNROLL
            if (out_ids[p] != 0) {
                // 只要 ID 不为 0，就存入结果数组
                ap_uint<EVENT_W> e = pack_event(base_idx + i + p, out_ids[p], p);
                beat_events[beat_event_count] = e;
                beat_event_count++;
            }
        }
      }

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