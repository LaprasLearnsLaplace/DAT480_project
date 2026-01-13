#include "krnl_proj.h"
#include "scanner.h"
#include <ap_int.h>
#include <hls_stream.h>

static const int EVENT_W = 128;
static const int SLOTS_PER_BEAT = DWIDTH / EVENT_W;

struct match_event {
    ap_uint<64> byte_index;
    ap_uint<16> pattern_id;
    ap_uint<8> lane;
    ap_uint<2> control;  // 0:有效, 1:Beat, 2:Packet
};

// ============================================================
// 进程 1：双通道并行生产 (DCAM_P=2, II=1)
// ============================================================
void process_and_stream(
  hls::stream<pkt> &n2k,
  hls::stream<match_event> m_stream[2],
  unsigned int num_packets)
{
  // 【关键修改】：将全局字节计数器定义在 packet_loop 之外
  // 这样它才能跨越不同的 packet 持续累加，不会在每个包开始时归零
  uint64_t global_byte_idx = 0; 

packet_loop:
  for (unsigned pkt_idx = 0; pkt_idx < num_packets; pkt_idx++)
  {
      bool first_beat = true;

  beat_loop:
      while (true)
      {
#pragma HLS LOOP_TRIPCOUNT min=1 max=22
          pkt v_in = n2k.read(); 
          bool is_last = (v_in.last == 1);
          ap_uint<512> local_data = v_in.data;

      byte_loop:
          for (int i = 0; i < DATA_WIDTH_BYTES; i += 2)
          {
#pragma HLS PIPELINE II=1
              unsigned char bytes[2];
#pragma HLS ARRAY_PARTITION variable=bytes complete
              
              // 手动提取 Lane 0 和 Lane 1 的字节
              bytes[0] = (unsigned char)local_data.range((i + 1) * 8 - 1, i * 8);
              bytes[1] = (unsigned char)local_data.range((i + 2) * 8 - 1, (i + 1) * 8);

              ap_uint<16> out_ids[2];
              // 仅在第一个 Packet 的第一个 Beat 的第一个字节处复位扫描器状态
              bool reset = (first_beat && (i == 0)); 
              dcam_step_multi(bytes, reset, out_ids);

              // 写入对应通道，使用持续累加的 global_byte_idx
              if (out_ids[0] != 0) m_stream[0].write({global_byte_idx + i, out_ids[0], 0, 0});
              if (out_ids[1] != 0) m_stream[1].write({global_byte_idx + i + 1, out_ids[1], 1, 0});
          }

          // 发送同步标记 (1=beat_end, 2=packet_end)
          match_event marker = {0, 0, 0, (ap_uint<2>)(is_last ? 2 : 1)};
          m_stream[0].write(marker);
          m_stream[1].write(marker);

          // 每个 Beat 处理完后，增加 64 字节偏移
          global_byte_idx += DATA_WIDTH_BYTES;
          first_beat = false;
          if (is_last) break;
      }
  }
}
// ============================================================
// 进程 2：稳妥版收集器 (解决 CSim 报错 + 维持 -0.28ns Slack)
// ============================================================
// ============================================================
// 进程 2：寄存器打拍版收集器 (彻底解决 -0.14ns 时序违例)
// ============================================================
void collect_and_output(
  hls::stream<match_event> m_stream[2],
  hls::stream<pkt> &k2n,
  unsigned int dest,
  unsigned int num_packets)
{
  // 将 512 位打包逻辑拆分为 4 个独立的触发器，消除数组索引带来的 MUX 延迟
  ap_uint<128> r0, r1, r2, r3;
  #pragma HLS ARRAY_PARTITION variable=r0 complete
  #pragma HLS ARRAY_PARTITION variable=r1 complete
  #pragma HLS ARRAY_PARTITION variable=r2 complete
  #pragma HLS ARRAY_PARTITION variable=r3 complete
  
  int count = 0;

packet_loop:
  for (unsigned pkt_idx = 0; pkt_idx < num_packets; pkt_idx++)
  {
      bool packet_done = false;
  beat_sync_loop:
      while (!packet_done)
      {
          // 每个 Beat 必须收到两个通道的标记才算完成
          bool lane_done[2] = {false, false};
      
      poll_loop:
          while (!(lane_done[0] && lane_done[1]))
          {
#pragma HLS PIPELINE II=1
              for (int l = 0; l < 2; l++) {
                  #pragma HLS UNROLL
                  match_event me;
                  // 使用 read_nb 确保不阻塞，解决 CSim 报错
                  if (!lane_done[l] && m_stream[l].read_nb(me)) {
                      if (me.control == 0) {
                          // 将位域拼接打散到寄存器赋值中
                          ap_uint<128> e = 0;
                          e.range(127, 64) = me.byte_index;
                          e.range(63, 48)  = me.pattern_id;
                          e.range(47, 40)  = me.lane;

                          // 关键优化：显式寄存器赋值，时序表现优于数组索引
                          if (count == 0)      r0 = e;
                          else if (count == 1) r1 = e;
                          else if (count == 2) r2 = e;
                          else                 r3 = e;

                          count++;
                          if (count == 4) {
                              pkt o; 
                              // 拼接四个打拍寄存器输出，切断组合逻辑
                              o.data = (r3, r2, r1, r0);
                              o.keep = 0xFFFFFFFFFFFFFFFFULL;
                              o.last = 0; o.dest = dest; o.user = 0; o.id = 0;
                              k2n.write(o);
                              count = 0;
                          }
                      } else {
                          // 标记处理
                          lane_done[l] = true;
                          if (me.control == 2) packet_done = true;
                      }
                  }
              }
          }

          // 每个 Beat 结束或 Packet 结束时，刷新当前 batch 中的残留数据
          if (count > 0) {
              pkt o; 
              o.data = (r3, r2, r1, r0);
              o.dest = dest; o.last = 0; o.user = 0; o.id = 0;
              
              // 根据 count 数量分配 keep 掩码
              if (count == 1)      o.keep = 0x000000000000FFFFULL;
              else if (count == 2) o.keep = 0x00000000FFFFFFFFULL;
              else                 o.keep = 0x0000FFFFFFFFFFFFULL;
              
              k2n.write(o);
              count = 0;
          }
      }
      // 发送数据包结束标记 0xEE，对齐 Testbench 逻辑
      pkt end_pkt;
      end_pkt.data = 0; end_pkt.data.range(7, 0) = 0xEE;
      end_pkt.keep = 0x00000000000000FFULL; 
      end_pkt.last = 1; end_pkt.dest = dest; end_pkt.user = 0; end_pkt.id = 0;
      k2n.write(end_pkt);
  }
}

void krnl_proj(hls::stream<pkt> &n2k, hls::stream<pkt> &k2n, unsigned int dest, unsigned int num_packets) {
#pragma HLS INTERFACE axis port=n2k
#pragma HLS INTERFACE axis port=k2n
#pragma HLS INTERFACE s_axilite port=dest bundle=control
#pragma HLS INTERFACE s_axilite port=num_packets bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

#pragma HLS DATAFLOW
    hls::stream<match_event> m_fifo[2];
#pragma HLS STREAM variable=m_fifo depth=64
    process_and_stream(n2k, m_fifo, num_packets);
    collect_and_output(m_fifo, k2n, dest, num_packets);
}