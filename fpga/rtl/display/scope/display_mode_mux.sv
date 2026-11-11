`default_nettype none
// Both renderers share raster reset, 720p timing and six pixel stages.
// Change only in vblank; the seventh stage registers RGB and timing together.
module display_mode_mux #(
    parameter bit ENABLE_FFT = 1'b1
) (
    input wire clk_i, rst_n_i,
    input wire fft_selected_i, vblank_start_i,
    input wire [23:0] scope_rgb_i, fft_rgb_i,
    input wire de_i, hs_i, vs_i,
    output reg [23:0] rgb_o,
    output reg de_o, hs_o, vs_o,
    output wire fft_active_o
);
    (* async_reg = "true", syn_preserve = 1 *) reg [2:0] mode_sync;
    reg active_fft;
    assign fft_active_o = active_fft;
    always @(posedge clk_i or negedge rst_n_i) begin
        if (!rst_n_i) begin
            mode_sync <= 0;
            active_fft <= 0;
            rgb_o <= 0;
            {de_o, hs_o, vs_o} <= 0;
        end else begin
            mode_sync <= ENABLE_FFT ? {mode_sync[1:0], fft_selected_i} : 3'd0;
            if (vblank_start_i && !de_i)
                active_fft <= ENABLE_FFT && mode_sync[2];
            rgb_o <= de_i ? (active_fft ? fft_rgb_i : scope_rgb_i) : 24'd0;
            {de_o, hs_o, vs_o} <= {de_i, hs_i, vs_i};
        end
    end
endmodule
`default_nettype wire
