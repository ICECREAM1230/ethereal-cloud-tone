// SPDX-License-Identifier: GPL-3.0-or-later
// Native chip policy; wire layout and mappings remain generated from the schema.
package jt51_adapter_pkg;
  import fm_contract_pkg::*;
  localparam logic [31:0] JT51_PITCH_MIN = 32'd851968; // MIDI13 Q16
  localparam logic [31:0] JT51_PITCH_MAX = 32'd7142400; // MIDI108 + 63/64
  function automatic logic jt51_pitch_valid(input logic [31:0] pitch);
    return pitch >= JT51_PITCH_MIN && pitch <= JT51_PITCH_MAX;
  endfunction
  function automatic logic [13:0] jt51_pitch_codes(input logic [31:0] pitch);
    logic [31:0] grid, semitone, octave, note;
    logic [3:0] code;
    grid=(pitch-JT51_PITCH_MIN+32'd512)>>10;
    semitone=grid>>6; octave=semitone/12; note=semitone%12;
    case(note)
      0:code=0;1:code=1;2:code=2;3:code=4;4:code=5;5:code=6;
      6:code=8;7:code=9;8:code=10;9:code=12;10:code=13;default:code=14;
    endcase
    return {1'b0,octave[2:0],code,grid[5:0]}; // [13:6] KC, [5:0] KF
  endfunction
  function automatic logic [7:0] jt51_pan_bits(input logic [6:0] pan);
    return pan < 7'd32 ? 8'h40 : pan < 7'd96 ? 8'hc0 : 8'h80;
  endfunction
  // Eight TL codes halve operator amplitude in JT51 (TL << 3 in EG,
  // then four fractional log bits in OP). Round -8*log2(level/127) to
  // keep MIDI velocity and channel volume approximately linear in amplitude.
  // This exact 7-bit lookup avoids real/log arithmetic in synthesized logic.
  function automatic logic [6:0] jt51_gain_attenuation(input logic [6:0] level);
    case(level)
      7'd0:return 7'd127;
      7'd1:return 7'd56;
      7'd2:return 7'd48;
      7'd3:return 7'd43;
      7'd4:return 7'd40;
      7'd5:return 7'd37;
      7'd6:return 7'd35;
      7'd7:return 7'd33;
      7'd8:return 7'd32;
      7'd9:return 7'd31;
      7'd10:return 7'd29;
      7'd11:return 7'd28;
      7'd12:return 7'd27;
      7'd13:return 7'd26;
      7'd14,7'd15:return 7'd25;
      7'd16:return 7'd24;
      7'd17,7'd18:return 7'd23;
      7'd19:return 7'd22;
      7'd20,7'd21:return 7'd21;
      7'd22,7'd23:return 7'd20;
      7'd24,7'd25:return 7'd19;
      7'd26,7'd27:return 7'd18;
      7'd28,7'd29,7'd30:return 7'd17;
      7'd31,7'd32,7'd33:return 7'd16;
      7'd34,7'd35,7'd36:return 7'd15;
      7'd37,7'd38,7'd39:return 7'd14;
      7'd40,7'd41,7'd42:return 7'd13;
      7'd43,7'd44,7'd45,7'd46:return 7'd12;
      7'd47,7'd48,7'd49,7'd50,7'd51:return 7'd11;
      7'd52,7'd53,7'd54,7'd55:return 7'd10;
      7'd56,7'd57,7'd58,7'd59,7'd60:return 7'd9;
      7'd61,7'd62,7'd63,7'd64,7'd65,7'd66:return 7'd8;
      7'd67,7'd68,7'd69,7'd70,7'd71,7'd72:return 7'd7;
      7'd73,7'd74,7'd75,7'd76,7'd77,7'd78:return 7'd6;
      7'd79,7'd80,7'd81,7'd82,7'd83,7'd84,7'd85:return 7'd5;
      7'd86,7'd87,7'd88,7'd89,7'd90,7'd91,7'd92,7'd93:return 7'd4;
      7'd94,7'd95,7'd96,7'd97,7'd98,7'd99,7'd100,7'd101,7'd102:return 7'd3;
      7'd103,7'd104,7'd105,7'd106,7'd107,7'd108,7'd109,7'd110,7'd111:return 7'd2;
      7'd112,7'd113,7'd114,7'd115,7'd116,7'd117,7'd118,7'd119,7'd120,7'd121:return 7'd1;
      7'd122,7'd123,7'd124,7'd125,7'd126,7'd127:return 7'd0;
      default:return 7'd127;
    endcase
  endfunction
  function automatic logic [6:0] jt51_effective_tl(input logic [6:0] base,
      input logic [6:0] velocity,input logic [6:0] volume);
    logic [8:0] sum;
    sum={2'b0,base}+{2'b0,jt51_gain_attenuation(velocity)}+
      {2'b0,jt51_gain_attenuation(volume)};
    return velocity==0 || volume==0 || sum>127 ? 7'd127 : sum[6:0];
  endfunction
  // Logical order is M1,C1,M2,C2; native register slots are 0,2,1,3.
  // These masks follow jt51_acc.sum_en with op_XVII two operator slots
  // behind the current input. Changing modulator TL would change FM timbre.
  function automatic logic [3:0] jt51_carrier_mask(input logic [2:0] algorithm);
    case(algorithm)
      3'd0,3'd1,3'd2,3'd3:return 4'b1000;
      3'd4:return 4'b1010;
      3'd5,3'd6:return 4'b1110;
      3'd7:return 4'b1111;
      default:return 4'b0000;
    endcase
  endfunction
  function automatic logic [6:0] jt51_operator_tl(input logic [6:0] base,
      input logic [6:0] velocity,input logic [6:0] volume,
      input logic [2:0] algorithm,input logic [1:0] logical_operator);
    logic [3:0] carriers;
    carriers=jt51_carrier_mask(algorithm);
    if(velocity==0 || volume==0)return 7'd127;
    return carriers[logical_operator] ? jt51_effective_tl(base,velocity,volume) : base;
  endfunction
  function automatic logic [7:0] jt51_key_bits(input logic [3:0] logical_mask);
    logic [7:0] result;
    result=0;
    for(integer op=0;op<4;op=op+1)
      if(logical_mask[op]) result=result | (8'd1 << fm_profile_native_key_on_bit(1,op));
    return result;
  endfunction
  function automatic logic jt51_lifecycle(input fm_command_t c);
    fm_preset_info_t p;
    p=fm_preset_info(1,c.value_u32);
    return (c.opcode_id==FM_SPI_OPCODE_SELECT_BACKEND && c.scope_id==FM_SCOPE_ENGINE &&
      c.target_word==32'hffffffff && c.value_u32<=FM_BACKEND_JT51_OPM) ||
      (c.opcode_id==FM_SPI_OPCODE_APPLY_PRESET && c.scope_id==FM_SCOPE_PATCH &&
      c.target_word==32'hffffffff && p.valid);
  endfunction
endpackage
