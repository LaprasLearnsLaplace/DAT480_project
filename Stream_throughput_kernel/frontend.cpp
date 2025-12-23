#include "frontend.h"
#include <string.h>


void frontend(
    sample stats_buf[MAX_SAMPLES],
    uint32_t num_samples,
    uint32_t rate,
    hls::stream<config>& cfg,
    const sample* samples
) {
    #pragma HLS INTERFACE m_axi port=stats_buf offset=slave bundle=gmem num_read_outstanding=1 max_read_burst_length=2 max_write_burst_length=2 num_write_outstanding=16
    #pragma HLS INTERFACE s_axilite port=stats_buf bundle=control
    #pragma HLS INTERFACE s_axilite port=num_samples bundle=control
    #pragma HLS INTERFACE s_axilite port=rate bundle=control
    #pragma HLS INTERFACE axis port=cfg
    #pragma HLS INTERFACE axis port=samples
    #pragma HLS INTERFACE s_axilite port=return bundle=control

    #pragma HLS aggregate variable=stats_buf
    #pragma HLS aggregate variable=samples

    const uint32_t total_samples = (num_samples > MAX_SAMPLES) ? MAX_SAMPLES : num_samples;
    {
        #pragma HLS protocol fixed
        cfg.write({total_samples, rate});
        memcpy((void*)stats_buf, (const void*)samples, total_samples * sizeof(sample));
    }
}
