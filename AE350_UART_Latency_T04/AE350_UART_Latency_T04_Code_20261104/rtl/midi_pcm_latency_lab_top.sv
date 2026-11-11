`default_nettype none
// Standalone latency build: identical physical pads, opt-in measurement.
module midi_pcm_latency_lab_top #(
    // This standalone lab enables the test; reusable control_lab_top defaults off.
    parameter bit ENABLE_MIDI_NOTE20_TEST=1'b1
) (
    input wire sys_clk,
    input wire [2:0] keys_n,
    input wire ESP_SPI_SCLK,ESP_SPI_CS_N,ESP_SPI_MOSI,
    output wire ESP_SPI_MISO,
    input wire [3:0] ESP_PMOD1_PASSIVE,
    inout wire USB_MIDI_DP,USB_MIDI_DM,
    output wire HP_BCK,HP_WS,HP_DIN,PCM_BCK,PCM_WS,PCM_DIN,PA_EN,
    output wire MIDI_DEBUG_TX,
    output wire MIDI_STATUS_LED,
    output wire tmds_clk_p,tmds_clk_n,
    output wire [2:0] tmds_data_p,tmds_data_n

);
    control_lab_top #(.ENABLE_MIDI_PCM_LATENCY_TEST(1'b0),.ENABLE_MIDI_NOTE20_TEST(ENABLE_MIDI_NOTE20_TEST),.DEBUG_UART_BAUD(200000)) u_lab(
        .sys_clk(sys_clk),
        .keys_n(keys_n),
        .ESP_SPI_SCLK(ESP_SPI_SCLK),
        .ESP_SPI_CS_N(ESP_SPI_CS_N),
        .ESP_SPI_MOSI(ESP_SPI_MOSI),
        .ESP_SPI_MISO(ESP_SPI_MISO),
        .ESP_PMOD1_PASSIVE(ESP_PMOD1_PASSIVE),
        .USB_MIDI_DP(USB_MIDI_DP),
        .USB_MIDI_DM(USB_MIDI_DM),
        .HP_BCK(HP_BCK),
        .HP_WS(HP_WS),
        .HP_DIN(HP_DIN),
        .PCM_BCK(PCM_BCK),
        .PCM_WS(PCM_WS),
        .PCM_DIN(PCM_DIN),
        .PA_EN(PA_EN),
        .MIDI_DEBUG_TX(MIDI_DEBUG_TX),
        .MIDI_STATUS_LED(MIDI_STATUS_LED),
        .tmds_clk_p(tmds_clk_p),
        .tmds_clk_n(tmds_clk_n),
        .tmds_data_p(tmds_data_p),
        .tmds_data_n(tmds_data_n));
endmodule
`default_nettype wire
