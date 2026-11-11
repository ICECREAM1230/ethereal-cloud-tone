`timescale 1ns/1ps

// TI SNOSCZ5B, register field tables, pp. 25-36.
// A word is {8-bit register pointer, 16-bit data, 16-bit verify mask}.
module fdc2214_config_rom #(
    parameter logic [15:0] RCOUNT_CH0 = 16'h1000,
    parameter logic [15:0] RCOUNT_CH1 = 16'h1000,
    parameter logic [15:0] SETTLECOUNT_CH0 = 16'h0100,
    parameter logic [15:0] SETTLECOUNT_CH1 = 16'h0100,
    parameter logic [15:0] CLOCK_DIVIDERS_CH0 = 16'h2001,
    parameter logic [15:0] CLOCK_DIVIDERS_CH1 = 16'h2001,
    parameter logic [4:0] IDRIVE_CH0 = 5'd15,
    parameter logic [4:0] IDRIVE_CH1 = 5'd15,
    parameter bit USE_EXTERNAL_CLOCK = 1'b1,
    parameter bit LOW_POWER_ACTIVATION = 1'b0
) (
    input  logic [3:0] index_i,
    output logic       valid_o,
    output logic [7:0] register_o,
    output logic [15:0] value_o,
    output logic [15:0] verify_mask_o
);
    // Reserved fields: CONFIG[12]=1, [10]=1, [8]=0, [5:0]=000001.
    localparam logic [15:0] ACTIVE_CONFIG = 16'h1401
        | (USE_EXTERNAL_CLOCK ? 16'h0200 : 16'h0000)
        | (LOW_POWER_ACTIVATION ? 16'h0800 : 16'h0000);

    always_comb begin
        valid_o = 1'b1;
        register_o = 8'h00;
        value_o = 16'h0000;
        verify_mask_o = 16'hffff;
        case (index_i)
            4'd0: begin
                register_o = 8'h1a;
                value_o = ACTIVE_CONFIG | 16'h2000; // Configure in sleep.
                verify_mask_o = 16'heac0;
            end
            4'd1: begin register_o = 8'h08; value_o = RCOUNT_CH0; end
            4'd2: begin register_o = 8'h09; value_o = RCOUNT_CH1; end
            4'd3: begin register_o = 8'h10; value_o = SETTLECOUNT_CH0; end
            4'd4: begin register_o = 8'h11; value_o = SETTLECOUNT_CH1; end
            4'd5: begin
                register_o = 8'h14;
                value_o = CLOCK_DIVIDERS_CH0;
                verify_mask_o = 16'h33ff;
            end
            4'd6: begin
                register_o = 8'h15;
                value_o = CLOCK_DIVIDERS_CH1;
                verify_mask_o = 16'h33ff;
            end
            4'd7: begin
                register_o = 8'h1e;
                value_o = {IDRIVE_CH0, 11'b0};
                verify_mask_o = 16'hf800;
            end
            4'd8: begin
                register_o = 8'h1f;
                value_o = {IDRIVE_CH1, 11'b0};
                verify_mask_o = 16'hf800;
            end
            4'd9: begin
                register_o = 8'h19;
                value_o = 16'h3821;
                verify_mask_o = 16'h3821;
            end
            4'd10: begin
                register_o = 8'h1b;
                value_o = 16'h820d; // CH0/CH1 scan, reserved=0x0208, 10MHz filter.
                verify_mask_o = 16'he007;
            end
            4'd11: begin
                register_o = 8'h1a;
                value_o = ACTIVE_CONFIG;
                verify_mask_o = 16'heac0;
            end
            default: valid_o = 1'b0;
        endcase
    end
endmodule
