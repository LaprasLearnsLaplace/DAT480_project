#ifndef _KRNL_PROJ_H_
#define _KRNL_PROJ_H_

#include "ap_axi_sdata.h"
#include "ap_int.h"
#include "hls_stream.h"

// 数据宽度为 512-bit (64 字节)
#define DWIDTH 512
// 目标 ID 宽度
#define TDWIDTH 16

// 窗口大小 64字节(旧) + 64字节(新)
#define DATA_WIDTH_BYTES 64
#define WINDOW_SIZE (DATA_WIDTH_BYTES * 2)

// 定义我们的数据包类型
typedef ap_axiu<DWIDTH, 1, 1, TDWIDTH> pkt;

/**
 * @brief krnl_proj
 * @param n2k  (Network-to-Kernel)
 * @param k2n  (Kernel-to-Network)
 * @param dest (Destination)
 */
extern "C"
{
  void krnl_proj(
      hls::stream<pkt> &n2k,
      hls::stream<pkt> &k2n,
      unsigned int dest);
} // extern "C"

#endif // _KRNL_PROJ_H_