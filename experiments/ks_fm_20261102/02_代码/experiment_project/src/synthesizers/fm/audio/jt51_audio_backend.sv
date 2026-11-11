// SPDX-License-Identifier: GPL-3.0-or-later
// Real JT51 backend: dedicated native96/legacy clocks only.
// Command/response payloads stay stable until the matching toggle returns.
module jt51_audio_backend #(
  // Compatibility parameter: only zero is supported after shared-mode retirement.
  parameter bit ENABLE_AE350_VOICE_OWNER=1'b0,
  parameter bit SHARED_CLOCK=1'b0,
  parameter bit NATIVE96=1'b0,
  parameter bit PCM_REQUEST_DRIVEN=1'b0
)(
  input wire [7:0] ae350_mute_i,
  output wire [7:0] ae350_idle_o,
  input logic clk_i,native_clk_i,rst_n_i,
  input logic command_valid_i,
  input fm_contract_pkg::fm_command_t command_i,
  output logic command_ready_o,command_done_o,
  output logic [31:0] command_result_o,
  input logic panic_valid_i,
  output logic panic_ready_o,panic_done_o,
  output logic ready_o,fault_o,audio_flush_o,audio_enable_o,
  input logic fifo_read_i,
  output logic [31:0] fifo_data_o,
  output logic fifo_empty_o,
  output logic [8:0] fifo_count_o,
  input wire pcm_request_valid_i,
  output wire pcm_request_ready_o,
  input wire [63:0] pcm_request_tag_i,
  output logic pcm_response_valid_o,
  output logic [63:0] pcm_response_tag_o,
  output logic signed [23:0] pcm_response_left_o,pcm_response_right_o
);
  import fm_contract_pkg::*;
  import jt51_adapter_pkg::*;
  fm_command_t mailbox, native_command;
  logic cmd_req,panic_req,cmd_pending,panic_pending,recovering;
  logic n_ack,n_panic_ack,n_ready,n_fault,n_bound;
  logic [31:0] n_result;
  (* ASYNC_REG="TRUE" *) logic ack_meta,ack_sync,panic_ack_meta,panic_ack_sync;
  (* ASYNC_REG="TRUE" *) logic ready_meta,ready_sync,fault_meta,fault_sync,bound_meta,bound_sync;
  (* ASYNC_REG="TRUE" *) logic bank_meta,bank_sync;
  (* ASYNC_REG="TRUE" *) logic req_meta,req_sync,panic_meta,panic_sync,open_meta,open_sync;
  logic req_seen,panic_seen,panic_active,lifecycle_active;
  logic rd_reset,wr_reset,core_reset,core_ready,sample_valid,writer_ready,writer_done,writer_fault;
  logic [7:0] chip_status,wr_addr,wr_data,bus_data;
  logic bus_cs,bus_wr,bus_a0,wr_valid;
  logic signed [15:0] left,right;
  logic fifo_full,fifo_raw_empty;
  logic native_rst_n;
  wire worker_clk=native_clk_i;
  wire request_done,operator_step,chip_request_ready;
  wire [63:0] request_done_tag;
  logic request_pending;
  logic [63:0] pending_request_tag;
  assign pcm_request_ready_o=PCM_REQUEST_DRIVEN && native_rst_n && !request_pending &&
    (core_reset || chip_request_ready);
  wire chip_request_valid=pcm_request_valid_i && pcm_request_ready_o && !core_reset;
