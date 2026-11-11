`timescale 1ns/1ps

// Register program adapted from Pololu VL53L0X Arduino library, commit
// 9f3773cb48d4e4e844d689cfc529a06f96d1d264. Copyright (c) 2017-2022
// Pololu Corporation; portions Copyright (c) 2016 STMicroelectronics.
// MIT and ST BSD 3-clause terms: licenses/vendor/vl53l0x/LICENSE.txt (FPGA root).
// The complete license notices are part of this source distribution.
module vl53l0x_init_rom (
    input  logic [7:0] index_i,
    output logic [3:0] operation_o,
    output logic [7:0] register_o,
    output logic [15:0] value_o,
    output logic [3:0] read_len_o,
    output logic [2:0] capture_o
);
    // 0=write8, 1=write16, 2=read, 3=temp|mask, 4=temp&mask,
    // 5=write stop variable, 6=SPAD poll, 7=calibration poll,
    // 8=filter SPAD map, 9=write map, A=configured, B=range poll,
    // C=jump; F=invalid instruction. Capture: 1=ID,2=temp,3=stop,
    // 4=SPAD info,5=SPAD map,6=result,7=publish after clear.
    always_comb begin
        operation_o = 4'hf;
        register_o = 8'h00;
        value_o = 16'h0000;
        read_len_o = 4'd0;
        capture_o = 3'd0;
        case (index_i)
            8'd0: begin operation_o=4'h2; register_o=8'hc0; value_o=16'h0000; read_len_o=4'd1; capture_o=3'd1; end // Model ID must be 0xEE
            8'd1: begin operation_o=4'h2; register_o=8'h89; value_o=16'h0000; read_len_o=4'd1; capture_o=3'd2; end
            8'd2: begin operation_o=4'h3; register_o=8'h89; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end
            8'd3: begin operation_o=4'h0; register_o=8'h88; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end
            8'd4: begin operation_o=4'h0; register_o=8'h80; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end
            8'd5: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end
            8'd6: begin operation_o=4'h0; register_o=8'h00; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end
            8'd7: begin operation_o=4'h2; register_o=8'h91; value_o=16'h0000; read_len_o=4'd1; capture_o=3'd3; end // Capture per-device stop variable
            8'd8: begin operation_o=4'h0; register_o=8'h00; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end
            8'd9: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end
            8'd10: begin operation_o=4'h0; register_o=8'h80; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end
            8'd11: begin operation_o=4'h2; register_o=8'h60; value_o=16'h0000; read_len_o=4'd1; capture_o=3'd2; end
            8'd12: begin operation_o=4'h3; register_o=8'h60; value_o=16'h0012; read_len_o=4'd0; capture_o=3'd0; end
            8'd13: begin operation_o=4'h1; register_o=8'h44; value_o=16'h0020; read_len_o=4'd0; capture_o=3'd0; end // Final signal rate limit 0.25 MCPS Q9.7
            8'd14: begin operation_o=4'h0; register_o=8'h01; value_o=16'h00ff; read_len_o=4'd0; capture_o=3'd0; end
            8'd15: begin operation_o=4'h0; register_o=8'h80; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end
            8'd16: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end
            8'd17: begin operation_o=4'h0; register_o=8'h00; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end
            8'd18: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0006; read_len_o=4'd0; capture_o=3'd0; end
            8'd19: begin operation_o=4'h2; register_o=8'h83; value_o=16'h0000; read_len_o=4'd1; capture_o=3'd2; end
            8'd20: begin operation_o=4'h3; register_o=8'h83; value_o=16'h0004; read_len_o=4'd0; capture_o=3'd0; end
            8'd21: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0007; read_len_o=4'd0; capture_o=3'd0; end
            8'd22: begin operation_o=4'h0; register_o=8'h81; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end
            8'd23: begin operation_o=4'h0; register_o=8'h80; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end
            8'd24: begin operation_o=4'h0; register_o=8'h94; value_o=16'h006b; read_len_o=4'd0; capture_o=3'd0; end
            8'd25: begin operation_o=4'h0; register_o=8'h83; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end
            8'd26: begin operation_o=4'h6; register_o=8'h83; value_o=16'h0000; read_len_o=4'd1; capture_o=3'd0; end // Bounded SPAD info readiness poll
            8'd27: begin operation_o=4'h0; register_o=8'h83; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end
            8'd28: begin operation_o=4'h2; register_o=8'h92; value_o=16'h0000; read_len_o=4'd1; capture_o=3'd4; end // SPAD count[6:0], aperture[7]
            8'd29: begin operation_o=4'h0; register_o=8'h81; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end
            8'd30: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0006; read_len_o=4'd0; capture_o=3'd0; end
            8'd31: begin operation_o=4'h2; register_o=8'h83; value_o=16'h0000; read_len_o=4'd1; capture_o=3'd2; end
            8'd32: begin operation_o=4'h4; register_o=8'h83; value_o=16'h00fb; read_len_o=4'd0; capture_o=3'd0; end
            8'd33: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end
            8'd34: begin operation_o=4'h0; register_o=8'h00; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end
            8'd35: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end
            8'd36: begin operation_o=4'h0; register_o=8'h80; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end
            8'd37: begin operation_o=4'h2; register_o=8'hb0; value_o=16'h0000; read_len_o=4'd6; capture_o=3'd5; end // Factory good-SPAD map
            8'd38: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end
            8'd39: begin operation_o=4'h0; register_o=8'h4f; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end
            8'd40: begin operation_o=4'h0; register_o=8'h4e; value_o=16'h002c; read_len_o=4'd0; capture_o=3'd0; end
            8'd41: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end
            8'd42: begin operation_o=4'h0; register_o=8'hb6; value_o=16'h00b4; read_len_o=4'd0; capture_o=3'd0; end
            8'd43: begin operation_o=4'h8; register_o=8'h00; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Filter factory map, retain first count allowed good SPADs
            8'd44: begin operation_o=4'h9; register_o=8'hb0; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Six SPAD bytes in one burst
            8'd45: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd46: begin operation_o=4'h0; register_o=8'h00; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd47: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd48: begin operation_o=4'h0; register_o=8'h09; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd49: begin operation_o=4'h0; register_o=8'h10; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd50: begin operation_o=4'h0; register_o=8'h11; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd51: begin operation_o=4'h0; register_o=8'h24; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd52: begin operation_o=4'h0; register_o=8'h25; value_o=16'h00ff; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd53: begin operation_o=4'h0; register_o=8'h75; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd54: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd55: begin operation_o=4'h0; register_o=8'h4e; value_o=16'h002c; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd56: begin operation_o=4'h0; register_o=8'h48; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd57: begin operation_o=4'h0; register_o=8'h30; value_o=16'h0020; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd58: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd59: begin operation_o=4'h0; register_o=8'h30; value_o=16'h0009; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd60: begin operation_o=4'h0; register_o=8'h54; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd61: begin operation_o=4'h0; register_o=8'h31; value_o=16'h0004; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd62: begin operation_o=4'h0; register_o=8'h32; value_o=16'h0003; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd63: begin operation_o=4'h0; register_o=8'h40; value_o=16'h0083; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd64: begin operation_o=4'h0; register_o=8'h46; value_o=16'h0025; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd65: begin operation_o=4'h0; register_o=8'h60; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd66: begin operation_o=4'h0; register_o=8'h27; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd67: begin operation_o=4'h0; register_o=8'h50; value_o=16'h0006; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd68: begin operation_o=4'h0; register_o=8'h51; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd69: begin operation_o=4'h0; register_o=8'h52; value_o=16'h0096; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd70: begin operation_o=4'h0; register_o=8'h56; value_o=16'h0008; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd71: begin operation_o=4'h0; register_o=8'h57; value_o=16'h0030; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd72: begin operation_o=4'h0; register_o=8'h61; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd73: begin operation_o=4'h0; register_o=8'h62; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd74: begin operation_o=4'h0; register_o=8'h64; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd75: begin operation_o=4'h0; register_o=8'h65; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd76: begin operation_o=4'h0; register_o=8'h66; value_o=16'h00a0; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd77: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd78: begin operation_o=4'h0; register_o=8'h22; value_o=16'h0032; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd79: begin operation_o=4'h0; register_o=8'h47; value_o=16'h0014; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd80: begin operation_o=4'h0; register_o=8'h49; value_o=16'h00ff; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd81: begin operation_o=4'h0; register_o=8'h4a; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd82: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd83: begin operation_o=4'h0; register_o=8'h7a; value_o=16'h000a; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd84: begin operation_o=4'h0; register_o=8'h7b; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd85: begin operation_o=4'h0; register_o=8'h78; value_o=16'h0021; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd86: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd87: begin operation_o=4'h0; register_o=8'h23; value_o=16'h0034; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd88: begin operation_o=4'h0; register_o=8'h42; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd89: begin operation_o=4'h0; register_o=8'h44; value_o=16'h00ff; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd90: begin operation_o=4'h0; register_o=8'h45; value_o=16'h0026; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd91: begin operation_o=4'h0; register_o=8'h46; value_o=16'h0005; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd92: begin operation_o=4'h0; register_o=8'h40; value_o=16'h0040; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd93: begin operation_o=4'h0; register_o=8'h0e; value_o=16'h0006; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd94: begin operation_o=4'h0; register_o=8'h20; value_o=16'h001a; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd95: begin operation_o=4'h0; register_o=8'h43; value_o=16'h0040; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd96: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd97: begin operation_o=4'h0; register_o=8'h34; value_o=16'h0003; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd98: begin operation_o=4'h0; register_o=8'h35; value_o=16'h0044; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd99: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd100: begin operation_o=4'h0; register_o=8'h31; value_o=16'h0004; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd101: begin operation_o=4'h0; register_o=8'h4b; value_o=16'h0009; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd102: begin operation_o=4'h0; register_o=8'h4c; value_o=16'h0005; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd103: begin operation_o=4'h0; register_o=8'h4d; value_o=16'h0004; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd104: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd105: begin operation_o=4'h0; register_o=8'h44; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd106: begin operation_o=4'h0; register_o=8'h45; value_o=16'h0020; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd107: begin operation_o=4'h0; register_o=8'h47; value_o=16'h0008; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd108: begin operation_o=4'h0; register_o=8'h48; value_o=16'h0028; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd109: begin operation_o=4'h0; register_o=8'h67; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd110: begin operation_o=4'h0; register_o=8'h70; value_o=16'h0004; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd111: begin operation_o=4'h0; register_o=8'h71; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd112: begin operation_o=4'h0; register_o=8'h72; value_o=16'h00fe; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd113: begin operation_o=4'h0; register_o=8'h76; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd114: begin operation_o=4'h0; register_o=8'h77; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd115: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd116: begin operation_o=4'h0; register_o=8'h0d; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd117: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd118: begin operation_o=4'h0; register_o=8'h80; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd119: begin operation_o=4'h0; register_o=8'h01; value_o=16'h00f8; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd120: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd121: begin operation_o=4'h0; register_o=8'h8e; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd122: begin operation_o=4'h0; register_o=8'h00; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd123: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd124: begin operation_o=4'h0; register_o=8'h80; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Pololu complete default tuning
            8'd125: begin operation_o=4'h0; register_o=8'h0a; value_o=16'h0004; read_len_o=4'd0; capture_o=3'd0; end
            8'd126: begin operation_o=4'h2; register_o=8'h84; value_o=16'h0000; read_len_o=4'd1; capture_o=3'd2; end
            8'd127: begin operation_o=4'h4; register_o=8'h84; value_o=16'h00ef; read_len_o=4'd0; capture_o=3'd0; end
            8'd128: begin operation_o=4'h0; register_o=8'h0b; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end
            8'd129: begin operation_o=4'h0; register_o=8'h01; value_o=16'h00e8; read_len_o=4'd0; capture_o=3'd0; end
            8'd130: begin operation_o=4'h1; register_o=8'h71; value_o=16'h028a; read_len_o=4'd0; capture_o=3'd0; end // 33 ms profile, pinned C formulas; see sources doc
            8'd131: begin operation_o=4'h0; register_o=8'h01; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end
            8'd132: begin operation_o=4'h0; register_o=8'h00; value_o=16'h0041; read_len_o=4'd0; capture_o=3'd0; end
            8'd133: begin operation_o=4'h7; register_o=8'h13; value_o=16'h0000; read_len_o=4'd1; capture_o=3'd0; end // VHV reference calibration
            8'd134: begin operation_o=4'h0; register_o=8'h0b; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end
            8'd135: begin operation_o=4'h0; register_o=8'h00; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end
            8'd136: begin operation_o=4'h0; register_o=8'h01; value_o=16'h0002; read_len_o=4'd0; capture_o=3'd0; end
            8'd137: begin operation_o=4'h0; register_o=8'h00; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end
            8'd138: begin operation_o=4'h7; register_o=8'h13; value_o=16'h0000; read_len_o=4'd1; capture_o=3'd0; end // Phase reference calibration
            8'd139: begin operation_o=4'h0; register_o=8'h0b; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end
            8'd140: begin operation_o=4'h0; register_o=8'h00; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end
            8'd141: begin operation_o=4'h0; register_o=8'h01; value_o=16'h00e8; read_len_o=4'd0; capture_o=3'd0; end
            8'd142: begin operation_o=4'h0; register_o=8'h80; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end
            8'd143: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end
            8'd144: begin operation_o=4'h0; register_o=8'h00; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end
            8'd145: begin operation_o=4'h5; register_o=8'h91; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Restore this sensor stop variable
            8'd146: begin operation_o=4'h0; register_o=8'h00; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd0; end
            8'd147: begin operation_o=4'h0; register_o=8'hff; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end
            8'd148: begin operation_o=4'h0; register_o=8'h80; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end
            8'd149: begin operation_o=4'h0; register_o=8'h00; value_o=16'h0002; read_len_o=4'd0; capture_o=3'd0; end
            8'd150: begin operation_o=4'ha; register_o=8'h00; value_o=16'h0000; read_len_o=4'd0; capture_o=3'd0; end // Configured only after both calibrations and continuous start
            8'd151: begin operation_o=4'hb; register_o=8'h13; value_o=16'h0000; read_len_o=4'd1; capture_o=3'd0; end // Poll independently of GPIO1
            8'd152: begin operation_o=4'h2; register_o=8'h14; value_o=16'h0000; read_len_o=4'd12; capture_o=3'd6; end // Raw status and big-endian millimetres at offsets 10,11
            8'd153: begin operation_o=4'h0; register_o=8'h0b; value_o=16'h0001; read_len_o=4'd0; capture_o=3'd7; end // Publish sample only after clear acknowledged
            8'd154: begin operation_o=4'hc; register_o=8'h00; value_o=16'h0097; read_len_o=4'd0; capture_o=3'd0; end // Next continuous result
            default: begin end
        endcase
    end
endmodule
