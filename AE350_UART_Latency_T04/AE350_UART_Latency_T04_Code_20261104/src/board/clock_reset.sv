`default_nettype none
module clock_reset (
    input wire sys_clk,sys_rst_n,
    input wire control_source_clk_i,control_source_locked_i,
    output wire control_clk,control_rst_n,subtractive_clk,subtractive_rst_n,
    output wire fm_native_clk,fm_rst_n,audio_clk,audio_rst_n
);
    // Reuse the free-running USB48 PLL for receive/decode/central control.
    // Its generator is independent of this reset and of USB session reset.
    assign control_clk=control_source_clk_i;
    jt51_reset_sync u_control_host_reset(.clk_i(control_clk),
        .rst_n_i(sys_rst_n && control_source_locked_i),.rst_n_o(control_rst_n));
    wire subtractive_lock;
    // Preserve the official generated IP; the instance/clock now identify its role.
    Synth_PLL u_subtractive_pll(
        .clkin(sys_clk),.init_clk(sys_clk),.clkout0(subtractive_clk),.lock(subtractive_lock));
    // Loss asserts even if the PLL clock stops; release needs local edges.
    jt51_reset_sync u_subtractive_host_reset(.clk_i(subtractive_clk),
        .rst_n_i(sys_rst_n && subtractive_lock),.rst_n_o(subtractive_rst_n));
    wire fm_pll_lock;
    // Independent16MHz clock; sample cadence comes from the PCM scheduler.
    JT51_Request_PLL u_fm_pll(
        .clkin(sys_clk),.init_clk(sys_clk),.clkout0(fm_native_clk),.lock(fm_pll_lock));
    jt51_reset_sync u_fm_host_reset(.clk_i(fm_native_clk),
        .rst_n_i(sys_rst_n && subtractive_rst_n && fm_pll_lock),.rst_n_o(fm_rst_n));
    wire audio_lock;
    PCM_Audio_PLL u_audio_pll(.clkin(sys_clk),.init_clk(sys_clk),.clkout0(audio_clk),.lock(audio_lock));
    jt51_reset_sync u_audio_host_reset(.clk_i(audio_clk),
        .rst_n_i(sys_rst_n && subtractive_rst_n && audio_lock),.rst_n_o(audio_rst_n));
endmodule
`default_nettype wire
