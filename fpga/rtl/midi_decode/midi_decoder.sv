// SPDX-License-Identifier: MIT
// 控制域保留主工程的三个MIDI上下文，source3用于原生传感器控制。
// RAW_SOURCE3_ENABLE兼容模式仍可实例化历史第四个MIDI解析器。
module midi_decoder #(
  parameter bit REGISTERED_CONTROLS=1'b0,
  parameter bit ENABLE_DIAGNOSTICS=1'b0,
  parameter integer DIAG_CLK_HZ=73734177,
  parameter bit INDEPENDENT_USB=1'b0,
  parameter bit RAW_SOURCE3_ENABLE=1'b1
) (
  input logic clk,rst,enabled,
  input logic [31:0] packet_data,
  input logic packet_valid,
  output logic packet_ready,
  input logic session_flush,
  input logic [7:0] raw_data,
  input logic [1:0] raw_source,
  input logic raw_valid,
  output logic raw_ready,
  input logic [3:0] flush,
  output logic [3:0] source_reset,
  output midi_pkg::midi_event_t event_data,
  output logic event_valid,
  output logic event_pending,
  input logic event_ready,
  output midi_pkg::midi_sysex_t sysex_data,
  output logic sysex_valid,
  input logic sysex_ready,
  output logic [31:0] fault_count,padding_count,event_count,sysex_count,
  output wire diagnostic_uart_tx
);
  initial if(DIAG_CLK_HZ < 500000)
    $error("MIDI diagnostics require at least two clocks per 250000-baud bit");
  localparam integer MIDI_PORTS = RAW_SOURCE3_ENABLE ? 4 : 3;
  logic [MIDI_PORTS-1:0][7:0] bytes;
  logic [MIDI_PORTS-1:0] byte_valid,byte_ready;
  logic [3:0] core_flush;
  logic [0:0][7:0] usb_byte;
  logic [0:0] usb_valid,usb_reset,usb_byte_ready;
  logic raw_slot_valid;
  logic [1:0] raw_slot_source;
  logic [7:0] raw_slot_data;
  // Parallel physical reset branches. All assert asynchronously and release
  // on the original hub_rst edge; do not merge these equivalent FFs.
  reg hub_rst /* synthesis syn_dont_touch = 1 */;
  reg hub_core_rst /* synthesis syn_dont_touch = 1 */;
  reg [MIDI_PORTS-1:0] hub_source_rst /* synthesis syn_dont_touch = 1 */;
  logic [3:0] hub_flush;
  wire [3:0] cancel_flush=REGISTERED_CONTROLS ? core_flush | hub_flush : core_flush;
  wire input_released=!REGISTERED_CONTROLS || !hub_rst;
  logic ingress_ready,packet_error,usb_packet_known_invalid;
  // Classify a held USB packet one cycle before admitting it. The CDC keeps
  // packet_data stable until packet_ready, so the registered decision uses the
  // exact SysEx context preceding this packet. On the following edge the
  // packet handshake and any source-0 cancellation still happen together.
  (* syn_preserve = 1 *) logic usb_packet_classified;
  (* syn_preserve = 1 *) logic usb_packet_known_invalid_q;
  logic [31:0] unused_compatibility_count;
  logic unused_event_is_master,unused_master_clock_pulse;
  // SPI ENABLE owns only RAW sources in the independent production mode.
  // Source 0 starts with the FPGA and is canceled only by its own session or
  // an explicit source-0/global musical boundary supplied through flush.
  wire [3:0] source_enable_mask=(INDEPENDENT_USB ? {{3{enabled}},1'b1} : {4{enabled}})
                               & (RAW_SOURCE3_ENABLE ? 4'b1111 : 4'b0111);
  wire usb_enabled=source_enable_mask[0];
  wire padding_word=packet_data==32'h00000000;
  wire usb_flush=flush[0] || session_flush || !usb_enabled;
  wire usb_packet_admitted=!REGISTERED_CONTROLS || usb_packet_classified;
  // Internal cancellation reaches the recorder only through local FFs. Raw
  // cancellation remains at admission and the complete outer output slots.
  generate if(REGISTERED_CONTROLS) begin : g_registered_controls
    always_ff @(posedge clk or posedge rst) begin
      if(rst)begin
        hub_rst<=1'b1;hub_core_rst<=1'b1;hub_source_rst<={MIDI_PORTS{1'b1}};
        hub_flush<=4'b1111;
      end else begin
        hub_rst<=1'b0;hub_core_rst<=1'b0;hub_source_rst<='0;
        hub_flush<=core_flush;
      end
    end
  end else begin : g_legacy_controls
    assign hub_rst=rst;
    assign hub_core_rst=rst;
    assign hub_source_rst={MIDI_PORTS{rst}};
    assign hub_flush=core_flush;
  end endgenerate
  assign packet_ready=input_released && !rst &&
    (!usb_enabled || (!usb_flush &&
      (padding_word || (usb_packet_admitted && ingress_ready))));
  assign usb_byte_ready[0]=byte_ready[0] &&
    (!REGISTERED_CONTROLS || (input_released && !cancel_flush[0]));
  usb_midi_ingress #(.CABLES(1),.PACKET_DEPTH(8)) ingress (
    .clk,.rst,.packet_data,
    .packet_valid(packet_valid && usb_packet_admitted && input_released &&
                  usb_enabled && !usb_flush && !padding_word),.packet_ready(ingress_ready),
    .flush(usb_flush),.byte_data(usb_byte),.byte_valid(usb_valid),
    .byte_ready(usb_byte_ready),.source_reset(usb_reset),.packet_error,
    .packet_known_invalid(usb_packet_known_invalid),
    .compat_count(unused_compatibility_count)
  );
  always_ff @(posedge clk or posedge rst) begin
    if(rst)begin
      usb_packet_classified<=1'b0;
      usb_packet_known_invalid_q<=1'b0;
    end else if(!REGISTERED_CONTROLS || !input_released || !packet_valid ||
                !usb_enabled || usb_flush || padding_word)begin
      usb_packet_classified<=1'b0;
      usb_packet_known_invalid_q<=1'b0;
    end else if(!usb_packet_classified)begin
      usb_packet_classified<=1'b1;
      usb_packet_known_invalid_q<=usb_packet_known_invalid;
    end else if(packet_ready)begin
      usb_packet_classified<=1'b0;
      usb_packet_known_invalid_q<=1'b0;
    end
  end
  always_comb begin
    source_reset=flush | (INDEPENDENT_USB ? ~source_enable_mask : 4'b0000);
    if(!RAW_SOURCE3_ENABLE)source_reset[3]=flush[3];
    source_reset[0]=flush[0] || session_flush || usb_reset[0];
    if(REGISTERED_CONTROLS)begin
      // F || (!F && E) == F || E for binary legal controls. Do not carry
      // current USB flush back through packet_error into this shared fence.
      source_reset[0]=flush[0] || session_flush;
      if(!rst)begin
        if(!usb_enabled)source_reset[0]=1'b1;
        // A captured invalid packet is guaranteed ready in usb_midi_ingress;
        // keep the raw payload/ingress-ready cone out of the cancel decision.
        // usb_packet_classified is also the registered non-padding qualifier.
        if(packet_valid && input_released && usb_enabled &&
           usb_packet_classified && usb_packet_known_invalid_q)
          source_reset[0]=1'b1;
      end
    end
    core_flush=source_reset | ~source_enable_mask;
  end
  always_comb begin
    bytes='0;byte_valid='0;
    bytes[0]=usb_byte[0];byte_valid[0]=usb_valid[0] && usb_enabled &&
      (!REGISTERED_CONTROLS || (input_released && !cancel_flush[0]));
    raw_ready=0;
    if(REGISTERED_CONTROLS)begin
      // DATA and SOURCE are FFs even while invalid; no enabled/flush mux is
      // allowed back into the parser's byte-data decoding cone.
      for(int s=1;s<MIDI_PORTS;s++)begin
        bytes[s]=raw_slot_data;
        byte_valid[s]=raw_slot_valid && raw_slot_source==2'(s);
      end
      if(raw_source!=0 && (RAW_SOURCE3_ENABLE || raw_source!=3) && enabled && input_released && !cancel_flush[raw_source])
        raw_ready=!raw_slot_valid && !rst;
    end else if(raw_source!=0 && (RAW_SOURCE3_ENABLE || raw_source!=3) && enabled && input_released && !cancel_flush[raw_source])begin
      bytes[raw_source]=raw_data;byte_valid[raw_source]=raw_valid;
      raw_ready=byte_ready[raw_source] && !rst;
    end
  end
  generate if(REGISTERED_CONTROLS)begin : g_raw_byte_slot
    always_ff @(posedge clk or posedge rst)begin
      if(rst)begin raw_slot_valid<=0;raw_slot_source<=0;raw_slot_data<=0;end
      else if(hub_rst || (raw_slot_valid && cancel_flush[raw_slot_source]))raw_slot_valid<=0;
      else if(raw_slot_valid && byte_ready[raw_slot_source])raw_slot_valid<=0;
      else if(raw_valid && raw_ready)begin
        raw_slot_valid<=1;raw_slot_source<=raw_source;raw_slot_data<=raw_data;
      end
    end
  end else begin : g_legacy_raw_byte
    assign raw_slot_valid=0;
    assign raw_slot_source=0;
    assign raw_slot_data=0;
  end endgenerate
  midi_pkg::midi_event_t core_event;
  midi_pkg::midi_sysex_t core_sysex;
  logic core_event_valid,core_event_ready,core_sysex_valid,core_sysex_ready;
  logic [63:0] tick,cutoff[4];
  logic [3:0] cutoff_valid;
  logic stale_event,stale_sysex;
  wire [3:0] event_before_cutoff,sysex_before_cutoff;
  function automatic logic cutoff_le64(
    input logic [3:0] less,equal,
    input logic [63:0] a,b
  );
    cutoff_le64=less[3] || (equal[3] &&
      (less[2] || (equal[2] && (less[1] || (equal[1] && (less[0] || equal[0]))))));
    // synthesis translate_off
    if($isunknown({a,b}))cutoff_le64=a<=b;
    // synthesis translate_on
  endfunction
  for(genvar cs=0;cs<4;cs=cs+1)begin:g_cutoff_compare
    // Keep each timestamp comparison's carry propagation local to 16 bits.
    (* syn_keep=1 *) wire [3:0] event_less,event_equal,sysex_less,sysex_equal;
    for(genvar cp=0;cp<4;cp=cp+1)begin:g_part
      assign event_less[cp]=core_event.end_tick[cp*16+:16]<cutoff[cs][cp*16+:16];
      assign event_equal[cp]=core_event.end_tick[cp*16+:16]==cutoff[cs][cp*16+:16];
      assign sysex_less[cp]=core_sysex.end_tick[cp*16+:16]<cutoff[cs][cp*16+:16];
      assign sysex_equal[cp]=core_sysex.end_tick[cp*16+:16]==cutoff[cs][cp*16+:16];
    end
    assign event_before_cutoff[cs]=cutoff_le64(event_less,event_equal,core_event.end_tick,cutoff[cs]);
    assign sysex_before_cutoff[cs]=cutoff_le64(sysex_less,sysex_equal,core_sysex.end_tick,cutoff[cs]);
  end
  midi_pkg::midi_event_t held_event;
  logic held_valid,held_flush,event_slot_ready;
  midi_pkg::midi_sysex_t held_sysex;
  logic held_sysex_valid,held_sysex_flush;
  midi_hub #(.PORTS(MIDI_PORTS),.DEPTH(8),
    .DISTRIBUTED_SOURCE_RESET(REGISTERED_CONTROLS)) hub (
    .clk,.rst(hub_core_rst),.source_rst_i(hub_source_rst),
    .byte_data(bytes),.byte_valid,.byte_ready,.flush(hub_flush[MIDI_PORTS-1:0]),
    .event_data(core_event),.event_valid(core_event_valid),.event_ready(core_event_ready),
    .sysex_data(core_sysex),.sysex_valid(core_sysex_valid),.sysex_ready(core_sysex_ready),
    .master_enable(1'b0),.master_source(8'd0),
    .event_is_master(unused_event_is_master),.master_clock_pulse(unused_master_clock_pulse)
  );
  // The underlying recorder retains stable global slots through local reset.
  // A performance consumer explicitly cancels its old session on flush.
  always_comb begin
    stale_event=1;stale_sysex=1;
    for(int s=0;s<4;s++) begin
      if(core_event.source==8'(s))
        stale_event=cancel_flush[s] || (cutoff_valid[s] && event_before_cutoff[s]);
      if(core_sysex.source==8'(s))
        stale_sysex=cancel_flush[s] || (cutoff_valid[s] && sysex_before_cutoff[s]);
    end
  end
  // Register cutoff classification at the consumer boundary. The elastic slot
  // preserves the complete event and timestamp while the consumer is stalled.
  // A matching source flush still cancels a held event combinationally, before
  // the consumer can accept it on that same edge.
  assign held_flush=cancel_flush[held_event.source[1:0]];
  assign held_sysex_flush=cancel_flush[held_sysex.source[1:0]];
  assign event_slot_ready=REGISTERED_CONTROLS ? !held_valid : !held_valid || event_ready || held_flush;
  assign event_data=held_event;
  // Conservative early submission barrier. This is the holding FF alone;
  // reset/flush qualification belongs to admission, not deep owner controls.
  assign event_pending=held_valid;
  assign sysex_data=REGISTERED_CONTROLS ? held_sysex : core_sysex;
  assign event_valid=held_valid && source_enable_mask[held_event.source[1:0]] &&
    !held_flush && (REGISTERED_CONTROLS || !rst);
  assign sysex_valid=REGISTERED_CONTROLS ?
    held_sysex_valid && source_enable_mask[held_sysex.source[1:0]] && !held_sysex_flush :
    core_sysex_valid && source_enable_mask[core_sysex.source[1:0]] && !stale_sysex && !rst;
  assign core_event_ready=REGISTERED_CONTROLS ? !held_valid : event_slot_ready || !(|source_enable_mask);
  assign core_sysex_ready=REGISTERED_CONTROLS ? !held_sysex_valid : sysex_ready || stale_sysex || !(|source_enable_mask);
  generate if(REGISTERED_CONTROLS) begin : g_registered_slots
    // Non-lookahead ready is the empty FF alone. A flush or consumption of a
    // full outer slot must not copy an upstream item that was not acknowledged.
    always_ff @(posedge clk or posedge rst) begin
      if(rst)begin
        held_valid<=0;held_event<='0;
        held_sysex_valid<=0;held_sysex<='0;
      end else begin
        // Empty slots may track invalid upstream records. Publication still
        // uses the exact original valid/cutoff handshake, with all metadata
        // captured together on that edge. Full slots always hold their data.
        if(!held_valid)held_event<=core_event;
        if(!held_sysex_valid)held_sysex<=core_sysex;
        if(hub_rst || !(|source_enable_mask) || (held_valid && held_flush) || (event_valid && event_ready)) held_valid<=0;
        else if(!held_valid)begin
          held_valid<=core_event_valid && !stale_event;
        end
        if(hub_rst || !(|source_enable_mask) || (held_sysex_valid && held_sysex_flush) || (sysex_valid && sysex_ready)) held_sysex_valid<=0;
        else if(!held_sysex_valid)begin
          held_sysex_valid<=core_sysex_valid && !stale_sysex;
        end
      end
    end
  end else begin : g_legacy_slots
    assign held_sysex='0;
    assign held_sysex_valid=0;
    always_ff @(posedge clk) begin
      if(rst || !(|source_enable_mask)) begin
        held_valid<=0;
        held_event<='0;
      end else if(event_slot_ready) begin
        held_valid<=core_event_valid && !stale_event;
        if(core_event_valid && !stale_event)held_event<=core_event;
      end
    end
  end endgenerate
  generate if(ENABLE_DIAGNOSTICS) begin : g_diagnostics
    midi_debug_uart #(.CLK_HZ(DIAG_CLK_HZ)) monitor (
      .clk,.rst(hub_rst),.event_data,.event_fire(event_valid && event_ready),
      .sysex_data,.sysex_fire(sysex_valid && sysex_ready),.source_reset,
      .reset_tick(tick),.fault_count,.padding_count,.uart_tx(diagnostic_uart_tx)
    );
  end else begin : g_no_diagnostics
    assign diagnostic_uart_tx=1'b1;
  end endgenerate
  always_ff @(posedge clk) begin
    // Match the recorder tick reset edge exactly, including local release.
    if(hub_rst) begin
      tick<=0;cutoff_valid<=0;
      for(int s=0;s<4;s++)cutoff[s]<=0;
      fault_count<=0;padding_count<=0;event_count<=0;sysex_count<=0;
    end else begin
      tick<=tick+1'b1;
      for(int s=0;s<4;s++)if(cancel_flush[s])begin cutoff[s]<=tick;cutoff_valid[s]<=1;end
      if(packet_error)fault_count<=fault_count+1'b1;
      if(packet_valid && packet_ready && usb_enabled && padding_word)padding_count<=padding_count+1'b1;
      if(event_valid && event_ready)event_count<=event_count+1'b1;
      if(sysex_valid && sysex_ready)sysex_count<=sysex_count+1'b1;
    end
  end
endmodule
