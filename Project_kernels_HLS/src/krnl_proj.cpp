#include "krnl_proj.h"
#include "scanner.h"
#include <ap_int.h>
#include <hls_stream.h>

static const int EVENT_W        = 128;
static const int EVENT_BYTES    = 16;
static const int SLOTS_PER_BEAT = DWIDTH / EVENT_W; // 4

static const ap_uint<8> EV_FLAG_END      = 1 << 0;
static const ap_uint<8> EV_FLAG_OVERFLOW = 1 << 1;

static inline ap_uint<DATA_WIDTH_BYTES> keep_mask_bytes(int nbytes) {
#pragma HLS INLINE
    ap_uint<DATA_WIDTH_BYTES> k = 0;
    for (int i = 0; i < DATA_WIDTH_BYTES; i++) {
#pragma HLS UNROLL
        k[i] = (i < nbytes) ? 1 : 0;
    }
    return k;
}

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

static inline pkt build_event_pkt(
    ap_uint<EVENT_W> evs[SLOTS_PER_BEAT],
    ap_uint<3>       n_ev
) {
#pragma HLS INLINE
    pkt o;
    o.data = 0;
    for (int s = 0; s < SLOTS_PER_BEAT; s++) {
#pragma HLS UNROLL
        if (s < n_ev) {
            o.data.range((s + 1) * EVENT_W - 1, s * EVENT_W) = evs[s];
        }
    }
    o.keep = keep_mask_bytes((int)n_ev * EVENT_BYTES);
    o.dest = 0;
    o.user = 0;
    o.id   = 0;
    o.last = 0;
    return o;
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

    ap_uint<64> total_in_bytes  = 0;
    ap_uint<64> total_out_bytes = 0;

packet_loop:
    for (unsigned int pkt_idx = 0; pkt_idx < num_packets; ++pkt_idx) {
#pragma HLS loop_flatten off

        ap_uint<64> pkt_in_bytes  = 0;
        ap_uint<64> pkt_in_beats  = 0;

        ap_uint<64> pkt_out_bytes = 0;
        ap_uint<64> pkt_out_beats = 0;

        bool saw_any_event = false;

read_beats:
        while (true) {
#pragma HLS loop_flatten off
            pkt v_in = n2k.read();
            pkt_in_beats += 1;

            ap_uint<DATA_WIDTH_BYTES> kin = v_in.keep;
            ap_uint<7> in_valid = 0;
            for (int i = 0; i < DATA_WIDTH_BYTES; i++) {
#pragma HLS UNROLL
                in_valid += (ap_uint<1>)kin[i];
            }
            pkt_in_bytes += in_valid;

            // --------------- 4-bank event stores (one write per bank per cycle) ---------------
            static const int MAX_EVENTS_PER_LANE = 16; // 64B beat / 4 lanes = 16 steps per lane
            ap_uint<EVENT_W> evt0[MAX_EVENTS_PER_LANE];
            ap_uint<EVENT_W> evt1[MAX_EVENTS_PER_LANE];
            ap_uint<EVENT_W> evt2[MAX_EVENTS_PER_LANE];
            ap_uint<EVENT_W> evt3[MAX_EVENTS_PER_LANE];
#pragma HLS BIND_STORAGE variable=evt0 type=ram_1p impl=bram
#pragma HLS BIND_STORAGE variable=evt1 type=ram_1p impl=bram
#pragma HLS BIND_STORAGE variable=evt2 type=ram_1p impl=bram
#pragma HLS BIND_STORAGE variable=evt3 type=ram_1p impl=bram

            ap_uint<5> cnt0 = 0, cnt1 = 0, cnt2 = 0, cnt3 = 0;

byte_loop:
            for (int i = 0; i < DATA_WIDTH_BYTES; i += DCAM_P) {
#pragma HLS PIPELINE II=1

                unsigned char in_bytes[DCAM_P];
#pragma HLS ARRAY_PARTITION variable=in_bytes complete
                ap_uint<TDWIDTH> out_ids[DCAM_P];
#pragma HLS ARRAY_PARTITION variable=out_ids complete

                for (int p = 0; p < DCAM_P; p++) {
#pragma HLS UNROLL
                    in_bytes[p] = (unsigned char)v_in.data.range((i + p) * 8 + 7, (i + p) * 8);
                }

                bool reset = (pkt_in_beats == 1) && (i == 0);
                dcam_step_multi(in_bytes, reset, out_ids);

                // One write per lane bank max
                // lane 0
                if (out_ids[0] != 0 && cnt0 < MAX_EVENTS_PER_LANE) {
                    ap_uint<64> byte_index = (ap_uint<64>)((pkt_in_beats - 1) * DATA_WIDTH_BYTES + (i + 0));
                    evt0[cnt0++] = pack_event(byte_index, (ap_uint<16>)out_ids[0], (ap_uint<8>)0, (ap_uint<8>)0);
                }
                // lane 1
                if (out_ids[1] != 0 && cnt1 < MAX_EVENTS_PER_LANE) {
                    ap_uint<64> byte_index = (ap_uint<64>)((pkt_in_beats - 1) * DATA_WIDTH_BYTES + (i + 1));
                    evt1[cnt1++] = pack_event(byte_index, (ap_uint<16>)out_ids[1], (ap_uint<8>)1, (ap_uint<8>)0);
                }
                // lane 2
                if (out_ids[2] != 0 && cnt2 < MAX_EVENTS_PER_LANE) {
                    ap_uint<64> byte_index = (ap_uint<64>)((pkt_in_beats - 1) * DATA_WIDTH_BYTES + (i + 2));
                    evt2[cnt2++] = pack_event(byte_index, (ap_uint<16>)out_ids[2], (ap_uint<8>)2, (ap_uint<8>)0);
                }
                // lane 3
                if (out_ids[3] != 0 && cnt3 < MAX_EVENTS_PER_LANE) {
                    ap_uint<64> byte_index = (ap_uint<64>)((pkt_in_beats - 1) * DATA_WIDTH_BYTES + (i + 3));
                    evt3[cnt3++] = pack_event(byte_index, (ap_uint<16>)out_ids[3], (ap_uint<8>)3, (ap_uint<8>)0);
                }
            }

            // --------------- pack + write outside byte_loop ---------------
            // Merge order (simple + deterministic): lane0 then lane1 then lane2 then lane3
            // This keeps timing easy. If you must preserve exact byte order, we can do a sorted merge later.
            ap_uint<EVENT_W> pack_evs[SLOTS_PER_BEAT];
#pragma HLS ARRAY_PARTITION variable=pack_evs complete
            ap_uint<3> n_ev = 0;

            auto push_ev = [&](ap_uint<EVENT_W> e) {
#pragma HLS INLINE
                pack_evs[n_ev++] = e;
                if (n_ev == SLOTS_PER_BEAT) {
                    pkt o = build_event_pkt(pack_evs, n_ev);
                    k2n.write(o);
                    pkt_out_beats += 1;
                    pkt_out_bytes += (ap_uint<64>)n_ev * EVENT_BYTES;
                    saw_any_event = true;
                    n_ev = 0;
                }
            };

            // lane0
            for (int j = 0; j < MAX_EVENTS_PER_LANE; j++) {
#pragma HLS PIPELINE II=1
                if (j < cnt0) push_ev(evt0[j]);
            }
            // lane1
            for (int j = 0; j < MAX_EVENTS_PER_LANE; j++) {
#pragma HLS PIPELINE II=1
                if (j < cnt1) push_ev(evt1[j]);
            }
            // lane2
            for (int j = 0; j < MAX_EVENTS_PER_LANE; j++) {
#pragma HLS PIPELINE II=1
                if (j < cnt2) push_ev(evt2[j]);
            }
            // lane3
            for (int j = 0; j < MAX_EVENTS_PER_LANE; j++) {
#pragma HLS PIPELINE II=1
                if (j < cnt3) push_ev(evt3[j]);
            }

            // flush partial
            if (n_ev != 0) {
                pkt o = build_event_pkt(pack_evs, n_ev);
                k2n.write(o);
                pkt_out_beats += 1;
                pkt_out_bytes += (ap_uint<64>)n_ev * EVENT_BYTES;
                saw_any_event = true;
                n_ev = 0;
            }

            if (v_in.last) break;
        }

        if (!saw_any_event) {
            pkt o;
            o.data = 0;
            ap_uint<128> endw = pack_event(0, 0, 0, EV_FLAG_END);
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

        total_in_bytes  += pkt_in_bytes;
        total_out_bytes += pkt_out_bytes;

        pkt rep;
        rep.data = 0;
        rep.data.range(63,0)     = pkt_in_bytes;
        rep.data.range(127,64)   = pkt_in_beats;
        rep.data.range(191,128)  = pkt_out_bytes;
        rep.data.range(255,192)  = pkt_out_beats;
        rep.data.range(319,256)  = (ap_uint<64>)pkt_idx;
        rep.data.range(383,320)  = total_in_bytes;
        rep.data.range(447,384)  = total_out_bytes;

        rep.keep = (ap_uint<DATA_WIDTH_BYTES>)(~(ap_uint<DATA_WIDTH_BYTES>)0);
        rep.dest = 0;
        rep.user = 1;
        rep.id   = 0;
        rep.last = 1;
        k2n.write(rep);
    }
}
} // extern "C"
