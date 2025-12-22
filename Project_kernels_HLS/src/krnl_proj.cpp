#include "krnl_proj.h"
#include "scanner.h"
#include <ap_int.h>
#include <hls_stream.h>

// ================================================================
// Sparse-event output (128-bit/event) packed into 512-bit AXI beats
// Beat.user == 0 : EVENT beat (0..4 events, keep = n_events*16 bytes)
// Beat.user == 1 : REPORT beat (always keep = 64B, last = 1)
// ================================================================

static const int EVENT_W        = 128;
static const int EVENT_BYTES    = 16;
static const int SLOTS_PER_BEAT = DWIDTH / EVENT_W; // 4

// Flags
static const ap_uint<8> EV_FLAG_END      = 1 << 0;
static const ap_uint<8> EV_FLAG_OVERFLOW = 1 << 1; // reserved (not used here)

// keep mask: lowest n bytes valid
static inline ap_uint<DATA_WIDTH_BYTES> keep_mask_bytes(int nbytes) {
#pragma HLS INLINE
    ap_uint<DATA_WIDTH_BYTES> k = 0;
    for (int i = 0; i < DATA_WIDTH_BYTES; i++) {
#pragma HLS UNROLL
        k[i] = (i < nbytes) ? 1 : 0;
    }
    return k;
}

// pack one 128b event
static inline ap_uint<EVENT_W> pack_event(
    ap_uint<64> byte_index,
    ap_uint<16> pattern_id,
    ap_uint<8>  lane,
    ap_uint<8>  flags,
    ap_uint<32> user_payload = 0
) {
#pragma HLS INLINE
    ap_uint<EVENT_W> w = 0;
    w.range(127, 64) = byte_index;
    w.range(63,  48) = pattern_id;
    w.range(47,  40) = lane;
    w.range(39,  32) = flags;
    w.range(31,   0) = user_payload;
    return w;
}

