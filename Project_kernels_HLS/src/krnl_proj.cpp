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

  // 静态变量保存历史状态
  static unsigned char prev_data[DATA_WIDTH_BYTES];
  static bool first_cycle = true;

  // #pragma HLS ARRAY_PARTITION variable = prev_data complete

process_loop:
  while (1)
  {
#pragma HLS PIPELINE II = 1

// Simulation only
#ifndef __SYNTHESIS__
    if (n2k.empty())
      break;
#endif

    pkt v_in;
    n2k.read(v_in);

    unsigned char window[WINDOW_SIZE];
    // #pragma HLS ARRAY_PARTITION variable = window complete

    ap_uint<512> curr_bits = v_in.data;

  unpack_loop:
    for (int i = 0; i < DATA_WIDTH_BYTES; i++)
    {
      // #pragma HLS UNROLL
      window[DATA_WIDTH_BYTES + i] = curr_bits((i * 8) + 7, i * 8);
      window[i] = prev_data[i];
    }

    // 1. 正常扫描 [Prev, Curr]
    if (!first_cycle)
    {
      perform_scan(window, v_in.dest);
    }

    // 2. 更新历史数据
  update_loop:
    for (int i = 0; i < DATA_WIDTH_BYTES; i++)
    {
#pragma HLS UNROLL
      prev_data[i] = window[DATA_WIDTH_BYTES + i];
    }

    // 3. 处理 TLAST (收尾检查)
    if (v_in.last)
    {
      unsigned char flush_window[WINDOW_SIZE];
      // #pragma HLS ARRAY_PARTITION variable = flush_window complete

    flush_fill:
      for (int i = 0; i < DATA_WIDTH_BYTES; i++)
      {
        // #pragma HLS UNROLL
        flush_window[i] = prev_data[i];
        flush_window[DATA_WIDTH_BYTES + i] = 0;
      }

      perform_scan(flush_window, v_in.dest);

      // 重置状态
      first_cycle = true;
    }
    else
    {
      first_cycle = false;
    }

    // 4. 发送数据包
    k2n.write(v_in);

    if (v_in.last)
    {
#ifndef __SYNTHESIS__
      break;
#endif
    }
  }
}