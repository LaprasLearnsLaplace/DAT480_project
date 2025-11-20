#include "scanner.h"
#include "patterns.h"
#include <iostream>

void perform_scan(
    unsigned char window[WINDOW_SIZE],
    ap_uint<TDWIDTH> &dest_signal // 引用传递
)
{
  // #pragma HLS INLINE

scan_loop:
  for (int offset = 0; offset < DATA_WIDTH_BYTES; offset++)
  {
    // #pragma HLS UNROLL

  rule_loop:
    for (int r = 0; r < NUM_PATTERNS; r++)
    {
      // #pragma HLS UNROLL factor = 2

      int len = rules[r].len;
      bool match = true;

    check_loop:
      for (int k = 0; k < PATTERN_MAX_LEN; k++)
      {
        // #pragma HLS UNROLL
        if (k < len)
        {
          if (window[offset + k] != rules[r].data[k])
          {
            match = false;
          }
        }
      }

      if (match)
      {
        // ID 越小优先级越高
        if (dest_signal == 0 || (r + 1) < dest_signal)
        {
          dest_signal = (r + 1);
        }
#ifndef __SYNTHESIS__
        std::cout << "Match Found! ID: " << (r + 1) << " at offset " << offset << std::endl;
#endif
      }
    }
  }
}