// stream_throughput.sv
// Connects to the HLS `frontend` block using internal AXI-Stream cfg (64b) and
// samples (256b) ports. Also passes through an external AXI-Stream data path
// (default 512b) while snooping signals to produce throughput samples.

module stream_throughput #(
    parameter DATA_WIDTH = 512
) (
    input  wire                  ap_clk,
    input  wire                  ap_rst_n,

    // External AXI-Stream input (source)
    input  wire [DATA_WIDTH-1:0] s_axis_in_tdata,
    input  wire                  s_axis_in_tvalid,
    output wire                  s_axis_in_tready,
    input  wire [DATA_WIDTH/8-1:0] s_axis_in_tkeep,
    input  wire                  s_axis_in_tlast,

    // External AXI-Stream output (sink)
    output wire [DATA_WIDTH-1:0] m_axis_out_tdata,
    output wire                  m_axis_out_tvalid,
    input  wire                  m_axis_out_tready,
    output wire [DATA_WIDTH/8-1:0] m_axis_out_tkeep,
    output wire                  m_axis_out_tlast,

    // cfg stream from HLS frontend (consumer of cfg messages)
    input  wire [63:0]           cfg_TDATA,
    input  wire                  cfg_TVALID,
    output reg                   cfg_TREADY,

    // samples stream to HLS frontend (producer) - 128 bits (4x32)
    output reg  [127:0]          samples_TDATA,
    output reg                   samples_TVALID,
    input  wire                  samples_TREADY
);

// Combinatorial pass-through for the data path
assign m_axis_out_tdata = s_axis_in_tdata;
assign m_axis_out_tvalid = s_axis_in_tvalid;
assign s_axis_in_tready = m_axis_out_tready;
assign m_axis_out_tkeep = s_axis_in_tkeep;
assign m_axis_out_tlast = s_axis_in_tlast;

// Internal registers for sampling FSM
reg sampling_active;
reg started;

reg [31:0] cfg_num_samples;
reg [31:0] cfg_rate;

 reg [31:0] cycles_cnt;
 reg [31:0] bytes_cnt;
 reg [31:0] ready_not_valid_cnt;
 reg [31:0] valid_not_ready_cnt;

reg [31:0] sample_count;
reg [31:0] rate_count;
reg emit_pending; // indicates that a sample is prepared and waiting to be accepted

// Helper: compute number of valid bytes in tkeep (SystemVerilog $countones)
wire [31:0] keep_bytes = $countones(s_axis_in_tkeep);

// snoop signals
// valid is source valid, ready is sink ready
wire valid = s_axis_in_tvalid;
wire ready = m_axis_out_tready;


// Reset and control logic
always_ff @(posedge ap_clk or negedge ap_rst_n) begin
    if (!ap_rst_n) begin
        sampling_active <= 1'b0;
        cfg_TREADY <= 1'b1; // ready to accept cfg when idle

        started <= 1'b0;
        cfg_num_samples <= 32'd0;
        cfg_rate <= 32'd1;

        cycles_cnt <= 64'd0;
        bytes_cnt <= 64'd0;
        ready_not_valid_cnt <= 64'd0;
        valid_not_ready_cnt <= 64'd0;

        sample_count <= 32'd0;
        rate_count <= 32'd0;
        emit_pending <= 1'b0;

        samples_TDATA <= 256'd0;
        samples_TVALID <= 1'b0;
    end else begin
        // cfg handshake: only accept when not currently sampling
        if (!sampling_active) begin
            cfg_TREADY <= 1'b1;
            if (cfg_TVALID && cfg_TREADY) begin
                // capture cfg fields: lower 32 bits = num_samples, upper 32 bits = rate
                cfg_num_samples <= cfg_TDATA[31:0];
                cfg_rate <= cfg_TDATA[63:32];
                // sanitize rate (treat 0 as 1)
                if (cfg_TDATA[63:32] == 32'd0)
                    cfg_rate <= 32'd1;

                // initialize counters
                cycles_cnt <= 64'd0;
                bytes_cnt <= 64'd0;
                ready_not_valid_cnt <= 64'd0;
                valid_not_ready_cnt <= 64'd0;

                sample_count <= 32'd0;
                rate_count <= 32'd0;
                started <= 1'b0;
                sampling_active <= 1'b1;
                emit_pending <= 1'b0;
                samples_TVALID <= 1'b0;

                // lower ready while active (cfg ready kept low while sampling)
                cfg_TREADY <= 1'b0;
            end
        end else begin
            // while active, cfg_TREADY stays low
            cfg_TREADY <= 1'b0;

            // update started flag
            started <= started || (valid && ready);

            // update counters every cycle after started
            if (started) begin
                cycles_cnt <= cycles_cnt + 64'd1;
                // bytes add only when a beat transferred (valid && ready)
                if (valid && ready) begin
                    bytes_cnt <= bytes_cnt + keep_bytes;
                end
                // ready without valid
                if (!valid && ready) begin
                    ready_not_valid_cnt <= ready_not_valid_cnt + 64'd1;
                end
                // valid without ready
                if (valid && !ready) begin
                    valid_not_ready_cnt <= valid_not_ready_cnt + 64'd1;
                end

                // sampling rate handling
                if (rate_count == (cfg_rate - 1)) begin
                    // prepare sample for emission
                    // Pack fields into 256b TDATA. The HLS-generated stream order tends to
                    // place the last-declared struct field in the MSBs when seen from
                    // Verilog. To mirror the typical HLS packing and the way cfg is
                    // handled, we concatenate fields in "reverse" order so that C
                    // ordering (cycles, bytes, ready_not_valid, valid_not_ready)
                    // maps as expected by the HLS consumer.
                        // Pack 4x32-bit fields into 128-bit TDATA. Order matches HLS
                        // struct packing: {valid_not_ready, ready_not_valid, bytes, cycles}
                        samples_TDATA <= { valid_not_ready_cnt,
                                           ready_not_valid_cnt,
                                           bytes_cnt,
                                           cycles_cnt };
                    samples_TVALID <= 1'b1;
                    emit_pending <= 1'b1;

                    // do not reset counters until the sample is accepted
                end else begin
                    rate_count <= rate_count + 32'd1;
                end
            end

            // If a sample has been prepared and is accepted, clear and advance
            if (emit_pending && samples_TVALID && samples_TREADY) begin
                // clear counters for next sample
                cycles_cnt <= 64'd0;
                bytes_cnt <= 64'd0;
                ready_not_valid_cnt <= 64'd0;
                valid_not_ready_cnt <= 64'd0;

                samples_TVALID <= 1'b0;
                emit_pending <= 1'b0;

                sample_count <= sample_count + 32'd1;

                // reset rate counter
                rate_count <= 32'd0;

                // check termination
                if (sample_count + 32'd1 >= cfg_num_samples) begin
                    sampling_active <= 1'b0;
                    cfg_TREADY <= 1'b1; // ready for next cfg once idle
                    started <= 1'b0;
                end
            end
        end
    end
end

endmodule
