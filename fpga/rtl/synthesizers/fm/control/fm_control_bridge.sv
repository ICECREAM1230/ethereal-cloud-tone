// FM ABI 1.1 control plane. Generated contract is the wire authority.
// SPI write signals are combinational final-edge decode, never delayed strobes.
`default_nettype none
module fm_control_bridge #(
    parameter bit ENABLE_AE350_VOICE_OWNER=1'b0,
    parameter bit ENABLE_MIDI = 0,
    parameter bit ENABLE_LOCAL_SELECT = 0,
    parameter bit REGISTERED_EVENT_INPUT = 0,
    parameter bit ASYNC_READBACK = 0,
    parameter logic [31:0] BACKEND_PROFILE_ID = fm_contract_pkg::FM_BACKEND_JT51_OPM,
    parameter integer VOICE_CAPACITY = fm_contract_pkg::FM_PROFILE_JT51_OPM_VOICE_COUNT,
    parameter logic [31:0] BACKEND_MAX_OPERATOR_COUNT = fm_contract_pkg::FM_PROFILE_JT51_OPM_MAX_OPERATOR_COUNT,
    parameter logic [31:0] BACKEND_PITCH_MIN_Q16 = 32'h000d0000,
    parameter logic [31:0] BACKEND_PITCH_MAX_Q16 = 32'h006cfc00,
    parameter logic [31:0] SOURCE_RATE_NUMERATOR = 32'd4921875,
    parameter logic [31:0] SOURCE_RATE_DENOMINATOR = 32'd88,
    parameter logic [31:0] OUTPUT_RATE_NUMERATOR = 32'd91015625,
    parameter logic [31:0] OUTPUT_RATE_DENOMINATOR = 32'd948
) (
    input wire clk_i, rst_n_i, spi_clk_i,
    // Used only when control and spi_bus fetch service have different clocks.
    input wire read_clk_i,
    input wire write_enable_i,
    input wire [7:0] write_addr_i,
    input wire [31:0] write_data_i,
    input wire [7:0] read_addr_i,
    output logic [31:0] read_data_o,
    output logic command_valid_o,
    input wire command_ready_i,
    output fm_contract_pkg::fm_command_t command_o,
    input wire command_done_i,
    input wire [31:0] command_result_i,
    output logic panic_valid_o,
    input wire panic_ready_i, panic_done_i,
    input wire backend_ready_i, backend_fault_i,
    output logic selected_o, mute_o,
    output logic selection_applied_o,
    input wire midi_enable_i,
    input midi_pkg::midi_event_t midi_event_i,
    input wire midi_event_valid_i,output wire midi_event_ready_o,
    input wire midi_capture_pending_i,
    input wire [3:0] midi_flush_fence_i,
    output wire midi_event_drop_o,
    input wire [3:0] midi_flush_i,
    input wire perf_write_enable_i,input wire [15:0] perf_write_addr_i,
    input wire [31:0] perf_write_data_i,
    // Value selected_o will commit on the next clk_i edge while rst_n_i is high.
    // Observational only: lets a global-reset-only guard retain the same-edge
    // binding across this bridge's FM-local asynchronous reset.
    output logic selection_next_o,
    output wire [31:0] expression_applied_data_o,
    output wire [31:0] expression_generation_o,
    // Native control-clock request. The caller holds bind/id through ready,
    // and owns the matching completion token. FM-local reset cancels both;
    // its CDC source must abandon the request rather than replaying it.
    input wire local_select_valid_i, local_select_bind_i,
    input wire [31:0] local_select_id_i,
    output wire local_select_ready_o,
    output logic local_select_done_o,
    output logic [31:0] local_select_done_id_o, local_select_result_o,
    // Whole local engine-switch transaction, including non-FM stages. SPI
    // still consumes/ACKs ordinary mailboxes with BUSY; priority Panic stays live.
    input wire local_switch_busy_i
);
    import fm_contract_pkg::*;
    localparam logic [31:0] PROFILE = BACKEND_PROFILE_ID;
    fm_profile_info_t profile_info;
    assign profile_info = fm_profile_info(PROFILE);
    // Capacity is independent of the installed profile's actual topology.
    localparam integer VOICES = VOICE_CAPACITY;
    localparam integer VOICE_AW = VOICES > 1 ? $clog2(VOICES) : 1;
    localparam logic [31:0] FEATURES = FM_FEATURE_NOTE_ON | FM_FEATURE_NOTE_OFF |
        FM_FEATURE_SET_PITCH | FM_FEATURE_SET_GAIN_PAN | FM_FEATURE_SET_PARAMETER |
        FM_FEATURE_APPLY_PRESET | FM_FEATURE_RELEASE_ALL | FM_FEATURE_PRIORITY_PANIC |
        FM_FEATURE_NEXT_NOTE_PARAMETERS | FM_FEATURE_NATIVE_STEREO_PAN;

    // Bundled-data CDC: the source holds each payload until its ack returns.
    // The real bus provides at least 32 SCLK edges between commit words. A
    // commit while the transport itself has no free slot is not overwritten.
    fm_command_t spi_shadow, spi_command_mailbox;
    wire [VOICE_AW-1:0] incoming_voice_index = spi_command_mailbox.target_word[16 +: VOICE_AW];

    logic [31:0] spi_commit_value;
    logic command_req_spi, command_ack_sys;
    (* async_reg = "true" *) logic command_ack_meta_spi, command_ack_sync_spi;
    (* async_reg = "true" *) logic command_req_meta_sys, command_req_sync_sys;
    logic [31:0] panic_session_shadow, panic_epoch_shadow;
    logic [31:0] panic_session_mailbox, panic_epoch_mailbox, panic_sequence_mailbox;
    logic panic_req_spi, panic_ack_sys;
    (* async_reg = "true" *) logic panic_ack_meta_spi, panic_ack_sync_spi;
    (* async_reg = "true" *) logic panic_req_meta_sys, panic_req_sync_sys;
    logic [31:0] query_mailbox;
    logic query_req_spi, query_ack_sys;
    (* async_reg = "true" *) logic query_ack_meta_spi, query_ack_sync_spi;
    (* async_reg = "true" *) logic query_req_meta_sys, query_req_sync_sys;

    always_ff @(posedge spi_clk_i or negedge rst_n_i) begin
        if (!rst_n_i) begin
            spi_shadow <= '0; spi_command_mailbox <= '0; spi_commit_value <= 0;
            command_req_spi <= 0; command_ack_meta_spi <= 0; command_ack_sync_spi <= 0;
            panic_session_shadow <= 0; panic_epoch_shadow <= 1;
            panic_session_mailbox <= 0; panic_epoch_mailbox <= 1; panic_sequence_mailbox <= 0;
            panic_req_spi <= 0; panic_ack_meta_spi <= 0; panic_ack_sync_spi <= 0;
            query_mailbox <= 0; query_req_spi <= 0; query_ack_meta_spi <= 0; query_ack_sync_spi <= 0;
        end else begin
            command_ack_meta_spi <= command_ack_sys; command_ack_sync_spi <= command_ack_meta_spi;
            panic_ack_meta_spi <= panic_ack_sys; panic_ack_sync_spi <= panic_ack_meta_spi;
            query_ack_meta_spi <= query_ack_sys; query_ack_sync_spi <= query_ack_meta_spi;
            if (write_enable_i) begin
                case (FM_SPI_WORD_BASE + {24'b0,write_addr_i})
                    FM_REG_CMD_OPCODE_ID: spi_shadow.opcode_id <= write_data_i;
                    FM_REG_CMD_SCOPE_ID: spi_shadow.scope_id <= write_data_i;
                    FM_REG_CMD_TARGET_WORD: spi_shadow.target_word <= write_data_i;
                    FM_REG_CMD_PARAMETER_ID: spi_shadow.parameter_id <= write_data_i;
                    FM_REG_CMD_VALUE_U32: spi_shadow.value_u32 <= write_data_i;
                    FM_REG_CMD_ARG0_U32: spi_shadow.arg0_u32 <= write_data_i;
                    FM_REG_CMD_ARG1_U32: spi_shadow.arg1_u32 <= write_data_i;
                    FM_REG_CMD_SESSION_ID: spi_shadow.session_id <= write_data_i;
                    FM_REG_CMD_ENGINE_EPOCH: spi_shadow.engine_epoch <= write_data_i;
                    FM_REG_CMD_TOPOLOGY_GENERATION: spi_shadow.topology_generation <= write_data_i;
                    FM_REG_CMD_VOICE_GENERATION: spi_shadow.voice_generation <= write_data_i;
                    FM_REG_CMD_SEQUENCE_ID: spi_shadow.sequence_id <= write_data_i;
                    FM_REG_CMD_COMMIT: if (command_req_spi == command_ack_sync_spi) begin
                        spi_command_mailbox <= spi_shadow; spi_commit_value <= write_data_i;
                        command_req_spi <= ~command_req_spi;
                    end
                    FM_REG_PANIC_SESSION: panic_session_shadow <= write_data_i;
                    FM_REG_PANIC_EPOCH: panic_epoch_shadow <= write_data_i;
                    FM_REG_PANIC_SEQUENCE: if (!ENABLE_AE350_VOICE_OWNER && panic_req_spi == panic_ack_sync_spi) begin
                        panic_session_mailbox <= panic_session_shadow;
                        panic_epoch_mailbox <= panic_epoch_shadow;
                        panic_sequence_mailbox <= write_data_i; panic_req_spi <= ~panic_req_spi;
                    end
                    FM_REG_VOICE_QUERY_INDEX: if (query_req_spi == query_ack_sync_spi) begin
                        query_mailbox <= write_data_i; query_req_spi <= ~query_req_spi;
                    end
                    default: ;
                endcase
            end
        end
    end

/* synthesis translate_off */
`ifndef SYNTHESIS
    initial begin : capacity_check
        fm_profile_info_t installed;
        installed = fm_profile_info(PROFILE);
        if (!installed.valid || VOICE_CAPACITY < installed.voice_count || VOICE_CAPACITY > FM_MAX_DESCRIPTOR_VOICES)
            $fatal(1,"FM storage capacity does not cover installed profile");
    end