`ifndef SYNTHESIS
  initial if(SHARED_CLOCK)
    $fatal(1,"Shared-clock JT51 mode has been retired; use the dedicated FM clock");
`endif
  wire native_step;
  logic [8:0] fifo_wnum,fifo_rnum;
  logic current_bank,program_bank,load_bank,select_next_bound,boot_initialized;
  // Two-bit bundled Panic payload stays stable through its independent ack.
  logic previous_bound,previous_bank,panic_restore_bound,panic_restore_bank;
  wire fifo_we=!PCM_REQUEST_DRIVEN && sample_valid && n_ready && n_bound && open_sync && !n_fault && !fifo_full;
  wire fifo_re=fifo_read_i && audio_enable_o && !fifo_raw_empty;
  assign rd_reset=!rst_n_i || recovering || fault_sync;
  assign fifo_empty_o=fifo_raw_empty || !audio_enable_o;
  assign fifo_count_o=audio_enable_o ? fifo_rnum : 9'd0;
  assign ready_o=ready_sync && !recovering && !fault_sync;
  assign fault_o=fault_sync;
  assign audio_enable_o=ready_o && bound_sync;
  assign command_ready_o=ready_o && !cmd_pending && !panic_pending && !panic_valid_i;
  assign panic_ready_o=!panic_pending;

  jt51_reset_sync native_reset(.clk_i(worker_clk),.rst_n_i(rst_n_i),.rst_n_o(native_rst_n));

  generate if(NATIVE96)begin:g_native96
    assign native_step=1'b1;
    jt51_native96_top #(.ENABLE_AE350_VOICE_OWNER(ENABLE_AE350_VOICE_OWNER),.PCM_REQUEST_DRIVEN(PCM_REQUEST_DRIVEN)) chip(
      .ae350_mute_i(ae350_mute_i),.ae350_idle_o(ae350_idle_o),.clk_i(worker_clk),.rst_i(core_reset),.panic_i(1'b0),
      .cs_n_i(bus_cs),.wr_n_i(bus_wr),.a0_i(bus_a0),.din_i(bus_data),.status_o(chip_status),
      .irq_n_o(),.ready_o(core_ready),.sample_valid_o(sample_valid),.left_o(left),.right_o(right),
      .chip_left_o(),.chip_right_o(),
      .pcm_request_valid_i(chip_request_valid),.pcm_request_ready_o(chip_request_ready),
      .pcm_request_tag_i(pcm_request_tag_i),.pcm_request_done_o(request_done),
      .pcm_request_tag_o(request_done_tag),.operator_step_o(operator_step));
  end else begin:g_native
    assign ae350_idle_o=8'd0;
    assign chip_request_ready=1'b0;
    assign request_done=1'b0;
    assign request_done_tag=64'd0;
    assign operator_step=1'b1;
    assign native_step=1'b1;
    jt51_native_top chip(.clk_i(worker_clk),.rst_i(core_reset),.panic_i(1'b0),
      .cs_n_i(bus_cs),.wr_n_i(bus_wr),.a0_i(bus_a0),.din_i(bus_data),.status_o(chip_status),
      .irq_n_o(),.ready_o(core_ready),.sample_valid_o(sample_valid),.left_o(left),.right_o(right),
      .chip_left_o(),.chip_right_o());
  end endgenerate

  generate if(!PCM_REQUEST_DRIVEN)begin:g_pcm_fifo
  JT51_PCM_FIFO pcm_fifo(.Data({left,right}),.WrReset(wr_reset),.RdReset(rd_reset),
    .WrClk(worker_clk),.RdClk(clk_i),.WrEn(fifo_we),.RdEn(fifo_re),
    .Wnum(fifo_wnum),.Rnum(fifo_rnum),.Q(fifo_data_o),.Empty(fifo_raw_empty),.Full(fifo_full));
  end else begin:g_request_pcm
    assign fifo_data_o=32'd0;assign fifo_wnum=9'd0;assign fifo_rnum=9'd0;
    assign fifo_raw_empty=1'b1;assign fifo_full=1'b0;
  end endgenerate
  always_ff @(posedge worker_clk or negedge native_rst_n)begin
    if(!native_rst_n)begin
      pcm_response_valid_o<=0;pcm_response_tag_o<=0;
      pcm_response_left_o<=0;pcm_response_right_o<=0;
      request_pending<=0;pending_request_tag<=0;
    end else begin
      pcm_response_valid_o<=0;
      if(PCM_REQUEST_DRIVEN)begin
        if(request_pending && (core_reset || request_done))begin
          pcm_response_valid_o<=1;pcm_response_tag_o<=pending_request_tag;request_pending<=0;
          pcm_response_left_o<=!core_reset && n_ready && n_bound && !n_fault ? {left,8'b0} : 24'sd0;
          pcm_response_right_o<=!core_reset && n_ready && n_bound && !n_fault ? {right,8'b0} : 24'sd0;
        end
        if(pcm_request_valid_i && pcm_request_ready_o)begin
          if(core_reset)begin
            // BOOT_PATCH/reset/Panic may last many output frames. Drain each
            // accepted request with tagged silence rather than blocking CDC.
            pcm_response_valid_o<=1;pcm_response_tag_o<=pcm_request_tag_i;
            pcm_response_left_o<=0;pcm_response_right_o<=0;
          end else begin
            request_pending<=1;pending_request_tag<=pcm_request_tag_i;
          end
        end
      end
    end
  end
  jt51_register_writer #(.CLOCK_ENABLE(PCM_REQUEST_DRIVEN)) writer(
    .clk_i(worker_clk),.step_i(PCM_REQUEST_DRIVEN ? operator_step : native_step),.rst_i(core_reset),.valid_i(wr_valid),
    .addr_i(wr_addr),.data_i(wr_data),.status_i(chip_status),.ready_o(writer_ready),
    .done_o(writer_done),.fault_o(writer_fault),.cs_n_o(bus_cs),.wr_n_o(bus_wr),
    .a0_o(bus_a0),.data_o(bus_data));

  always_ff @(posedge clk_i or negedge rst_n_i)begin
    if(!rst_n_i)begin
      mailbox<='0;cmd_req<=0;panic_req<=0;cmd_pending<=0;panic_pending<=0;recovering<=1;
      ack_meta<=0;ack_sync<=0;panic_ack_meta<=0;panic_ack_sync<=0;
      ready_meta<=0;ready_sync<=0;fault_meta<=0;fault_sync<=0;bound_meta<=0;bound_sync<=0;
      bank_meta<=0;bank_sync<=0;previous_bound<=0;previous_bank<=0;panic_restore_bound<=0;panic_restore_bank<=0;
      command_done_o<=0;command_result_o<=FM_RESULT_OK;panic_done_o<=0;audio_flush_o<=0;
    end else begin
      ack_meta<=n_ack;ack_sync<=ack_meta;panic_ack_meta<=n_panic_ack;panic_ack_sync<=panic_ack_meta;
      ready_meta<=n_ready;ready_sync<=ready_meta;fault_meta<=n_fault;fault_sync<=fault_meta;
      bound_meta<=n_bound;bound_sync<=bound_meta;
      bank_meta<=current_bank;bank_sync<=bank_meta;
      command_done_o<=0;panic_done_o<=0;audio_flush_o<=0;
      // Invalid-cycle data may track ahead of acceptance. The real handshake
      // below still owns request/pending/lifecycle changes, with raw fault and
      // Panic gating intact. Freeze the bundle until its ack, including the
      // whole Panic drain barrier after an ordinary request is cancelled.
      // On the first visible done edge, Panic reads the old mailbox/snapshot
      // before these nonblocking assignments can track the next input.
      if(!cmd_pending && !panic_pending)begin
        mailbox<=command_i;previous_bound<=bound_sync;previous_bank<=bank_sync;
      end
      if(fault_meta && !fault_sync)audio_flush_o<=1;
      if(panic_valid_i && panic_ready_o)begin
        panic_req<=~panic_req;panic_pending<=1;cmd_pending<=0;recovering<=1;audio_flush_o<=1;
        // Include the one cycle where done is already visible but the bridge
        // accepted cancellation at the preceding edge without seeing it.
        panic_restore_bound<=jt51_lifecycle(mailbox) && mailbox.opcode_id==FM_SPI_OPCODE_SELECT_BACKEND &&
          (cmd_pending || command_done_o) ? previous_bound : bound_sync;
        panic_restore_bank<=jt51_lifecycle(mailbox) && (cmd_pending || command_done_o) ? previous_bank : bank_sync;
      end else if(panic_pending)begin
        // Native Panic ack is a barrier: all old requests are drained and no
        // older command_done can survive into the next command ownership.
        if(panic_ack_sync==panic_req && ready_sync && ack_sync==cmd_req)begin
          panic_pending<=0;recovering<=0;panic_done_o<=1;
        end
      end else begin
        if(recovering && !cmd_pending && ready_sync && !fault_sync)recovering<=0;
        if(cmd_pending && ack_sync==cmd_req)begin
          cmd_pending<=0;recovering<=0;command_result_o<=n_result;command_done_o<=1;
        end
        if(command_valid_i && command_ready_o)begin
          cmd_req<=~cmd_req;cmd_pending<=1;
          if(jt51_lifecycle(command_i))begin
            recovering<=1;audio_flush_o<=1;
          end
        end
      end
    end
  end

  typedef enum logic [3:0] {BOOT_PATCH,RESET_CORE,INIT_VOICE,START_WRITE,WAIT_WRITE,
    WARMUP,RUN,CAPTURE_COMMAND,DECODE_COMMAND,FAULT} state_t;
  typedef enum logic [1:0] {FULL_PATCH,OFF_ONLY,PITCH_ONLY,GAIN_PAN} mode_t;
  state_t state;
  mode_t mode;
  // Keep each validated YM2151 field at its contract width. Raw 32-bit
  // commands are still checked before any field is stored.
  typedef struct packed {
    logic [3:0] mul;
    logic [6:0] tl;
    logic [4:0] ar,d1r,d2r;
    logic [3:0] rr,d1l;
    logic [2:0] dt1;
    logic [1:0] dt2,ks;
    logic am;
  } operator_params_t;
  logic [2:0] patch_algorithm[0:1],patch_feedback[0:1];
  operator_params_t patch_operator[0:1][0:3];
  logic [3:0] patch_keys[0:1];
  logic [2:0] voice_algorithm[0:7],voice_feedback[0:7];
  operator_params_t voice_operator[0:7][0:3];
  // Zero-extended field views retain the indexed register-packing interface.
  wire [7:0] patch_ops[0:1][0:3][0:10];
  wire [7:0] voice_ops[0:7][0:3][0:10];
  function automatic logic [7:0] operator_value(input operator_params_t op,input integer index);
    case(index)
      0:return {4'd0,op.mul}; 1:return {1'd0,op.tl};
      2:return {3'd0,op.ar}; 3:return {3'd0,op.d1r}; 4:return {3'd0,op.d2r};
      5:return {4'd0,op.rr}; 6:return {4'd0,op.d1l}; 7:return {5'd0,op.dt1};
      8:return {6'd0,op.dt2}; 9:return {6'd0,op.ks}; 10:return {7'd0,op.am};
      default:return 8'd0;
    endcase
  endfunction
  for(genvar bank=0;bank<2;bank++)begin:g_patch_fields
    for(genvar op=0;op<4;op++)begin:g_operator
      for(genvar field=0;field<11;field++)begin:g_field
        assign patch_ops[bank][op][field]=operator_value(patch_operator[bank][op],field);
      end
    end
  end
  for(genvar voice=0;voice<8;voice++)begin:g_voice_fields
    for(genvar op=0;op<4;op++)begin:g_operator
      for(genvar field=0;field<11;field++)begin:g_field
        assign voice_ops[voice][op][field]=operator_value(voice_operator[voice][op],field);
      end
    end
  end
  task automatic store_patch_operator(input logic bank,input logic [1:0] op,
                                     input logic [31:0] parameter_id,value);
    case(parameter_id)
      16:patch_operator[bank][op].mul<=value[3:0];
      17:patch_operator[bank][op].tl<=value[6:0];
      18:patch_operator[bank][op].ar<=value[4:0];
      19:patch_operator[bank][op].d1r<=value[4:0];
      20:patch_operator[bank][op].d2r<=value[4:0];
      21:patch_operator[bank][op].rr<=value[3:0];
      22:patch_operator[bank][op].d1l<=value[3:0];
      23:patch_operator[bank][op].dt1<=value[2:0];
      24:patch_operator[bank][op].dt2<=value[1:0];
      25:patch_operator[bank][op].ks<=value[1:0];
      26:patch_operator[bank][op].am<=value[0];
      default:;
    endcase
  endtask
  logic [6:0] voice_velocity[0:7],voice_volume[0:7],voice_pan[0:7];
  logic [3:0] voice_keys[0:7];
  logic [31:0] voice_pitch[0:7];
  logic [2:0] channel;
  logic [5:0] step;
  logic [6:0] reset_ticks;
  logic [3:0] warmup;
  logic [31:0] preset_id,preset_value_index;
  logic initializing,releasing_all;
  fm_preset_value_info_t preset_value;
  fm_preset_info_t preset_info;
  logic [31:0] target_voice,target_operator;
  logic voice_target_ok;
  logic [13:0] pitch_codes;
  logic [31:0] logical_operator,reg_group,native_slot;
  logic [7:0] operator_data;
  // Every helper below reads only explicit arguments (plus generated constants).
  // Argument-based sensitivity avoids shared function-local events in Icarus.
  always @* begin
    core_reset=!native_rst_n || state==BOOT_PATCH || state==RESET_CORE || state==FAULT;
    wr_reset=!native_rst_n || !n_ready || n_fault;
    wr_valid=state==START_WRITE && !(mode==GAIN_PAN && step==0 && voice_volume[channel]!=0);
    preset_value=fm_preset_value_info(1,preset_id,preset_value_index);
    preset_info=fm_preset_info(1,preset_id);
    target_voice={16'd0,fm_target_voice(native_command.target_word)};
    target_operator={16'd0,fm_target_operator(native_command.target_word)};
    voice_target_ok=native_command.scope_id==FM_SCOPE_VOICE &&
      fm_validate_target(FM_SCOPE_VOICE,native_command.target_word,8,4,4);
    pitch_codes=jt51_pitch_codes(voice_pitch[channel]);
    logical_operator=0;reg_group=0;operator_data=0;native_slot=0;
    wr_addr=8'h08;wr_data={5'd0,channel};
    if(mode==FULL_PATCH)begin
      case(step)
        0:begin wr_addr=8'h08;wr_data={5'd0,channel};end
        1:begin wr_addr=8'h20+{5'd0,channel};wr_data=jt51_pan_bits(voice_pan[channel]) |
          (voice_feedback[channel]<<3) | voice_algorithm[channel];end
        2:begin wr_addr=8'h28+{5'd0,channel};wr_data=pitch_codes[13:6];end
        3:begin wr_addr=8'h30+{5'd0,channel};wr_data={pitch_codes[5:0],2'b0};end
        4:begin wr_addr=8'h38+{5'd0,channel};wr_data=0;end
        29:begin wr_addr=8'h08;wr_data=jt51_key_bits(voice_volume[channel]==0 ? 4'd0 : voice_keys[channel]) | {5'd0,channel};end
        default:begin
          logical_operator=({26'd0,step}-5)/6;reg_group=({26'd0,step}-5)%6;
          native_slot=fm_profile_native_register_slot(1,logical_operator);
          case(reg_group)
            0:operator_data=(voice_ops[channel][logical_operator][7]<<4)|voice_ops[channel][logical_operator][0];
            1:operator_data={1'b0,jt51_operator_tl(voice_ops[channel][logical_operator][1][6:0],voice_velocity[channel],voice_volume[channel],
              voice_algorithm[channel][2:0],logical_operator[1:0])};
            2:operator_data=(voice_ops[channel][logical_operator][9]<<6)|voice_ops[channel][logical_operator][2];
            3:operator_data=(voice_ops[channel][logical_operator][10]<<7)|voice_ops[channel][logical_operator][3];
            4:operator_data=(voice_ops[channel][logical_operator][8]<<6)|voice_ops[channel][logical_operator][4];
            default:operator_data=(voice_ops[channel][logical_operator][5]<<4)|voice_ops[channel][logical_operator][6];
          endcase
          wr_addr=8'(32'h40+reg_group*32'h20+native_slot*8+{29'd0,channel});wr_data=operator_data;
        end
      endcase
    end else if(mode==PITCH_ONLY)begin
      wr_addr=step==0 ? 8'h28+{5'd0,channel} : 8'h30+{5'd0,channel};
      wr_data=step==0 ? pitch_codes[13:6] : {pitch_codes[5:0],2'b0};
    end else if(mode==GAIN_PAN)begin
      if(step==1)begin wr_addr=8'h20+{5'd0,channel};wr_data=jt51_pan_bits(voice_pan[channel]) |
        (voice_feedback[channel]<<3) | voice_algorithm[channel];end
      else if(step>=2)begin
        logical_operator={26'd0,step}-2;native_slot=fm_profile_native_register_slot(1,logical_operator);
        wr_addr=8'(32'h60+native_slot*8+{29'd0,channel});
        wr_data={1'b0,jt51_operator_tl(voice_ops[channel][logical_operator][1][6:0],voice_velocity[channel],voice_volume[channel],
              voice_algorithm[channel][2:0],logical_operator[1:0])};
      end
    end
  end
  task automatic finish_command(input logic [31:0] result);
    n_result<=result;n_ack<=req_seen;state<=RUN;
  endtask
  task automatic snapshot_patch(input logic [2:0] ch,input logic bank);
    voice_algorithm[ch]<=patch_algorithm[bank];voice_feedback[ch]<=patch_feedback[bank];
    for(integer op=0;op<4;op=op+1)voice_operator[ch][op]<=patch_operator[bank][op];
  endtask
  always_ff @(posedge worker_clk or negedge native_rst_n)begin
    if(!native_rst_n)begin
      req_meta<=0;req_sync<=0;panic_meta<=0;panic_sync<=0;open_meta<=0;open_sync<=0;
      req_seen<=0;panic_seen<=0;n_ack<=0;n_panic_ack<=0;n_ready<=0;n_fault<=0;n_bound<=0;
      n_result<=FM_RESULT_OK;native_command<='0;state<=BOOT_PATCH;mode<=FULL_PATCH;
      panic_active<=0;lifecycle_active<=0;preset_id<=0;preset_value_index<=0;
      channel<=0;step<=0;reset_ticks<=0;warmup<=8;
      current_bank<=0;program_bank<=0;load_bank<=0;select_next_bound<=0;boot_initialized<=0;
      initializing<=1;releasing_all<=0;
      for(integer bank=0;bank<2;bank=bank+1)begin
        patch_algorithm[bank]<=0;patch_feedback[bank]<=0;patch_keys[bank]<=0;
        for(integer op=0;op<4;op=op+1)patch_operator[bank][op]<='0;
      end
      for(integer ch=0;ch<8;ch=ch+1)begin
        voice_algorithm[ch]<=0;voice_feedback[ch]<=0;voice_velocity[ch]<=127;voice_volume[ch]<=127;
        voice_pan[ch]<=64;voice_keys[ch]<=0;voice_pitch[ch]<=32'd4521984;
        for(integer op=0;op<4;op=op+1)voice_operator[ch][op]<='0;
      end
    end else if(native_step)begin
      req_meta<=cmd_req;req_sync<=req_meta;panic_meta<=panic_req;panic_sync<=panic_meta;
      open_meta<=!rd_reset;open_sync<=open_meta;
      if(panic_sync!=panic_seen)begin
        panic_seen<=panic_sync;panic_active<=1;lifecycle_active<=0;
        req_seen<=req_sync;n_ack<=req_sync;n_ready<=0;n_fault<=0;
        // Preserve UI patch shadow. Core reset clears voices; INIT_VOICE
        // restores the current patch with KeyOff, then drains/reset FIFO.
        n_bound<=panic_restore_bound;select_next_bound<=panic_restore_bound;
        current_bank<=panic_restore_bank;program_bank<=panic_restore_bank;
        reset_ticks<=0;initializing<=1;releasing_all<=0;
        if(boot_initialized)state<=RESET_CORE;
        else begin state<=BOOT_PATCH;preset_id<=0;preset_value_index<=0;load_bank<=0;program_bank<=0;end
      end else if(writer_fault || (sample_valid && n_ready && n_bound && open_sync && fifo_full))begin
        n_fault<=1;n_ready<=0;n_result<=FM_RESULT_BACKEND_FAULT;n_ack<=req_sync;req_seen<=req_sync;state<=FAULT;
      end else case(state)
        BOOT_PATCH:begin
          if(preset_value.parameter_id==1)patch_algorithm[load_bank]<=preset_value.value_u32[2:0];
          else if(preset_value.parameter_id==2)patch_feedback[load_bank]<=preset_value.value_u32[2:0];
          else store_patch_operator(load_bank,preset_value.operator_index[1:0],preset_value.parameter_id,preset_value.value_u32);
          if(preset_value_index+1==preset_info.values_count)begin
            patch_keys[load_bank]<=preset_info.key_on_logical_mask[3:0];state<=RESET_CORE;reset_ticks<=0;
          end else preset_value_index<=preset_value_index+1;
        end
        RESET_CORE:if(reset_ticks==127)begin state<=INIT_VOICE;channel<=0;initializing<=1;end
          else reset_ticks<=reset_ticks+1'b1;
        INIT_VOICE:begin
          snapshot_patch(channel,program_bank);voice_velocity[channel]<=127;voice_volume[channel]<=127;
          voice_pan[channel]<=64;voice_keys[channel]<=0;voice_pitch[channel]<=32'd4521984;
          mode<=FULL_PATCH;step<=0;state<=START_WRITE;
        end
        START_WRITE:if(mode==GAIN_PAN && step==0 && voice_volume[channel]!=0)step<=1;
          else if(writer_ready)state<=WAIT_WRITE;
        WAIT_WRITE:if(writer_done)begin
          if((mode==FULL_PATCH && step==29)||(mode==PITCH_ONLY && step==1)||
             (mode==GAIN_PAN && step==5)||mode==OFF_ONLY)begin
            if(initializing)begin
              if(channel==7)begin initializing<=0;warmup<=8;state<=WARMUP;end
              else begin channel<=channel+1'b1;state<=INIT_VOICE;end
            end else if(releasing_all && channel!=7)begin channel<=channel+1'b1;step<=0;state<=START_WRITE;end
            else begin releasing_all<=0;finish_command(FM_RESULT_OK);end
          end else begin step<=step+1'b1;state<=START_WRITE;end
        end
        WARMUP:if(sample_valid && core_ready)begin
          if(warmup==1)begin
            n_ready<=1;state<=RUN;boot_initialized<=1;
            if(panic_active)begin n_ack<=req_sync;req_seen<=req_sync;n_panic_ack<=panic_seen;panic_active<=0;end
            else if(lifecycle_active)begin
              current_bank<=program_bank;
              if(native_command.opcode_id==FM_SPI_OPCODE_SELECT_BACKEND)n_bound<=select_next_bound;
              n_result<=FM_RESULT_OK;n_ack<=req_seen;lifecycle_active<=0;
            end
          end else warmup<=warmup-1'b1;
        end
        RUN:if(req_sync!=req_seen)begin req_seen<=req_sync;state<=CAPTURE_COMMAND;end
        CAPTURE_COMMAND:begin native_command<=mailbox;state<=DECODE_COMMAND;end
        DECODE_COMMAND:begin
          channel<=target_voice[2:0];step<=0;releasing_all<=0;
          case(native_command.opcode_id)
            FM_SPI_OPCODE_SELECT_BACKEND,FM_SPI_OPCODE_APPLY_PRESET:
              if(jt51_lifecycle(native_command))begin
                n_ready<=0;lifecycle_active<=1;state<=BOOT_PATCH;preset_value_index<=0;
                load_bank<=~current_bank;program_bank<=~current_bank;
                if(native_command.opcode_id==FM_SPI_OPCODE_SELECT_BACKEND)begin select_next_bound<=native_command.value_u32==1;preset_id<=0;end
                else preset_id<=native_command.value_u32;
              end else finish_command(FM_RESULT_BAD_VALUE);
            FM_SPI_OPCODE_SET_PARAMETER:
              if(fm_validate_parameter(1,native_command.parameter_id,native_command.scope_id,
                native_command.target_word,native_command.value_u32))begin
                if(native_command.parameter_id==1)patch_algorithm[current_bank]<=native_command.value_u32[2:0];
                else if(native_command.parameter_id==2)patch_feedback[current_bank]<=native_command.value_u32[2:0];
                else store_patch_operator(current_bank,target_operator[1:0],native_command.parameter_id,native_command.value_u32);
                finish_command(FM_RESULT_OK);
              end else finish_command(FM_RESULT_BAD_VALUE);
            FM_SPI_OPCODE_NOTE_ON:
              if(!voice_target_ok)finish_command(FM_RESULT_BAD_TARGET);
              else if(!jt51_pitch_valid(native_command.value_u32)||native_command.arg0_u32[7:0]==0||
                native_command.arg0_u32[7:0]>127||native_command.arg0_u32[31:15]!=0||native_command.arg1_u32>127)
                  finish_command(FM_RESULT_BAD_VALUE);
              else begin
                snapshot_patch(target_voice[2:0],current_bank);voice_pitch[target_voice]<=native_command.value_u32;
                voice_velocity[target_voice]<=native_command.arg0_u32[6:0];voice_volume[target_voice]<=native_command.arg0_u32[14:8];
                voice_pan[target_voice]<=native_command.arg1_u32[6:0];voice_keys[target_voice]<=patch_keys[current_bank];
                mode<=FULL_PATCH;state<=START_WRITE;
              end
            FM_SPI_OPCODE_NOTE_OFF:
              if(!voice_target_ok)finish_command(FM_RESULT_BAD_TARGET);
              else begin voice_keys[target_voice]<=0;mode<=OFF_ONLY;state<=START_WRITE;end
            FM_SPI_OPCODE_SET_PITCH:
              if(!voice_target_ok)finish_command(FM_RESULT_BAD_TARGET);
              else if(!jt51_pitch_valid(native_command.value_u32))finish_command(FM_RESULT_BAD_VALUE);
              else begin voice_pitch[target_voice]<=native_command.value_u32;mode<=PITCH_ONLY;state<=START_WRITE;end
            FM_SPI_OPCODE_SET_GAIN_PAN:
              if(!voice_target_ok)finish_command(FM_RESULT_BAD_TARGET);
              else if(native_command.value_u32>127||native_command.arg0_u32>127)finish_command(FM_RESULT_BAD_VALUE);
              else begin voice_volume[target_voice]<=native_command.value_u32[6:0];voice_pan[target_voice]<=native_command.arg0_u32[6:0];mode<=GAIN_PAN;state<=START_WRITE;end
            FM_SPI_OPCODE_RELEASE_ALL:
              if(native_command.scope_id!=FM_SCOPE_ENGINE||native_command.target_word!=32'hffffffff)finish_command(FM_RESULT_BAD_TARGET);
              else begin for(integer ch=0;ch<8;ch=ch+1)voice_keys[ch]<=0;channel<=0;mode<=OFF_ONLY;releasing_all<=1;state<=START_WRITE;end
            default:finish_command(FM_RESULT_UNSUPPORTED);
          endcase
        end
        FAULT:;
        default:begin n_ready<=0;n_fault<=1;state<=FAULT;end
      endcase
    end
  end
endmodule
