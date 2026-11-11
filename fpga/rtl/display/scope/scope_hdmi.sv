`default_nettype none
// Official Tang Mega HDMI path, shared reset for both waveform-bank endpoints.
module scope_hdmi #(
    parameter bit ENABLE_FFT = 1'b1,
    parameter integer SAMPLE_RATE_NUM = 91015625,
    parameter integer SAMPLE_RATE_DEN = 948,
    parameter integer LOCK_HOLD_CYCLES = 100000,
    parameter [1:0] DEFAULT_TIMEBASE = 2'd2,
    parameter [1:0] DEFAULT_GAIN = 2'd0,
    parameter bit DEFAULT_AUTO = 1'b1,
    parameter bit DEFAULT_FALLING = 1'b0,
    parameter bit DEFAULT_CHANNEL = 1'b0,
    parameter signed [23:0] DEFAULT_THRESHOLD = 24'sd0,
    parameter [23:0] DEFAULT_HYSTERESIS = 24'd65536,
    parameter [15:0] DEFAULT_HOLDOFF = 16'd256,
    parameter [15:0] DEFAULT_TIMEOUT = 16'd9600
) (
    input wire sys_clk_i, sys_rst_n_i, core_clk_i, audio_rst_n_i,
    input wire signed [23:0] pcm_left_i, pcm_right_i,
    input wire pcm_valid_i,
    input wire fft_selected_i, // stable requested mode, synchronized in pixel domain
    output wire tmds_clk_p_o, tmds_clk_n_o,
    output wire [2:0] tmds_data_p_o, tmds_data_n_o
);
    wire pixel_clk, serial_clk, lane_reset_clk, video_lock, video_ready;
    Spectrum_Video_PLL u_video_pll (
        .clkin(sys_clk_i), .init_clk(sys_clk_i), .clkout0(pixel_clk),
        .clkout1(serial_clk), .clkout2(lane_reset_clk), .lock(video_lock)
    );
    spectrum_lock_filter #(.HOLD_CYCLES(LOCK_HOLD_CYCLES)) u_lock_filter (
        .clk_i(sys_clk_i), .rst_n_i(sys_rst_n_i), .lock_i(video_lock), .qualified_o(video_ready)
    );
    wire visual_reset_n = sys_rst_n_i & audio_rst_n_i & video_lock & video_ready;
    (* async_reg = "true", syn_preserve = 1 *) reg [2:0] core_reset_sync;
    (* async_reg = "true", syn_preserve = 1 *) reg [2:0] pixel_reset_sync;
    always @(posedge core_clk_i or negedge visual_reset_n) begin
        if (!visual_reset_n) core_reset_sync <= 0;
        else core_reset_sync <= {core_reset_sync[1:0], 1'b1};
    end
    always @(posedge pixel_clk or negedge visual_reset_n) begin
        if (!visual_reset_n) pixel_reset_sync <= 0;
        else pixel_reset_sync <= {pixel_reset_sync[1:0], 1'b1};
    end
    wire [23:0] rgb;
    wire de, hs, vs;
    wire [23:0] scope_rgb, fft_rgb;
    wire scope_de, scope_hs, scope_vs, scope_frame_start;
    generate if (ENABLE_FFT) begin : g_fft
        // FFT runs directly from the board's 50 MHz clock. The audio tap is
        // captured atomically and held until the destination acknowledges it.
        // A normal 96 kHz sample interval is far longer than the CDC round trip.
        (* async_reg = "true", syn_preserve = 1 *) reg [2:0] fft_reset_sync;
        always @(posedge sys_clk_i or negedge visual_reset_n) begin
            if (!visual_reset_n) fft_reset_sync <= 0;
            else fft_reset_sync <= {fft_reset_sync[1:0], 1'b1};
        end
        wire [23:0] fft_pcm;
        wire fft_pcm_valid, fft_pcm_ready;
        held_bus_cdc #(.WIDTH(24)) u_pcm_cdc (
            .src_clk_i(core_clk_i), .dst_clk_i(sys_clk_i), .rst_n_i(visual_reset_n),
            .src_data_i(pcm_left_i), .src_valid_i(pcm_valid_i), .src_ready_o(fft_pcm_ready),
            .dst_data_o(fft_pcm), .dst_valid_o(fft_pcm_valid), .dst_ready_i(1'b1)
        );
        spectrum_core #(.SAMPLE_RATE_NUM(SAMPLE_RATE_NUM),.SAMPLE_RATE_DEN(SAMPLE_RATE_DEN)) u_fft_core (
            .core_clk_i(sys_clk_i), .pixel_clk_i(pixel_clk),
            .core_rst_n_i(fft_reset_sync[2]), .pixel_rst_n_i(pixel_reset_sync[2]),
            .pcm_i($signed(fft_pcm)), .pcm_valid_i(fft_pcm_valid),
            .rgb_o(fft_rgb), .de_o(), .hs_o(), .vs_o(), .frame_tick_o()
        );
    end else begin : g_no_fft
        assign fft_rgb = 24'd0;
    end endgenerate
    display_mode_mux #(.ENABLE_FFT(ENABLE_FFT)) u_mode_mux (
        .clk_i(pixel_clk), .rst_n_i(pixel_reset_sync[2]),
        .fft_selected_i(fft_selected_i), .vblank_start_i(scope_frame_start),
        .scope_rgb_i(scope_rgb), .fft_rgb_i(fft_rgb),
        .de_i(scope_de), .hs_i(scope_hs), .vs_i(scope_vs),
        .rgb_o(rgb), .de_o(de), .hs_o(hs), .vs_o(vs), .fft_active_o()
    );
    // Accept one complete preset transaction after common reset. These RTL
    // parameters are the shipped board control surface; no buttons or SPI
    // registers are claimed. The separate scope_core ready/valid API supports
    // future runtime controls without changing the capture/display contract.
    wire cfg_ready;
    reg preset_pending;
    always @(posedge core_clk_i) begin
        if (!core_reset_sync[2]) preset_pending <= 1'b1;
        else if (cfg_ready) preset_pending <= 1'b0;
    end
    scope_core #(
        .SAMPLE_RATE_NUM(SAMPLE_RATE_NUM), .SAMPLE_RATE_DEN(SAMPLE_RATE_DEN)
    ) u_core (
        .core_clk_i(core_clk_i), .pixel_clk_i(pixel_clk),
        .core_rst_n_i(core_reset_sync[2]), .pixel_rst_n_i(pixel_reset_sync[2]),
        .pcm_left_i(pcm_left_i), .pcm_right_i(pcm_right_i), .pcm_valid_i(pcm_valid_i),
        .cfg_valid_i(preset_pending), .cfg_ready_o(cfg_ready), .cfg_timebase_i(DEFAULT_TIMEBASE), .cfg_gain_i(DEFAULT_GAIN),
        .cfg_auto_i(DEFAULT_AUTO), .cfg_falling_i(DEFAULT_FALLING), .cfg_channel_i(DEFAULT_CHANNEL),
        .cfg_threshold_i(DEFAULT_THRESHOLD), .cfg_hysteresis_i(DEFAULT_HYSTERESIS),
        .cfg_holdoff_i(DEFAULT_HOLDOFF), .cfg_timeout_i(DEFAULT_TIMEOUT),
        .rgb_o(scope_rgb), .de_o(scope_de), .hs_o(scope_hs), .vs_o(scope_vs), .frame_start_o(scope_frame_start),
        .sample_count_o(), .publish_count_o(), .overrun_count_o(), .waiting_o(),
        .image_valid_o(), .active_meta_o(), .frame_count_o()
    );
    spectrum_hdmi_tx u_hdmi (
        .pixel_clk_i(pixel_clk), .serial_clk_i(serial_clk), .reset_clk_i(lane_reset_clk),
        .rst_n_i(visual_reset_n & pixel_reset_sync[2]), .rgb_i(rgb), .de_i(de), .hs_i(hs), .vs_i(vs),
        .tmds_clk_p_o(tmds_clk_p_o), .tmds_clk_n_o(tmds_clk_n_o),
        .tmds_data_p_o(tmds_data_p_o), .tmds_data_n_o(tmds_data_n_o)
    );
endmodule
`default_nettype wire