extern "C" {
void krnl_proj(
    hls::stream<pkt> &n2k,
    hls::stream<pkt> &k2n,
    unsigned int dest,
    unsigned int num_packets
) {
#pragma HLS INTERFACE axis port = n2k
#pragma HLS INTERFACE axis port = k2n depth=256
#pragma HLS INTERFACE s_axilite port = dest        bundle = control
#pragma HLS INTERFACE s_axilite port = num_packets bundle = control
#pragma HLS INTERFACE s_axilite port = return      bundle = control

    // Totals (exclude REPORT)
    ap_uint<64> total_in_bytes  = 0;
    ap_uint<64> total_out_bytes = 0;

packet_loop:
    for (unsigned int pkt_idx = 0; pkt_idx < num_packets; ++pkt_idx) {
        #pragma HLS loop_flatten off
        ap_uint<64> pkt_in_bytes  = 0;
        ap_uint<64> pkt_in_beats  = 0;

        // Event payload only (hit events + END event), excludes REPORT
        ap_uint<64> pkt_out_bytes = 0;
        ap_uint<64> pkt_out_beats = 0;

        bool saw_any_event = false;

    read_beats:
        while (true) {
// #pragma HLS PIPELINE II=1
#pragma HLS loop_flatten off

            // -------- Read one 64B input beat --------
            pkt v_in = n2k.read();
            pkt_in_beats += 1;

            // Count valid input bytes via keep
            ap_uint<DATA_WIDTH_BYTES> kin = v_in.keep;
            ap_uint<7> in_valid = 0;
            for (int i = 0; i < DATA_WIDTH_BYTES; i++) {
#pragma HLS UNROLL
                in_valid += (ap_uint<1>)kin[i];
            }
            pkt_in_bytes += in_valid;

            // -------- Process this beat in 4B steps --------
            // Key design choice for timing:
            // - Do NOT accumulate events across steps (no ev_count chain).
            // - Each 4B step emits at most ONE EVENT beat with 1..4 events.
        byte_loop:
            for (int i = 0; i < DATA_WIDTH_BYTES; i += DCAM_P) {
#pragma HLS PIPELINE II=1

                unsigned char in_bytes[DCAM_P];
#pragma HLS ARRAY_PARTITION variable=in_bytes complete
                ap_uint<TDWIDTH> out_ids[DCAM_P];
#pragma HLS ARRAY_PARTITION variable=out_ids complete

                // Load 4 bytes
                for (int p = 0; p < DCAM_P; p++) {
#pragma HLS UNROLL
                    in_bytes[p] = (unsigned char)v_in.data.range((i + p) * 8 + 7, (i + p) * 8);
                }

                bool reset = (pkt_in_beats == 1) && (i == 0);
                dcam_step_multi(in_bytes, reset, out_ids);

                // Collect hits into local slots (0..3)
                ap_uint<EVENT_W> ev_local[SLOTS_PER_BEAT];
#pragma HLS ARRAY_PARTITION variable=ev_local complete
                ap_uint<3> n_ev = 0;

                for (int p = 0; p < DCAM_P; p++) {
#pragma HLS UNROLL
                    ap_uint<TDWIDTH> id = out_ids[p];
                    if (id != 0) {
                        ap_uint<64> byte_index =
                            (ap_uint<64>)((pkt_in_beats - 1) * DATA_WIDTH_BYTES + (i + p));
                        ev_local[n_ev] = pack_event(byte_index, (ap_uint<16>)id, (ap_uint<8>)p, (ap_uint<8>)0);
                        n_ev++;
                    }
                }

                if (n_ev != 0) {
                    // Emit ONE event beat containing 1..4 events
                    pkt o;
                    o.data = 0;
                    for (int s = 0; s < SLOTS_PER_BEAT; s++) {
#pragma HLS UNROLL
                        if (s < n_ev) {
                            o.data.range((s + 1) * EVENT_W - 1, s * EVENT_W) = ev_local[s];
                        }
                    }
                    o.keep = keep_mask_bytes((int)n_ev * EVENT_BYTES);
                    o.dest = 0;
                    o.user = 0;
                    o.id   = 0;
                    o.last = 0;
                    k2n.write(o);

                    saw_any_event = true;
                    pkt_out_beats += 1;
                    pkt_out_bytes += (ap_uint<64>)n_ev * EVENT_BYTES;
                }
            }

            if (v_in.last) {
                break; // End of input packet
            }
        }

        // If no events at all for the packet, emit a single END marker event
        if (!saw_any_event) {
            pkt o;
            o.data = 0;
            ap_uint<128> endw = pack_event(/*byte_index*/0, /*pattern_id*/0, /*lane*/0, EV_FLAG_END);
            o.data.range(127, 0) = endw;
            o.keep = keep_mask_bytes(EVENT_BYTES);
            o.dest = 0;
            o.user = 0;
            o.id   = 0;
            o.last = 0;
            k2n.write(o);

            pkt_out_beats += 1;
            pkt_out_bytes += EVENT_BYTES;
        }

        // Update totals (exclude REPORT)
        total_in_bytes  += pkt_in_bytes;
        total_out_bytes += pkt_out_bytes;

        // -------- REPORT beat (user=1) --------
        // Layout matches your current TB:
        // [63:0]    pkt_in_bytes
        // [127:64]  pkt_in_beats
        // [191:128] pkt_out_bytes   (event payload only, excludes REPORT)
        // [255:192] pkt_out_beats
        // [319:256] packet_seq
        // [383:320] total_in_bytes
        // [447:384] total_out_bytes
        pkt rep;
        rep.data = 0;
        rep.data.range(63,0)     = pkt_in_bytes;
        rep.data.range(127,64)   = pkt_in_beats;
        rep.data.range(191,128)  = pkt_out_bytes;
        rep.data.range(255,192)  = pkt_out_beats;
        rep.data.range(319,256)  = (ap_uint<64>)pkt_idx;
        rep.data.range(383,320)  = total_in_bytes;
        rep.data.range(447,384)  = total_out_bytes;

        rep.keep = (ap_uint<DATA_WIDTH_BYTES>)(~(ap_uint<DATA_WIDTH_BYTES>)0); // 64B valid
        rep.dest = 0;
        rep.user = 1;
        rep.id   = 0;
        rep.last = 1;
        k2n.write(rep);
    }
}
} // extern "C"
