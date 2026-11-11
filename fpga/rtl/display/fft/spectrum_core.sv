`default_nettype none
// PCM -> spectrum -> frame-synchronous raster. The 2560-bit publication bus
// is bundled data: it is held until the receiver acknowledges a complete copy.
// Both domains must share reset assertion; each may release reset locally.
module spectrum_core #(
    parameter integer SAMPLE_RATE_NUM = 91015625,
    parameter integer SAMPLE_RATE_DEN = 948
) (
    input  wire               core_clk_i,
    input  wire               pixel_clk_i,
    input  wire               core_rst_n_i,
    input  wire               pixel_rst_n_i,
    input  wire signed [23:0] pcm_i,
    input  wire               pcm_valid_i,
    output wire [23:0]        rgb_o,
    output wire               de_o,
    output wire               hs_o,
    output wire               vs_o,
    output wire               frame_tick_o
);
    wire column_valid, frame_done;
    wire [7:0] column_index;
    wire [9:0] column_level;
    reg [2559:0] held_levels;
    reg request_toggle;
    wire ack_toggle;
    (* async_reg = "true", syn_preserve = 1 *) reg [2:0] ack_sync;
    // frame_done also closes the one-clock gap before request_toggle updates.
    // A coincident PCM hop must not launch a second unpublished frame there.
    wire publish_ready = (request_toggle == ack_sync[2]) && !frame_done;

    audio_spectrum_fft u_fft (
        .clk_i(core_clk_i), .rst_n_i(core_rst_n_i),
        .pcm_i(pcm_i), .pcm_valid_i(pcm_valid_i),
        .publish_ready_i(publish_ready),
        .column_valid_o(column_valid), .column_index_o(column_index),
        .column_level_o(column_level), .frame_done_o(frame_done),
        .frame_count_o(), .skipped_count_o()
    );

    always @(posedge core_clk_i) begin
        if (!core_rst_n_i) begin
            held_levels <= 0;
            request_toggle <= 0;
            ack_sync <= 0;
        end else begin
            ack_sync <= {ack_sync[1:0], ack_toggle};
            if (column_valid)
                held_levels[12'(column_index) * 12'd10 +: 10] <= column_level;
            // The final column precedes frame_done by one full core clock.
            if (frame_done) request_toggle <= ~request_toggle;
        end
    end

    spectrum_display #(.SPLIT_VIEW(1'b0),
        .SAMPLE_RATE_NUM(SAMPLE_RATE_NUM), .SAMPLE_RATE_DEN(SAMPLE_RATE_DEN)
    ) u_display (
        .clk_i(pixel_clk_i), .rst_n_i(pixel_rst_n_i),
        .levels_i(held_levels), .request_toggle_i(request_toggle),
        .ack_toggle_o(ack_toggle), .rgb_o(rgb_o), .de_o(de_o),
        .hs_o(hs_o), .vs_o(vs_o), .frame_tick_o(frame_tick_o),
        .raster_x_o(), .raster_y_o()
    );
endmodule
`default_nettype wire
