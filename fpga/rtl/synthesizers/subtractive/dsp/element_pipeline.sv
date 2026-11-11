// Copyright (c) 2026 ICECREAM1230
//--------------------------------------------------------------------
// element_pipeline.sv — coordinator for the 256-element DDS renderer.
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
//
// S1 param/state reads -> S2 decode -> S3/S3B/S3C DDS and coefficients
// -> S3D resonance multiply -> 17-stage SVF -> S9B/S10 stereo gain
// -> S11 state writeback and mix. Acceptance to writeback: 33 clocks.
// Bank swap stays at slot 512; the last element writes at slot 288.
// The master limiter publishes PCM 10 clocks after sample_tick.
// See docs/dds_module_architecture.md for responsibilities and seams.
//--------------------------------------------------------------------
`default_nettype none
module element_pipeline #(
    parameter bit ENABLE_AE350_VOICE_OWNER = 1'b0,
    parameter bit ENABLE_AE350_FDC = 1'b0,
    parameter integer FDC_WATCHDOG_CYCLES = 36_867_000,
    parameter bit ENABLE_MIDI = 1'b0,
    parameter bit REGISTERED_EVENT_INPUT = 1'b0,
    parameter bit EXTERNAL_DDS = 1'b0,
    parameter bit LOCAL_CHORD_CONTROL = 1'b0,
    parameter bit LOCAL_MIDI_BOOT = 1'b0,
    parameter int NUM_ELEMENTS = synth_pkg::NUM_ELEMENTS,
    // Boot parameter images. Synthesis uses the tree's generated
    // images; testbenches override with rtl/tb/ref_boot_* (committed
    // fixtures) so bench expectations never depend on bench-local
    // experiments in scripts/gen_boot_image.py.
    parameter P0_HEX = "rtl/synthesizers/subtractive/data/boot_p0.hex",
    parameter P1_HEX = "rtl/synthesizers/subtractive/data/boot_p1.hex",
    parameter P2_HEX = "rtl/synthesizers/subtractive/data/boot_p2.hex",
    parameter P3_HEX = "rtl/synthesizers/subtractive/data/boot_p3.hex",
    parameter bit SPLIT_MIDI_ENABLE = 1'b0
) (

    input wire ae350_cmd_valid_i,output wire ae350_cmd_ready_o,
    output wire [1:0] ae350_cmd_result_o,
    input wire [159:0] ae350_cmd_data_i,input wire [31:0] ae350_cmd_sequence_i,
    output wire ae350_fb_valid_o,input wire ae350_fb_ready_i,
    output wire [4:0] ae350_fb_voice_o,output wire [31:0] ae350_fb_generation_o,
    output wire ae350_ready_o,
    input wire logic           clk,
    input wire logic           rst_n,

    input wire logic [9:0]     slot,
    input wire logic           lane_enter,
    input wire logic           sample_tick,
    input wire legacy_chord_play_i,

    // Per-element parameter writes from the SPI control plane.
    // sclk-domain write port on the param RAMs; the pipeline reads on
    // clk (sysclk) — the dual-clock BSRAM is the CDC (design doc).
    //
    // PING-PONG (memory map decision 7): the banks are doubled —
    // reads always hit the ACTIVE half, writes always the SHADOW half,
    // so a read and a write can never collide on one address (the
    // click-on-sweep bug). swap_req (an sclk-domain toggle from
    // CTRL@0x0002) flips the active half at drum slot 512: the
    // pipeline is drained there (last writeback is slot 288), so every sample's
    // 256 elements read one consistent bank generation.
    input wire logic           sclk,
    input wire logic           elem_write_enable,
    input wire logic [2:0]     elem_write_word,     // 0..6 = p0..p3, GATE, PTRS0, PTRS1

    // Bus-write mailbox from spi_bus (sclk-domain toggle + payload).
    // Synced here and committed to bus RAM only in an idle drum slot,
    // so a commit never collides with a lane's bus read.
    input wire logic [9:0]     dmem_wr_addr,
    input wire logic [17:0]    dmem_wr_data,
    input wire logic           dmem_wr_toggle,
    output wire                dmem_wr_ack,
    output wire [15:0]         control_pass_count,
    output wire                bank_active_status,

    // Instruction table writes (sclk domain, banked — wiring per law 4)
    input wire logic           imem_write_enable,
    input wire logic [9:0]     imem_write_addr,     // {entry[7:0], word[1:0]} (#100)
    input wire logic [31:0]    imem_write_data,
    input wire logic [7:0]     elem_write_index,
    input wire logic [31:0]    elem_write_data,
    input wire logic           swap_req,    // sclk-domain toggle

    input wire midi_enable,
    input midi_pkg::midi_event_t midi_event,
    input wire midi_event_valid,
    input wire midi_capture_pending,
    output wire midi_event_ready,
    input wire [3:0] midi_flush,
    input wire perf_write_enable,
    input wire [15:0] perf_write_addr,
    input wire [31:0] perf_write_data,
    output wire [31:0] midi_template_status,

    output logic signed [23:0] mix_left,    // Q0.24, published 10 cycles
                                             // after sample_tick (S11
                                             // limiter, #121); stable by
                                             // the next tick, which is when
                                             // every consumer latches it
    output logic signed [23:0] mix_right,

    // Test-tone control (audio-chain purity check, issue #81): bus
    // address 1023 is reserved as a control latch — bit 0 enables the
    // top-level 187.5 Hz full-scale sine that replaces the mix at the
    // outputs. Latched here because the bus mailbox already has the
    // sclk→sysclk CDC; no pointer ever references bus 1023.
    output logic           test_tone_en,
    output wire dds_req_valid,
    output wire [13:0] dds_req_pitch,
    output wire [23:0] dds_req_phase,dds_req_duty,
    output wire [1:0] dds_req_wave,
    input wire dds_resp_valid,
    input wire [23:0] dds_resp_phase,
    input wire signed [17:0] dds_resp_sample,
    input wire midi_render_enable, midi_csp_enable
);

    import element_types_pkg::*;
    localparam int VW = $clog2(NUM_ELEMENTS);
    wire [VW-1:0] elem_read_index = lane_enter ? slot[VW-1:0] : '0;
    wire bank_active, bank_shadow;
    assign bank_active_status = bank_active;
    element_config_t s1_config, legacy_s1_config, runtime_s1_config,native_s1_config;
    wire native_cfg_write;wire [2:0] native_cfg_word;wire [7:0] native_cfg_index;wire [31:0] native_cfg_data;
    element_pointers_t s1_pointers,legacy_s1_pointers;
    element_state_t s1_state, s10_state;
    element_modulation_t s2_modulation,csp_modulation;
    logic [VW-1:0] s1_idx;
    wire host_template_valid;
    wire prepared_global_active;wire [319:0] prepared_global_config;
    logic prepared_audio_active;
    wire signed [17:0] local_gain;
    logic local_program_active,local_audio_active;
    // Program changes begin on a complete CSP pass. Its output generation
    // becomes readable one frame later, so pointers/modulation follow then.
    always_ff @(posedge clk or negedge rst_n)
        if(!rst_n)begin local_program_active<=LOCAL_MIDI_BOOT;local_audio_active<=LOCAL_MIDI_BOOT;prepared_audio_active<=0;end
        else if(sample_tick)begin
            local_program_active<=LOCAL_MIDI_BOOT&&!host_template_valid;
            local_audio_active<=local_program_active;
            prepared_audio_active<=ENABLE_AE350_VOICE_OWNER&&prepared_global_active;
        end
    wire [31:0] runtime_gate, runtime_tail_done;
    wire [31:0] fdc_sensor_owned;wire [543:0] fdc_gain;wire [1023:0] fdc_generation;
    wire fdc_alive;
    wire [575:0] runtime_amp, runtime_mod;
    wire signed [17:0] runtime_bend_s1;
    wire signed [19:0] prepared_cutoff_bend_s1;
    wire signed [17:0] prepared_touch_cutoff_s1,prepared_touch_resonance_s1;
    wire [7:0] runtime_keytrack_s1;
    wire [7:0] runtime_wheel_gain_s1;
    wire signed [17:0] wheel_wave_s2;
    wire signed [6:0] runtime_touch_x_s1,runtime_touch_y_s1;
    wire signed [15:0] runtime_touch_cutoff_depth_s1,runtime_touch_resonance_depth_s1;
    wire signed [15:0] wheel_vibrato_depth;
    // Default users retain the original single enable, including unconnected
    // optional ports. Production branches are same-edge equivalents.
    wire runtime_enabled = (ENABLE_MIDI || ENABLE_AE350_VOICE_OWNER) && (SPLIT_MIDI_ENABLE ? midi_render_enable : midi_enable);
    wire csp_midi_enabled = (ENABLE_MIDI || ENABLE_AE350_VOICE_OWNER) && (SPLIT_MIDI_ENABLE ? midi_csp_enable : midi_enable);
    generate if(ENABLE_AE350_VOICE_OWNER)begin:g_ae350_runtime
        assign midi_event_ready=1'b1;
        ae350_voice_backend_sub #(.LOCAL_MIDI_BOOT(LOCAL_MIDI_BOOT),.ENABLE_AE350_FDC(ENABLE_AE350_FDC),
            .FDC_WATCHDOG_CYCLES(FDC_WATCHDOG_CYCLES)) u_runtime(
            .clk(clk),.rst_n(rst_n),.sclk(sclk),.enable(1'b1),.slot(slot),
            .sample_tick(sample_tick),.global_config_o(prepared_global_config),.global_active_o(prepared_global_active),
            .cmd_valid_i(ae350_cmd_valid_i),.cmd_ready_o(ae350_cmd_ready_o),.cmd_result_o(ae350_cmd_result_o),.cmd_data_i(ae350_cmd_data_i),.ready_o(ae350_ready_o),
            .fb_valid_o(ae350_fb_valid_o),.fb_ready_i(ae350_fb_ready_i),.fb_voice_o(ae350_fb_voice_o),.fb_generation_o(ae350_fb_generation_o),
            .control_pass_count(control_pass_count),.read_index(8'(elem_read_index)),
            .perf_write_enable(perf_write_enable),.perf_write_addr(perf_write_addr),.perf_write_data(perf_write_data),
            .template_status(midi_template_status),.host_template_valid_o(host_template_valid),.native_config_i(native_s1_config),
            .native_cfg_write_o(native_cfg_write),.native_cfg_word_o(native_cfg_word),.native_cfg_index_o(native_cfg_index),.native_cfg_data_o(native_cfg_data),.runtime_config(runtime_s1_config),
            .runtime_bend(runtime_bend_s1),.runtime_cutoff_bend(prepared_cutoff_bend_s1),.runtime_keytrack(runtime_keytrack_s1),
            .runtime_touch_cutoff_delta(prepared_touch_cutoff_s1),.runtime_touch_resonance_delta(prepared_touch_resonance_s1),
            .runtime_wheel_gain(runtime_wheel_gain_s1),.runtime_touch_x(runtime_touch_x_s1),.runtime_touch_y(runtime_touch_y_s1),
            .runtime_touch_cutoff_depth(runtime_touch_cutoff_depth_s1),.runtime_touch_resonance_depth(runtime_touch_resonance_depth_s1),
            .wheel_vibrato_depth(wheel_vibrato_depth),.voice_gate(runtime_gate),.amp_depth(runtime_amp),.mod_depth(runtime_mod),.tail_done(runtime_tail_done),
            .fdc_sensor_owned_o(fdc_sensor_owned),.fdc_gain_o(fdc_gain),.fdc_generation_o(fdc_generation),.fdc_alive_o(fdc_alive));
    end else if (ENABLE_MIDI) begin : g_midi
        assign prepared_global_active=1'b0;assign prepared_global_config=0;
        midi_dds_performance #(.REGISTERED_EVENT_INPUT(REGISTERED_EVENT_INPUT),.LOCAL_MIDI_BOOT(LOCAL_MIDI_BOOT)) u_runtime (
            .clk(clk), .rst_n(rst_n), .sclk(sclk), .enable(midi_enable), .slot(slot),
            .control_pass_count(control_pass_count), .read_index(8'(elem_read_index)),
            .perf_write_enable(perf_write_enable), .perf_write_addr(perf_write_addr),
            .perf_write_data(perf_write_data), .midi_event(midi_event),
            .midi_event_valid(midi_event_valid), .midi_event_ready(midi_event_ready),
            .midi_capture_pending(midi_capture_pending),
            .midi_flush(midi_flush), .template_status(midi_template_status),
            .host_template_valid_o(host_template_valid),
            .runtime_config(runtime_s1_config), .runtime_bend(runtime_bend_s1),
            .runtime_cutoff_bend(), .runtime_keytrack(runtime_keytrack_s1),
            .runtime_wheel_gain(runtime_wheel_gain_s1),.runtime_touch_x(runtime_touch_x_s1),.runtime_touch_y(runtime_touch_y_s1),
            .runtime_touch_cutoff_depth(runtime_touch_cutoff_depth_s1),.runtime_touch_resonance_depth(runtime_touch_resonance_depth_s1),
            .wheel_vibrato_depth(wheel_vibrato_depth),
            .voice_gate(runtime_gate), .amp_depth(runtime_amp), .mod_depth(runtime_mod),
            .tail_done(runtime_tail_done)
        );
    end else begin : g_legacy
        assign midi_event_ready = 1'b0;
        assign midi_template_status = 32'd0;
        assign host_template_valid = 1'b0;
        assign prepared_global_active=1'b0;assign prepared_global_config=0;
        assign runtime_s1_config = '0;
        assign runtime_bend_s1 = 18'sd0;
        assign runtime_keytrack_s1 = 8'd64;
        assign runtime_wheel_gain_s1=0;
        assign runtime_touch_x_s1=0;assign runtime_touch_y_s1=0;
        assign runtime_touch_cutoff_depth_s1=0;assign runtime_touch_resonance_depth_s1=0;
        assign wheel_vibrato_depth=0;
        assign runtime_gate = '0;
        assign runtime_amp = '0;
        assign runtime_mod = '0;
    end endgenerate
    generate if(!ENABLE_AE350_VOICE_OWNER)begin:g_no_ae350
        assign native_cfg_write=0;assign native_cfg_word=0;assign native_cfg_index=0;assign native_cfg_data=0;
        assign ae350_cmd_result_o=0;assign prepared_cutoff_bend_s1=0;assign prepared_touch_cutoff_s1=0;assign prepared_touch_resonance_s1=0;
        assign ae350_cmd_ready_o=0;assign ae350_ready_o=0;assign ae350_fb_valid_o=0;
        assign ae350_fb_voice_o=0;assign ae350_fb_generation_o=0;
    end endgenerate

    // Local keys gate only the power-up/legacy configuration. They never
    // rewrite either bank or mask the independent MIDI runtime configuration.
    element_config_t local_legacy_config;
    always_comb begin
        local_legacy_config=legacy_s1_config;
        if(LOCAL_CHORD_CONTROL && !legacy_chord_play_i)begin
            local_legacy_config.volume_l=0;
            local_legacy_config.volume_r=0;
        end
    end
    assign s1_config = (ENABLE_AE350_VOICE_OWNER || runtime_enabled) ? runtime_s1_config : local_legacy_config;
    // MIDI routing is fixed by the existing 32 voices x 8 elements ABI.
    // It must not depend on ESP32 clearing/rebuilding the legacy banks.
    // s1_idx is registered on the same edge as the parameter/context read.
    always_comb begin
        s1_pointers=legacy_s1_pointers;
        if(ENABLE_AE350_VOICE_OWNER || (LOCAL_MIDI_BOOT && csp_midi_enabled))begin
            s1_pointers='0;
            s1_pointers.gain_l=9'd16+9'(s1_idx[7:3]);
            s1_pointers.gain_r=9'd16+9'(s1_idx[7:3]);
            if(!local_audio_active||prepared_audio_active)begin
                s1_pointers.pitch=9'd2;s1_pointers.duty=9'd1;
                s1_pointers.cutoff=9'd48+9'(s1_idx[7:3]);
                s1_pointers.resonance=9'd3;
            end
        end
    end
    always_comb begin
        s2_modulation=csp_modulation;
        if(LOCAL_MIDI_BOOT && runtime_enabled && local_audio_active&&!prepared_audio_active)begin
            // The local gain cache has no SPI write path. Uncommitted bus
            // uploads and the null bus cannot alter the default profile.
            s2_modulation='0;
            s2_modulation.gain_l=local_gain;s2_modulation.gain_r=local_gain;
        end
    end
    logic s1_act;
    wire s10_act;
    wire [VW-1:0] s10_idx;
    wire signed [17:0] s10_outl, s10_outr;
    wire signed [25:0] mix_l_acc, mix_r_acc;

    element_param_bank #(.AE_NATIVE_WRITE(ENABLE_AE350_VOICE_OWNER),.NUM_ELEMENTS(NUM_ELEMENTS),
        .P0_HEX(P0_HEX),.P1_HEX(P1_HEX),.P2_HEX(P2_HEX),.P3_HEX(P3_HEX)) u_param_bank(
        .clk,.rst_n,.sclk,.slot,.read_index(elem_read_index),
        .elem_write_enable(ENABLE_AE350_VOICE_OWNER?native_cfg_write:elem_write_enable),
        .elem_write_word(ENABLE_AE350_VOICE_OWNER?native_cfg_word:elem_write_word),
        .elem_write_index(ENABLE_AE350_VOICE_OWNER?native_cfg_index:elem_write_index),
        .elem_write_data(ENABLE_AE350_VOICE_OWNER?native_cfg_data:elem_write_data),
        .swap_req,.bank_active,.bank_shadow,.config_out(native_s1_config),.pointers_out(legacy_s1_pointers));
    assign legacy_s1_config=native_s1_config;
    element_state_store #(.NUM_ELEMENTS(NUM_ELEMENTS)) u_state_store (
        .clk(clk), .read_index(elem_read_index), .state_out(s1_state),
        .write_enable(s10_act), .write_index(s10_idx), .state_in(s10_state)
    );
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin s1_act <= 1'b0; s1_idx <= '0; end
        else begin s1_act <= lane_enter; s1_idx <= slot[VW-1:0]; end
    end

    // Control sources remain separate from the audio renderer. S1
    // pointer addresses produce CSP data alongside the S2 capture.
    csp #(.ENABLE_MIDI(ENABLE_MIDI),.LOCAL_MIDI_BOOT(LOCAL_MIDI_BOOT),.ENABLE_EXPRESSION(ENABLE_MIDI),.ENABLE_PREPARED_PATCH(ENABLE_AE350_VOICE_OWNER)) u_csp (
        .local_program_active_i(local_program_active),.local_gain_o(local_gain),
        .prepared_patch_valid_i(prepared_global_active),.prepared_patch_i(prepared_global_config),
        .midi_enable(csp_midi_enabled), .midi_voice_gate(runtime_gate),
        .midi_amp_depth(runtime_amp), .midi_mod_depth(runtime_mod), .midi_tail_done(runtime_tail_done),
        .slot(slot), .dmem_wr_ack(dmem_wr_ack), .control_pass_count(control_pass_count),
        .clk(clk), .rst_n(rst_n), .sample_tick(sample_tick), .sclk(sclk),
        .bank_active(bank_active), .bank_shadow(bank_shadow),
        .dmem_wr_addr(dmem_wr_addr), .dmem_wr_data(dmem_wr_data),
        .dmem_wr_toggle(dmem_wr_toggle), .imem_write_enable(imem_write_enable),
        .imem_write_addr(imem_write_addr), .imem_write_data(imem_write_data),
        .rd_pitch_a(s1_pointers.pitch), .rd_duty_a(s1_pointers.duty),
        .rd_fc_a(s1_pointers.cutoff), .rd_q_a(s1_pointers.resonance),
        .rd_gl_a(s1_pointers.gain_l), .rd_gr_a(s1_pointers.gain_r),
        .rd_pitch_d(csp_modulation.pitch), .rd_duty_d(csp_modulation.duty),
        .rd_fc_d(csp_modulation.cutoff), .rd_q_d(csp_modulation.resonance),
        .rd_gl_d(csp_modulation.gain_l), .rd_gr_d(csp_modulation.gain_r),
        .test_tone_en(test_tone_en),
        .midi_wheel_depth(wheel_vibrato_depth),.midi_wheel_wave_o(wheel_wave_s2)
    );
    element_renderer #(.IDXW(VW), .ENABLE_MIDI(ENABLE_MIDI),
        .PREPARED_CONTROL(ENABLE_AE350_VOICE_OWNER),.PIPELINED_KEYTRACK_BEND(ENABLE_MIDI),.EXTERNAL_DDS(EXTERNAL_DDS),.ENABLE_EXPRESSION(ENABLE_MIDI)) u_renderer (
        .clk(clk), .rst_n(rst_n), .in_valid(s1_act), .in_index(s1_idx),
        .config_in(s1_config), .state_in(s1_state), .modulation_in(s2_modulation),
        .runtime_enable_in(runtime_enabled), .runtime_bend_in(runtime_bend_s1),
        .runtime_cutoff_bend_in(prepared_cutoff_bend_s1),
        .prepared_touch_cutoff_in(prepared_touch_cutoff_s1),.prepared_touch_resonance_in(prepared_touch_resonance_s1), .runtime_keytrack_in(runtime_keytrack_s1),
        .runtime_wheel_gain_in(runtime_wheel_gain_s1),.runtime_wheel_wave_in(wheel_wave_s2),
        .runtime_touch_x_in(runtime_touch_x_s1),.runtime_touch_y_in(runtime_touch_y_s1),
        .runtime_touch_cutoff_depth_in(runtime_touch_cutoff_depth_s1),.runtime_touch_resonance_depth_in(runtime_touch_resonance_depth_s1),
        .out_valid(s10_act), .out_index(s10_idx), .sample_left(s10_outl),
        .sample_right(s10_outr), .state_out(s10_state),
        .dds_req_valid,.dds_req_pitch,.dds_req_phase,.dds_req_duty,.dds_req_wave,
        .dds_resp_valid,.dds_resp_phase,.dds_resp_sample
    );
    wire mix_input_valid;
    wire signed [17:0] mix_input_left,mix_input_right;
    generate if(ENABLE_AE350_FDC && ENABLE_AE350_VOICE_OWNER)begin:g_fdc_mix
        wire [4:0] rendered_voice=5'(s10_idx>>3);
        wire sensor=fdc_sensor_owned[rendered_voice];
        wire [16:0] gain=fdc_alive?fdc_gain[rendered_voice*17+:17]:17'd0;
        wire [16:0] effective_gain=(!sensor||gain[16])?17'd65536:gain;
        logic input_valid_q;
        logic signed [17:0] left_q,right_q;
        logic [16:0] gain_q;
        // Finish voice selection and the unity clamp before the DSP input.
        // Capture coefficient and both channels from the same S10 element;
        // bubbles hold the payload while valid still advances every clock.
        always_ff @(posedge clk) begin
            if(!rst_n)begin
                input_valid_q<=1'b0;
                left_q<='0;right_q<='0;gain_q<='0;
            end else begin
                input_valid_q<=s10_act;
                if(s10_act)begin
                    left_q<=s10_outl;right_q<=s10_outr;gain_q<=effective_gain;
                end
            end
        end
        // State writeback remains S10; only the mix stream gains three clocks.
        // Native commits publish in idle slots >=320, after all 256 rendered
        // elements and the capture/multiply/round stages have drained (<slot300).
        stereo_expression_gain #(.SAMPLE_W(18)) u_sensor_gain(
            .clk_i(clk),.rst_ni(rst_n),.in_valid_i(input_valid_q),
            .left_i(left_q),.right_i(right_q),.gain_q_i(gain_q),
            .mute_i(1'b0),.bypass_i(1'b0),.out_valid_o(mix_input_valid),
            .left_o(mix_input_left),.right_o(mix_input_right));
        // synthesis translate_off
        initial begin
            assert(synth_pkg::LANE_BASE+NUM_ELEMENTS+synth_pkg::LANE_LATENCY+3<=320)
                else $fatal(1,"FDC gain pipeline overlaps native publication");
        end
        always @(posedge clk)if(rst_n)begin
            assert(!(sample_tick&&mix_input_valid))else $fatal(1,"FDC mix overlaps sample tick");
            if(slot==512)assert(!mix_input_valid)else $fatal(1,"FDC mix not drained at swap");
            if(slot>=320&&slot<700)assert(!mix_input_valid)else $fatal(1,"FDC mix overlaps native publication");
        end
        // synthesis translate_on
    end else begin:g_no_fdc_mix
        assign mix_input_valid=s10_act;assign mix_input_left=s10_outl;assign mix_input_right=s10_outr;
    end endgenerate
    // On sample_tick, the limiter captures the pre-clear accumulator;
    // the mixer starts the next sample on that same edge, as before.
    stereo_mixer u_mixer (
        .clk(clk), .rst_n(rst_n), .sample_tick(sample_tick), .in_act(mix_input_valid),
        .in_left(mix_input_left), .in_right(mix_input_right), .sum_left(mix_l_acc), .sum_right(mix_r_acc)
    );
    master_limiter u_master_limiter (
        .clk(clk), .rst_n(rst_n), .sample_tick(sample_tick),
        .sum_left(mix_l_acc), .sum_right(mix_r_acc), .pcm_left(mix_left), .pcm_right(mix_right)
    );

// synthesis translate_off
`ifndef SYNTHESIS
    // Simulation-only schedule contract. A future stage change must
    // update shared metadata before the bank swap can rely on it.
    logic [synth_pkg::LANE_LATENCY-1:0] lane_valid_history;
    initial begin
        assert (synth_pkg::LANE_BASE + NUM_ELEMENTS + synth_pkg::LANE_LATENCY
                <= synth_pkg::SWAP_SLOT)
            else $fatal(1, "lane writeback span overlaps bank swap");
    end
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) lane_valid_history <= '0;
        else if (rst_n === 1'b1) begin
            assert (s10_act == lane_valid_history[synth_pkg::LANE_LATENCY-1])
                else $fatal(1, "lane latency differs from shared metadata");
            if (slot == synth_pkg::SWAP_SLOT) begin
                assert (!lane_enter && !s10_act && !(|lane_valid_history))
                    else $fatal(1, "bank swap slot reached before lane drain");
            end
            assert (!(sample_tick && s10_act))
                else $fatal(1, "sample tick overlaps unfinished mix writeback");
            lane_valid_history <= {lane_valid_history[synth_pkg::LANE_LATENCY-2:0], lane_enter};
        end
    end
`endif
// synthesis translate_on
endmodule
