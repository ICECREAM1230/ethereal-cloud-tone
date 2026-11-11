#!/usr/bin/env python3
"""Create integration copies only; never writes current production sources."""
from pathlib import Path
import difflib,hashlib,json
root=Path(__file__).resolve().parents[3]
out=Path(__file__).resolve().parent/'candidate'
required=['control_migration/rtl/ae350_voice_backend_sub.sv',
          'src/synthesizers/subtractive/dsp/element_pipeline.sv',
          'src/synthesizers/subtractive/subtractive_engine.sv',
          'src/synthesizers/synthesizer_manager.sv']
missing=[name for name in required if not (root/name).is_file()]
if missing:
 raise SystemExit('Historical integration generator requires the original pre-FDC baseline, not the current FPGA tree. Missing: '+', '.join(missing))
record={}
def change(name,edits):
 src=root/name;old=src.read_text();text=old
 for before,after in edits:
  assert text.count(before)==1,(name,before[:100],text.count(before))
  text=text.replace(before,after,1)
 dst=out/name;dst.parent.mkdir(parents=True,exist_ok=True);dst.write_text(text)
 record[name]={'baseline':hashlib.sha256(src.read_bytes()).hexdigest(),'candidate':hashlib.sha256(dst.read_bytes()).hexdigest()}
 return ''.join(difflib.unified_diff(old.splitlines(True),text.splitlines(True),fromfile='a/'+name,tofile='b/'+name))
patch=''
patch+=change('control_migration/rtl/ae350_voice_backend_sub.sv',[
 ("parameter bit LOCAL_MIDI_BOOT = 1'b0","parameter bit LOCAL_MIDI_BOOT = 1'b0,\n    parameter bit ENABLE_AE350_FDC = 1'b0,\n    parameter integer FDC_WATCHDOG_CYCLES = 36_867_000"),
 ('    input wire [31:0] tail_done','    output wire [31:0] fdc_sensor_owned_o,\n    output wire [543:0] fdc_gain_o,\n    output wire [1023:0] fdc_generation_o,\n    output wire fdc_alive_o,\n    input wire [31:0] tail_done'),
 ('    logic [139:0] values[0:31];','    localparam integer VALUE_WIDTH = ENABLE_AE350_FDC ? 158 : 140;\n    logic [VALUE_WIDTH-1:0] values[0:31];'),
 ('    logic [1:0] stage_mode;','    logic [1:0] stage_mode;\n    logic stage_liveness;'),
 ('if(flags>2||payload!=0||gen==0||rev==0||stage_valid)result=2;',"if((ENABLE_AE350_FDC ? (flags[7:3]!=0 || flags[1:0]==3) : flags>2)||payload!=0||gen==0||rev==0||stage_valid)result=2;"),
 ('(flags==0 ? gen<=selected_generation:!matching)','(flags[1:0]==0 ? gen<=selected_generation:!matching)'),
 ('else if(flags==1&&selected_state!=V_HELD&&selected_state!=V_WAIT)result=2;','else if(flags[1:0]==1&&selected_state!=V_HELD&&selected_state!=V_WAIT)result=2;'),
 ("if(flags>2||(flags==2&&payload[63:12]!=0))result=2;","if(flags>2 || (flags==2 && (ENABLE_AE350_FDC ?\n                    (payload[63:30]!=0 || (payload[29] ? payload[28:12]>17'd65536 : payload[28:12]!=0)) :\n                    payload[63:12]!=0)))result=2;"),
 ('    logic [139:0] read_values,selected_values;','    logic [VALUE_WIDTH-1:0] read_values,selected_values;'),
 ('values[v]&{140{read_select_q[v]}}','values[v]&{VALUE_WIDTH{read_select_q[v]}}'),
 ('if(publish_select_q[v])values[v]<=params_stage[139:0];','if(publish_select_q[v])values[v]<=params_stage[VALUE_WIDTH-1:0];'),
 ('else if(!complete_stage)result=2;','else if(!complete_stage || (ENABLE_AE350_FDC && stage_liveness && !params_stage[157]))result=2;'),
 ('stage_gen<=0;stage_revision<=0;last_revision<=0;params_stage<=0;','stage_liveness<=0;stage_gen<=0;stage_revision<=0;last_revision<=0;params_stage<=0;'),
 ('stage_mode<=flags[1:0];context_mask<=0;params_mask<=0;params_stage<=0;','stage_mode<=flags[1:0];stage_liveness<=ENABLE_AE350_FDC&&flags[2];context_mask<=0;params_mask<=0;params_stage<=0;'),
 ('if(flags==0)begin generations[voice]<=gen;','if(flags[1:0]==0)begin generations[voice]<=gen;'),
 ('    integer feedback_pick;',"""    // Identity and gain come from the same actual native COMMIT as pitch.
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
    integer feedback_pick;""")])