`endif
/* synthesis translate_on */
    logic [31:0] session_id, epoch, topology_generation;
    logic [31:0] voice_generation [0:VOICES-1];
    logic [31:0] query_index;
    logic command_pending, command_inflight, recovering, panic_pending, panic_inflight;
    // These predicates follow the same pending/payload writers, rather than
    // registering their old combinational values one edge later.
    logic local_barrier_q, local_release_all_q;
    logic select_previous_selected;
    logic panic_is_normal;
    fm_command_t active_command, recent_command;
    logic recent_valid;
    logic [31:0] recent_result, sequence_watermark;
    logic [31:0] accepted_sequence, accepted_session, accepted_epoch;
    logic [31:0] completed_sequence, completed_session, completed_epoch;
    logic [31:0] result_sequence, result_code, result_session, result_epoch;
    logic panic_recent_valid;
    logic [31:0] panic_recent_session, panic_recent_epoch, panic_recent_sequence, panic_recent_result;
    logic [31:0] panic_sequence_watermark;
    logic [31:0] panic_accepted_sequence, panic_completed_sequence;
    logic [31:0] panic_result_sequence, panic_result_code, panic_result_session, panic_result_epoch;
    logic [31:0] active_panic_session, active_panic_epoch, active_panic_sequence;
    // Update this identity tag beside every writer, on the same edge. The
    // status image must not put a 96-bit compare ahead of its wide change test.
    logic panic_result_is_active_q;
    wire panic_mailbox_is_active = panic_sequence_mailbox == active_panic_sequence &&
        panic_session_mailbox == active_panic_session && panic_epoch_mailbox == active_panic_epoch;
    logic cancelled_valid;
    fm_command_t cancelled_command;
    logic [31:0] phase, flags;
    wire local_valid,local_ready,local_hard_mute;
    fm_command_t local_command;
    wire [31:0] local_stamp;
    wire [7:0] local_cancel_mask;
    wire [255:0] local_voice_stamps;
    logic active_local,panic_is_local,local_panic_wait;
    logic [31:0] local_sequence,active_local_stamp,local_result,local_accepted,local_completed;
    logic active_local_select, cancelled_local_select;
    logic [31:0] active_local_select_id, cancelled_local_select_id;
    wire local_switch_lock=ENABLE_LOCAL_SELECT&&local_switch_busy_i;
    wire local_select_request=ENABLE_LOCAL_SELECT&&local_select_valid_i;
    wire local_enable,local_cancel,local_event_edge;
    // These tags are captured from the same new payload, on every writer.
    logic [3:0] active_classes_q;
    logic [7:0] active_voice_onehot_q;
    wire active_is_select=active_classes_q[3];
    wire active_is_preset=active_classes_q[2];
    wire active_is_release=active_classes_q[1];
    wire active_is_on=active_classes_q[0];
    function automatic logic [3:0] command_classes(input fm_command_t c);
      return {c.opcode_id==FM_SPI_OPCODE_SELECT_BACKEND,
              c.opcode_id==FM_SPI_OPCODE_APPLY_PRESET,
              c.opcode_id==FM_SPI_OPCODE_RELEASE_ALL,
              c.opcode_id==FM_SPI_OPCODE_NOTE_ON};
    endfunction
    wire [7:0] cancel_by_voice;
    generate for(genvar av=0;av<8;av=av+1)begin:g_active_voice_cancel
      wire [3:0] stamp_diff_bytes;
      for(genvar ab=0;ab<4;ab=ab+1)begin:g_stamp_byte
        assign stamp_diff_bytes[ab]=|(local_voice_stamps[32*av+8*ab+:8]^active_local_stamp[8*ab+:8]);
      end
      assign cancel_by_voice[av]=local_cancel_mask[av]||(|stamp_diff_bytes);
    end endgenerate
    wire owner_enable,owner_event_valid,owner_event_ready;
    wire local_submit_allowed=local_enable&&owner_enable;
    generate if(ENABLE_MIDI&&REGISTERED_EVENT_INPUT)begin:g_owner_availability
      logic available_q;
      always_ff @(posedge clk_i or negedge rst_n_i)
        if(!rst_n_i)available_q<=1'b0;
        else available_q<=local_enable;
      assign owner_enable=available_q;
    end else begin:g_direct_owner_availability
      assign owner_enable=local_enable;
    end endgenerate
    assign owner_event_valid=(ENABLE_MIDI&&REGISTERED_EVENT_INPUT)?
      midi_event_valid_i&&owner_enable:midi_event_valid_i;
    // Raw drop owns draining; when raw has recovered, wait for the registered
    // owner qualification before acknowledging an event into the deep state.
    assign midi_event_ready_o=(ENABLE_MIDI&&REGISTERED_EVENT_INPUT)?
      (midi_event_drop_o?1'b1:owner_enable&&owner_event_ready):owner_event_ready;
    wire raw_pending=(REGISTERED_EVENT_INPUT&&ENABLE_MIDI)?midi_capture_pending_i:1'b0;
    wire flush_fence=(REGISTERED_EVENT_INPUT&&ENABLE_MIDI)?(|midi_flush_fence_i):1'b0;
    wire raw_priority_pending=(REGISTERED_EVENT_INPUT&&ENABLE_MIDI)?
      panic_req_sync_sys!=panic_ack_sys:1'b0;
    // owner_enable is the preceding raw qualification. Detect that raw edge,
    // rather than issuing a delayed second Panic from the deep owner's q edge.
    wire local_hard_mute_request=(REGISTERED_EVENT_INPUT&&ENABLE_MIDI)?
      owner_enable&&!local_enable:local_hard_mute;

    function automatic logic [31:0] next_generation(input logic [31:0] current);
        return current == 32'hffffffff ? 32'd1 : current + 32'd1;
    endfunction

    // Legality is checked at full uint32 width before indexing voice storage.
    function automatic logic [31:0] command_legality(
        input fm_command_t c, input fm_parameter_info_t parameter_info,
        input fm_preset_info_t preset_info, input logic [31:0] operators);
        if (ENABLE_AE350_VOICE_OWNER && !(c.opcode_id==FM_SPI_OPCODE_SET_PARAMETER &&
            (c.scope_id==FM_SCOPE_PATCH || c.scope_id==FM_SCOPE_PATCH_OPERATOR)) &&
            c.opcode_id!=FM_SPI_OPCODE_APPLY_PRESET) return FM_RESULT_UNSUPPORTED;
        if (!fm_command_valid(c)) return FM_RESULT_BAD_VALUE;
        case (c.opcode_id)
            FM_SPI_OPCODE_SELECT_BACKEND: begin
                if (c.scope_id != FM_SCOPE_ENGINE || !fm_validate_target(c.scope_id,c.target_word,profile_info.voice_count,operators,profile_info.patch_operator_count)) return FM_RESULT_BAD_TARGET;
                if (c.parameter_id != 0 || c.arg1_u32 != 0 || c.voice_generation != 0 || c.arg0_u32 == 0) return FM_RESULT_BAD_VALUE;
                if (c.value_u32 != FM_BACKEND_UNBOUND && c.value_u32 != profile_info.backend_id) return FM_RESULT_UNSUPPORTED;
            end
            FM_SPI_OPCODE_PANIC, FM_SPI_OPCODE_RELEASE_ALL: begin
                if (c.scope_id != FM_SCOPE_ENGINE || !fm_validate_target(c.scope_id,c.target_word,profile_info.voice_count,operators,profile_info.patch_operator_count)) return FM_RESULT_BAD_TARGET;
                if ({c.parameter_id,c.value_u32,c.arg0_u32,c.arg1_u32,c.voice_generation} != 160'b0) return FM_RESULT_BAD_VALUE;
            end
            FM_SPI_OPCODE_NOTE_ON, FM_SPI_OPCODE_NOTE_OFF, FM_SPI_OPCODE_SET_PITCH, FM_SPI_OPCODE_SET_GAIN_PAN: begin
                if (c.scope_id != FM_SCOPE_VOICE || !fm_validate_target(c.scope_id,c.target_word,profile_info.voice_count,operators,profile_info.patch_operator_count)) return FM_RESULT_BAD_TARGET;
                if (c.parameter_id != 0) return FM_RESULT_BAD_VALUE;
                if (c.opcode_id == FM_SPI_OPCODE_NOTE_ON) begin
                    if (c.value_u32 < BACKEND_PITCH_MIN_Q16 || c.value_u32 > BACKEND_PITCH_MAX_Q16 || c.arg0_u32[31:16] != 0 || c.arg0_u32[7:0] == 0 || c.arg0_u32[7:0] > 127 || c.arg0_u32[15:8] > 127 || c.arg1_u32 > 127) return FM_RESULT_BAD_VALUE;
                end else if (c.opcode_id == FM_SPI_OPCODE_NOTE_OFF) begin
                    if ({c.value_u32,c.arg0_u32,c.arg1_u32} != 96'b0) return FM_RESULT_BAD_VALUE;
                end else if (c.opcode_id == FM_SPI_OPCODE_SET_PITCH) begin
                    if (c.value_u32 < BACKEND_PITCH_MIN_Q16 || c.value_u32 > BACKEND_PITCH_MAX_Q16 || {c.arg0_u32,c.arg1_u32} != 64'b0) return FM_RESULT_BAD_VALUE;
                end else if (c.value_u32 > 127 || c.arg0_u32 > 127 || c.arg1_u32 != 0) return FM_RESULT_BAD_VALUE;
            end
            FM_SPI_OPCODE_SET_PARAMETER: begin
                if (!parameter_info.valid) return FM_RESULT_UNSUPPORTED;
                if (c.scope_id != parameter_info.scope_id || !fm_validate_target(c.scope_id,c.target_word,profile_info.voice_count,operators,profile_info.patch_operator_count)) return FM_RESULT_BAD_TARGET;
                if (!profile_info.valid || c.value_u32 < parameter_info.minimum || c.value_u32 > parameter_info.maximum || {c.arg0_u32,c.arg1_u32,c.voice_generation} != 96'b0) return FM_RESULT_BAD_VALUE;
            end
            FM_SPI_OPCODE_APPLY_PRESET: begin
                if (c.scope_id != FM_SCOPE_PATCH || !fm_validate_target(c.scope_id,c.target_word,profile_info.voice_count,operators,profile_info.patch_operator_count)) return FM_RESULT_BAD_TARGET;
                if (!preset_info.valid || {c.parameter_id,c.arg0_u32,c.arg1_u32,c.voice_generation} != 128'b0) return FM_RESULT_BAD_VALUE;
            end
            default: return FM_RESULT_UNSUPPORTED;
        endcase
        return FM_RESULT_OK;
    endfunction

    // Only pure descriptor/field checks precede the stateful acceptance stages.
    // The SPI mailbox remains stable until the final dispatch edge returns ack.
    typedef enum logic [2:0] { CHECK_IDLE, CHECK_STATIC, CHECK_IDENTITY,
        CHECK_CLASSIFY, CHECK_DISPATCH } check_stage_t;
    check_stage_t check_stage;
    fm_parameter_info_t checked_parameter;
    fm_preset_info_t checked_preset;
    logic [31:0] checked_operators, checked_legality;
    logic checked_header_bad;
    logic checked_recent_identity;
    logic [11:0] checked_recent_words_equal;
    wire checked_recent_payload = &checked_recent_words_equal;
    logic checked_session, checked_epoch, checked_topology, checked_sequence;
    logic checked_busy, checked_select_nonce, checked_voice, checked_local_switch_lock;
    logic mailbox_saw_local_lock;
    logic checked_backend_fault, checked_backend_ready, checked_selected;
    logic [31:0] checked_recent_result;
    logic [31:0] incoming_result, classified_result;
    logic incoming_replay, incoming_consumes, classified_replay, classified_consumes;
    always_comb begin
        classified_result = FM_RESULT_OK; classified_replay = 0; classified_consumes = 0;
        if (checked_header_bad) classified_result = FM_RESULT_BAD_VALUE;
        else if (checked_recent_identity) begin
            classified_replay = 1;
            classified_result = checked_recent_payload ? checked_recent_result : FM_RESULT_SEQUENCE_CONFLICT;
        end else if (!checked_session) classified_result = FM_RESULT_STALE_SESSION;
        else if (!checked_epoch) classified_result = FM_RESULT_STALE_EPOCH;
        else if (!checked_topology) classified_result = FM_RESULT_STALE_TOPOLOGY;
        else if (!checked_sequence) classified_result = FM_RESULT_STALE_SEQUENCE;
        else if (checked_busy) classified_result = FM_RESULT_BUSY;
        else begin
            classified_result = checked_legality;
            // SELECT's same-session nonce error precedes unsupported backend,
            // but does not supersede an invalid header/target/reserved field.
            if (spi_command_mailbox.opcode_id == FM_SPI_OPCODE_SELECT_BACKEND &&
                checked_select_nonce && (checked_legality == FM_RESULT_OK || checked_legality == FM_RESULT_UNSUPPORTED))
                classified_result = FM_RESULT_BAD_VALUE;
            if (classified_result == FM_RESULT_OK && spi_command_mailbox.opcode_id != FM_SPI_OPCODE_SELECT_BACKEND && spi_command_mailbox.opcode_id != FM_SPI_OPCODE_PANIC) begin
                if (checked_backend_fault) classified_result = FM_RESULT_BACKEND_FAULT;
                else if (!checked_selected) classified_result = FM_RESULT_UNSUPPORTED;
                else if (!checked_backend_ready) classified_result = FM_RESULT_BUSY;
            end
            if (classified_result == FM_RESULT_OK && spi_command_mailbox.scope_id == FM_SCOPE_VOICE && !checked_voice)
                classified_result = FM_RESULT_STALE_VOICE;
            if (classified_result == FM_RESULT_OK) classified_consumes = 1;
        end
    end

    logic [31:0] incoming_panic_result;
    logic incoming_panic_replay, incoming_panic_consumes;
    logic panic_bad_header, panic_cache_hit, panic_bad_session, panic_bad_epoch;
    logic panic_bad_sequence, panic_busy;
    logic [5:0] panic_result_select;
    always_comb begin
        // Independent comparisons followed by disjoint masks avoid cascading
        // the entire result encoder into priority-Panic cancellation enables.
        // Explicit if normalization preserves the original X/Z branch behavior.
        panic_bad_header = 0; panic_cache_hit = 0; panic_bad_session = 0;
        panic_bad_epoch = 0; panic_bad_sequence = 0;
        if (panic_sequence_mailbox == 0) panic_bad_header = 1;
        if (panic_recent_valid && panic_session_mailbox == panic_recent_session &&
            panic_epoch_mailbox == panic_recent_epoch && panic_sequence_mailbox == panic_recent_sequence)
            panic_cache_hit = 1;
        if (panic_session_mailbox != session_id) panic_bad_session = 1;
        if (panic_epoch_mailbox != epoch) panic_bad_epoch = 1;
        if (panic_sequence_mailbox <= panic_sequence_watermark) panic_bad_sequence = 1;
        panic_busy = 0; if (panic_pending || recovering) panic_busy = 1;
        panic_result_select[0] = panic_bad_header;
        panic_result_select[1] = !panic_bad_header && panic_cache_hit;
        panic_result_select[2] = !(panic_bad_header || panic_cache_hit) && panic_bad_session;
        panic_result_select[3] = !(panic_bad_header || panic_cache_hit || panic_bad_session) && panic_bad_epoch;
        panic_result_select[4] = !(panic_bad_header || panic_cache_hit || panic_bad_session || panic_bad_epoch) && panic_bad_sequence;
        panic_result_select[5] = !(panic_bad_header || panic_cache_hit || panic_bad_session || panic_bad_epoch || panic_bad_sequence) && panic_busy;
        incoming_panic_replay = panic_result_select[1];
        incoming_panic_consumes = !(panic_bad_header || panic_cache_hit || panic_bad_session || panic_bad_epoch || panic_bad_sequence || panic_busy);
        incoming_panic_result = ({32{panic_result_select[0]}} & FM_RESULT_BAD_VALUE) |
            ({32{panic_result_select[1]}} & panic_recent_result) |
            ({32{panic_result_select[2]}} & FM_RESULT_STALE_SESSION) |
            ({32{panic_result_select[3]}} & FM_RESULT_STALE_EPOCH) |
            ({32{panic_result_select[4]}} & FM_RESULT_STALE_SEQUENCE) |
            ({32{panic_result_select[5]}} & FM_RESULT_BUSY);
    end

/* synthesis translate_off */
`ifndef SYNTHESIS
    always @(posedge clk_i) if (recent_valid && !active_local && !active_local_select && recent_command !== active_command)
        $fatal(1,"FM accepted cache must track the active payload");
