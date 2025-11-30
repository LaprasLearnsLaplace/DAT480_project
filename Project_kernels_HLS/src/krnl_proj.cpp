#include "krnl_proj.h"
#include "scanner.h"
#include <iostream>

/**
 * @brief Pattern matching kernel
 * @param n2k         input stream (from network or upstream)
 * @param k2n         output stream (to S2MM into memory)
 * @param dest        reserved parameter (destination/config)
 * @param num_packets run control:
 *                     0 = infinite loop (hardware)
 *                     N = stop after N packets (TLAST) in simulation
 */
void krnl_proj(
    hls::stream<pkt> &n2k,
    hls::stream<pkt> &k2n,
    unsigned int dest,
    unsigned int num_packets
)
{
#pragma HLS INTERFACE axis port = n2k
#pragma HLS INTERFACE axis port = k2n depth=256
#pragma HLS INTERFACE s_axilite port = dest        bundle = control
#pragma HLS INTERFACE s_axilite port = num_packets bundle = control
#pragma HLS INTERFACE s_axilite port = return      bundle = control

    unsigned int packet_count = 0;

#ifndef __SYNTHESIS__
    // Simulation guard to avoid infinite loop
    if (num_packets == 0)
        num_packets = 1;
#endif

packet_loop:
    while ( (num_packets == 0) || (packet_count < num_packets) )
    {
        bool start_new_packet = true;

    beat_loop:
        while (1)
        {
        #pragma HLS LOOP_FLATTEN off

            // ========= 1. Read one 512-bit beat =========
            pkt v_in;
            n2k.read(v_in);

            ap_uint<DWIDTH> data = v_in.data;

            // Split 512-bit into 64 bytes
            unsigned char data_buffer[DATA_WIDTH_BYTES];
        #pragma HLS ARRAY_PARTITION variable=data_buffer complete

        fill_buffer:
            for (int j = 0; j < DATA_WIDTH_BYTES; ++j) {
            #pragma HLS UNROLL
                data_buffer[j] = data(j * 8 + 7, j * 8);
            }

            // ========= 2. Prepare two 512-bit output buffers for 64 bytes =========
            ap_uint<512> packer_low  = 0; // for bytes 0..31
            ap_uint<512> packer_high = 0; // for bytes 32..63

            unsigned char      next_byte = 0;
            unsigned char      curr_byte = 0;
            ap_uint<TDWIDTH>   match_id  = 0;

        // Manual N+1 pipeline (65 iterations); iteration 0 only prefetches
        byte_loop:
            for (int i = 0; i < DATA_WIDTH_BYTES + 1; ++i) {
            #pragma HLS PIPELINE II=1

                // Stage A: Prefetch next byte
                if (i < DATA_WIDTH_BYTES) {
                    next_byte = data_buffer[i];
                }

                // Stage B: Run dcam_step on previous byte
                if (i > 0) {
                    int k = i - 1;
                    bool this_reset = start_new_packet && (k == 0);

                    match_id = 0;
                    dcam_step(curr_byte, this_reset, match_id);
                    if (k < 32) {
                        packer_low(k * 16 + 15, k * 16) = match_id;
                    } else {
                        int k_off = k - 32;
                        packer_high(k_off * 16 + 15, k_off * 16) = match_id;
                    }
                }

                // Stage C: Shift register
                curr_byte = next_byte;
            }

            // ========= 3. Write two result beats =========
            pkt o1, o2;

            // Matches for bytes 0..31
            o1.data = packer_low;
            o1.keep = v_in.keep;
            o1.dest = 0;
            o1.last = 0;          // Not last because high part follows
            k2n.write(o1);

            // Matches for bytes 32..63
            o2.data = packer_high;
            o2.keep = v_in.keep;
            o2.dest = 0;
            o2.last = v_in.last;  // Propagate TLAST to the final high beat
            k2n.write(o2);

            // ========= 4. Packet end check =========
            if (v_in.last) {
                // One AXI packet ended
                break;
            } else {
                // Subsequent beats of same packet; clear start_new_packet
                start_new_packet = false;
            }
        } // end beat_loop

        packet_count++;
    } // end packet_loop
}
