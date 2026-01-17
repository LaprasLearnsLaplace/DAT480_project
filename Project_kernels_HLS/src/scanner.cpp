#include "scanner.h"

// 为每个组匹配patterns
void match_group(
    ap_uint<HISTORY_LEN> group_hist[MAX_UNIQUE_BYTES],
    int group_id,
    ap_uint<TDWIDTH> group_out[DCAM_P])
{
#pragma HLS INLINE
  
  int num_patterns = group_info[group_id].num_patterns;
  int max_len = group_info[group_id].max_len;
  
  for (int p = 0; p < DCAM_P; p++)
  {
#pragma HLS UNROLL
    ap_uint<TDWIDTH> local_best = 0;
    
    for (int r = 0; r < MAX_PATTERNS_PER_GROUP; r++)
    {
#pragma HLS UNROLL
      if (r >= num_patterns) continue;
      
      int len = group_rules[group_id][r].len;
      int rule_id = group_rules[group_id][r].original_id;
      
      bool match = (len > 0);
      for (int k = 0; k < MAX_PATTERN_LEN; k++)
      {
#pragma HLS UNROLL
        if (k < len)
        {
          int b_idx = group_rules[group_id][r].byte_index[k];
          int offset = (DCAM_P - 1 - p) + (len - 1 - k);
          match &= (bool)group_hist[b_idx][offset];
        }
      }
      
      if (match)
        local_best = (ap_uint<TDWIDTH>)rule_id;
    }
    group_out[p] = local_best;
  }
}

void dcam_step_multi(unsigned char in_bytes[DCAM_P], bool reset, ap_uint<TDWIDTH> out_ids[DCAM_P])
{
#pragma HLS PIPELINE II=1
#pragma HLS INLINE off

  // 每组独立的history
  static ap_uint<HISTORY_LEN> history[NUM_GROUPS][MAX_UNIQUE_BYTES];
#pragma HLS ARRAY_PARTITION variable=history complete dim=2
  
  // 1. 为每组更新history
  for (int g = 0; g < NUM_GROUPS; g++)
  {
#pragma HLS UNROLL
    int num_bytes = group_info[g].num_unique_bytes;
    
    for (int b = 0; b < MAX_UNIQUE_BYTES; b++)
    {
#pragma HLS UNROLL
      if (b >= num_bytes) continue;
      
      ap_uint<DCAM_P> match_bits = 0;
      for (int p = 0; p < DCAM_P; p++)
      {
#pragma HLS UNROLL
        match_bits[DCAM_P - 1 - p] = (in_bytes[p] == group_bytes[g][b]);
      }
      
      history[g][b] = reset ? (ap_uint<HISTORY_LEN>)0 
                            : (ap_uint<HISTORY_LEN>)((history[g][b] << DCAM_P) | match_bits);
    }
  }
  
  // 2. 每组匹配
  ap_uint<TDWIDTH> g_res[NUM_GROUPS][DCAM_P];
#pragma HLS ARRAY_PARTITION variable=g_res complete dim=0
  
  for (int g = 0; g < NUM_GROUPS; g++)
  {
#pragma HLS UNROLL
    match_group(history[g], g, g_res[g]);
  }
  
  // 3. 合并结果（优先返回第一个匹配）
  for (int p = 0; p < DCAM_P; p++)
  {
#pragma HLS UNROLL
    ap_uint<TDWIDTH> final_id = 0;
    for (int g = 0; g < NUM_GROUPS; g++)
    {
#pragma HLS UNROLL
      if (g_res[g][p] != 0 && final_id == 0)
        final_id = g_res[g][p];
    }
    out_ids[p] = final_id;
  }
}