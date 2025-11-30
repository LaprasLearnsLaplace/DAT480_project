#ifndef _KRNL_PROJ_H_
#define _KRNL_PROJ_H_

#include "ap_axi_sdata.h"
#include "ap_int.h"
#include "hls_stream.h"

// Data width: 512-bit (64 bytes)
#define DWIDTH 512
// Match ID width
#define TDWIDTH 16

// Window size: 64B old + 64B new
#define DATA_WIDTH_BYTES 64
#define WINDOW_SIZE (DATA_WIDTH_BYTES * 2)

// Packet type definition
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
      unsigned int dest,
      unsigned int num_packets);
} // extern "C"

#endif // _KRNL_PROJ_H_
