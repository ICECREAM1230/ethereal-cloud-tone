// Copyright (c) 2026 ICECREAM1230
// SPDX-License-Identifier: MIT
// Independent partial state and mix, consuming the shared DDS service.
// Ratios UQ8.16; gains/envelopes/pan UQ0.16; phase modulo 2^24.
`default_nettype none
module additive_renderer #(
    parameter int VOICES=8, PARTIALS=32, START_SLOT=320,
    parameter int DDS_LATENCY=11
) (
    input wire clk,rst_n,enable,sample_tick,
    input wire [9:0] slot,
    input wire [VOICES*24-1:0] voice_steps,
    input wire [VOICES*4*16-1:0] voice_envelopes,
    input wire [VOICES*16-1:0] voice_pan,
    input wire [VOICES*8-1:0] voice_generation,
    input wire [VOICES-1:0] voice_active,
    input wire [PARTIALS*24-1:0] ratios,
    input wire [PARTIALS*2-1:0] groups,
    input wire [PARTIALS*16-1:0] gains,
    input wire [15:0] master_gain,master_pan,
    output wire add_valid,
    output wire [23:0] add_phase,add_step,
    input wire add_ready,add_out_valid,
    input wire [23:0] add_new_phase,
    input wire signed [17:0] add_sample,
    output logic signed [23:0] pcm_left,pcm_right,
    output logic pcm_valid
);
    localparam int TOTAL=VOICES*PARTIALS;
    localparam int IW=$clog2(TOTAL), PW=$clog2(PARTIALS), VW=$clog2(VOICES);
    wire issue=enable && slot>=10'(START_SLOT) && slot<10'(START_SLOT+TOTAL);
    wire [IW-1:0] read_index=IW'(slot-10'(START_SLOT));
    wire [PW-1:0] read_partial=read_index[PW-1:0];
    wire [1:0] read_group=groups[read_partial*2+:2];
    // Decode the schedule/partial group before selecting voice parameters.
    // This separates the group mux from the envelope mux feeding the DSP.
    logic read_valid_q;
    logic [IW-1:0] read_index_q;
    logic [1:0] read_group_q;
    wire [PW-1:0] partial_q=read_index_q[PW-1:0];
    wire [VW-1:0] voice_q=read_index_q[IW-1:PW];
    reg [23:0] phases[0:TOTAL-1];
    reg [7:0] generations[0:TOTAL-1];
    logic [TOTAL-1:0] phase_initialized;
    logic f1_valid,f2_valid,f3_valid;
    logic [IW-1:0] f1_index,f2_index,f3_index;
    logic [23:0] f1_phase,f1_base,f1_ratio,f2_phase,f3_phase,f3_step;
    logic [7:0] f1_generation,f2_generation,f3_generation;
    // Keep the 32-way gain mux register outside the fixed DSP site, so the
    // parameter selector and multiplier can be placed independently.
    logic [15:0] f1_gain /* synthesis syn_preserve = 1 */;
    logic [15:0] f1_env,f2_pan,f3_pan;
    logic [15:0] f1_pan,f1_master_pan;
    logic f1_active;
    logic [47:0] f2_frequency;
    logic [31:0] f2_amplitude;
    // Keep six extra fractional bits through envelope, band gain and pan.
    // Weak partials must not be reduced to 16-bit gains before multiplying sine.
    logic [37:0] f3_amplitude;
    wire [31:0] frequency=f2_frequency[47:16];
    wire [15:0] band_gain=frequency>=32'h800000 ? 16'd0 :
        frequency<=32'h700000 ? 16'hffff : 16'((32'h800000-frequency)>>4);
    wire [16:0] pan_sum={1'b0,f1_pan}+{1'b0,f1_master_pan};
    wire [15:0] resolved_pan=pan_sum<17'd32768 ? 16'd0 :
        pan_sum>17'd98303 ? 16'hffff : 16'(pan_sum-17'd32768);
    assign add_valid=f3_valid && enable;
    assign add_phase=f3_phase;
    assign add_step=f3_step;

    logic [DDS_LATENCY-1:0] tag_valid;
    logic [IW-1:0] tag_index[0:DDS_LATENCY-1];
    logic [7:0] tag_generation[0:DDS_LATENCY-1];
    logic [21:0] tag_gain[0:DDS_LATENCY-1];
    logic [15:0] tag_pan[0:DDS_LATENCY-1];
    wire write_phase=enable && add_out_valid && tag_valid[DDS_LATENCY-1];
    logic phase_run_q,phase_write_valid_q;
    logic [IW-1:0] phase_write_index_q;
    logic [23:0] phase_write_data_q;
    logic [7:0] phase_write_generation_q;
    // No resets on RAM data/read ports: preserve inferred synchronous memory.
    // Each partial is read once per frame. Register the response writeback so
    // its RAM address/enable and initialization bit use only local FF outputs.
    always_ff @(posedge clk)begin
        phase_write_index_q<=tag_index[DDS_LATENCY-1];
        phase_write_data_q<=add_new_phase;
        phase_write_generation_q<=tag_generation[DDS_LATENCY-1];
        f1_phase <= (phase_initialized[read_index_q] &&
            generations[read_index_q]==voice_generation[voice_q*8+:8]) ? phases[read_index_q] : 24'd0;
        if(phase_write_valid_q)begin
            phases[phase_write_index_q]<=phase_write_data_q;
            generations[phase_write_index_q]<=phase_write_generation_q;
        end
    end
    always_ff @(posedge clk or negedge rst_n)begin
        if(!rst_n)begin
            phase_initialized<='0;
            phase_run_q<=0;phase_write_valid_q<=0;
            read_valid_q<=0;read_index_q<='0;read_group_q<='0;
            f1_valid<=0;f2_valid<=0;f3_valid<=0;tag_valid<='0;
            f1_index<='0;f2_index<='0;f3_index<='0;
            f1_base<=0;f1_ratio<=0;f2_phase<=0;f3_phase<=0;f3_step<=0;
            f1_generation<=0;f2_generation<=0;f3_generation<=0;
            f1_gain<=0;f1_env<=0;f1_active<=0;f1_pan<=0;f1_master_pan<=0;
            f2_frequency<=0;f2_amplitude<=0;f3_amplitude<=0;f2_pan<=0;f3_pan<=0;
            for(int i=0;i<DDS_LATENCY;i++)begin
                tag_index[i]<='0;tag_generation[i]<=0;tag_gain[i]<=0;tag_pan[i]<=0;
            end
        end else begin
            phase_run_q<=enable;phase_write_valid_q<=write_phase;
            // Disable still suppresses requests/mix immediately. The bitmap
            // clears one edge later, before the next valid parameter read:
            // read_valid_q was cleared on the original disable edge.
            if(!phase_run_q)phase_initialized<='0;
            else if(phase_write_valid_q)phase_initialized[phase_write_index_q]<=1'b1;
            read_valid_q<=issue;read_index_q<=read_index;read_group_q<=read_group;
            f1_valid<=read_valid_q && enable;f2_valid<=f1_valid && enable;f3_valid<=f2_valid && enable;
            f1_index<=read_index_q;f2_index<=f1_index;f3_index<=f2_index;
            f1_base<=voice_steps[voice_q*24+:24];f1_ratio<=ratios[partial_q*24+:24];
            f1_gain<=gains[partial_q*16+:16];
            f1_env<=voice_envelopes[(voice_q*4+read_group_q)*16+:16];
            f1_active<=voice_active[voice_q];
            f1_pan<=voice_pan[voice_q*16+:16];f1_master_pan<=master_pan;
            f1_generation<=voice_generation[voice_q*8+:8];
            f2_generation<=f1_generation;f3_generation<=f2_generation;
            f2_phase<=f1_phase;f3_phase<=f2_phase;
            f2_frequency<=f1_base*f1_ratio;
            f2_amplitude<=f1_active ? f1_gain*f1_env : 32'd0;
            f2_pan<=resolved_pan;f3_pan<=f2_pan;
            f3_step<=frequency>=32'h800000 ? 24'd0 : frequency[23:0];
            f3_amplitude<=f2_amplitude[31:10]*band_gain;
            tag_valid<={tag_valid[DDS_LATENCY-2:0],add_valid && add_ready};
            tag_index[0]<=f3_index;tag_generation[0]<=f3_generation;
            tag_gain[0]<=f3_amplitude[37:16];tag_pan[0]<=f3_pan;
            for(int i=1;i<DDS_LATENCY;i++)begin
                tag_index[i]<=tag_index[i-1];tag_generation[i]<=tag_generation[i-1];
                tag_gain[i]<=tag_gain[i-1];tag_pan[i]<=tag_pan[i-1];
            end
        end
    end

    logic m1_valid,m2_valid;
    logic signed [40:0] m1_product,m2_left,m2_right;
    logic [15:0] m1_pan;
    wire signed [23:0] mono=24'(m1_product>>>16);
    logic signed [31:0] sum_left,sum_right,captured_left,captured_right;
    logic [15:0] captured_gain;
    logic signed [48:0] output_left,output_right;
    logic capture_valid,scale_valid;
    function automatic signed [23:0] saturate(input signed [48:0] product);
        logic signed [48:0] scaled;
        begin
            // Undo the six retained fractional bits only at final PCM output.
            scaled=product>>>14; // Original scale >>>8 plus six guard bits.
            saturate=scaled>49'sd8388607 ? 24'sh7fffff :
                scaled < -49'sd8388608 ? 24'sh800000 : scaled[23:0];
        end
    endfunction
    always_ff @(posedge clk or negedge rst_n)begin
        if(!rst_n)begin
            m1_valid<=0;m2_valid<=0;m1_product<=0;m2_left<=0;m2_right<=0;m1_pan<=0;
            sum_left<=0;sum_right<=0;captured_left<=0;captured_right<=0;captured_gain<=0;
            output_left<=0;output_right<=0;capture_valid<=0;scale_valid<=0;
            pcm_valid<=0;pcm_left<=0;pcm_right<=0;
        end else begin
            m1_valid<=write_phase;m2_valid<=m1_valid && enable;
            m1_product<=$signed(add_sample)*$signed({1'b0,tag_gain[DDS_LATENCY-1]});
            m1_pan<=tag_pan[DDS_LATENCY-1];
            m2_left<=mono*$signed({1'b0,(16'hffff-m1_pan)});
            m2_right<=mono*$signed({1'b0,m1_pan});
            if(!enable || sample_tick)begin sum_left<=0;sum_right<=0;end
            else if(m2_valid)begin
                sum_left<=sum_left+32'(m2_left>>>16);
                sum_right<=sum_right+32'(m2_right>>>16);
            end
            capture_valid<=sample_tick && enable;scale_valid<=capture_valid;pcm_valid<=scale_valid;
            if(sample_tick)begin captured_left<=sum_left;captured_right<=sum_right;captured_gain<=master_gain;end
            output_left<=captured_left*$signed({1'b0,captured_gain});
            output_right<=captured_right*$signed({1'b0,captured_gain});
            if(!enable)begin pcm_left<=0;pcm_right<=0;end
            else if(scale_valid)begin pcm_left<=saturate(output_left);pcm_right<=saturate(output_right);end
        end
    end
`ifndef SYNTHESIS
    initial begin
        assert(VOICES>=2 && PARTIALS>=2 && (PARTIALS&(PARTIALS-1))==0 && (VOICES&(VOICES-1))==0)
            else $fatal(1,"additive renderer sizes must be powers of two >=2");
        assert(START_SLOT+TOTAL+DDS_LATENCY+9<768)
            else $fatal(1,"additive partials do not drain before sample boundary");
        assert(TOTAL<=256)
            else $fatal(1,"six-bit fractional renderer requires TOTAL<=256 for 32-bit accumulation");
    end
    always @(posedge clk)if(rst_n && enable)begin
        assert(!add_valid || add_ready)else $fatal(1,"reserved additive DDS window was not available");
        assert(add_out_valid==tag_valid[DDS_LATENCY-1])else $fatal(1,"additive DDS response/tag mismatch");
        assert(!(sample_tick && (|tag_valid || read_valid_q || f1_valid || f2_valid || f3_valid || m1_valid || m2_valid)))
            else $fatal(1,"additive frame published before pipeline drained");
    end
`endif
endmodule
`default_nettype wire
