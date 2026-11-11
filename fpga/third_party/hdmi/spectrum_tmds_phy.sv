// Adapted from Sipeed's hdmi_colorbar/dvi_tx_tmds_phy.v, Apache-2.0.
// OSER10 and ELVDS_OBUF are Gowin primitives, simulated with vendor models.
module spectrum_tmds_phy (
    input  wire        pixel_clk_i,
    input  wire        serial_clk_i,
    input  wire        reset_clk_i, // connected to PCLK by scope_hdmi
    input  wire        reset_i,    // active-high asynchronous request
    input  wire [9:0]  word_i,
    output wire        data_p_o,
    output wire        data_n_o
);
    // Keep FCLK on the dedicated serializers. Fabric release FFs share PCLK
    // with the encoder, restoring the previously routed clock connection.
    // Three falling edges retain the original mod-5 serializer word/bit phase
    // and asynchronous assertion at clock loss. OSER10 recovery/removal and
    // reset-chain timing still require official physical verification.
    (* async_reg = "true", syn_preserve = 1, syn_keep = 1 *) reg [2:0] reset_sync;
    always @(negedge reset_clk_i or posedge reset_i) begin
        if (reset_i) reset_sync <= 3'b111;
        else reset_sync <= {reset_sync[1:0], 1'b0};
    end
    wire serial_data;
    OSER10 u_serializer (
        .Q(serial_data),
        .D0(word_i[0]), .D1(word_i[1]), .D2(word_i[2]), .D3(word_i[3]),
        .D4(word_i[4]), .D5(word_i[5]), .D6(word_i[6]), .D7(word_i[7]),
        .D8(word_i[8]), .D9(word_i[9]),
        .PCLK(pixel_clk_i), .FCLK(serial_clk_i), .RESET(reset_sync[2])
    );
    ELVDS_OBUF u_data_buffer (
        .I(serial_data), .O(data_p_o), .OB(data_n_o)
    );
endmodule
