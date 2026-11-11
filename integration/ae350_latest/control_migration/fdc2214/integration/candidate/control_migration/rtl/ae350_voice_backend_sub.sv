// Copyright (c) 2026 ICECREAM1230
// SPDX-License-Identifier: MIT
// Prepared ABI2: storage and lifecycle only; CPU supplies every static value.
`default_nettype none
module ae350_voice_backend_sub #(
    parameter bit LOCAL_MIDI_BOOT = 1'b0,
    parameter bit ENABLE_AE350_FDC = 1'b0,
    parameter integer FDC_WATCHDOG_CYCLES = 36_867_000
) (
    input wire clk, rst_n, sclk, enable,
    input wire [9:0] slot,
    input wire [15:0] control_pass_count,
    input wire [7:0] read_index,
    input wire perf_write_enable,
    input wire [15:0] perf_write_addr,
    input wire [31:0] perf_write_data,
    input wire cmd_valid_i,
    output wire cmd_ready_o,
    output wire [1:0] cmd_result_o,
    input wire [159:0] cmd_data_i,
    output wire ready_o,
    output wire fb_valid_o, input wire fb_ready_i,
    output wire [4:0] fb_voice_o,
    output wire [31:0] fb_generation_o,
    output logic [31:0] template_status,
    output logic host_template_valid_o,
    input element_types_pkg::element_config_t native_config_i,
    output wire native_cfg_write_o,output wire [2:0] native_cfg_word_o,
    output wire [7:0] native_cfg_index_o,output logic [31:0] native_cfg_data_o,
    output element_types_pkg::element_config_t runtime_config,
    output logic signed [17:0] runtime_bend /* synthesis syn_preserve=1 */,
    output logic signed [19:0] runtime_cutoff_bend,
    output logic [7:0] runtime_keytrack /* synthesis syn_preserve=1 */,
    output logic [7:0] runtime_wheel_gain /* synthesis syn_preserve=1 */,
    output logic signed [6:0] runtime_touch_x /* synthesis syn_preserve=1 */,
    output logic signed [6:0] runtime_touch_y /* synthesis syn_preserve=1 */,
    output logic signed [15:0] runtime_touch_cutoff_depth /* synthesis syn_preserve=1 */,
    output logic signed [15:0] runtime_touch_resonance_depth /* synthesis syn_preserve=1 */,
    output logic signed [17:0] runtime_touch_cutoff_delta,runtime_touch_resonance_delta,
    output logic signed [15:0] wheel_vibrato_depth,
    output wire [31:0] voice_gate,
    output wire [575:0] amp_depth, mod_depth,
    output wire [31:0] fdc_sensor_owned_o,
    output wire [543:0] fdc_gain_o,
    output wire [1023:0] fdc_generation_o,
    output wire fdc_alive_o,
    input wire [31:0] tail_done
);
    localparam [1:0] V_IDLE=0,V_WAIT=1,V_HELD=2,V_RELEASE=3;
    logic [1:0] state[0:31];
    logic [31:0] generations[0:31],live,prepared,context_valid,idle_pending;
    logic [15:0] off_pass[0:31];
    localparam integer VALUE_WIDTH = ENABLE_AE350_FDC ? 158 : 140;
    logic [VALUE_WIDTH-1:0] values[0:31];
    logic stage_valid,copying,commit_done;
    logic [4:0] stage_voice;
    logic [1:0] stage_mode;
    logic stage_liveness;
    logic [31:0] stage_gen,stage_revision,last_revision;
    logic [89:0] context_stage[0:7];
    logic [191:0] params_stage;
    logic [15:0] context_mask;
    logic [2:0] params_mask,copy_index,copy_word;
    // The completion CDC holds the entire command until cmd_ready_o. Read
    // the indexed voice state first, then register its validation result;
    // the wide per-voice write enables never depend on the live comparator.
    localparam [1:0] C_IDLE=0,C_CHECK=1,C_EXECUTE=2;
    logic [1:0] command_phase,selected_state,validated_result;
    logic [31:0] selected_generation;
    wire [7:0] voice_byte=cmd_data_i[15:8],op=cmd_data_i[23:16],flags=cmd_data_i[31:24];
    wire [4:0] voice=voice_byte[4:0];
    wire [31:0] rev=cmd_data_i[63:32],gen=cmd_data_i[159:128];
    wire [63:0] payload=cmd_data_i[127:64];
    wire matching=gen!=0&&selected_generation==gen;
    wire stage_match=stage_valid&&voice==stage_voice&&gen==stage_gen&&rev==stage_revision;
    wire complete_stage=params_mask==7&&(stage_mode==2||context_mask==16'hffff);
    logic [1:0] result;
    always_comb begin
        result=0;
        if(cmd_data_i[7:0]!=0||voice_byte>=32)result=2;
        else case(op)
            8'h01,8'h04:begin
                if(flags!=0||cmd_data_i[127:32]!=0||gen==0)result=2;
                else if(!matching)result=1;
            end
            8'h10:begin
                if((ENABLE_AE350_FDC ? (flags[7:3]!=0 || flags[1:0]==3) : flags>2)||payload!=0||gen==0||rev==0||stage_valid)result=2;
                else if(rev<=last_revision||(flags[1:0]==0 ? gen<=selected_generation:!matching))result=1;
                else if(flags[1:0]==1&&selected_state!=V_HELD&&selected_state!=V_WAIT)result=2;
            end
            8'h11:begin
                if(flags[7:4]!=0||(flags[3]&&payload[63:26]!=0))result=2;
                else if(!stage_match)result=1;
                else if(stage_mode==2)result=2;
            end
            8'h12:begin
                if(flags>2 || (flags==2 && (ENABLE_AE350_FDC ?
                    (payload[63:30]!=0 || (payload[29] ? payload[28:12]>17'd65536 : payload[28:12]!=0)) :
                    payload[63:12]!=0)))result=2;
                else if(!stage_match)result=1;
            end
            8'h13:begin
                if(flags!=0||payload!=0)result=2;
                else if(!stage_match)result=1;
                else if(!complete_stage || (ENABLE_AE350_FDC && stage_liveness && !params_stage[157]))result=2;
            end
            8'h15:if(flags!=0||voice_byte!=0||gen!=0||cmd_data_i[127:48]!=0)result=2;
            default:result=2;
        endcase
    end
    wire commit_request=command_phase==C_EXECUTE&&cmd_valid_i&&op==8'h13&&validated_result==0;
    assign cmd_ready_o=enable&&command_phase==C_EXECUTE&&(!copying)&&
        (op!=8'h13||validated_result!=0||commit_done);
    assign cmd_result_o=validated_result;
    wire fire=cmd_valid_i&&cmd_ready_o;
    always_ff @(posedge clk or negedge rst_n)begin
        if(!rst_n)begin
            command_phase<=C_IDLE;selected_generation<=0;selected_state<=V_IDLE;validated_result<=0;
        end else begin
            case(command_phase)
                C_IDLE:if(cmd_valid_i&&enable)begin
                    selected_generation<=generations[voice];selected_state<=state[voice];
                    command_phase<=C_CHECK;
                end
                C_CHECK:begin
                    if(cmd_valid_i)begin validated_result<=result;command_phase<=C_EXECUTE;end
                    else command_phase<=C_IDLE;
                end
                C_EXECUTE:if(fire||(!cmd_valid_i&&!copying))command_phase<=C_IDLE;
                default:command_phase<=C_IDLE;
            endcase
        end
    end
    assign ready_o=enable&&rst_n;
    assign template_status=0;
    assign host_template_valid_o=1'b0;
    // The production drum traverses 0..767 without a mid-frame stall and
    // parks at 767 between requests. Decode the NEXT edge's safe windows:
    // q at actual slots 320..699 / 320..659 / 320..698 respectively.
    // This ends running/slot decode at three control FFs, not the wide CE.
    logic copy_window_q,start_window_q,scalar_issue_window_q,schedule_primed_q;
    always_ff @(posedge clk or negedge rst_n)begin
        if(!rst_n)begin copy_window_q<=0;start_window_q<=0;scalar_issue_window_q<=0;schedule_primed_q<=0;end
        else begin
            schedule_primed_q<=1;
            copy_window_q<=slot>=10'd319&&slot<10'd699;
            start_window_q<=slot>=10'd319&&slot<10'd659;
            scalar_issue_window_q<=slot>=10'd319&&slot<10'd698;
        end
    end
    wire copy_write=copying&&copy_window_q;
    // Per-voice publication FFs localize each 140-bit write enable. The
    // stage payload is held until the real COMMIT ACK, so no data copy is
    // needed. Contexts issue on word 38, then publish on actual word 39;
    // scalar-only transactions reserve the final safe edge for publication.
    logic [31:0] publish_select_q /* synthesis syn_preserve=1 */;
    logic publish_pending;
    wire scalar_issue=commit_request&&!commit_done&&!copying&&!publish_pending&&stage_mode==2&&scalar_issue_window_q;
    wire context_issue=copy_write&&copy_index==7&&copy_word==3;
    wire publish_issue=scalar_issue||context_issue;
    always_ff @(posedge clk or negedge rst_n)begin
        if(!rst_n)begin publish_select_q<=0;publish_pending<=0;end
        else begin
            publish_pending<=publish_issue;
            for(integer v=0;v<32;v++)publish_select_q[v]<=publish_issue&&stage_voice==5'(v);
        end
    end
    assign native_cfg_write_o=copy_write;
    assign native_cfg_word_o=copy_word;
    assign native_cfg_index_o={stage_voice,copy_index};
    wire [89:0] context_word=context_stage[copy_index];
    always_comb begin
        case(copy_word)
            0:native_cfg_data_o={16'd0,context_word[76:75],context_word[13:0]};
            1:native_cfg_data_o={8'd0,context_word[74:51]};
            2:native_cfg_data_o={4'd0,context_word[50:37],params_stage[51:38]};
            3:native_cfg_data_o={13'd0,context_word[36:18]};
            default:native_cfg_data_o={31'd0,context_word[77]};
        endcase
    end
    // A naive registered read_index adds a cycle and misaligns the original
    // synchronous element bank and S1 valid/index. Instead predecode the next
    // voice one edge early, preserving their existing edge. The registered
    // one-hot controls fan out only within one voice (142 loads), rather than
    // sending five running-gated binary index bits through the entire mux.
    // Reset/parked slot 767 predict voice zero, including first accept=0.
    logic [31:0] read_select_q /* synthesis syn_preserve=1 */;
    wire [4:0] next_read_voice=slot[7:3]+5'(&slot[2:0]);
    always_ff @(posedge clk or negedge rst_n)begin
        if(!rst_n)read_select_q<=32'd1;
        else for(integer v=0;v<32;v++)read_select_q[v]<=next_read_voice==5'(v);
    end
    logic read_live,read_valid,selected_live,selected_valid;
    logic [VALUE_WIDTH-1:0] read_values,selected_values;
    always_comb begin
        selected_values=0;selected_live=0;selected_valid=0;
        for(integer v=0;v<32;v++)begin
            selected_values|=values[v]&{VALUE_WIDTH{read_select_q[v]}};
            selected_live|=live[v]&&read_select_q[v];
            selected_valid|=context_valid[v]&&read_select_q[v];
        end
    end
    always_ff @(posedge clk)begin
        // Owner reset can release in the middle of an unreset audio drum.
        // The first edge primes the predictor while reset-empty contexts
        // remain invalid; the next edge is aligned at any release slot.
        read_live<=enable&&schedule_primed_q&&selected_live;
        read_valid<=schedule_primed_q&&selected_valid;
        read_values<=selected_values;
    end
    always_comb begin
        runtime_config='0;
        if(read_valid)begin
            runtime_config=native_config_i;
            runtime_config.cutoff=read_values[51:38];
            runtime_config.volume_l=read_live?native_config_i.volume_l:8'd0;
            runtime_config.volume_r=read_live?native_config_i.volume_r:8'd0;
        end
        runtime_bend=$signed(read_values[17:0]);runtime_cutoff_bend=$signed(read_values[37:18]);
        runtime_keytrack=read_values[95:88];runtime_wheel_gain=read_values[103:96];
        runtime_touch_cutoff_delta=$signed(read_values[121:104]);
        runtime_touch_resonance_delta=$signed(read_values[139:122]);
        runtime_touch_x=0;runtime_touch_y=0;runtime_touch_cutoff_depth=0;runtime_touch_resonance_depth=0;
    end
    for(genvar v=0;v<32;v++)begin:g_gate
        assign voice_gate[v]=enable&&state[v]==V_HELD;
        assign amp_depth[v*18+:18]=values[v][69:52];
        assign mod_depth[v*18+:18]=values[v][87:70];
    end
    // Identity and gain come from the same actual native COMMIT as pitch.
    // A MIDI NEW publishes sensor=0 for its new occupancy, so the old FDC
    // timeout cannot mute that replacement. No mailbox writes these values.
    generate if (ENABLE_AE350_FDC) begin : g_fdc
        localparam integer WCW=FDC_WATCHDOG_CYCLES>1?$clog2(FDC_WATCHDOG_CYCLES+1):1;
        logic [WCW-1:0] remaining;
        wire sensor_publish=(|publish_select_q) && params_stage[157] && stage_liveness;
        initial if(FDC_WATCHDOG_CYCLES<1)$fatal(1,"FDC watchdog must be positive");
        always_ff @(posedge clk or negedge rst_n) begin
            if(!rst_n)remaining<=0;
            else if(sensor_publish)remaining<=WCW'(FDC_WATCHDOG_CYCLES);
            else if(remaining!=0)remaining<=remaining-1'b1;
        end
        assign fdc_alive_o=remaining!=0;
        for(genvar f=0;f<32;f++)begin:g_voice
            assign fdc_sensor_owned_o[f]=values[f][157];
            assign fdc_gain_o[f*17+:17]=values[f][156:140];
            assign fdc_generation_o[f*32+:32]=generations[f];
        end
    end else begin:g_no_fdc
        assign fdc_sensor_owned_o=0;assign fdc_gain_o=0;
        assign fdc_generation_o=0;assign fdc_alive_o=0;
    end endgenerate
    integer feedback_pick;
    always_comb begin
        feedback_pick=0;
        for(integer v=31;v>=0;v--)if(idle_pending[v])feedback_pick=v;
    end
    logic feedback_valid;logic [4:0] feedback_voice;logic [31:0] feedback_generation;
    wire feedback_capture=(!feedback_valid||fb_ready_i)&&(|idle_pending);
    assign fb_valid_o=feedback_valid;assign fb_voice_o=feedback_voice;assign fb_generation_o=feedback_generation;
    always_ff @(posedge clk or negedge rst_n)begin
        if(!rst_n)begin feedback_valid<=0;feedback_voice<=0;feedback_generation<=0;end
        else if(!feedback_valid||fb_ready_i)begin
            feedback_valid<=|idle_pending;
            if(|idle_pending)begin feedback_voice<=5'(feedback_pick);feedback_generation<=generations[feedback_pick];end
        end
    end
    always_ff @(posedge clk or negedge rst_n)begin
        if(!rst_n)begin
            live<=0;prepared<=0;context_valid<=0;idle_pending<=0;
            stage_valid<=0;copying<=0;commit_done<=0;stage_voice<=0;stage_mode<=0;
            stage_liveness<=0;stage_gen<=0;stage_revision<=0;last_revision<=0;params_stage<=0;context_mask<=0;params_mask<=0;copy_index<=0;copy_word<=0;wheel_vibrato_depth<=0;
            for(integer v=0;v<32;v++)begin state[v]<=V_IDLE;generations[v]<=0;off_pass[v]<=0;values[v]<=0;end
            for(integer e=0;e<8;e++)context_stage[e]<=0;
        end else begin
            if(feedback_capture)idle_pending[feedback_pick]<=0;
            for(integer v=0;v<32;v++)begin
                if(publish_select_q[v])values[v]<=params_stage[VALUE_WIDTH-1:0];
                if(state[v]==V_WAIT&&prepared[v]&&16'(control_pass_count-off_pass[v])>=2)begin state[v]<=V_HELD;live[v]<=1;end
                if(state[v]==V_RELEASE&&tail_done[v])begin state[v]<=V_IDLE;live[v]<=0;idle_pending[v]<=1;end
            end
            if(publish_pending)commit_done<=1;
            if(commit_request&&!commit_done&&!copying&&!publish_pending)begin
                if(stage_mode!=2&&start_window_q)begin copying<=1;copy_index<=0;copy_word<=0;end
            end
            if(copy_write)begin
                if(copy_word==4&&copy_index==7)begin
                    context_valid[stage_voice]<=1;prepared[stage_voice]<=1;
                    copying<=0;
                end else if(copy_word==4)begin copy_index<=copy_index+1'b1;copy_word<=0;end
                else copy_word<=copy_word+1'b1;
            end
            if(fire)begin
                if(validated_result==0)case(op)
                    8'h10:begin
                        stage_valid<=1;stage_voice<=voice;stage_gen<=gen;stage_revision<=rev;last_revision<=rev;
                        stage_mode<=flags[1:0];stage_liveness<=ENABLE_AE350_FDC&&flags[2];context_mask<=0;params_mask<=0;params_stage<=0;
                        if(flags[1:0]==0)begin generations[voice]<=gen;state[voice]<=V_WAIT;live[voice]<=0;prepared[voice]<=0;off_pass[voice]<=control_pass_count;idle_pending[voice]<=0;end
                    end
                    8'h11:begin
                        if(flags[3])context_stage[flags[2:0]][89:64]<=payload[25:0];
                        else context_stage[flags[2:0]][63:0]<=payload;
                        context_mask[{flags[2:0],flags[3]}]<=1;
                    end
                    8'h12:begin params_stage[flags[1:0]*64+:64]<=payload;params_mask[flags[1:0]]<=1;end
                    8'h13:begin stage_valid<=0;commit_done<=0;end
                    8'h01,8'h04:begin
                        if(stage_valid&&stage_voice==voice&&stage_gen==gen)begin stage_valid<=0;copying<=0;commit_done<=0;end
                        if(op==4||state[voice]==V_WAIT||state[voice]==V_IDLE)begin state[voice]<=V_IDLE;live[voice]<=0;prepared[voice]<=0;idle_pending[voice]<=1;end
                        else if(state[voice]==V_HELD)state[voice]<=V_RELEASE;
                    end
                    8'h15:wheel_vibrato_depth<=$signed(rev[15:0]);
                    default:;
                endcase
            end
        end
    end
// synthesis translate_off
`ifndef SYNTHESIS
    // read_index is the original S1 lane address, meaningful only at 0..255.
    // The fixed 32x8/768 schedule is an internal contract, not a CPU ABI.
    always @(posedge clk)if(rst_n)begin
        if(!schedule_primed_q)begin
            assert(context_valid==0&&prepared==0&&live==0&&!stage_valid&&!copying&&!publish_pending)
                else $fatal(1,"prepared SUB owner released with unprimed live state");
        end
        if(schedule_primed_q&&slot<256)begin
            assert(read_index==slot[7:0]&&read_select_q==(32'd1<<read_index[7:3]))
                else $fatal(1,"prepared SUB scalar/native S1 address mismatch");
        end
        if(schedule_primed_q)begin
            assert(copy_window_q==(slot>=320&&slot<700))
                else $fatal(1,"prepared SUB registered copy window mismatch");
            assert(start_window_q==(slot>=320&&slot<660))
                else $fatal(1,"prepared SUB registered start window mismatch");
        end
        if(publish_pending)begin
            assert(slot>=320&&slot<700&&publish_select_q==(32'd1<<stage_voice))
                else $fatal(1,"prepared SUB publication escaped safe window");
            assert(stage_mode==2||(copy_write&&copy_index==7&&copy_word==4))
                else $fatal(1,"prepared SUB context/scalar publication skew");
        end
    end
`endif
// synthesis translate_on
endmodule
`default_nettype wire
