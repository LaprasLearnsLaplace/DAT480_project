#ifndef SCANNER_H
#define SCANNER_H

#include "krnl_proj.h"
#include "patterns.h"
#include <ap_int.h>

// 多字节处理并行度
#ifndef DCAM_P
#define DCAM_P 8
#endif

// History长度：使用patterns.h中计算的值
#ifndef HISTORY_LEN
#define HISTORY_LEN HISTORY_BITS
#endif

/**
 * @brief DCAM多字节匹配器
 * 
 * @param in_bytes  输入P个字节 [0]=最早, [P-1]=最晚
 * @param reset     复位信号（新packet开始时为true）
 * @param out_ids   输出P个匹配ID (0=无匹配)
 */
void dcam_step_multi(
    unsigned char        in_bytes[DCAM_P],
    bool                 reset,
    ap_uint<TDWIDTH>     out_ids[DCAM_P]
);

#endif // SCANNER_H