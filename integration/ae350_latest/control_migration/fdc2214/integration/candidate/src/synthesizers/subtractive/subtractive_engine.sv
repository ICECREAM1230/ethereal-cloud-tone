// Thin adapter; original core and parameter semantics are preserved.
`default_nettype none
module subtractive_engine #(
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
    parameter P0_HEX = "src/synthesizers/subtractive/data/boot_p0.hex",
    parameter P1_HEX = "src/synthesizers/subtractive/data/boot_p1.hex",
    parameter P2_HEX = "src/synthesizers/subtractive/data/boot_p2.hex",
    parameter P3_HEX = "src/synthesizers/subtractive/data/boot_p3.hex",
    parameter bit SPLIT_MIDI_ENABLE = 1'b0,
    parameter bit PCM_REQUEST_DRIVEN = 1'b0
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
    input wire midi_render_enable, midi_csp_enable,
    // Native-compute-clock request interface. Existing ports remain the
    // legacy schedule when PCM_REQUEST_DRIVEN=0.
    input wire pcm_request_valid_i,
    input wire [63:0] pcm_request_tag_i,
    output wire pcm_request_ready_o,
    output wire [9:0] pcm_slot_o,
    output wire pcm_sample_tick_o, pcm_lane_enter_o,
    output wire pcm_response_valid_o,
    output wire [63:0] pcm_response_tag_o,
    output wire signed [23:0] pcm_response_left_o, pcm_response_right_o
);
    wire [9:0] pipeline_slot;
    wire pipeline_lane_enter, pipeline_sample_tick;
    wire [63:0] pipeline_frame_tag;
    assign pcm_slot_o=pipeline_slot;
    assign pcm_sample_tick_o=pipeline_sample_tick;
    assign pcm_lane_enter_o=pipeline_lane_enter;
    generate if (PCM_REQUEST_DRIVEN) begin : g_pcm_request
        pcm_request_drum #(.NUM_LANES(NUM_ELEMENTS)) u_schedule (
            .clk_i(clk),.rst_n_i(rst_n),
            .request_valid_i(pcm_request_valid_i),.request_tag_i(pcm_request_tag_i),
            .request_ready_o(pcm_request_ready_o),.frame_tag_o(pipeline_frame_tag),
            .slot_o(pipeline_slot),.sample_tick_o(pipeline_sample_tick),.lane_enter_o(pipeline_lane_enter)
        );
        // The frame edge captures the completed PREVIOUS accumulator. Its
        // limiter result is published ten compute clocks later. Never label
        // the first empty accumulator as a requested rendered sample.
        logic rendered_once, publication_owned;
        logic [63:0] rendered_tag, publication_tag, response_tag;
        logic [3:0] publication_delay;
        logic response_valid;
        always_ff @(posedge clk or negedge rst_n) begin
            if (!rst_n) begin
                rendered_once<=0;publication_owned<=0;rendered_tag<=0;
                publication_tag<=0;publication_delay<=0;response_tag<=0;response_valid<=0;
            end else begin
                response_valid<=0;
                if (publication_delay!=0) begin
                    publication_delay<=publication_delay-4'd1;
                    if (publication_delay==1) begin
                        response_valid<=publication_owned;response_tag<=publication_tag;
                        publication_owned<=0;
                    end
                end
                if (pipeline_sample_tick) begin
                    publication_tag<=rendered_tag;publication_owned<=rendered_once;
                    publication_delay<=4'd10;rendered_tag<=pipeline_frame_tag;rendered_once<=1;
                end
            end
        end
        assign pcm_response_valid_o=response_valid;
        assign pcm_response_tag_o=response_tag;
        assign pcm_response_left_o=mix_left;
        assign pcm_response_right_o=mix_right;
    end else begin : g_pcm_legacy
        assign pipeline_slot=slot;
        assign pipeline_lane_enter=lane_enter;
        assign pipeline_sample_tick=sample_tick;
        assign pipeline_frame_tag=0;
        assign pcm_request_ready_o=0;
        assign pcm_response_valid_o=0;
        assign pcm_response_tag_o=0;
        assign pcm_response_left_o=0;
        assign pcm_response_right_o=0;
    end endgenerate

    element_pipeline #(.ENABLE_AE350_VOICE_OWNER(ENABLE_AE350_VOICE_OWNER),
        .ENABLE_AE350_FDC(ENABLE_AE350_FDC),.FDC_WATCHDOG_CYCLES(FDC_WATCHDOG_CYCLES),
        .ENABLE_MIDI(ENABLE_MIDI),
        .SPLIT_MIDI_ENABLE(SPLIT_MIDI_ENABLE),
        .REGISTERED_EVENT_INPUT(REGISTERED_EVENT_INPUT),
        .EXTERNAL_DDS(EXTERNAL_DDS),
        .LOCAL_CHORD_CONTROL(LOCAL_CHORD_CONTROL),
        .LOCAL_MIDI_BOOT(LOCAL_MIDI_BOOT),
        .NUM_ELEMENTS(NUM_ELEMENTS),
        .P0_HEX(P0_HEX),
        .P1_HEX(P1_HEX),
        .P2_HEX(P2_HEX),
        .P3_HEX(P3_HEX)
    ) u_pipeline(.ae350_cmd_valid_i(ae350_cmd_valid_i),.ae350_cmd_ready_o(ae350_cmd_ready_o),
        .ae350_cmd_data_i(ae350_cmd_data_i),.ae350_cmd_sequence_i(ae350_cmd_sequence_i),.ae350_cmd_result_o(ae350_cmd_result_o),
        .ae350_fb_valid_o(ae350_fb_valid_o),.ae350_fb_ready_i(ae350_fb_ready_i),
        .ae350_fb_voice_o(ae350_fb_voice_o),.ae350_fb_generation_o(ae350_fb_generation_o),.ae350_ready_o(ae350_ready_o),
        .clk(clk),
        .rst_n(rst_n),
        .slot(pipeline_slot),
        .lane_enter(pipeline_lane_enter),
        .sample_tick(pipeline_sample_tick),
        .legacy_chord_play_i(legacy_chord_play_i),
        .sclk(sclk),
        .elem_write_enable(elem_write_enable),
        .elem_write_word(elem_write_word),
        .dmem_wr_addr(dmem_wr_addr),
        .dmem_wr_data(dmem_wr_data),
        .dmem_wr_toggle(dmem_wr_toggle),
        .dmem_wr_ack(dmem_wr_ack),
        .control_pass_count(control_pass_count),
        .bank_active_status(bank_active_status),
        .imem_write_enable(imem_write_enable),
        .imem_write_addr(imem_write_addr),
        .imem_write_data(imem_write_data),
        .elem_write_index(elem_write_index),
        .elem_write_data(elem_write_data),
        .swap_req(swap_req),
        .midi_enable(midi_enable),
        .midi_render_enable(midi_render_enable), .midi_csp_enable(midi_csp_enable),
        .midi_event(midi_event),
        .midi_event_valid(midi_event_valid),
        .midi_capture_pending(midi_capture_pending),
        .midi_event_ready(midi_event_ready),
        .midi_flush(midi_flush),
        .perf_write_enable(perf_write_enable),
        .perf_write_addr(perf_write_addr),
        .perf_write_data(perf_write_data),
        .midi_template_status(midi_template_status),
        .mix_left(mix_left),
        .mix_right(mix_right),
        .test_tone_en(test_tone_en),
        .dds_req_valid(dds_req_valid),
        .dds_req_pitch(dds_req_pitch),
        .dds_req_phase(dds_req_phase),
        .dds_req_duty(dds_req_duty),
        .dds_req_wave(dds_req_wave),
        .dds_resp_valid(dds_resp_valid),
        .dds_resp_phase(dds_resp_phase),
        .dds_resp_sample(dds_resp_sample)
    );
endmodule
`default_nettype wire
