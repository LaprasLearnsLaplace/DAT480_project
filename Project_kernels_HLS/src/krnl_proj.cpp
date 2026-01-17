#include "krnl_proj.h"
#include "scanner.h"

void process_and_stream(hls::stream<pkt> &n2k, hls::stream<match_event> m_stream[2], unsigned int num_packets)
{
packet_loop:
  for (unsigned pkt_idx = 0; pkt_idx < num_packets; pkt_idx++)
  {
    uint64_t packet_byte_idx = 0;
    bool first_beat = true;

  beat_loop:
    while (true)
    {
      pkt v_in = n2k.read();
      bool is_last = (v_in.last == 1);

      // 预先计算reset信号，避免在pipeline中动态计算
      bool reset_signal = first_beat;

    byte_loop:
      for (int i = 0; i < DATA_WIDTH_BYTES; i += 2)
      {
#pragma HLS PIPELINE II = 1
#pragma HLS DEPENDENCE variable = m_stream inter false

        unsigned char bytes[2];
#pragma HLS ARRAY_PARTITION variable = bytes complete

        bytes[0] = (unsigned char)v_in.data.range((i + 1) * 8 - 1, i * 8);
        bytes[1] = (unsigned char)v_in.data.range((i + 2) * 8 - 1, (i + 1) * 8);

        ap_uint<16> ids[2];
#pragma HLS ARRAY_PARTITION variable = ids complete

        // Reset只在beat的第一个iteration
        bool do_reset = reset_signal && (i == 0);
        dcam_step_multi(bytes, do_reset, ids);

        // 使用非阻塞写入 - 关键优化！
        match_event ev0 = {packet_byte_idx + i, ids[0], 0, 0};
        match_event ev1 = {packet_byte_idx + i + 1, ids[1], 1, 0};

        // 条件写入，但不阻塞pipeline
        if (ids[0] != 0)
        {
          m_stream[0].write(ev0);
        }
        if (ids[1] != 0)
        {
          m_stream[1].write(ev1);
        }
      }

      // 在loop外发送marker
      match_event marker = {0, 0, 0, (ap_uint<2>)(is_last ? 2 : 1)};
      m_stream[0].write(marker);
      m_stream[1].write(marker);

      packet_byte_idx += DATA_WIDTH_BYTES;
      first_beat = false;

      if (is_last)
        break;
    }
  }
}

void collect_and_output(hls::stream<match_event> m_stream[2], hls::stream<pkt> &k2n, unsigned int dest, unsigned int num_packets)
{
  ap_uint<128> r0, r1, r2, r3;
#pragma HLS ARRAY_PARTITION variable = r0 complete
  int count = 0;

packet_loop:
  for (unsigned pkt_idx = 0; pkt_idx < num_packets; pkt_idx++)
  {
    bool packet_done = false;

  beat_sync_loop:
    while (!packet_done)
    {
      for (int l = 0; l < 2; l++)
      {
        bool lane_done = false;
        while (!lane_done)
        {
#pragma HLS PIPELINE II = 1
          match_event me = m_stream[l].read();

          if (me.control == 0)
          {
            ap_uint<128> e = 0;
            e.range(127, 64) = me.byte_index;
            e.range(63, 48) = me.pattern_id;
            e.range(47, 40) = me.lane;

            if (count == 0)
              r0 = e;
            else if (count == 1)
              r1 = e;
            else if (count == 2)
              r2 = e;
            else
              r3 = e;

            count++;

            if (count == 4)
            {
              pkt o;
              o.data = (r3, r2, r1, r0);
              o.keep = -1;
              o.last = 0;
              o.dest = dest;
              k2n.write(o);
              count = 0;
            }
          }
          else
          {
            lane_done = true;
            if (me.control == 2)
              packet_done = true;
          }
        }
      }

      // Flush残留数据
      if (count > 0)
      {
        pkt o;
        o.data = (r3, r2, r1, r0);
        o.keep = -1;
        o.last = 0;
        o.dest = dest;
        k2n.write(o);
        count = 0;
      }
    }

    // Packet结束标记
    pkt end_pkt;
    end_pkt.data = 0xEE;
    end_pkt.last = 1;
    end_pkt.dest = dest;
    k2n.write(end_pkt);
  }
}

void krnl_proj(hls::stream<pkt> &n2k, hls::stream<pkt> &k2n, unsigned int dest, unsigned int num_packets)
{
#pragma HLS INTERFACE axis port = n2k
#pragma HLS INTERFACE axis port = k2n
#pragma HLS INTERFACE s_axilite port = dest bundle = control
#pragma HLS INTERFACE s_axilite port = num_packets bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control
#pragma HLS DATAFLOW

  hls::stream<match_event> m_fifo[2];
#pragma HLS STREAM variable = m_fifo depth = 1024

  process_and_stream(n2k, m_fifo, num_packets);
  collect_and_output(m_fifo, k2n, dest, num_packets);
}