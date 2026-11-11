// DVI-compatible video over the Tang Mega HDMI connector.
// TMDS encoder: Sipeed TangMega-138K-example, Apache-2.0; see origin.json.
// PHY wiring follows that example. Pixel RGB and DE/HS/VS must be aligned
// before this boundary. serial_clk_i = 5 * pixel_clk_i, phase-related PLL outputs.
module spectrum_hdmi_tx (
    input  wire         pixel_clk_i,
    input  wire         serial_clk_i,
    input  wire         reset_clk_i, // connected to pixel_clk_i by scope_hdmi
    input  wire         rst_n_i,
    input  wire [23:0]  rgb_i,       // 24'hRRGGBB
    input  wire         de_i,
    input  wire         hs_i,
    input  wire         vs_i,
    output wire         tmds_clk_p_o,
    output wire         tmds_clk_n_o,
    output wire [2:0]   tmds_data_p_o,
    output wire [2:0]   tmds_data_n_o
);
    // rst_n_i includes the qualified lock of the shared pixel/serial PLL.
    // pixel_ready_q retains PCLK qualification after both PLL outputs run.
    // PCLK also clocks the PHY release chains; FCLK clocks only the dedicated
    // serializers.
    (* syn_preserve = 1 *) reg [1:0] pixel_ready_q;
    always @(posedge pixel_clk_i or negedge rst_n_i) begin
        if (!rst_n_i) pixel_ready_q <= 2'b00;
        else          pixel_ready_q <= {pixel_ready_q[0], 1'b1};
    end
    wire pixel_reset = !pixel_ready_q[1];
    wire [9:0] tmds_word [0:2];

    genvar lane;
    generate
        for (lane = 0; lane < 3; lane = lane + 1) begin : lanes
            // Lane 0 carries blue plus {VS,HS}; lanes 1/2 green/red.
            wire [1:0] control = (lane == 0) ? {vs_i, hs_i} : 2'b00;
            dvi_tx_tmds_enc u_encoder (
                .clock(pixel_clk_i),
                .reset(pixel_reset),
                .den(de_i),
                .data(rgb_i[lane*8 +: 8]),
                .ctrl(control),
                .tmds(tmds_word[lane])
            );
            spectrum_tmds_phy u_phy (
                .pixel_clk_i(pixel_clk_i),
                .serial_clk_i(serial_clk_i),
                .reset_clk_i(reset_clk_i),
                .reset_i(pixel_reset),
                .word_i(tmds_word[lane]),
                .data_p_o(tmds_data_p_o[lane]),
                .data_n_o(tmds_data_n_o[lane])
            );
        end
    endgenerate

    ELVDS_OBUF u_clock_buffer (
        .I(pixel_clk_i), .O(tmds_clk_p_o), .OB(tmds_clk_n_o)
    );
endmodule
