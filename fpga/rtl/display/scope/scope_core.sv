`default_nettype none
// Continuous PCM capture and immutable double-bank column display.
// Both reset endpoints must be asserted together and released synchronously
// in their respective domains. No image framebuffer or scan-time backpressure.
module scope_core #(
    parameter integer SAMPLE_RATE_NUM = 91015625,
    parameter integer SAMPLE_RATE_DEN = 948
) (
    input wire core_clk_i, pixel_clk_i,
    input wire core_rst_n_i, pixel_rst_n_i,
    input wire pcm_valid_i,
    input wire signed [23:0] pcm_left_i, pcm_right_i,
    input wire cfg_valid_i,
    output wire cfg_ready_o,
    input wire [1:0] cfg_timebase_i, cfg_gain_i,
    input wire cfg_auto_i, cfg_falling_i, cfg_channel_i,
    input wire signed [23:0] cfg_threshold_i,
    input wire [23:0] cfg_hysteresis_i,
    input wire [15:0] cfg_holdoff_i, cfg_timeout_i,
    output wire [23:0] rgb_o,
    output wire de_o, hs_o, vs_o, frame_start_o,
    output wire [31:0] sample_count_o, publish_count_o, overrun_count_o,
    output wire waiting_o, image_valid_o,
    output wire [95:0] active_meta_o,
    output wire [31:0] frame_count_o
);
    wire ring_wr_en, ring_rd_en, feature_wr_en, feature_rd_en;
    wire [12:0] ring_wr_addr, ring_rd_addr;
    wire [63:0] ring_wr_data, ring_rd_data;
    wire [9:0] feature_wr_addr, feature_rd_addr;
    wire [35:0] feature_wr_data, feature_rd_data;
    wire publish_toggle, ack_toggle;
    wire [95:0] publish_meta;
    (* async_reg = "true", syn_preserve = 1 *) reg [2:0] ack_sync;
    always @(posedge core_clk_i or negedge core_rst_n_i) begin
        if (!core_rst_n_i) ack_sync <= 3'b000;
        else ack_sync <= {ack_sync[1:0], ack_toggle};
    end
    scope_acquisition u_acquisition (
        .clk_i(core_clk_i), .rst_n_i(core_rst_n_i),
        .pcm_valid_i(pcm_valid_i), .pcm_left_i(pcm_left_i), .pcm_right_i(pcm_right_i),
        .cfg_valid_i(cfg_valid_i), .cfg_ready_o(cfg_ready_o),
        .cfg_timebase_i(cfg_timebase_i), .cfg_gain_i(cfg_gain_i),
        .cfg_auto_i(cfg_auto_i), .cfg_falling_i(cfg_falling_i), .cfg_channel_i(cfg_channel_i),
        .cfg_threshold_i(cfg_threshold_i), .cfg_hysteresis_i(cfg_hysteresis_i),
        .cfg_holdoff_i(cfg_holdoff_i), .cfg_timeout_i(cfg_timeout_i),
        .ack_toggle_i(ack_sync[2]),
        .ring_wr_en_o(ring_wr_en), .ring_wr_addr_o(ring_wr_addr), .ring_wr_data_o(ring_wr_data),
        .ring_rd_en_o(ring_rd_en), .ring_rd_addr_o(ring_rd_addr), .ring_rd_data_i(ring_rd_data),
        .feature_wr_en_o(feature_wr_en), .feature_wr_addr_o(feature_wr_addr), .feature_wr_data_o(feature_wr_data),
        .publish_toggle_o(publish_toggle), .publish_meta_o(publish_meta),
        .sample_count_o(sample_count_o), .publish_count_o(publish_count_o),
        .overrun_count_o(overrun_count_o), .waiting_o(waiting_o)
    );
    // Actual generated wrappers: synchronous one-cycle Bypass read. SYNC
    // RESET clears outputs only; acquisition/renderer validity hides stale RAM.
    scope_ring_storage u_ring (
        .clka(core_clk_i), .cea(ring_wr_en), .ada(ring_wr_addr), .din(ring_wr_data),
        .clkb(core_clk_i), .ceb(ring_rd_en), .adb(ring_rd_addr), .dout(ring_rd_data),
        .oce(1'b1), .reset(~core_rst_n_i)
    );
    Scope_Feature_RAM u_features (
        .clka(core_clk_i), .cea(feature_wr_en), .ada(feature_wr_addr), .din(feature_wr_data),
        .clkb(pixel_clk_i), .ceb(feature_rd_en), .adb(feature_rd_addr), .dout(feature_rd_data),
        .oce(1'b1), .reset(~pixel_rst_n_i)
    );
    scope_renderer #(
        .SAMPLE_RATE_NUM(SAMPLE_RATE_NUM), .SAMPLE_RATE_DEN(SAMPLE_RATE_DEN)
    ) u_renderer (
        .clk_i(pixel_clk_i), .rst_n_i(pixel_rst_n_i),
        .publish_toggle_i(publish_toggle), .publish_meta_i(publish_meta),
        .feature_rd_en_o(feature_rd_en), .feature_rd_addr_o(feature_rd_addr), .feature_rd_data_i(feature_rd_data),
        .ack_toggle_o(ack_toggle), .rgb_o(rgb_o), .de_o(de_o), .hs_o(hs_o), .vs_o(vs_o),
        .frame_start_o(frame_start_o), .image_valid_o(image_valid_o), .active_meta_o(active_meta_o),
        .frame_count_o(frame_count_o), .raw_x_o(), .raw_y_o()
    );
endmodule
`default_nettype wire
