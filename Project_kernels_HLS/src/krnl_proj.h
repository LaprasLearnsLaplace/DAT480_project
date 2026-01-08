#ifndef _KRNL_PROJ_H_
#define _KRNL_PROJ_H_

#include "ap_axi_sdata.h"
#include "ap_int.h"
#include "hls_stream.h"

#define DWIDTH 512
#define TDWIDTH 16
#define DATA_WIDTH_BYTES 64

typedef ap_axiu<DWIDTH, 1, 1, TDWIDTH> pkt;

struct event_t {
    uint64_t byte_index;
    uint16_t pattern_id;
    uint8_t  lane;
    
    event_t() : byte_index(0), pattern_id(0), lane(0) {}
    event_t(uint64_t idx, uint16_t pid, uint8_t l) 
        : byte_index(idx), pattern_id(pid), lane(l) {}
};

extern "C" 
{
    void krnl_proj(
        hls::stream<pkt> &n2k,          
        hls::stream<pkt> &k2n_payload,   
        hls::stream<pkt> &k2n_events,   
        unsigned int dest_payload,         
        unsigned int dest_events,            
        unsigned int num_packets
    );
}

#endif