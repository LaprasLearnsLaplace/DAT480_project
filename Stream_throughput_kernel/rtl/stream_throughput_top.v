`timescale 1ns / 1ps

module stream_throughput_top #(
    parameter DATA_WIDTH = 512
) (
    input  wire        ap_clk,
    input  wire        ap_rst_n,

    // m_axi_gmem (256-bit AXI master) - exposed
    output wire        m_axi_gmem_AWVALID,
    input  wire        m_axi_gmem_AWREADY,
    output wire [63:0] m_axi_gmem_AWADDR,
    output wire [0:0]  m_axi_gmem_AWID,
    output wire [7:0]  m_axi_gmem_AWLEN,
    output wire [2:0]  m_axi_gmem_AWSIZE,
    output wire [1:0]  m_axi_gmem_AWBURST,
    output wire [1:0]  m_axi_gmem_AWLOCK,
    output wire [3:0]  m_axi_gmem_AWCACHE,
    output wire [2:0]  m_axi_gmem_AWPROT,
    output wire [3:0]  m_axi_gmem_AWQOS,
    output wire [3:0]  m_axi_gmem_AWREGION,
    output wire [0:0]  m_axi_gmem_AWUSER,

    output wire        m_axi_gmem_WVALID,
    input  wire        m_axi_gmem_WREADY,
    output wire [127:0] m_axi_gmem_WDATA,
    output wire [15:0] m_axi_gmem_WSTRB,
    output wire        m_axi_gmem_WLAST,
    output wire [0:0]  m_axi_gmem_WID,
    output wire [0:0]  m_axi_gmem_WUSER,

    output wire        m_axi_gmem_ARVALID,
    input  wire        m_axi_gmem_ARREADY,
    output wire [63:0] m_axi_gmem_ARADDR,
    output wire [0:0]  m_axi_gmem_ARID,
    output wire [7:0]  m_axi_gmem_ARLEN,
    output wire [2:0]  m_axi_gmem_ARSIZE,
    output wire [1:0]  m_axi_gmem_ARBURST,
    output wire [1:0]  m_axi_gmem_ARLOCK,
    output wire [3:0]  m_axi_gmem_ARCACHE,
    output wire [2:0]  m_axi_gmem_ARPROT,
    output wire [3:0]  m_axi_gmem_ARQOS,
    output wire [3:0]  m_axi_gmem_ARREGION,
    output wire [0:0]  m_axi_gmem_ARUSER,

    input  wire        m_axi_gmem_RVALID,
    output wire        m_axi_gmem_RREADY,
    input  wire [127:0] m_axi_gmem_RDATA,
    input  wire        m_axi_gmem_RLAST,
    input  wire [0:0]  m_axi_gmem_RID,
    input  wire [0:0]  m_axi_gmem_RUSER,
    input  wire [1:0]  m_axi_gmem_RRESP,

    input  wire        m_axi_gmem_BVALID,
    output wire        m_axi_gmem_BREADY,
    input  wire [1:0]  m_axi_gmem_BRESP,
    input  wire [0:0]  m_axi_gmem_BID,
    input  wire [0:0]  m_axi_gmem_BUSER,

    // s_axi_control (AXI-Lite slave, 32-bit data) - exposed
    input  wire        s_axi_control_AWVALID,
    output wire        s_axi_control_AWREADY,
    input  wire [5:0] s_axi_control_AWADDR,

    input  wire        s_axi_control_WVALID,
    output wire        s_axi_control_WREADY,
    input  wire [31:0] s_axi_control_WDATA,
    input  wire [3:0]  s_axi_control_WSTRB,

    input  wire        s_axi_control_ARVALID,
    output wire        s_axi_control_ARREADY,
    input  wire [5:0] s_axi_control_ARADDR,

    output wire        s_axi_control_RVALID,
    input  wire        s_axi_control_RREADY,
    output wire [31:0] s_axi_control_RDATA,
    output wire [1:0]  s_axi_control_RRESP,

    output wire        s_axi_control_BVALID,
    input  wire        s_axi_control_BREADY,
    output wire [1:0]  s_axi_control_BRESP,

    // interrupt - exposed
    output wire        interrupt
,
    // External AXI-Stream interface (data path) - parameterizable width
    input  wire [DATA_WIDTH-1:0]  s_axis_in_tdata,
    input  wire                  s_axis_in_tvalid,
    output wire                  s_axis_in_tready,
    input  wire [DATA_WIDTH/8-1:0] s_axis_in_tkeep,
    input  wire                  s_axis_in_tlast,

    output wire [DATA_WIDTH-1:0]  m_axis_out_tdata,
    output wire                  m_axis_out_tvalid,
    input  wire                  m_axis_out_tready,
    output wire [DATA_WIDTH/8-1:0] m_axis_out_tkeep,
    output wire                  m_axis_out_tlast
);

// Internal AXI-Stream signals for cfg (64-bit) and samples (256-bit)
wire [63:0]  cfg_TDATA;
wire         cfg_TVALID;
wire         cfg_TREADY;

wire [127:0] samples_TDATA;
wire         samples_TVALID;
wire         samples_TREADY;

// Instantiate stream_throughput (connects external AXI-Stream to internal cfg/samples)
stream_throughput #(.DATA_WIDTH(DATA_WIDTH)) u_stream_throughput (
    .ap_clk(ap_clk),
    .ap_rst_n(ap_rst_n),

    .s_axis_in_tdata(s_axis_in_tdata),
    .s_axis_in_tvalid(s_axis_in_tvalid),
    .s_axis_in_tready(s_axis_in_tready),
    .s_axis_in_tkeep(s_axis_in_tkeep),
    .s_axis_in_tlast(s_axis_in_tlast),

    .m_axis_out_tdata(m_axis_out_tdata),
    .m_axis_out_tvalid(m_axis_out_tvalid),
    .m_axis_out_tready(m_axis_out_tready),
    .m_axis_out_tkeep(m_axis_out_tkeep),
    .m_axis_out_tlast(m_axis_out_tlast),

    .cfg_TDATA(cfg_TDATA),
    .cfg_TVALID(cfg_TVALID),
    .cfg_TREADY(cfg_TREADY),

    .samples_TDATA(samples_TDATA),
    .samples_TVALID(samples_TVALID),
    .samples_TREADY(samples_TREADY)
);

// Instantiate the existing frontend module
frontend u_frontend (
    .ap_clk(ap_clk),
    .ap_rst_n(ap_rst_n),

    .m_axi_gmem_AWVALID(m_axi_gmem_AWVALID),
    .m_axi_gmem_AWREADY(m_axi_gmem_AWREADY),
    .m_axi_gmem_AWADDR(m_axi_gmem_AWADDR),
    .m_axi_gmem_AWID(m_axi_gmem_AWID),
    .m_axi_gmem_AWLEN(m_axi_gmem_AWLEN),
    .m_axi_gmem_AWSIZE(m_axi_gmem_AWSIZE),
    .m_axi_gmem_AWBURST(m_axi_gmem_AWBURST),
    .m_axi_gmem_AWLOCK(m_axi_gmem_AWLOCK),
    .m_axi_gmem_AWCACHE(m_axi_gmem_AWCACHE),
    .m_axi_gmem_AWPROT(m_axi_gmem_AWPROT),
    .m_axi_gmem_AWQOS(m_axi_gmem_AWQOS),
    .m_axi_gmem_AWREGION(m_axi_gmem_AWREGION),
    .m_axi_gmem_AWUSER(m_axi_gmem_AWUSER),

    .m_axi_gmem_WVALID(m_axi_gmem_WVALID),
    .m_axi_gmem_WREADY(m_axi_gmem_WREADY),
    .m_axi_gmem_WDATA(m_axi_gmem_WDATA),
    .m_axi_gmem_WSTRB(m_axi_gmem_WSTRB),
    .m_axi_gmem_WLAST(m_axi_gmem_WLAST),
    .m_axi_gmem_WID(m_axi_gmem_WID),
    .m_axi_gmem_WUSER(m_axi_gmem_WUSER),

    .m_axi_gmem_ARVALID(m_axi_gmem_ARVALID),
    .m_axi_gmem_ARREADY(m_axi_gmem_ARREADY),
    .m_axi_gmem_ARADDR(m_axi_gmem_ARADDR),
    .m_axi_gmem_ARID(m_axi_gmem_ARID),
    .m_axi_gmem_ARLEN(m_axi_gmem_ARLEN),
    .m_axi_gmem_ARSIZE(m_axi_gmem_ARSIZE),
    .m_axi_gmem_ARBURST(m_axi_gmem_ARBURST),
    .m_axi_gmem_ARLOCK(m_axi_gmem_ARLOCK),
    .m_axi_gmem_ARCACHE(m_axi_gmem_ARCACHE),
    .m_axi_gmem_ARPROT(m_axi_gmem_ARPROT),
    .m_axi_gmem_ARQOS(m_axi_gmem_ARQOS),
    .m_axi_gmem_ARREGION(m_axi_gmem_ARREGION),
    .m_axi_gmem_ARUSER(m_axi_gmem_ARUSER),

    .m_axi_gmem_RVALID(m_axi_gmem_RVALID),
    .m_axi_gmem_RREADY(m_axi_gmem_RREADY),
    .m_axi_gmem_RDATA(m_axi_gmem_RDATA),
    .m_axi_gmem_RLAST(m_axi_gmem_RLAST),
    .m_axi_gmem_RID(m_axi_gmem_RID),
    .m_axi_gmem_RUSER(m_axi_gmem_RUSER),
    .m_axi_gmem_RRESP(m_axi_gmem_RRESP),

    .m_axi_gmem_BVALID(m_axi_gmem_BVALID),
    .m_axi_gmem_BREADY(m_axi_gmem_BREADY),
    .m_axi_gmem_BRESP(m_axi_gmem_BRESP),
    .m_axi_gmem_BID(m_axi_gmem_BID),
    .m_axi_gmem_BUSER(m_axi_gmem_BUSER),

    .cfg_TDATA(cfg_TDATA),
    .cfg_TVALID(cfg_TVALID),
    .cfg_TREADY(cfg_TREADY),

    .samples_TDATA(samples_TDATA),
    .samples_TVALID(samples_TVALID),
    .samples_TREADY(samples_TREADY),

    .s_axi_control_AWVALID(s_axi_control_AWVALID),
    .s_axi_control_AWREADY(s_axi_control_AWREADY),
    .s_axi_control_AWADDR(s_axi_control_AWADDR),

    .s_axi_control_WVALID(s_axi_control_WVALID),
    .s_axi_control_WREADY(s_axi_control_WREADY),
    .s_axi_control_WDATA(s_axi_control_WDATA),
    .s_axi_control_WSTRB(s_axi_control_WSTRB),

    .s_axi_control_ARVALID(s_axi_control_ARVALID),
    .s_axi_control_ARREADY(s_axi_control_ARREADY),
    .s_axi_control_ARADDR(s_axi_control_ARADDR),

    .s_axi_control_RVALID(s_axi_control_RVALID),
    .s_axi_control_RREADY(s_axi_control_RREADY),
    .s_axi_control_RDATA(s_axi_control_RDATA),
    .s_axi_control_RRESP(s_axi_control_RRESP),

    .s_axi_control_BVALID(s_axi_control_BVALID),
    .s_axi_control_BREADY(s_axi_control_BREADY),
    .s_axi_control_BRESP(s_axi_control_BRESP),

    .interrupt(interrupt)
);

endmodule