patch+=change('src/synthesizers/subtractive/dsp/element_pipeline.sv',[
 ("    parameter bit ENABLE_AE350_VOICE_OWNER = 1'b0,","    parameter bit ENABLE_AE350_VOICE_OWNER = 1'b0,\n    parameter bit ENABLE_AE350_FDC = 1'b0,\n    parameter integer FDC_WATCHDOG_CYCLES = 36_867_000,"),
 ('    wire [31:0] runtime_gate, runtime_tail_done;','    wire [31:0] runtime_gate, runtime_tail_done;\n    wire [31:0] fdc_sensor_owned;wire [543:0] fdc_gain;wire [1023:0] fdc_generation;\n    wire fdc_alive;'),
 ('ae350_voice_backend_sub #(.LOCAL_MIDI_BOOT(LOCAL_MIDI_BOOT)) u_runtime(','ae350_voice_backend_sub #(.LOCAL_MIDI_BOOT(LOCAL_MIDI_BOOT),.ENABLE_AE350_FDC(ENABLE_AE350_FDC),\n            .FDC_WATCHDOG_CYCLES(FDC_WATCHDOG_CYCLES)) u_runtime('),
 ('.wheel_vibrato_depth(wheel_vibrato_depth),.voice_gate(runtime_gate),.amp_depth(runtime_amp),.mod_depth(runtime_mod),.tail_done(runtime_tail_done));','.wheel_vibrato_depth(wheel_vibrato_depth),.voice_gate(runtime_gate),.amp_depth(runtime_amp),.mod_depth(runtime_mod),.tail_done(runtime_tail_done),\n            .fdc_sensor_owned_o(fdc_sensor_owned),.fdc_gain_o(fdc_gain),.fdc_generation_o(fdc_generation),.fdc_alive_o(fdc_alive));'),
 ('    // On sample_tick, the limiter captures the pre-clear accumulator;',"""    wire mix_input_valid;
    wire signed [17:0] mix_input_left,mix_input_right;
    generate if(ENABLE_AE350_FDC && ENABLE_AE350_VOICE_OWNER)begin:g_fdc_mix
        wire [4:0] rendered_voice=5'(s10_idx>>3);
        wire sensor=fdc_sensor_owned[rendered_voice];
        wire [16:0] gain=fdc_alive?fdc_gain[rendered_voice*17+:17]:17'd0;
        // State writeback remains S10; only the mix stream gains two clocks.
        // Native commits publish in idle slots >=320, after all 256 rendered
        // elements and both gain stages have drained (<slot300).
        stereo_expression_gain #(.SAMPLE_W(18)) u_sensor_gain(
            .clk_i(clk),.rst_ni(rst_n),.in_valid_i(s10_act),
            .left_i(s10_outl),.right_i(s10_outr),.gain_q_i(gain),
            .mute_i(1'b0),.bypass_i(!sensor),.out_valid_o(mix_input_valid),
            .left_o(mix_input_left),.right_o(mix_input_right));
        // synthesis translate_off
        always @(posedge clk)if(rst_n)begin
            assert(!(sample_tick&&mix_input_valid))else $fatal(1,"FDC mix overlaps sample tick");
            if(slot==512)assert(!mix_input_valid)else $fatal(1,"FDC mix not drained at swap");
            if(slot>=320&&slot<700)assert(!mix_input_valid)else $fatal(1,"FDC mix overlaps native publication");
        end
        // synthesis translate_on
    end else begin:g_no_fdc_mix
        assign mix_input_valid=s10_act;assign mix_input_left=s10_outl;assign mix_input_right=s10_outr;
    end endgenerate
    // On sample_tick, the limiter captures the pre-clear accumulator;"""),
 ('.sample_tick(sample_tick), .in_act(s10_act),\n        .in_left(s10_outl), .in_right(s10_outr)', '.sample_tick(sample_tick), .in_act(mix_input_valid),\n        .in_left(mix_input_left), .in_right(mix_input_right)')])
for name,instance in [
 ('src/synthesizers/subtractive/subtractive_engine.sv','element_pipeline'),
 ('src/synthesizers/synthesizer_manager.sv','subtractive_engine')]:
 patch+=change(name,[
  ("    parameter bit ENABLE_AE350_VOICE_OWNER = 1'b0,","    parameter bit ENABLE_AE350_VOICE_OWNER = 1'b0,\n    parameter bit ENABLE_AE350_FDC = 1'b0,\n    parameter integer FDC_WATCHDOG_CYCLES = 36_867_000,"),
  (instance+' #(.ENABLE_AE350_VOICE_OWNER(ENABLE_AE350_VOICE_OWNER),',instance+' #(.ENABLE_AE350_VOICE_OWNER(ENABLE_AE350_VOICE_OWNER),\n        .ENABLE_AE350_FDC(ENABLE_AE350_FDC),.FDC_WATCHDOG_CYCLES(FDC_WATCHDOG_CYCLES),')])
result=root/'sim/results/fdc_ae350_20261030/native_rtl';result.mkdir(parents=True,exist_ok=True)
(result/'source-baseline.json').write_text(json.dumps(record,indent=2)+'\n')
(result/'candidate.patch').write_text(patch)
print(json.dumps(record,indent=2))
