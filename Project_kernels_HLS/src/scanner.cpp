#include "scanner.h"
#include "patterns.h"

void init_byte_lookup(unsigned char byte_to_index[256])
{
#pragma HLS INLINE
  for (int i = 0; i < 256; i++)
  {
#pragma HLS UNROLL
    byte_to_index[i] = 255;
  }
  for (int i = 0; i < NUM_USED_BYTES; i++)
  {
#pragma HLS UNROLL
    byte_to_index[used_bytes[i]] = (unsigned char)i;
  }
}

void dcam_bank_process(
    unsigned char bytes[DCAM_P],
    bool reset,
    int bank_id,
    unsigned char byte_to_index[256],
    ap_uint<16> out_ids[DCAM_P])
{
#pragma HLS INLINE off
#pragma HLS ARRAY_PARTITION variable = bytes complete
#pragma HLS ARRAY_PARTITION variable = out_ids complete

  static ap_uint<1> history[NUM_BANKS][PATTERNS_PER_BANK][HISTORY_BITS];
#pragma HLS ARRAY_PARTITION variable = history complete dim = 1
#pragma HLS ARRAY_PARTITION variable = history complete dim = 3

  for (int p = 0; p < DCAM_P; p++)
  {
#pragma HLS UNROLL
    out_ids[p] = 0;
  }

  if (reset)
  {
    for (int pat = 0; pat < PATTERNS_PER_BANK; pat++)
    {
      for (int pos = 0; pos < HISTORY_BITS; pos++)
      {
#pragma HLS UNROLL
        history[bank_id][pat][pos] = 0;
      }
    }
    return;
  }

  unsigned char incoming_idx[DCAM_P];
#pragma HLS ARRAY_PARTITION variable = incoming_idx complete

  for (int p = 0; p < DCAM_P; p++)
  {
#pragma HLS UNROLL
    incoming_idx[p] = byte_to_index[bytes[p]];
  }

  for (int local_pat = 0; local_pat < PATTERNS_PER_BANK; local_pat++)
  {
#pragma HLS PIPELINE off

    int global_pat = bank_id * PATTERNS_PER_BANK + local_pat;
    unsigned char plen = rules[global_pat].len;
    if (plen == 0 || plen > PATTERN_MAX_LEN)
      continue;

    for (int p = 0; p < DCAM_P; p++)
    {
      ap_uint<1> p_bits[PATTERN_MAX_LEN];
#pragma HLS ARRAY_PARTITION variable = p_bits complete

      for (int pos = 0; pos < PATTERN_MAX_LEN; pos++)
      {
#pragma HLS UNROLL
        if (pos < plen)
        {
          unsigned char expected_idx = rules[global_pat].byte_index[pos];
          p_bits[pos] = (incoming_idx[p] == expected_idx) ? 1 : 0;
        }
        else
        {
          p_bits[pos] = 0;
        }
      }

      ap_uint<1> new_history[HISTORY_BITS];
#pragma HLS ARRAY_PARTITION variable = new_history complete

      new_history[0] = p_bits[0];
      for (int pos = 1; pos < HISTORY_BITS; pos++)
      {
#pragma HLS UNROLL
        if (pos < plen)
        {
          new_history[pos] = history[bank_id][local_pat][pos - 1] & p_bits[pos];
        }
        else
        {
          new_history[pos] = 0;
        }
      }

      if (new_history[plen - 1] == 1)
      {
        out_ids[p] = (ap_uint<16>)(global_pat + 1);
      }

      for (int pos = 0; pos < HISTORY_BITS; pos++)
      {
#pragma HLS UNROLL
        history[bank_id][local_pat][pos] = new_history[pos];
      }
    }
  }
}

void merge_bank_results(
    ap_uint<16> bank_results[NUM_BANKS][DCAM_P],
    ap_uint<16> out_ids[DCAM_P])
{
#pragma HLS INLINE off
#pragma HLS PIPELINE II = 1
#pragma HLS ARRAY_PARTITION variable = bank_results complete dim = 0
#pragma HLS ARRAY_PARTITION variable = out_ids complete

  for (int p = 0; p < DCAM_P; p++)
  {
#pragma HLS UNROLL
    ap_uint<16> result = 0;
    for (int b = 0; b < NUM_BANKS; b++)
    {
#pragma HLS UNROLL
      if (result == 0 && bank_results[b][p] != 0)
      {
        result = bank_results[b][p];
      }
    }
    out_ids[p] = result;
  }
}

void dcam_step_multi(
    unsigned char bytes[DCAM_P],
    bool reset,
    ap_uint<16> out_ids[DCAM_P])
{
#pragma HLS INLINE off
#pragma HLS ARRAY_PARTITION variable = bytes complete
#pragma HLS ARRAY_PARTITION variable = out_ids complete

  static unsigned char byte_to_index[256];
  static bool initialized = false;

  if (!initialized)
  {
#pragma HLS OCCURRENCE cycle = 1
    initialized = true;
    init_byte_lookup(byte_to_index);
  }

  for (int p = 0; p < DCAM_P; p++)
  {
#pragma HLS UNROLL
    out_ids[p] = 0;
  }

  ap_uint<16> bank_results[NUM_BANKS][DCAM_P];
#pragma HLS ARRAY_PARTITION variable = bank_results complete dim = 0

  dcam_bank_process(bytes, reset, 0, byte_to_index, bank_results[0]);
  dcam_bank_process(bytes, reset, 1, byte_to_index, bank_results[1]);
  dcam_bank_process(bytes, reset, 2, byte_to_index, bank_results[2]);
  dcam_bank_process(bytes, reset, 3, byte_to_index, bank_results[3]);

  merge_bank_results(bank_results, out_ids);
}