`endif
/* synthesis translate_on */
    assign command_o = active_command;
    assign command_valid_o = command_pending && !command_inflight && !recovering &&
        !(active_local&&(local_cancel||local_event_edge||!local_submit_allowed));
    assign panic_valid_o = panic_pending && !panic_inflight;
    always_comb begin
        flags = (backend_ready_i ? FM_FLAG_READY : 32'd0) |
            (selected_o ? FM_FLAG_SELECTED : 32'd0) | (mute_o ? FM_FLAG_MUTED : 32'd0) |
            ((command_pending || panic_pending || recovering) ? FM_FLAG_BUSY : 32'd0) |
            (backend_fault_i ? FM_FLAG_FAULT : 32'd0);
        if (!profile_info.valid) phase = FM_PHASE_UNAVAILABLE;
        else if (backend_fault_i) phase = FM_PHASE_BLOCKED;
        else if (recovering || panic_pending) phase = FM_PHASE_RECOVERING;
        else if (command_pending && active_is_select) phase = FM_PHASE_SWITCHING;
        else if (!backend_ready_i) phase = FM_PHASE_INITIALIZING;
        else phase = FM_PHASE_READY;
    end
    assign mute_o = !selected_o || !backend_ready_i || backend_fault_i || recovering || panic_pending ||
        local_barrier_q;

    // A successful SELECT completion observed on this edge commits binding,
    // including when the priority Panic is accepted on the same edge.
    wire select_completing = command_done_i && command_pending && command_inflight && !recovering &&
        active_is_select && command_result_i == FM_RESULT_OK;
    // Completion, recovery and priority-Panic edges can change any cached
    // identity/result predicate. Restart classification instead of allowing an
    // earlier OK/BUSY/replay result to cross that boundary. Pure checks remain
    // valid because the source cannot replace the unacknowledged mailbox.
    wire control_state_change =
        (command_done_i && command_pending && command_inflight && !recovering) ||
        (panic_done_i && panic_pending && panic_inflight) ||
        (recovering && !panic_pending && backend_ready_i && !backend_fault_i);
    wire checked_backend_changed = (backend_ready_i != checked_backend_ready) ||
        (backend_fault_i != checked_backend_fault);
    wire checked_local_lock_changed = ENABLE_LOCAL_SELECT &&
        (local_switch_lock != checked_local_switch_lock);
    wire ordinary_dispatch = check_stage == CHECK_DISPATCH &&
        command_req_sync_sys != command_ack_sys && !control_state_change &&
        !checked_backend_changed && !checked_local_lock_changed;
    // This mailbox arrived only when its synchronized request differs from
    // the destination ACK. Keep an observed local lock across reclassification
    // and unlock; clear only on its real transport ACK, or asynchronous reset.
    always_ff @(posedge clk_i or negedge rst_n_i) begin
        if(!rst_n_i)mailbox_saw_local_lock<=0;
        else if(ordinary_dispatch && panic_req_sync_sys==panic_ack_sys)mailbox_saw_local_lock<=0;
        else if(command_req_sync_sys!=command_ack_sys && local_switch_lock)mailbox_saw_local_lock<=1;
    end
    always_ff @(posedge clk_i or negedge rst_n_i) begin
        if (!rst_n_i) begin
            check_stage <= CHECK_IDLE;
            checked_parameter <= '0; checked_preset <= '0; checked_operators <= 0;
            checked_legality <= FM_RESULT_BAD_VALUE; checked_header_bad <= 1;
            checked_recent_identity <= 0; checked_recent_words_equal <= 0; checked_recent_result <= 0;
            checked_session <= 0; checked_epoch <= 0; checked_topology <= 0; checked_sequence <= 0;
            checked_busy <= 0; checked_select_nonce <= 0; checked_voice <= 0; checked_local_switch_lock <= 0;
            checked_backend_fault <= 0; checked_backend_ready <= 0; checked_selected <= 0;
            incoming_result <= FM_RESULT_BAD_VALUE; incoming_replay <= 0; incoming_consumes <= 0;
        end else if (check_stage >= CHECK_IDENTITY &&
            (control_state_change || panic_req_sync_sys != panic_ack_sys ||
                (check_stage != CHECK_IDENTITY && (checked_backend_changed || checked_local_lock_changed)))) begin
            check_stage <= CHECK_IDENTITY;
        end else begin
            case (check_stage)
                CHECK_IDLE: if (command_req_sync_sys != command_ack_sys) begin
                    checked_parameter <= fm_parameter_info(PROFILE,spi_command_mailbox.parameter_id);
                    checked_preset <= fm_preset_info(PROFILE,spi_command_mailbox.value_u32);
                    checked_operators <= fm_profile_operator_count(PROFILE,{16'b0,fm_target_voice(spi_command_mailbox.target_word)});
                    check_stage <= CHECK_STATIC;
                end
                CHECK_STATIC: begin
                    checked_legality <= command_legality(spi_command_mailbox,checked_parameter,checked_preset,
                        checked_operators == 32'hffffffff ? 32'd0 : checked_operators);
                    checked_header_bad <= spi_commit_value == 0 || spi_commit_value != spi_command_mailbox.sequence_id;
                    check_stage <= CHECK_IDENTITY;
                end
                CHECK_IDENTITY: begin
                    checked_recent_identity <= recent_valid && spi_command_mailbox.session_id == recent_command.session_id &&
                        spi_command_mailbox.engine_epoch == recent_command.engine_epoch && spi_command_mailbox.sequence_id == recent_command.sequence_id;
                    checked_recent_words_equal <= {
                        spi_command_mailbox.opcode_id == recent_command.opcode_id,
                        spi_command_mailbox.scope_id == recent_command.scope_id,
                        spi_command_mailbox.target_word == recent_command.target_word,
                        spi_command_mailbox.parameter_id == recent_command.parameter_id,
                        spi_command_mailbox.value_u32 == recent_command.value_u32,
                        spi_command_mailbox.arg0_u32 == recent_command.arg0_u32,
                        spi_command_mailbox.arg1_u32 == recent_command.arg1_u32,
                        spi_command_mailbox.session_id == recent_command.session_id,
                        spi_command_mailbox.engine_epoch == recent_command.engine_epoch,
                        spi_command_mailbox.topology_generation == recent_command.topology_generation,
                        spi_command_mailbox.voice_generation == recent_command.voice_generation,
                        spi_command_mailbox.sequence_id == recent_command.sequence_id };
                    checked_recent_result <= recent_result;
                    checked_session <= spi_command_mailbox.session_id == session_id;
                    checked_epoch <= spi_command_mailbox.engine_epoch == epoch;
                    checked_topology <= spi_command_mailbox.topology_generation == topology_generation;
                    checked_sequence <= spi_command_mailbox.sequence_id > sequence_watermark;
                    checked_busy <= command_pending || recovering || panic_pending || local_switch_lock || mailbox_saw_local_lock;
                    checked_local_switch_lock <= local_switch_lock;
                    checked_select_nonce <= spi_command_mailbox.arg0_u32 == session_id;
                    checked_backend_fault <= backend_fault_i; checked_backend_ready <= backend_ready_i;
                    checked_selected <= selected_o;
                    if (spi_command_mailbox.opcode_id == FM_SPI_OPCODE_NOTE_ON)
                        checked_voice <= spi_command_mailbox.voice_generation == next_generation(voice_generation[incoming_voice_index]);
                    else checked_voice <= spi_command_mailbox.voice_generation == voice_generation[incoming_voice_index];
                    check_stage <= CHECK_CLASSIFY;
                end
                CHECK_CLASSIFY: begin
                    incoming_result <= classified_result; incoming_replay <= classified_replay;
                    incoming_consumes <= classified_consumes;
                    check_stage <= CHECK_DISPATCH;
                end
                CHECK_DISPATCH: check_stage <= CHECK_IDLE;
                default: check_stage <= CHECK_IDLE;
            endcase
        end
    end
    // Local music shares dispatch, generations and backend completion. SPI
    // acceptance/result snapshots remain stable while S3 polls its transaction.
    wire local_barrier=local_barrier_q;
    wire local_release_all=local_release_all_q;
    wire local_health=ENABLE_MIDI&&midi_enable_i&&selected_o&&backend_ready_i&&!backend_fault_i&&!recovering&&!panic_pending&&
      !local_switch_lock&&!local_select_request;
    assign local_enable=local_health&&!local_barrier;
    assign midi_event_drop_o=!local_enable||local_barrier||local_release_all;
    // Compare each live stamp first. Select only its one-bit cancellation
    // result with the registered index; the full command stays unchanged.
    assign local_cancel=ENABLE_MIDI&&active_local&&active_is_on&&
      (|(active_voice_onehot_q&cancel_by_voice));
    assign local_event_edge=ENABLE_MIDI&&(midi_event_valid_i||midi_flush_i!=0||raw_pending||flush_fence||raw_priority_pending);
    generate if(ENABLE_MIDI)begin:g_midi
      midi_fm_performance performance(.clk(clk_i),.rst_n(rst_n_i),.sclk(spi_clk_i),.enable(owner_enable),.clear(local_barrier),
        .release_all(local_release_all),
        .event_i(midi_event_i),.event_valid(owner_event_valid),.event_ready(owner_event_ready),.flush(midi_flush_i),
        .perf_write_enable(perf_write_enable_i),.perf_write_addr(perf_write_addr_i),.perf_write_data(perf_write_data_i),
        .command_valid(local_valid),.command_ready(local_ready),.command(local_command),.cancel_on(local_cancel_mask),
        .voice_stamps(local_voice_stamps),.command_stamp(local_stamp),.hard_mute(local_hard_mute),
        .expression_applied_data_o(expression_applied_data_o),.expression_generation_o(expression_generation_o));
    end else begin:g_legacy
      assign local_valid=0;assign local_command='0;assign local_cancel_mask=0;assign local_voice_stamps=0;
      assign local_stamp=0;assign local_hard_mute=0;assign owner_event_ready=1'b1;
      assign expression_applied_data_o=0;assign expression_generation_o=0;
    end endgenerate
    // !command_pending absorbs local_barrier and ordinary completion, while
    // local_health absorbs Panic completion and recovery state-change terms.
    // These are same-edge Boolean reductions, with no registered READY/ACK.
    assign local_ready=local_health&&owner_enable&&!command_pending&&!local_panic_wait&&!raw_pending&&!flush_fence&&
      check_stage==CHECK_IDLE&&command_req_sync_sys==command_ack_sys&&panic_req_sync_sys==panic_ack_sys;
    // No selected/MIDI qualification: this endpoint must be able to bind a
    // freshly booted, unbound backend with SPI SCLK completely stopped.
    // A lock/request can disable the MIDI owner on this very edge. Drain that
    // existing hard-mute Panic before accepting SELECT, otherwise it remains
    // queued behind the new lifecycle and fires after a successful local ACK.
    // A fault response owns no backend command. Retire it even if MIDI has
    // already started its recovery Panic; waiting for that recovery would hide
    // the observed fault and silently turn this request into a later SELECT.
    assign local_select_ready_o=ENABLE_LOCAL_SELECT&&!command_pending&&
      (backend_fault_i||(!panic_pending&&!recovering&&!local_panic_wait&&!local_hard_mute_request&&backend_ready_i))&&
      check_stage==CHECK_IDLE&&command_req_sync_sys==command_ack_sys&&panic_req_sync_sys==panic_ack_sys;
    integer v;
    always_ff @(posedge clk_i or negedge rst_n_i) begin
        if (!rst_n_i) begin
            command_req_meta_sys <= 0; command_req_sync_sys <= 0; command_ack_sys <= 0;
            panic_req_meta_sys <= 0; panic_req_sync_sys <= 0; panic_ack_sys <= 0;
            query_req_meta_sys <= 0; query_req_sync_sys <= 0; query_ack_sys <= 0;
            session_id <= 0; epoch <= 1; topology_generation <= 1; selected_o <= 0; query_index <= 0;
            select_previous_selected <= 0; selection_applied_o <= 0;
            for (v=0;v<VOICES;v=v+1) voice_generation[v] <= 0;
            command_pending <= 0; command_inflight <= 0; recovering <= 0;
            local_barrier_q <= 0; local_release_all_q <= 0;
            panic_pending <= 0; panic_inflight <= 0; panic_is_normal <= 0;
            active_command <= '0; active_classes_q<=command_classes('0); active_voice_onehot_q<=8'b00000001; recent_command <= '0; recent_valid <= 0; recent_result <= 0; sequence_watermark <= 0;
            accepted_sequence <= 0; accepted_session <= 0; accepted_epoch <= 0;
            completed_sequence <= 0; completed_session <= 0; completed_epoch <= 0;
            result_sequence <= 0; result_code <= 0; result_session <= 0; result_epoch <= 0;
            panic_recent_valid <= 0; panic_recent_session <= 0; panic_recent_epoch <= 0; panic_recent_sequence <= 0; panic_recent_result <= 0; panic_sequence_watermark <= 0;
            panic_accepted_sequence <= 0; panic_completed_sequence <= 0;
            panic_result_sequence <= 0; panic_result_code <= 0; panic_result_session <= 0; panic_result_epoch <= 0;
            active_panic_session <= 0; active_panic_epoch <= 0; active_panic_sequence <= 0;
            panic_result_is_active_q <= 1'b1;
            cancelled_valid <= 0; cancelled_command <= '0;
            active_local<=0;panic_is_local<=0;local_panic_wait<=0;
            local_sequence<=32'h80000001;active_local_stamp<=0;local_result<=FM_RESULT_OK;local_accepted<=0;local_completed<=0;
            active_local_select<=0;active_local_select_id<=0;cancelled_local_select<=0;cancelled_local_select_id<=0;
            local_select_done_o<=0;local_select_done_id_o<=0;local_select_result_o<=FM_RESULT_OK;
        end else begin
            selection_applied_o <= 0;
            local_select_done_o<=0;
            if(local_hard_mute_request&&!panic_pending&&!recovering)local_panic_wait<=1;
            if(command_pending&&active_local&&!command_inflight&&local_cancel)begin
              local_barrier_q<=0;local_release_all_q<=0;
              command_pending<=0;local_result<=FM_RESULT_CANCELLED;local_completed<=active_command.sequence_id;
            end
            if(local_panic_wait&&!panic_pending&&!recovering&&(!command_pending||active_local))begin
              local_panic_wait<=0;panic_is_local<=1;panic_is_normal<=0;
              panic_pending<=1;panic_inflight<=0;recovering<=1;
              if(active_local&&command_pending)begin command_pending<=0;local_barrier_q<=0;local_release_all_q<=0;command_inflight<=0;local_result<=FM_RESULT_CANCELLED;local_completed<=active_command.sequence_id;end
            end
            if(local_valid&&local_ready)begin
              // Local owner commands are On/Off/pitch/gain, with no engine barrier.
              local_barrier_q<=0;local_release_all_q<=0;
              active_command<=local_command;
              active_classes_q<=command_classes(local_command);
              active_voice_onehot_q<=8'b00000001<<local_command.target_word[18:16];
              active_command.session_id<=session_id;active_command.engine_epoch<=epoch;
              active_command.topology_generation<=topology_generation;
              active_command.sequence_id<=local_sequence;
              active_command.voice_generation<=local_command.opcode_id==FM_SPI_OPCODE_NOTE_ON?
                next_generation(voice_generation[local_command.target_word[16+:VOICE_AW]]):voice_generation[local_command.target_word[16+:VOICE_AW]];
              if(local_command.opcode_id==FM_SPI_OPCODE_NOTE_ON)
                voice_generation[local_command.target_word[16+:VOICE_AW]]<=next_generation(voice_generation[local_command.target_word[16+:VOICE_AW]]);
              active_local<=1;active_local_select<=0;active_local_stamp<=local_stamp;local_accepted<=local_sequence;
              command_pending<=1;command_inflight<=0;
              local_sequence<=local_sequence==32'hffffffff ? 32'h80000001:local_sequence+1;
            end
            if(local_select_request&&local_select_ready_o)begin
              if(local_select_id_i==0||backend_fault_i)begin
                local_select_done_o<=1;local_select_done_id_o<=local_select_id_i;
                local_select_result_o<=local_select_id_i==0 ? FM_RESULT_BAD_VALUE : FM_RESULT_BACKEND_FAULT;
              end else begin
                // A third command owner, distinct from MIDI and SPI. Use the
                // real backend SELECT reset/program/warmup/FIFO lifecycle.
                active_command<='0;
                active_command.opcode_id<=FM_SPI_OPCODE_SELECT_BACKEND;
                active_command.scope_id<=FM_SCOPE_ENGINE;active_command.target_word<=32'hffffffff;
                active_command.value_u32<=local_select_bind_i ? profile_info.backend_id : FM_BACKEND_UNBOUND;
                active_command.arg0_u32<=next_generation(session_id);
                active_command.session_id<=session_id;active_command.engine_epoch<=epoch;
                active_command.topology_generation<=topology_generation;active_command.sequence_id<=local_select_id_i;
                active_classes_q<=4'b1000;active_voice_onehot_q<=0;
                active_local<=0;active_local_select<=1;active_local_select_id<=local_select_id_i;
                local_barrier_q<=1;local_release_all_q<=0;
                select_previous_selected<=selected_o;selected_o<=local_select_bind_i;
                session_id<=next_generation(session_id);epoch<=next_generation(epoch);
                sequence_watermark<=0;panic_sequence_watermark<=0;
                // Old SPI retries cannot borrow an earlier context's cache
                // after a physical key takes ownership. Published SPI result
                // identities remain untouched for transport accounting.
                recent_valid<=0;panic_recent_valid<=0;
                for(v=0;v<VOICES;v=v+1)voice_generation[v]<=0;
                command_pending<=1;command_inflight<=0;
              end
            end
            command_req_meta_sys <= command_req_spi; command_req_sync_sys <= command_req_meta_sys;
            panic_req_meta_sys <= panic_req_spi; panic_req_sync_sys <= panic_req_meta_sys;
            query_req_meta_sys <= query_req_spi; query_req_sync_sys <= query_req_meta_sys;
            if (query_req_sync_sys != query_ack_sys) begin
                query_ack_sys <= query_req_sync_sys;
                if (query_mailbox < profile_info.voice_count) query_index <= query_mailbox;
            end
            if (command_valid_o && command_ready_i) command_inflight <= 1;
            if (panic_valid_o && panic_ready_i) panic_inflight <= 1;
            // Completion cannot precede ready. panic_done is the cancellation
            // barrier: the adapter must never emit an old done after it.
            if (command_done_i && command_pending && command_inflight && !recovering &&
                !(panic_req_sync_sys != panic_ack_sys && incoming_panic_consumes && !select_completing)) begin
                command_pending <= 0; command_inflight <= 0;
                local_barrier_q <= 0; local_release_all_q <= 0;
                if (active_is_select) begin
                    if (command_result_i == FM_RESULT_OK) selection_applied_o <= 1;
                    else selected_o <= select_previous_selected;
                end
                if(active_local_select)begin
                    local_select_done_o<=1;local_select_done_id_o<=active_local_select_id;local_select_result_o<=command_result_i;
                end else if(active_local)begin local_result<=command_result_i;local_completed<=active_command.sequence_id;end
                else begin
                    completed_sequence <= active_command.sequence_id; completed_session <= active_command.session_id; completed_epoch <= active_command.engine_epoch;
                    result_sequence <= active_command.sequence_id; result_code <= command_result_i;
                    result_session <= active_command.session_id; result_epoch <= active_command.engine_epoch;
                    if (recent_valid) recent_result <= command_result_i;
                end
            end
            if (panic_done_i && panic_pending && panic_inflight) begin
                panic_pending <= 0; panic_inflight <= 0;
                if (panic_is_local) begin
                    panic_is_local<=0;
                end else if (panic_is_normal) begin
                    completed_sequence <= active_command.sequence_id; completed_session <= active_command.session_id; completed_epoch <= active_command.engine_epoch;
                    result_sequence <= active_command.sequence_id; result_code <= FM_RESULT_OK; result_session <= active_command.session_id; result_epoch <= active_command.engine_epoch;
                    if (recent_valid) recent_result <= FM_RESULT_OK;
                end else begin
                    panic_result_is_active_q <= 1'b1; // external priority completion restores its identity
                    panic_completed_sequence <= active_panic_sequence;
                    panic_result_sequence <= active_panic_sequence; panic_result_code <= FM_RESULT_OK;
                    panic_result_session <= active_panic_session; panic_result_epoch <= active_panic_epoch;
                    panic_recent_result <= FM_RESULT_OK;
                end
                if (cancelled_valid) begin
                    completed_sequence <= cancelled_command.sequence_id; completed_session <= cancelled_command.session_id; completed_epoch <= cancelled_command.engine_epoch;
                    cancelled_valid <= 0;
                end
                if(cancelled_local_select)begin
                    local_select_done_o<=1;local_select_done_id_o<=cancelled_local_select_id;
                    local_select_result_o<=FM_RESULT_CANCELLED;cancelled_local_select<=0;
                end
            end
            if (recovering && !panic_pending && backend_ready_i && !backend_fault_i) recovering <= 0;
            // Independent priority channel owns the whole acceptance edge.
            if (panic_req_sync_sys != panic_ack_sys) begin
                panic_ack_sys <= panic_req_sync_sys;
                panic_result_sequence <= panic_sequence_mailbox; panic_result_code <= incoming_panic_result;
                panic_result_session <= panic_session_mailbox; panic_result_epoch <= panic_epoch_mailbox;
                // This later writer also owns the tag on a simultaneous done.
                // A consumed request replaces active identity with this mailbox.
                panic_result_is_active_q <= incoming_panic_consumes || panic_mailbox_is_active;
                if (incoming_panic_consumes) begin
                    panic_recent_valid <= 1; panic_recent_session <= panic_session_mailbox; panic_recent_epoch <= panic_epoch_mailbox;
                    panic_recent_sequence <= panic_sequence_mailbox; panic_recent_result <= FM_RESULT_OK;
                    panic_accepted_sequence <= panic_sequence_mailbox;
                    active_panic_session <= panic_session_mailbox; active_panic_epoch <= panic_epoch_mailbox; active_panic_sequence <= panic_sequence_mailbox;
                    panic_is_local <= 0; panic_is_normal <= 0; panic_pending <= 1; panic_inflight <= 0; recovering <= 1;
                    epoch <= next_generation(epoch); sequence_watermark <= 0; panic_sequence_watermark <= 0;
                    for (v=0;v<VOICES;v=v+1) voice_generation[v] <= 0;
                    if (command_pending && !select_completing) begin
                        if (active_is_select)
                            selected_o <= select_previous_selected;
                        if(active_local_select)begin
                            cancelled_local_select<=1;cancelled_local_select_id<=active_local_select_id;
                        end else if(active_local)begin local_result<=FM_RESULT_CANCELLED;local_completed<=active_command.sequence_id;end
                        else begin
                            cancelled_valid <= 1; cancelled_command <= active_command;
                            result_sequence <= active_command.sequence_id; result_code <= FM_RESULT_CANCELLED;
                            result_session <= active_command.session_id; result_epoch <= active_command.engine_epoch;
                            if (recent_valid) recent_result <= FM_RESULT_CANCELLED;
                        end
                    end
                    command_pending <= 0; command_inflight <= 0;
                    local_barrier_q <= 0; local_release_all_q <= 0;
                end
            end else if (ordinary_dispatch) begin
                command_ack_sys <= command_req_sync_sys;
                result_sequence <= spi_command_mailbox.sequence_id; result_code <= incoming_result;
                result_session <= spi_command_mailbox.session_id; result_epoch <= spi_command_mailbox.engine_epoch;
                if (incoming_consumes && !incoming_replay) begin
                    sequence_watermark <= spi_command_mailbox.sequence_id;
                    recent_valid <= 1; recent_command <= spi_command_mailbox; recent_result <= incoming_result;
                    if (incoming_result == FM_RESULT_OK) begin
                        active_command <= spi_command_mailbox; active_local<=0;active_local_select<=0;
                        local_barrier_q<=spi_command_mailbox.opcode_id==FM_SPI_OPCODE_SELECT_BACKEND||spi_command_mailbox.opcode_id==FM_SPI_OPCODE_APPLY_PRESET;
                        local_release_all_q<=spi_command_mailbox.opcode_id==FM_SPI_OPCODE_RELEASE_ALL;
                        active_classes_q<=command_classes(spi_command_mailbox);
                        active_voice_onehot_q<=8'b00000001<<spi_command_mailbox.target_word[18:16];
                        accepted_sequence <= spi_command_mailbox.sequence_id; accepted_session <= spi_command_mailbox.session_id; accepted_epoch <= spi_command_mailbox.engine_epoch;
                        command_inflight <= 0;
                        if (spi_command_mailbox.opcode_id == FM_SPI_OPCODE_SELECT_BACKEND) begin
                            select_previous_selected <= selected_o;
                            selected_o <= spi_command_mailbox.value_u32 == profile_info.backend_id;
                            session_id <= spi_command_mailbox.arg0_u32; epoch <= next_generation(epoch);
                            sequence_watermark <= 0; panic_sequence_watermark <= 0;
                            for (v=0;v<VOICES;v=v+1) voice_generation[v] <= 0;
                            command_pending <= 1;
                        end else if (spi_command_mailbox.opcode_id == FM_SPI_OPCODE_PANIC) begin
                            epoch <= next_generation(epoch); sequence_watermark <= 0; panic_sequence_watermark <= 0;
                            for (v=0;v<VOICES;v=v+1) voice_generation[v] <= 0;
                            panic_is_local <= 0; panic_is_normal <= 1; panic_pending <= 1; panic_inflight <= 0; recovering <= 1;
                        end else begin
                            command_pending <= 1;
                            if (spi_command_mailbox.opcode_id == FM_SPI_OPCODE_NOTE_ON)
                                voice_generation[incoming_voice_index] <= spi_command_mailbox.voice_generation;
                        end
                    end
                end
            end
        end
    end

    // Mirror every selected_o writer in source order, including rollback.
    // This drives one retained binding FF outside the FM-local reset domain;
    // it does not enter the command/control register enable cones.
    always_comb begin
        selection_next_o = selected_o;
        if(local_select_request&&local_select_ready_o&&local_select_id_i!=0&&!backend_fault_i)
            selection_next_o=local_select_bind_i;
        if (command_done_i && command_pending && command_inflight && !recovering &&
            !(panic_req_sync_sys != panic_ack_sys && incoming_panic_consumes && !select_completing) &&
            active_is_select) begin
            if (command_result_i == FM_RESULT_OK) begin end
            else selection_next_o = select_previous_selected;
        end
        if (panic_req_sync_sys != panic_ack_sys) begin
            if (incoming_panic_consumes && command_pending && !select_completing && active_is_select)
                selection_next_o = select_previous_selected;
        end else if (ordinary_dispatch && incoming_consumes && !incoming_replay &&
                     incoming_result == FM_RESULT_OK &&
                     spi_command_mailbox.opcode_id == FM_SPI_OPCODE_SELECT_BACKEND)
            selection_next_o = spi_command_mailbox.value_u32 == profile_info.backend_id;
    end

    // Coherent owner-domain image. The optional held snapshot transfers the
    // entire observation, including its generation, to spi_bus's read clock.
    typedef struct packed {
        logic [31:0] session_id, epoch, phase, flags, topology_generation;
        logic [31:0] accepted_sequence, accepted_session, accepted_epoch;
        logic [31:0] completed_sequence, completed_session, completed_epoch;
        logic [31:0] result_sequence, result_code, result_session, result_epoch;
        logic [31:0] panic_accepted_sequence, panic_completed_sequence;
        logic [31:0] panic_result_sequence, panic_result_code, panic_result_session, panic_result_epoch;
        logic [31:0] query_index, query_generation, query_operators;
    } status_t;
    status_t status_live, status_snapshot;
    logic [31:0] status_generation_sys;
    always_comb begin
        status_live.session_id = session_id; status_live.epoch = epoch; status_live.phase = phase;
        status_live.flags = flags; status_live.topology_generation = topology_generation;
        status_live.accepted_sequence = accepted_sequence; status_live.accepted_session = accepted_session; status_live.accepted_epoch = accepted_epoch;
        status_live.completed_sequence = completed_sequence; status_live.completed_session = completed_session; status_live.completed_epoch = completed_epoch;
        status_live.result_sequence = result_sequence; status_live.result_code = result_code; status_live.result_session = result_session; status_live.result_epoch = result_epoch;
        // Panic has one result identity on the wire. Hide unrelated historical
        // sequence markers so a rejected same-sequence/other-session request
        // cannot borrow the acceptance or completion of the previous Panic.
        status_live.panic_accepted_sequence = 0; status_live.panic_completed_sequence = 0;
        if (panic_result_is_active_q) begin
            status_live.panic_accepted_sequence = panic_accepted_sequence;
            status_live.panic_completed_sequence = panic_completed_sequence;
        end
        status_live.panic_result_sequence = panic_result_sequence; status_live.panic_result_code = panic_result_code;
        status_live.panic_result_session = panic_result_session; status_live.panic_result_epoch = panic_result_epoch;
        status_live.query_index = query_index; status_live.query_generation = voice_generation[query_index[VOICE_AW-1:0]];
        status_live.query_operators = fm_profile_operator_count(PROFILE,query_index);
    end
    always_ff @(posedge clk_i or negedge rst_n_i) begin
        if (!rst_n_i) begin
            status_snapshot <= '0; status_generation_sys <= 0;
        end else begin
            // Sampling an equal value preserves the snapshot. Only the version
            // counter needs the wide change predicate; both still update on
            // this same edge when the image changes.
            status_snapshot <= status_live;
            if (status_live != status_snapshot)
                status_generation_sys <= next_generation(status_generation_sys);
        end
    end
    status_t read_status_snapshot;
    wire [31:0] read_status_generation;
    wire [31:0] read_local_accepted, read_local_completed, read_local_result;
    generate if (ASYNC_READBACK) begin : g_async_readback
        localparam integer STATUS_WIDTH = $bits(status_t) + 128;
        wire [STATUS_WIDTH-1:0] image;
        // Keep the read image at zero until one complete post-reset snapshot.
        // Source control and all payload reset branches release together.
        held_bus_cdc #(.WIDTH(STATUS_WIDTH),.DISTRIBUTE_SOURCE_RESET(1'b1)) u_status_cdc (
            .src_clk_i(clk_i), .dst_clk_i(read_clk_i), .rst_n_i(rst_n_i),
            .src_data_i({status_generation_sys,status_snapshot,local_accepted,local_completed,local_result}),
            .src_valid_i(1'b1), .src_ready_o(), .dst_data_o(image),
            .dst_valid_o(), .dst_ready_i(1'b1)
        );
        assign {read_status_generation,read_status_snapshot,read_local_accepted,
                read_local_completed,read_local_result} = image;
    end else begin : g_direct_readback
        assign read_status_snapshot = status_snapshot;
        assign read_status_generation = status_generation_sys;
        assign read_local_accepted = local_accepted;
        assign read_local_completed = local_completed;
        assign read_local_result = local_result;
    end endgenerate
    always_comb begin
        read_data_o = 0;
        case (FM_SPI_WORD_BASE + {24'b0,read_addr_i})
            FM_REG_CAP_MAGIC: read_data_o = FM_CAP_MAGIC;
            FM_REG_ABI_VERSION: read_data_o = FM_ABI_VERSION_WORD;
            FM_REG_BACKEND_ID: read_data_o = profile_info.backend_id;
            FM_REG_VOICE_COUNT: read_data_o = profile_info.voice_count;
            FM_REG_MAX_OPERATOR_COUNT: read_data_o = BACKEND_MAX_OPERATOR_COUNT;
            FM_REG_FEATURES: read_data_o = FEATURES;
            FM_REG_EPOCH: read_data_o = read_status_snapshot.epoch;
            FM_REG_PHASE: read_data_o = read_status_snapshot.phase;
            FM_REG_ACCEPTED_SEQUENCE: read_data_o = read_status_snapshot.accepted_sequence;
            FM_REG_COMPLETED_SEQUENCE: read_data_o = read_status_snapshot.completed_sequence;
            FM_REG_RESULT_SEQUENCE: read_data_o = read_status_snapshot.result_sequence;
            FM_REG_RESULT: read_data_o = read_status_snapshot.result_code;
            FM_REG_TOPOLOGY_GENERATION: read_data_o = read_status_snapshot.topology_generation;
            FM_REG_STATUS_GENERATION: read_data_o = read_status_generation;
            FM_REG_FLAGS: read_data_o = read_status_snapshot.flags;
            FM_REG_SESSION: read_data_o = read_status_snapshot.session_id;
            FM_REG_RESULT_SESSION: read_data_o = read_status_snapshot.result_session;
            FM_REG_RESULT_EPOCH: read_data_o = read_status_snapshot.result_epoch;
            FM_REG_ACCEPTED_SESSION: read_data_o = read_status_snapshot.accepted_session;
            FM_REG_ACCEPTED_EPOCH: read_data_o = read_status_snapshot.accepted_epoch;
            FM_REG_COMPLETED_SESSION: read_data_o = read_status_snapshot.completed_session;
            FM_REG_COMPLETED_EPOCH: read_data_o = read_status_snapshot.completed_epoch;
            FM_REG_PANIC_ACCEPTED_SEQUENCE: read_data_o = read_status_snapshot.panic_accepted_sequence;
            FM_REG_PANIC_COMPLETED_SEQUENCE: read_data_o = read_status_snapshot.panic_completed_sequence;
            FM_REG_PANIC_RESULT_SEQUENCE: read_data_o = read_status_snapshot.panic_result_sequence;
            FM_REG_PANIC_RESULT: read_data_o = read_status_snapshot.panic_result_code;
            FM_REG_PANIC_RESULT_SESSION: read_data_o = read_status_snapshot.panic_result_session;
            FM_REG_PANIC_RESULT_EPOCH: read_data_o = read_status_snapshot.panic_result_epoch;
            FM_REG_SOURCE_RATE_NUMERATOR: read_data_o = SOURCE_RATE_NUMERATOR;
            FM_REG_SOURCE_RATE_DENOMINATOR: read_data_o = SOURCE_RATE_DENOMINATOR;
            FM_REG_OUTPUT_RATE_NUMERATOR: read_data_o = OUTPUT_RATE_NUMERATOR;
            FM_REG_OUTPUT_RATE_DENOMINATOR: read_data_o = OUTPUT_RATE_DENOMINATOR;
            FM_REG_PROFILE_VERSION: read_data_o = profile_info.profile_version;
            FM_REG_PARAMETER_COUNT: read_data_o = profile_info.parameter_count;
            FM_REG_PRESET_COUNT: read_data_o = profile_info.preset_count;
            FM_REG_VOICE_QUERY_INDEX: read_data_o = read_status_snapshot.query_index;
            FM_REG_VOICE_QUERY_GENERATION: read_data_o = read_status_snapshot.query_generation;
            FM_REG_VOICE_QUERY_OPERATOR_COUNT: read_data_o = read_status_snapshot.query_operators;
            FM_REG_PROFILE_SHA256_0: read_data_o = fm_profiles_sha256_word(0);
            FM_REG_PROFILE_SHA256_1: read_data_o = fm_profiles_sha256_word(1);
            FM_REG_PROFILE_SHA256_2: read_data_o = fm_profiles_sha256_word(2);
            FM_REG_PROFILE_SHA256_3: read_data_o = fm_profiles_sha256_word(3);
            FM_REG_PROFILE_SHA256_4: read_data_o = fm_profiles_sha256_word(4);
            FM_REG_PROFILE_SHA256_5: read_data_o = fm_profiles_sha256_word(5);
            FM_REG_PROFILE_SHA256_6: read_data_o = fm_profiles_sha256_word(6);
            FM_REG_PROFILE_SHA256_7: read_data_o = fm_profiles_sha256_word(7);
            32'h00000670: if(ENABLE_MIDI)read_data_o=read_local_accepted;
            32'h00000671: if(ENABLE_MIDI)read_data_o=read_local_completed;
            32'h00000672: if(ENABLE_MIDI)read_data_o=read_local_result;
            default: ;
        endcase
    end
endmodule
`default_nettype wire
