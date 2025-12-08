#ifndef _KRNL_PROJ_H_
#define _KRNL_PROJ_H_

#include "ap_axi_sdata.h"
#include "ap_int.h"
#include "hls_stream.h"

#define DWIDTH 512
#define TDWIDTH 16
#define DATA_WIDTH_BYTES 64

typedef ap_axiu<DWIDTH, 1, 1, TDWIDTH> pkt;

extern "C" 
{
    void krnl_proj(
        hls::stream<pkt> &n2k,
        hls::stream<pkt> &k2n,
        unsigned int dest,
        unsigned int num_packets
    );
}

#endif