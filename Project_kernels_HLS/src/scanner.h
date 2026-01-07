#ifndef SCANNER_H
#define SCANNER_H

#include "krnl_proj.h"
#include "patterns.h"
#include <ap_int.h>

#define DCAM_P 4             // 并行处理8个bytes
#define NUM_BANKS 4          // 4个bank
#define PATTERNS_PER_BANK 64 // 每个bank 64个patterns
#define HISTORY_BITS 16      // History寄存器深度

void dcam_step_multi(
    unsigned char bytes[DCAM_P],
    bool reset,
    ap_uint<16> out_ids[DCAM_P]);

#endif // SCANNER_H