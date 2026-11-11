// Copyright (c) 2026 ICECREAM1230
// SPDX-License-Identifier: MIT
// DDS runtime owner. Static templates are compiled by the controller; this
// module owns only keystrokes, source/channel expression and release lifetime.
`default_nettype none
module midi_dds_performance #(
    parameter bit REGISTERED_EVENT_INPUT = 1'b0,
    parameter bit LOCAL_MIDI_BOOT = 1'b0
) (
    input wire clk, rst_n, sclk, enable,
    input wire [9:0] slot,
    input wire [15:0] control_pass_count,
    input wire [7:0] read_index,
    input wire perf_write_enable,
    input wire [15:0] perf_write_addr,
    input wire [31:0] perf_write_data,
    input midi_pkg::midi_event_t midi_event,
    input wire midi_event_valid,
    input wire midi_capture_pending,
    output wire midi_event_ready,
    input wire [3:0] midi_flush,
    output logic [31:0] template_status,
    output logic host_template_valid_o,
    output element_types_pkg::element_config_t runtime_config,
    output logic signed [17:0] runtime_bend /* synthesis syn_preserve=1 */,
    output logic signed [19:0] runtime_cutoff_bend,
    output logic [7:0] runtime_keytrack /* synthesis syn_preserve=1 */,
    output logic [7:0] runtime_wheel_gain /* synthesis syn_preserve=1 */,
    output logic signed [6:0] runtime_touch_x /* synthesis syn_preserve=1 */,
    output logic signed [6:0] runtime_touch_y /* synthesis syn_preserve=1 */,
    output logic signed [15:0] runtime_touch_cutoff_depth /* synthesis syn_preserve=1 */,
    output logic signed [15:0] runtime_touch_resonance_depth /* synthesis syn_preserve=1 */,
    output wire signed [15:0] wheel_vibrato_depth,
    output wire [31:0] voice_gate,
    output wire [575:0] amp_depth, mod_depth,
    input wire [31:0] tail_done
);
    localparam [1:0] V_IDLE=0,V_WAIT=1,V_HELD=2,V_RELEASE=3;
    // Bundled-data mailbox: all shadow words stay stable until COMMIT ACK.
    logic [31:0] shadow [0:63];
    logic [7:0] keytrack_shadow;
    logic [6:0] bendrange_shadow;
    logic [31:0] commit_shadow;
    logic request_spi,ack_sys;
    (* async_reg="true" *) logic ack_meta,ack_sync,req_meta,req_sync;
    (* async_reg="true" *) logic bank_meta,bank_sync;
    logic depth_bank;
    wire sender_busy=request_spi!=ack_sync;
    wire template_write=perf_write_enable && !sender_busy;
    wire amp_write=template_write && perf_write_addr[15:7]==9'h102;
    wire mod_write=template_write && perf_write_addr[15:7]==9'h103;
    logic [31:0] active_template [0:63];
    logic [7:0] keytrack;
    logic [6:0] bendrange;
    logic template_valid;
    // A usable local profile is distinct from a controller COMMIT ACK:
    // status stays zero until an actual SPI template has been accepted.
    function automatic [31:0] boot_template(input integer word_index);
        case(word_index)
            2,10: boot_template=32'h00000200;
            3,11: boot_template=32'h0001d7d7;
            4: boot_template=32'h00000100; // saw, enabled
            8: boot_template=32'hfffffc00; // second oscillator, one octave down
            12: boot_template=32'h00000103; // sine, enabled
            default: boot_template=0;
        endcase
    endfunction
    // The same velocity curve as midi_templates.c, evaluated only when
    // building this constant ROM; no run-time divider is introduced.
    reg [17:0] boot_velocity [0:127];
    logic [17:0] boot_velocity_q;
    for(genvar bv=0;bv<128;bv=bv+1)begin:g_boot_velocity
        initial boot_velocity[bv]=18'((10240*(65536-64*(65536-bv*65536/127)/127))>>16);
    end
    always_ff @(posedge sclk or negedge rst_n) begin
        if(!rst_n) begin
            request_spi<=0;ack_meta<=0;ack_sync<=0;bank_meta<=0;bank_sync<=0;
            keytrack_shadow<=64;bendrange_shadow<=2;commit_shadow<=0;
            for(integer i=0;i<64;i=i+1)shadow[i]<=0;
        end else begin
            ack_meta<=ack_sys;ack_sync<=ack_meta;bank_meta<=depth_bank;bank_sync<=bank_meta;
            if(template_write) begin
                if(perf_write_addr>=16'h8000 && perf_write_addr<16'h8040)
                    shadow[perf_write_addr[5:0]]<=perf_write_data;
                if(perf_write_addr==16'h8040)keytrack_shadow<=perf_write_data[7:0];
                if(perf_write_addr==16'h8041)bendrange_shadow<=perf_write_data[6:0];
                if(perf_write_addr==16'h80ff && perf_write_data!=0) begin
                    commit_shadow<=perf_write_data;request_spi<=~request_spi;
                end
            end
        end
    end

    logic [1:0] state[0:31];
    logic [1:0] source[0:31];
    logic [3:0] channel[0:31];
    logic [6:0] note[0:31],velocity[0:31];
    logic [63:0] serial;
    (* syn_keep=1 *) wire [2:0] serial_carry={&serial[47:0],&serial[31:0],&serial[15:0]};
    function automatic logic [63:0] next_serial64(input logic [63:0] value,input logic [2:0] carry);
        next_serial64[15:0]=value[15:0]+16'd1;
        next_serial64[31:16]=value[31:16]+16'(carry[0]);
        next_serial64[47:32]=value[47:32]+16'(carry[1]);
        next_serial64[63:48]=value[63:48]+16'(carry[2]);
        // synthesis translate_off
        if($isunknown(value))next_serial64=value+64'd1;
        // synthesis translate_on
    endfunction
    wire [63:0] serial_increment=next_serial64(serial,serial_carry);
    logic [31:0] stamp_initialized;
    logic [15:0] off_pass[0:31];
    // Two adjacent binary counts differ in one reflected-Gray bit, including
    // 16'hffff -> 0. Capture that bit's index with each voice's start count.
    // Ignoring it makes one exact comparison reject ages 0 and 1; every other
    // modulo-16-bit age is accepted. No predecessor/carry network is needed
    // between the current pass counter and the per-voice state writer.
    logic [3:0] off_pass_skip[0:31];
    function automatic logic [3:0] pass_window_skip(input logic [15:0] value);
        logic [15:0] zeros;
        logic [7:0] octet;
        logic [3:0] nibble,index;
        logic [1:0] pair;
        zeros=~value;
        index[3]=~|zeros[7:0];
        octet=index[3]?zeros[15:8]:zeros[7:0];
        index[2]=~|octet[3:0];
        nibble=index[2]?octet[7:4]:octet[3:0];
        index[1]=~|nibble[1:0];
        pair=index[1]?nibble[3:2]:nibble[1:0];
        index[0]=~pair[0];
        // All-ones wraps to zero by flipping Gray bit 15, also index 15.
        pass_window_skip=index;
    endfunction
    wire [3:0] control_pass_skip=pass_window_skip(control_pass_count);
    function automatic logic pass_age_ready(
        input logic [15:0] now_pass,started_pass,
        input logic [3:0] skip_bit
    );
        logic [15:0] gray_difference,significant_difference;
        gray_difference=(now_pass ^ (now_pass>>1)) ^
                        (started_pass ^ (started_pass>>1));
        for(integer bit_index=0;bit_index<16;bit_index=bit_index+1)
            significant_difference[bit_index]=gray_difference[bit_index] &&
                                               (skip_bit!=4'(bit_index));
        pass_age_ready=|significant_difference;
        // Preserve relational arithmetic's procedural-if X/Z behavior.
        // synthesis translate_off
        if($isunknown({now_pass,started_pass}))
            pass_age_ready=16'(now_pass-started_pass)>=16'd2;
        // synthesis translate_on
    endfunction
    logic [31:0] key_down,live,prepared,dirty,context_valid;
    logic [31:0] was_wait_q;
    wire [31:0] wait_flush;
    logic [17:0] depth_amp[0:31],depth_mod[0:31];
    logic [13:0] voice_pitch[0:31], voice_cutoff[0:31];
    logic [7:0] voice_keytrack[0:31];
    logic [63:0] sustain;
    // Transient source/channel state, never written into the patch/template.
    // CC1 is nearest Q0.7 (0..128); XY is signed (value-64)/64.
    logic [7:0] wheel_gain[0:63];
    logic signed [6:0] touch_x[0:63],touch_y[0:63];
    wire [5:0] expression_read_owner={source[read_index[7:3]],channel[read_index[7:3]]};
    assign wheel_vibrato_depth=$signed(active_template[5][15:0]);
    for(genvar ec=0;ec<64;ec=ec+1)begin:g_expression
        always_ff @(posedge clk or negedge rst_n)
            if(!rst_n)begin wheel_gain[ec]<=0;touch_x[ec]<=0;touch_y[ec]<=0;end
            else if(!enable || midi_flush[ec/16])begin
                wheel_gain[ec]<=0;touch_x[ec]<=0;touch_y[ec]<=0;
            end else if(midi_event_valid && midi_event_ready && midi_event.source<4 &&
                        {midi_event.source[1:0],midi_event.status[3:0]}==6'(ec) &&
                        midi_event.status[7:4]==4'hb)begin
                case(midi_event.data1)
                    1:wheel_gain[ec]<={1'b0,midi_event.data2[6:0]}+8'(midi_event.data2[6]);
                    71:touch_x[ec]<=$signed(midi_event.data2[6:0]^7'h40);
                    74:touch_y[ec]<=$signed(midi_event.data2[6:0]^7'h40);
                    121:begin wheel_gain[ec]<=0;touch_x[ec]<=0;touch_y[ec]<=0;end
                    default:;
                endcase
            end
    end
    always_ff @(posedge clk or negedge rst_n)
        if(!rst_n)begin runtime_wheel_gain<=0;runtime_touch_x<=0;runtime_touch_y<=0;runtime_touch_cutoff_depth<=0;runtime_touch_resonance_depth<=0;end
        else begin
            runtime_wheel_gain<=enable && !midi_flush[expression_read_owner[5:4]] ? wheel_gain[expression_read_owner]:8'd0;
            runtime_touch_x<=enable && !midi_flush[expression_read_owner[5:4]] ? touch_x[expression_read_owner]:7'sd0;
            runtime_touch_y<=enable && !midi_flush[expression_read_owner[5:4]] ? touch_y[expression_read_owner]:7'sd0;
            runtime_touch_cutoff_depth<=$signed(active_template[6][15:0]);
            runtime_touch_resonance_depth<=$signed(active_template[7][15:0]);
        end
    // Only the wide CC owner updates need a local execution boundary. Capture
    // both the complete command and each voice's owner match on the original
    // handshake; no ingress status/source decode reaches the state writer.
    logic cc_pending;
    logic [5:0] cc_owner_q;
    logic [7:0] cc_controller_q,cc_data_q;
    (* syn_preserve=1, dont_touch="true" *) logic [31:0] cc_owner_match_q;
    (* syn_preserve=1, dont_touch="true" *) logic [31:0] cc_hard_q,cc_all_off_q,cc_pedal_up_q;
    // Keep the exact signed MIDI scaling, but separate multiplication from
    // division. A bounded small-remainder divider avoids a long LUT chain.
    logic bend_pending,bend_apply,bend_dividing,bend_negative;
    logic [18:0] bend_dividend;
    logic [1:0] bend_remainder;
    logic [4:0] bend_bit;
    wire [2:0] bend_trial={bend_remainder,bend_dividend[18]};
    logic signed [17:0] bend_quotient;
    logic [5:0] bend_owner;
    logic signed [23:0] bend_product;
    logic signed [17:0] bends[0:63];
    // CC121 used to drive every wide bank's CE through the complete ingress
    // ready path. Register its local owner/voice qualifications, then forward
    // the clear while the physical bank catches up on the following edge.
    (* syn_preserve=1 *) logic [63:0] bend_reset_q;
    (* syn_preserve=1 *) logic [31:0] voice_bend_reset_q;
    wire signed [17:0] effective_bends[0:63];
    // A voice's source/channel changes only on assignment. Maintain its bend
    // alongside that assignment and every owner update; S1 now reads one
    // 32-word mux instead of cascading the voice and 64-owner muxes.
    logic signed [17:0] voice_bend[0:31];
    wire signed [17:0] read_voice_bend=voice_bend_reset_q[read_index[7:3]]
        ? 18'sd0 : voice_bend[read_index[7:3]];
    logic [1:0] copy_state;
    logic [4:0] copy_voice;
    logic [13:0] copy_pitch,copy_cutoff;
    logic [2:0] copy_element;
    wire copy_write=copy_state==2 && slot>=10'd320 && slot<10'd700;
    wire [9:0] context_address={2'b0,copy_voice,copy_element};
    wire [89:0] context_data={12'd0,active_template[{copy_element,3'b100}][8],
        active_template[{copy_element,3'b100}][1:0],
        active_template[{copy_element,3'b001}][23:0],
        active_template[{copy_element,3'b010}][13:0],
        active_template[{copy_element,3'b011}][18:0],
        4'd0,clamp14(int'({1'b0,voice_pitch[copy_voice]})+int'($signed(active_template[{copy_element,3'b000}][17:0])))};
    wire [89:0] context_read;
    for(genvar b=0;b<5;b=b+1)begin:g_context
        // Reuse the official, checked-in 18x1024 SDPX9B configuration.
        sdp_ram_1024x18 u_ram(.dout(context_read[b*18+:18]),
            .clka(clk),.cea(copy_write),.clkb(clk),.ceb(1'b1),.oce(1'b1),.reset(1'b0),
            .ada(context_address),.din(context_data[b*18+:18]),.adb({2'b0,read_index}));
    end
    wire [17:0] amp_table_q,mod_table_q;
    wire [9:0] table_read_address={depth_bank,2'b0,velocity[copy_voice]};
    sdp_ram_1024x18 u_amp_table(.dout(amp_table_q),.clka(sclk),.cea(amp_write),
        .clkb(clk),.ceb(1'b1),.oce(1'b1),.reset(1'b0),
        .ada({~bank_sync,2'b0,perf_write_addr[6:0]}),.din(perf_write_data[17:0]),.adb(table_read_address));
    sdp_ram_1024x18 u_mod_table(.dout(mod_table_q),.clka(sclk),.cea(mod_write),
        .clkb(clk),.ceb(1'b1),.oce(1'b1),.reset(1'b0),
        .ada({~bank_sync,2'b0,perf_write_addr[6:0]}),.din(perf_write_data[17:0]),.adb(table_read_address));
    always_ff @(posedge clk)boot_velocity_q<=boot_velocity[velocity[copy_voice]];

    function automatic integer pitch(input [6:0] n);
        pitch=(int'(n)/12)*1024+((int'(n)%12)*1024)/12;
    endfunction
    function automatic [13:0] clamp14(input integer value);
        clamp14=value<0?14'd0:value>16383?14'h3fff:14'(value);
    endfunction
    logic [13:0] read_cutoff;
    logic read_live,read_context_valid;
    // Keep these S1 fabric registers outside the DSP input registers. The
    // renderer multiplies on S2; absorbing S1 would recreate the owner mux
    // setup path into the hard DSP without changing the RTL latency.
    // Key Track is unsigned Q2.6 (64 = unity), separate from CSP's pitch LFO.
    wire signed [25:0] cutoff_bend_product =
        read_voice_bend * $signed({1'b0,voice_keytrack[read_index[7:3]]});
    always_ff @(posedge clk) begin
        read_cutoff<=voice_cutoff[read_index[7:3]];
        read_live<=enable && live[read_index[7:3]] && !wait_flush[read_index[7:3]];
        read_context_valid<=context_valid[read_index[7:3]];
        runtime_bend<=enable?read_voice_bend:18'sd0;
        runtime_cutoff_bend<=enable?20'(cutoff_bend_product >>> 6):20'sd0;
        runtime_keytrack<=voice_keytrack[read_index[7:3]];
    end
    always_comb begin
        runtime_config='0;
        // Unwritten context RAM must never poison free-running phase/filter
        // state with X before the first keystroke. All inactive fields are zero.
        if (read_context_valid) begin
        runtime_config.pitch=context_read[13:0];
        runtime_config.wave=context_read[76:75];
        runtime_config.duty=context_read[74:51];
        runtime_config.cutoff=read_cutoff;
        runtime_config.resonance=context_read[50:37];
        runtime_config.dual=context_read[34];
        runtime_config.filter_type=context_read[36:35];
        runtime_config.volume_l=read_live && context_read[77]?context_read[25:18]:8'd0;
        runtime_config.volume_r=read_live && context_read[77]?context_read[33:26]:8'd0;
        end
    end
    for(genvar v=0;v<32;v=v+1)begin:g_voice
        // The flush is registered alongside engine ingress. Remember the
        // state on its capture edge so a canceled WAIT cannot become a tail.
        always_ff @(posedge clk or negedge rst_n)
            if(!rst_n)was_wait_q[v]<=1'b0;
            else was_wait_q[v]<=REGISTERED_EVENT_INPUT && state[v]==V_WAIT;
        assign wait_flush[v]=REGISTERED_EVENT_INPUT && midi_flush[source[v]] && was_wait_q[v];
        // A held voice drops its gate on the flush capture edge, but keeps
        // its existing live tail. Only a prior WAIT loses audible read-live.
        assign voice_gate[v]=enable && state[v]==V_HELD &&
            !(REGISTERED_EVENT_INPUT && midi_flush[source[v]]);
        assign amp_depth[v*18+:18]=depth_amp[v];
        assign mod_depth[v*18+:18]=depth_mod[v];
    end

    // Registered read/compare stages keep 32x64-bit age comparisons off the
    // timing-critical event acceptance path. Flush can cancel this scan.
    logic allocating,on_phase,on_apply;
    // Register the selected write target on the existing final compare edge.
    // Independent retained nodes let each 64-bit age bank use a nearby enable.
    (* syn_preserve=1, dont_touch="true" *) logic [31:0] stamp_apply_q;
    logic [4:0] on_apply_voice;
    logic [1:0] on_candidate_rank;
    logic [63:0] on_candidate_stamp;
    midi_pkg::midi_event_t allocated_event;
    logic [4:0] scan_voice, best_voice;
    logic [1:0] best_rank;
    logic [63:0] best_stamp;
    // Retain short parallel carry segments instead of one 64-bit borrow
    // chain. The candidate/read/compare edges and unsigned ordering stay put.
    (* syn_keep=1 *) wire [3:0] on_age_less,off_age_less;
    (* syn_keep=1 *) wire [3:1] on_age_equal,off_age_equal;
    function automatic logic age_less64(
        input logic [3:0] less,
        input logic [3:1] equal,
        input logic [63:0] a,b
    );
        age_less64=less[3] || (equal[3] &&
            (less[2] || (equal[2] && (less[1] || (equal[1] && less[0])))));
        // Preserve relational-operator X propagation in four-state checks.
        // The hardware contract contains only initialized/published stamps.
        // synthesis translate_off
        if($isunknown({a,b}))age_less64=a<b;
        // synthesis translate_on
    endfunction
    wire [1:0] scan_rank = state[scan_voice]==V_IDLE ? 2'd0 :
                           state[scan_voice]==V_RELEASE ? 2'd1 : 2'd2;
    wire scan_wins = on_candidate_rank < best_rank ||
                     (on_candidate_rank == best_rank && age_less64(on_age_less,on_age_equal,on_candidate_stamp,best_stamp));
    wire [4:0] selected_voice = scan_wins ? scan_voice : best_voice;
    // Off has its own registered read/compare/apply pipeline. A running
    // winner must never index another age mux in an unrolled 32-entry loop.
    logic off_scanning,off_phase,off_apply,off_found,off_sustain;
    midi_pkg::midi_event_t off_event;
    logic [4:0] off_cursor,off_best_voice,off_apply_voice;
    logic [63:0] off_candidate_stamp,off_best_stamp;
    for(genvar age_part=0;age_part<4;age_part=age_part+1)begin:g_age_compare
        assign on_age_less[age_part]=on_candidate_stamp[age_part*16+:16]<best_stamp[age_part*16+:16];
        assign off_age_less[age_part]=off_candidate_stamp[age_part*16+:16]<off_best_stamp[age_part*16+:16];
        if(age_part!=0)begin:g_high_equal
            assign on_age_equal[age_part]=on_candidate_stamp[age_part*16+:16]==best_stamp[age_part*16+:16];
            assign off_age_equal[age_part]=off_candidate_stamp[age_part*16+:16]==off_best_stamp[age_part*16+:16];
        end
    end
    logic off_candidate_match,off_candidate_wait,off_best_wait;
    logic off_apply_found,off_apply_wait;
    wire off_candidate_wins=off_candidate_match &&
        (!off_found || age_less64(off_age_less,off_age_equal,off_candidate_stamp,off_best_stamp));
    integer copy_pick;
    always_comb begin
        copy_pick=-1;
        for(integer v=31;v>=0;v=v-1)if(dirty[v]&&(state[v]==V_WAIT||state[v]==V_HELD))copy_pick=v;
    end
    assign midi_event_ready=enable && template_valid && copy_state==0 && !allocating && !off_scanning && !bend_pending &&
        !(REGISTERED_EVENT_INPUT && cc_pending) && req_sync==ack_sys && !(|midi_flush);
    wire [5:0] owner={midi_event.source[1:0],midi_event.status[3:0]};
    wire event_on=midi_event.status[7:4]==4'h9 && midi_event.data2!=0;
    wire event_off=midi_event.status[7:4]==4'h8 || (midi_event.status[7:4]==4'h9 && midi_event.data2==0);
    wire off_start=midi_event_valid && midi_event_ready && midi_event.source<4 && event_off;
    wire cc_start=REGISTERED_EVENT_INPUT && midi_event_valid && midi_event_ready && midi_event.source<4 &&
        midi_event.status[7:4]==4'hb && (midi_event.data1==64 || midi_event.data1==120 || midi_event.data1==121 || midi_event.data1==123);
    wire bend_reset_start=midi_event_valid && midi_event_ready && midi_event.source<4 &&
        midi_event.status[7:4]==4'hb && midi_event.data1==121;
    wire bend_commit=bend_pending && bend_apply && !midi_flush[bend_owner[5:4]];
    wire signed [17:0] bend_commit_data=bend_negative?-bend_quotient:bend_quotient;
    wire voice_assign=allocating && on_apply && !midi_flush[allocated_event.source[1:0]];
    wire [5:0] assigned_owner={allocated_event.source[1:0],allocated_event.status[3:0]};
    for(genvar o=0;o<64;o=o+1)begin:g_bend_bank
        always_ff @(posedge clk or negedge rst_n)begin
            if(!rst_n)bend_reset_q[o]<=1'b0;
            else bend_reset_q[o]<=enable && bend_reset_start && owner==6'(o);
        end
        assign effective_bends[o]=bend_reset_q[o]?18'sd0:bends[o];
        always_ff @(posedge clk or negedge rst_n)begin
            if(!rst_n)bends[o]<=18'sd0;
            else if(!enable || midi_flush[o/16])bends[o]<=18'sd0;
            else if(bend_commit && bend_owner==6'(o))bends[o]<=bend_commit_data;
            else if(bend_reset_q[o])bends[o]<=18'sd0;
        end
    end
    for(genvar v=0;v<32;v=v+1)begin:g_voice_bend
        always_ff @(posedge clk or negedge rst_n)begin
            if(!rst_n)voice_bend_reset_q[v]<=1'b0;
            else voice_bend_reset_q[v]<=enable && bend_reset_start && {source[v],channel[v]}==owner;
        end
        always_ff @(posedge clk or negedge rst_n)begin
            if(!rst_n)voice_bend[v]<=18'sd0;
            else if(!enable)voice_bend[v]<=18'sd0;
            else begin
                if(midi_flush[source[v]] || voice_bend_reset_q[v])voice_bend[v]<=18'sd0;
                if(bend_commit && {source[v],channel[v]}==bend_owner)voice_bend[v]<=bend_commit_data;
                if(voice_assign && on_apply_voice==5'(v))
                    voice_bend[v]<=bend_commit && assigned_owner==bend_owner
                        ? bend_commit_data:effective_bends[assigned_owner];
            end
        end
    end
    // The registered ingress hides a newly captured event for one edge. Its
    // bare pending/empty-slot pulse covers that edge. A staged Off then keeps the
    // barrier until acceptance, even if a template commit temporarily drops
    // ready. A CC capture and its local execution edge use the same barrier:
    // an older prepared On must not become HELD before the command kills it.
    // Only new copies and WAIT activation pause; in-flight work drains.
    wire off_guard=off_start || (REGISTERED_EVENT_INPUT &&
        (midi_capture_pending || cc_start || cc_pending || (midi_event_valid && midi_event.source<4 && event_off)));
    generate if(REGISTERED_EVENT_INPUT)begin:g_registered_cc
    always_ff @(posedge clk or negedge rst_n) begin
        if(!rst_n)begin
            cc_pending<=0;cc_owner_q<=0;cc_controller_q<=0;cc_data_q<=0;
        end else if(!enable)cc_pending<=0;
        else begin
            // No refill on the execution edge: ready remains low for the one
            // occupied cycle, including a source-flush cancellation.
            cc_pending<=cc_start;
            if(cc_start)begin
                cc_owner_q<=owner;cc_controller_q<=midi_event.data1;cc_data_q<=midi_event.data2;
            end
        end
    end
    for(genvar v=0;v<32;v=v+1)begin:g_cc_decode
        // Retain local command nodes as well as owner matches, rather than
        // moving the same high-fanout ingress decode to another shared net.
        always_ff @(posedge clk or negedge rst_n) begin
            if(!rst_n)begin
                cc_owner_match_q[v]<=0;cc_hard_q[v]<=0;cc_all_off_q[v]<=0;cc_pedal_up_q[v]<=0;
            end else if(cc_start)begin
                cc_owner_match_q[v]<={source[v],channel[v]}==owner;
                cc_hard_q[v]<=midi_event.data1==120;
                cc_all_off_q[v]<=midi_event.data1==120 || midi_event.data1==123;
                cc_pedal_up_q[v]<=(midi_event.data1==64 && midi_event.data2<64) || midi_event.data1==121;
            end
        end
    end
    end else begin:g_direct_cc
        assign cc_pending=1'b0;
        assign cc_owner_q=6'd0;
        assign cc_controller_q=8'd0;
        assign cc_data_q=8'd0;
        assign cc_owner_match_q=32'd0;
        assign cc_hard_q=32'd0;
        assign cc_all_off_q=32'd0;
        assign cc_pedal_up_q=32'd0;
    end endgenerate
    // Both age scans already separate candidate capture from comparison. On
    // each comparison edge prefetch the next cursor, so the synchronous RAM
    // output is ready at the original candidate-capture edge. Idle/start
    // prefetches voice zero; command acceptance and scan latency stay intact.
    wire [4:0] stamp_read_voice = allocating
        ? scan_voice + 5'(on_phase && scan_voice!=31)
        : off_scanning ? off_cursor + 5'(off_phase && off_cursor!=31) : 5'd0;
    wire [71:0] stamp_read_data;
    wire [71:0] stamp_write_data={8'd0,serial_increment};
    logic stamp_write;
    always_comb begin
        // Match the existing procedural disable/flush priority even for
        // four-state controls; the RAM only sees a definite write enable.
        stamp_write=1'b0;
        if(!enable) begin end
        else if(midi_flush[allocated_event.source[1:0]]) begin end
        else if(stamp_apply_q[on_apply_voice])stamp_write=1'b1;
    end
    for(genvar b=0;b<4;b=b+1)begin:g_stamp_ram
        // Same official 18x1024 SDPX9B resource as the context/table banks.
        // Reset only validity: unpublished RAM contents always forward zero.
        sdp_ram_1024x18 u_ram(.dout(stamp_read_data[b*18+:18]),
            .clka(clk),.cea(stamp_write),.clkb(clk),.ceb(1'b1),.oce(1'b1),.reset(1'b0),
            .ada({5'd0,on_apply_voice}),.din(stamp_write_data[b*18+:18]),
            .adb({5'd0,stamp_read_voice}));
    end
    for(genvar v=0;v<32;v=v+1)begin:g_stamp_bank
        always_ff @(posedge clk or negedge rst_n) begin
            if(!rst_n)stamp_apply_q[v]<=1'b0;
            else if(!enable)stamp_apply_q[v]<=1'b0;
            else begin
                if(allocating)begin
                    if(midi_flush[allocated_event.source[1:0]])stamp_apply_q[v]<=1'b0;
                    else if(on_apply)stamp_apply_q[v]<=1'b0;
                    else if(on_phase && scan_voice==31)
                        stamp_apply_q[v] <= selected_voice == 5'(v);
                end
                if(midi_event_valid && midi_event_ready && midi_event.source<4)
                    if(event_on && midi_event.data1<128 && midi_event.data2<128)
                        stamp_apply_q[v]<=1'b0;
            end
        end
        // Preserve the original final-edge disable/flush priority, including
        // procedural four-state behavior. The one-hot adds no commit latency.
        always_ff @(posedge clk or negedge rst_n) begin
            if(!rst_n)stamp_initialized[v]<=1'b0;
            else if(!enable) begin end
            else if(midi_flush[allocated_event.source[1:0]]) begin end
            else if(stamp_apply_q[v])stamp_initialized[v]<=1'b1;
        end
    end
    always_ff @(posedge clk or negedge rst_n) begin
        if(!rst_n)begin
            req_meta<=0;req_sync<=0;ack_sys<=0;depth_bank<=0;template_status<=0;
            template_valid<=LOCAL_MIDI_BOOT;host_template_valid_o<=0;
            keytrack<=64;bendrange<=2;serial<=0;
            key_down<=0;live<=0;prepared<=0;dirty<=0;sustain<=0;context_valid<=0;
            bend_pending<=0;bend_apply<=0;bend_dividing<=0;bend_negative<=0;
            bend_dividend<=0;bend_remainder<=0;bend_bit<=0;bend_quotient<=0;bend_owner<=0;bend_product<=0;
            copy_state<=0;copy_voice<=0;copy_pitch<=0;copy_cutoff<=0;copy_element<=0;allocating<=0;allocated_event<='0;
            scan_voice<=0;best_voice<=0;best_rank<=3;best_stamp<=64'hffffffffffffffff;
            on_phase<=0;on_apply<=0;on_apply_voice<=0;on_candidate_rank<=0;on_candidate_stamp<=0;
            off_scanning<=0;off_phase<=0;off_apply<=0;off_found<=0;off_sustain<=0;off_event<='0;
            off_cursor<=0;off_best_voice<=0;off_apply_voice<=0;
            off_candidate_stamp<=0;off_best_stamp<=0;off_candidate_match<=0;off_candidate_wait<=0;
            off_best_wait<=0;off_apply_found<=0;off_apply_wait<=0;
            for(integer i=0;i<64;i=i+1)begin
                active_template[i]<=LOCAL_MIDI_BOOT?boot_template(i):32'd0;
            end
            for(integer v=0;v<32;v=v+1)begin
                state[v]<=V_IDLE;source[v]<=0;channel[v]<=0;note[v]<=0;velocity[v]<=0;
                off_pass[v]<=0;off_pass_skip[v]<=0;depth_amp[v]<=0;depth_mod[v]<=0;voice_pitch[v]<=0;voice_cutoff[v]<=0;
                voice_keytrack[v]<=8'd64;
            end
        end else begin
            req_meta<=request_spi;req_sync<=req_meta;
            if(req_sync!=ack_sys && copy_state==0)begin
                for(integer i=0;i<64;i=i+1)active_template[i]<=shadow[i];
                keytrack<=keytrack_shadow;bendrange<=bendrange_shadow;depth_bank<=~depth_bank;
                template_status<=commit_shadow;template_valid<=1;host_template_valid_o<=1;ack_sys<=req_sync;
                for(integer v=0;v<32;v=v+1)if(state[v]==V_HELD||state[v]==V_WAIT)dirty[v]<=1;
            end
            if(!enable)begin
                key_down<=0;live<=0;prepared<=0;dirty<=0;sustain<=0;copy_state<=0;allocating<=0;on_apply<=0;off_scanning<=0;off_apply<=0;bend_pending<=0;bend_apply<=0;bend_dividing<=0;
                for(integer v=0;v<32;v=v+1)state[v]<=V_IDLE;
            end else begin
                // Only renderer-idle slots write runtime RAM: no read/write
                // collision, no new audio latency, no shadow-bank ownership change.
                case(copy_state)
                    0:if(copy_pick>=0 && !allocating && !off_scanning && !off_guard && req_sync==ack_sys && slot>=320 && slot<680)begin
                        copy_voice<=5'(copy_pick);copy_pitch<=voice_pitch[copy_pick];copy_element<=0;copy_state<=1;
                    end
                    1:begin
                        // Use the existing context setup cycle to separate
                        // the pitch read mux, keytracking math and bank write.
                        copy_cutoff<=clamp14(5120+((int'({1'b0,copy_pitch})-5120)*int'(keytrack))/64+512);
                        copy_state<=2;
                    end
                    2:if(copy_write)begin
                        if(copy_element==7)begin
                            copy_state<=0;dirty[copy_voice]<=0;prepared[copy_voice]<=1;context_valid[copy_voice]<=1;
                            depth_amp[copy_voice]<=LOCAL_MIDI_BOOT&&!host_template_valid_o?boot_velocity_q:amp_table_q;
                            depth_mod[copy_voice]<=LOCAL_MIDI_BOOT&&!host_template_valid_o?18'd0:mod_table_q;
                            voice_cutoff[copy_voice]<=copy_cutoff;
                            // Publish the tracking coefficient with its base.
                            // RELEASE tails retain their previous template.
                            voice_keytrack[copy_voice]<=keytrack;
                        end else copy_element<=copy_element+1'b1;
                    end
                    default:copy_state<=0;
                endcase
                for(integer v=0;v<32;v=v+1)begin
                    if(state[v]==V_WAIT && !off_guard &&
                       !(off_scanning && source[v]==off_event.source[1:0] &&
                         channel[v]==off_event.status[3:0] && note[v]==off_event.data1[6:0]) && prepared[v] && pass_age_ready(control_pass_count,off_pass[v],off_pass_skip[v]))begin
                        state[v]<=V_HELD;live[v]<=1;
                    end
                    if(state[v]==V_RELEASE && tail_done[v])begin state[v]<=V_IDLE;live[v]<=0;end
                    if(midi_flush[source[v]])begin
                        key_down[v]<=0;
                        if(state[v]==V_WAIT || (REGISTERED_EVENT_INPUT && was_wait_q[v]))begin
                            state[v]<=V_IDLE;live[v]<=0;dirty[v]<=0;prepared[v]<=0;
                        end
                        else if(state[v]==V_HELD)state[v]<=V_RELEASE;
                    end
                end
                for(integer o=0;o<64;o=o+1)if(midi_flush[o/16])sustain[o]<=0;
                if(allocating)begin
                    if(midi_flush[allocated_event.source[1:0]])begin allocating<=0;on_apply<=0;end
                    else if(on_apply)begin
                        allocating<=0;on_apply<=0;
                        state[on_apply_voice]<=V_WAIT;
                        source[on_apply_voice]<=allocated_event.source[1:0];
                        channel[on_apply_voice]<=allocated_event.status[3:0];
                        note[on_apply_voice]<=allocated_event.data1[6:0];
                        velocity[on_apply_voice]<=allocated_event.data2[6:0];
                        voice_pitch[on_apply_voice]<=14'(pitch(allocated_event.data1[6:0]));
                        serial<=serial_increment;
                        off_pass[on_apply_voice]<=control_pass_count;
                        off_pass_skip[on_apply_voice]<=control_pass_skip;
                        key_down[on_apply_voice]<=1;prepared[on_apply_voice]<=0;dirty[on_apply_voice]<=1;
                    end else if(!on_phase)begin
                        // Indexed RAM/register selection and age comparison
                        // have separate cycles, including the final winner.
                        on_candidate_rank<=scan_rank;on_candidate_stamp<=stamp_initialized[scan_voice]?stamp_read_data[63:0]:64'd0;on_phase<=1;
                    end else begin
                        on_phase<=0;
                        if(scan_wins)begin
                            best_voice<=scan_voice;best_rank<=on_candidate_rank;best_stamp<=on_candidate_stamp;
                        end
                        if(scan_voice==31)begin
                            on_apply<=1;on_apply_voice<=selected_voice;
                        end else scan_voice<=scan_voice+1'b1;
                    end
                end
                if(off_scanning)begin
                    // Flush/disable is a cancellation barrier, including the
                    // final apply edge after a winning voice was registered.
                    if(midi_flush[off_event.source[1:0]])begin off_scanning<=0;off_apply<=0;end
                    else if(off_apply)begin
                        off_scanning<=0;off_apply<=0;
                        if(off_apply_found)begin
                            key_down[off_apply_voice]<=0;
                            if(off_apply_wait)begin
                                state[off_apply_voice]<=V_IDLE;live[off_apply_voice]<=0;
                                dirty[off_apply_voice]<=0;prepared[off_apply_voice]<=0;
                            end else if(!off_sustain)state[off_apply_voice]<=V_RELEASE;
                        end
                    end else if(!off_phase)begin
                        // One indexed candidate is sampled before the 64-bit
                        // comparison. The critical path cannot span candidates.
                        off_candidate_stamp<=stamp_initialized[off_cursor]?stamp_read_data[63:0]:64'd0;
                        off_candidate_wait<=state[off_cursor]==V_WAIT;
                        off_candidate_match<=(state[off_cursor]==V_HELD || state[off_cursor]==V_WAIT) &&
                            key_down[off_cursor] && source[off_cursor]==off_event.source[1:0] &&
                            channel[off_cursor]==off_event.status[3:0] && note[off_cursor]==off_event.data1[6:0];
                        off_phase<=1;
                    end else begin
                        off_phase<=0;
                        if(off_candidate_wins)begin
                            off_found<=1;off_best_voice<=off_cursor;off_best_stamp<=off_candidate_stamp;
                            off_best_wait<=off_candidate_wait;
                        end
                        if(off_cursor==31)begin
                            off_apply<=1;off_apply_found<=off_found || off_candidate_wins;
                            off_apply_voice<=off_candidate_wins?off_cursor:off_best_voice;
                            off_apply_wait<=off_candidate_wins?off_candidate_wait:off_best_wait;
                        end else off_cursor<=off_cursor+1'b1;
                    end
                end
                if(bend_pending)begin
                    if(midi_flush[bend_owner[5:4]])begin
                        bend_pending<=0;bend_apply<=0;bend_dividing<=0;
                    end else if(bend_apply)begin
                        bend_pending<=0;bend_apply<=0;
                    end else if(!bend_dividing)begin
                        // trunc(product/96) = sign * floor((abs(product)>>5)/3).
                        // Read one dividend bit per cycle; remainder is 0..2.
                        bend_negative<=bend_product[23];
                        bend_dividend<=19'((bend_product[23]?$unsigned(-bend_product):$unsigned(bend_product))>>5);
                        bend_quotient<=0;bend_remainder<=0;bend_bit<=0;bend_dividing<=1;
                    end else begin
                        bend_dividend<={bend_dividend[17:0],1'b0};
                        bend_quotient<={bend_quotient[16:0],bend_trial>=3'd3};
                        bend_remainder<=2'(bend_trial>=3'd3 ? bend_trial-3'd3 : bend_trial);
                        if(bend_bit==18)begin bend_dividing<=0;bend_apply<=1;end
                        else bend_bit<=bend_bit+1'b1;
                    end
                end
                // Disable is handled above; a matching flush cancels the whole
                // command, while another source's flush must not suppress it.
                if(REGISTERED_EVENT_INPUT && cc_pending && !midi_flush[cc_owner_q[5:4]])begin
                    sustain[cc_owner_q]<=cc_controller_q==64 && cc_data_q>=64;
                    for(integer v=0;v<32;v=v+1)if(cc_owner_match_q[v])begin
                        if(cc_all_off_q[v])begin
                            key_down[v]<=0;
                            if(cc_hard_q[v] || state[v]==V_WAIT)begin
                                state[v]<=V_IDLE;live[v]<=0;dirty[v]<=0;prepared[v]<=0;
                            end else if(state[v]==V_HELD)state[v]<=V_RELEASE;
                        end else if(cc_pedal_up_q[v] && state[v]==V_HELD && !key_down[v])state[v]<=V_RELEASE;
                    end
                end
                if(midi_event_valid && midi_event_ready && midi_event.source<4)begin
                    if(event_on && midi_event.data1<128 && midi_event.data2<128)begin
                        allocated_event<=midi_event;allocating<=1;scan_voice<=0;on_phase<=0;on_apply<=0;
                        best_rank<=3;best_stamp<=64'hffffffffffffffff;best_voice<=0;
                    end else if(event_off)begin
                        off_event<=midi_event;off_sustain<=sustain[owner];off_scanning<=1;
                        off_phase<=0;off_apply<=0;off_found<=0;off_cursor<=0;
                    end else if(!REGISTERED_EVENT_INPUT && midi_event.status[7:4]==4'hb)begin
                        if(midi_event.data1==64 || midi_event.data1==121)begin
                            sustain[owner]<=midi_event.data1==64 && midi_event.data2>=64;
                            if(midi_event.data1==121 || midi_event.data2<64)for(integer v=0;v<32;v=v+1)
                                if(state[v]==V_HELD && !key_down[v] && {source[v],channel[v]}==owner)state[v]<=V_RELEASE;
                        end
                        if(midi_event.data1==120 || midi_event.data1==123)begin
                            sustain[owner]<=0;
                            for(integer v=0;v<32;v=v+1)if({source[v],channel[v]}==owner)begin
                                key_down[v]<=0;
                                if(midi_event.data1==120 || state[v]==V_WAIT)begin
                                    state[v]<=V_IDLE;live[v]<=0;dirty[v]<=0;prepared[v]<=0;
                                end else if(state[v]==V_HELD)state[v]<=V_RELEASE;
                            end
                        end
                    end else if(midi_event.status[7:4]==4'he)begin
                        bend_pending<=1;bend_apply<=0;bend_dividing<=0;bend_owner<=owner;
                        bend_product<=24'(((int'(midi_event.data2)*128+int'(midi_event.data1))-8192)*int'(bendrange));
                    end
                end
            end
        end
    end
endmodule
`default_nettype wire
