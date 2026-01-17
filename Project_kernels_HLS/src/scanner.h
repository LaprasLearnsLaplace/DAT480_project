#ifndef __SCANNER_H__
#define __SCANNER_H__

#include <ap_int.h>
#include "patterns.h"  // 改用新的分组头文件

// 使用全局最大值
#define HISTORY_LEN MAX_HISTORY_BITS  // 256 (来自patterns_grouped.h)
#define TDWIDTH 16

struct match_event
{
  ap_uint<64> byte_index;
  ap_uint<16> pattern_id;
  ap_uint<8> lane;
  ap_uint<2> control; 
};

void dcam_step_multi(unsigned char in_bytes[DCAM_P], bool reset, ap_uint<TDWIDTH> out_ids[DCAM_P]);

#endif