// SPDX-License-Identifier: MIT
// Native single-device USB FS host; no processor or external host controller.
module usb_midi_host #(
  parameter integer FRAME_CYCLES=48000,
  parameter integer FRAME_GUARD=6000,
  parameter integer ATTACH_CYCLES=4800000,
  parameter integer RESET_CYCLES=480000,
  parameter integer RECOVER_CYCLES=480000,
  parameter integer ADDRESS_CYCLES=96000,
  parameter integer POLL_CYCLES=4800,
  parameter integer DISCONNECT_CYCLES=120,
  parameter integer LINK_WATCHDOG_CYCLES=480000,
  parameter integer MAX_CONFIG_BYTES=1024,
  parameter bit TRACE_ENABLE=0,
  parameter bit ENABLE_SINK_FLUSH=0
)(
  input wire clk,rst,
  input wire [1:0] line_state,
  output wire bus_reset,
  output wire [31:0] packet_data,
  output wire packet_valid,
  input wire packet_ready,
  output wire session_flush,
  input wire sink_flush_request,
  output reg sink_flush_done,
  output wire configured,
  output wire [7:0] midi_interface,
  output wire [3:0] midi_endpoint,
  output wire [6:0] midi_max_packet_size,
  output wire [4:0] midi_cables,
  output wire enumerating,
  output wire trace_start,
  input wire trace_start_ready,
  output wire [7:0] trace_type,
  output wire [15:0] trace_request_len,trace_index,
  output wire [6:0] trace_address,trace_ep0_mps,
  output wire [7:0] trace_byte_data,
  output wire [15:0] trace_byte_offset,
  output wire trace_byte_valid,
  input wire trace_byte_ready,
  output wire trace_done,
  output wire [7:0] trace_error,
  output wire [15:0] trace_received,
  output reg [7:0] error_code,
  output wire [7:0] utmi_tx_data,
  output wire utmi_tx_valid,
  input wire utmi_tx_ready,
  input wire [7:0] utmi_rx_data,
  input wire utmi_rx_valid,utmi_rx_active,utmi_rx_error
);
  localparam [4:0] ATTACH=0,RESET=1,RECOVER=2,GET8=3,GET8_WAIT=4,
    ADDRESS=5,ADDRESS_WAIT=6,ADDRESS_DELAY=7,DEVICE=8,DEVICE_WAIT=9,
    CONFIG9=10,CONFIG9_WAIT=11,CONFIG_ALL=12,CONFIG_ALL_WAIT=13,CONFIG_CHECK=14,
    SET_CONFIG=15,SET_CONFIG_WAIT=16,RUN=17,CLEAR_HALT=18,CLEAR_HALT_WAIT=19,ERROR=20;
  localparam [1:0] OWNER_CTRL=0,OWNER_SOF=1,OWNER_POLL=2;
  reg [4:0] state;
  reg [23:0] delay_count;
  reg [7:0] se0_count;
  reg cancel,flush_only,link_active;
  reg [6:0] device_address,ep0_mps;
  reg [15:0] config_length;
  reg [7:0] header[0:17];
  reg [15:0] control_bytes;
  reg [15:0] frame_time;
  reg [10:0] frame_number;
  reg sof_pending;
  reg [15:0] poll_delay;
  reg [23:0] link_watchdog;
  reg poll_inflight,bulk_data1;
  reg [3:0] transfer_errors;
  reg [1:0] owner;
  wire detached = link_active && !bus_reset && se0_count >= 8'(DISCONNECT_CYCLES-1) && line_state==0;
  // A sink barrier does not reset the transaction engine or endpoint toggle.
  // Finish any already-owned poll, clear the actual transfer bank exactly once,
  // then keep polling blocked until the requester explicitly releases us.
  wire sink_flush_active = ENABLE_SINK_FLUSH && sink_flush_request;
  reg sink_flush_serviced;
  always @(posedge clk) begin
    if(rst) begin sink_flush_serviced<=0;sink_flush_done<=0;end
    else begin
      sink_flush_done<=0;
      if(!sink_flush_active) sink_flush_serviced<=0;
      else if(!sink_flush_serviced && !poll_inflight) begin
        sink_flush_serviced<=1;sink_flush_done<=1;
      end
    end
  end
  assign session_flush = rst || cancel || flush_only || detached || sink_flush_done;
  assign bus_reset = state==RESET && !rst;
  assign configured = state==RUN || state==CLEAR_HALT || state==CLEAR_HALT_WAIT;
  assign enumerating = state!=ATTACH && state!=RUN && state!=ERROR;
  wire core_rst = rst || cancel || detached || state==ATTACH || state==RESET || state==ERROR;

  wire c_busy,c_done,c_start;
  wire [7:0] c_error,c_data;
  wire [15:0] c_offset;
  wire c_data_valid,c_data_pending;
  wire trace_read_start = state==GET8 || state==DEVICE || state==CONFIG9 || state==CONFIG_ALL;
  wire trace_read_wait = state==GET8_WAIT || state==DEVICE_WAIT || state==CONFIG9_WAIT || state==CONFIG_ALL_WAIT;
  wire parser_ready = state==CONFIG_ALL_WAIT ? desc_ready : 1'b1;
  wire c_data_ready = parser_ready && (!TRACE_ENABLE || !trace_read_wait || trace_byte_ready);
  wire c_valid,c_ready,c_data1;
  wire [7:0] c_pid;
  wire [6:0] c_addr,c_len;
  wire [3:0] c_ep;
  wire [511:0] c_payload;
  reg [63:0] c_setup;
  wire start_state = state==GET8 || state==ADDRESS || state==DEVICE || state==CONFIG9 ||
    state==CONFIG_ALL || state==SET_CONFIG || state==CLEAR_HALT;
  assign c_start = start_state && !c_busy && !core_rst &&
    (!TRACE_ENABLE || !trace_read_start || trace_start_ready);
  reg [7:0] trace_type_hold;
  assign trace_start = TRACE_ENABLE && c_start && trace_read_start;
  assign trace_type = trace_start ? c_setup[31:24] : trace_type_hold;
  assign trace_request_len = c_setup[63:48];
  assign trace_index = c_setup[47:32];
  assign trace_address = device_address;
  assign trace_ep0_mps = ep0_mps;
  assign trace_byte_data = c_data;
  assign trace_byte_offset = c_offset;
  assign trace_byte_valid = TRACE_ENABLE && trace_read_wait && c_data_valid && parser_ready;
  assign trace_done = TRACE_ENABLE && c_done && trace_read_wait;
  assign trace_error = c_error;
  assign trace_received = control_bytes;
  always @* begin
    case(state)
      GET8: c_setup=64'h0008_0000_0100_0680;
      ADDRESS: c_setup=64'h0000_0000_0001_0500;
      DEVICE: c_setup=64'h0012_0000_0100_0680;
      CONFIG9: c_setup=64'h0009_0000_0200_0680;
      CONFIG_ALL: c_setup={config_length,16'h0000,16'h0200,16'h0680};
      SET_CONFIG: c_setup={16'h0000,16'h0000,8'h00,desc_configuration,16'h0900};
      CLEAR_HALT: c_setup={16'h0000,8'h00,4'h8,desc_endpoint,16'h0000,16'h0102};
      default: c_setup=0;
    endcase
  end
  wire x_done;
  wire [2:0] x_result;
  wire [6:0] x_rx_len;
  wire [511:0] x_rx_payload;
  usb_control control_engine(
    .clk,.rst(core_rst),.start(c_start),.setup(c_setup),.device_address,.ep0_mps,
    .busy(c_busy),.done(c_done),.error_code(c_error),.data_byte(c_data),.data_offset(c_offset),
    .data_valid(c_data_valid),.data_pending(c_data_pending),.data_ready(c_data_ready),.req_valid(c_valid),.req_ready(c_ready),
    .req_pid(c_pid),.req_addr(c_addr),.req_ep(c_ep),.req_data1(c_data1),.req_len(c_len),
    .req_payload(c_payload),.x_done(x_done && owner==OWNER_CTRL),.x_result,.x_rx_len,.x_rx_payload);

  wire desc_valid,desc_error,desc_ready;
  wire [7:0] desc_configuration,desc_interface;
  wire [3:0] desc_endpoint;
  wire [6:0] desc_mps;
  wire [4:0] desc_cables;
  assign midi_interface=desc_interface;
  assign midi_endpoint=desc_endpoint;
  assign midi_max_packet_size=desc_mps;
  assign midi_cables=desc_cables;
  midi_descriptor #(.MAX_CONFIG_BYTES(MAX_CONFIG_BYTES),.USE_RAW_BITMAP_VALID(1'b1)) descriptor(
    .clk,.rst(core_rst),.start(c_start && state==CONFIG_ALL),
    .finish(c_done && state==CONFIG_ALL_WAIT),.byte_data(c_data),
    .byte_valid(c_data_valid && state==CONFIG_ALL_WAIT && (!TRACE_ENABLE || trace_byte_ready)),.byte_ready(desc_ready),
    .bitmap_byte_valid(c_data_pending && state==CONFIG_ALL_WAIT && (!TRACE_ENABLE || trace_byte_ready)),
    .valid(desc_valid),.error(desc_error),.configuration_value(desc_configuration),
    .interface_number(desc_interface),.endpoint_number(desc_endpoint),
    .max_packet_size(desc_mps),.cable_count(desc_cables));

  wire queue_ready,format_error;
  wire poll_success = x_done && owner==OWNER_POLL && state==RUN && x_result==0 && !session_flush;
  midi_packet_queue packets(
    .clk,.rst,.flush(session_flush),.in_valid(poll_success),.in_ready(queue_ready),
    .in_len(x_rx_len),.in_payload(x_rx_payload),.packet_data,.packet_valid,.packet_ready,.format_error);

  wire x_ready;
  wire frame_slot = link_active && !sof_pending && frame_time < 16'(FRAME_CYCLES-FRAME_GUARD);
  wire select_sof = sof_pending && link_active;
  wire select_control = !select_sof && c_valid && frame_slot && poll_delay==0;
  wire select_poll = !select_sof && !c_busy && state==RUN && !poll_inflight && queue_ready && frame_slot && poll_delay==0 && !sink_flush_active;
  wire x_valid = !core_rst && (select_sof || select_control || select_poll);
  wire [7:0] x_pid = select_sof ? 8'ha5 : (select_control ? c_pid : 8'h69);
  wire [6:0] x_addr = select_sof ? frame_number[6:0] : (select_control ? c_addr : device_address);
  wire [3:0] x_ep = select_sof ? frame_number[10:7] : (select_control ? c_ep : desc_endpoint);
  wire x_data1 = select_control ? c_data1 : bulk_data1;
  wire [6:0] x_len = select_sof ? 7'd0 : (select_control ? c_len : desc_mps);
  wire [511:0] x_payload = select_control ? c_payload : 512'd0;
  assign c_ready = x_ready && select_control && !core_rst;
  fs_packet_engine transactions(
    .clk,.rst(core_rst),.req_valid(x_valid),.req_ready(x_ready),.req_pid(x_pid),
    .req_addr(x_addr),.req_ep(x_ep),.req_data1(x_data1),.req_len(x_len),.req_payload(x_payload),
    .done(x_done),.result(x_result),.rx_len(x_rx_len),.rx_payload(x_rx_payload),
    .utmi_tx_data,.utmi_tx_valid,.utmi_tx_ready,.utmi_rx_data,.utmi_rx_valid,.utmi_rx_active,.utmi_rx_error,
    .utmi_line_state(line_state));

  task restart(input [7:0] code);
    begin
      state<=ERROR;error_code<=code;cancel<=1;link_active<=0;delay_count<=0;poll_inflight<=0;
    end
  endtask
  always @(posedge clk) begin
    if(rst) begin
      state<=ATTACH;delay_count<=0;se0_count<=0;cancel<=1;flush_only<=0;link_active<=0;
      device_address<=0;ep0_mps<=8;config_length<=0;control_bytes<=0;error_code<=0;
      frame_time<=0;frame_number<=0;sof_pending<=0;poll_delay<=0;poll_inflight<=0;
      link_watchdog<=0;trace_type_hold<=0;
      bulk_data1<=0;transfer_errors<=0;owner<=OWNER_SOF;
      for(integer i=0;i<18;i=i+1)header[i]<=0;
    end else begin
      cancel<=0;
      flush_only<=0;
      if(!link_active || x_done)link_watchdog<=0;
      else link_watchdog<=link_watchdog+1'b1;
      if(line_state==0 && !bus_reset) begin
        if(se0_count<8'(DISCONNECT_CYCLES))se0_count<=se0_count+1'b1;
      end else se0_count<=0;
      if(!link_active) begin frame_time<=0;sof_pending<=0;frame_number<=0;end
      else if(frame_time==16'(FRAME_CYCLES-1)) begin
        frame_time<=0;sof_pending<=1;
      end else frame_time<=frame_time+1'b1;
      if(poll_delay!=0)poll_delay<=poll_delay-1'b1;
      if(x_valid && x_ready) begin
        if(select_sof) begin owner<=OWNER_SOF;sof_pending<=0;frame_number<=frame_number+1'b1;end
        else if(select_control) owner<=OWNER_CTRL;
        else begin owner<=OWNER_POLL;poll_inflight<=1;end
      end
      if(x_done && owner==OWNER_CTRL && x_result!=0)poll_delay<=16'(POLL_CYCLES);
      if(c_start) begin
        if(trace_read_start)trace_type_hold<=c_setup[31:24];
        control_bytes<=0;
        for(integer i=0;i<18;i=i+1)header[i]<=0;
      end
      if(c_data_valid && c_data_ready) begin
        control_bytes<=control_bytes+1'b1;
        if(c_offset<18)header[c_offset[4:0]]<=c_data;
      end
      case(state)
        ATTACH: begin
          device_address<=0;ep0_mps<=8;poll_inflight<=0;bulk_data1<=0;
          if(line_state==1) begin
            if(delay_count==24'(ATTACH_CYCLES-1)) begin state<=RESET;delay_count<=0;error_code<=0;cancel<=1;end
            else delay_count<=delay_count+1'b1;
          end else begin delay_count<=0;if(line_state==2)error_code<=8'h10;end
        end
        RESET: begin
          device_address<=0;ep0_mps<=8;bulk_data1<=0;transfer_errors<=0;error_code<=0;
          if(delay_count==24'(RESET_CYCLES-1)) begin
            state<=RECOVER;delay_count<=0;link_active<=1;
          end else delay_count<=delay_count+1'b1;
        end
        RECOVER: begin
          if(delay_count==24'(RECOVER_CYCLES-1))begin state<=GET8;delay_count<=0;end
          else delay_count<=delay_count+1'b1;
        end
        GET8: if(c_start)state<=GET8_WAIT;
        GET8_WAIT: if(c_done) begin
          if(c_error!=0)restart(c_error);
          else if(control_bytes!=8 || header[0]<18 || header[1]!=1 ||
              !(header[7]==8 || header[7]==16 || header[7]==32 || header[7]==64)) restart(8'h31);
          else begin ep0_mps<=header[7][6:0];state<=ADDRESS;end
        end
        ADDRESS: if(c_start)state<=ADDRESS_WAIT;
        ADDRESS_WAIT: if(c_done) begin
          if(c_error!=0)restart(c_error);
          else begin device_address<=1;state<=ADDRESS_DELAY;delay_count<=0;end
        end
        ADDRESS_DELAY: begin
          if(delay_count==24'(ADDRESS_CYCLES-1))begin state<=DEVICE;delay_count<=0;end
          else delay_count<=delay_count+1'b1;
        end
        DEVICE: if(c_start)state<=DEVICE_WAIT;
        DEVICE_WAIT: if(c_done) begin
          if(c_error!=0)restart(c_error);
          else if(control_bytes!=18 || header[0]!=18 || header[1]!=1 || header[17]==0)restart(8'h32);
          else state<=CONFIG9;
        end
        CONFIG9: if(c_start)state<=CONFIG9_WAIT;
        CONFIG9_WAIT: if(c_done) begin
          if(c_error!=0)restart(c_error);
          else if(control_bytes!=9 || header[0]!=9 || header[1]!=2 || {header[3],header[2]}<9 ||
              {header[3],header[2]}>16'(MAX_CONFIG_BYTES))restart(8'h33);
          else begin config_length<={header[3],header[2]};state<=CONFIG_ALL;end
        end
        CONFIG_ALL: if(c_start)state<=CONFIG_ALL_WAIT;
        CONFIG_ALL_WAIT: if(c_done)begin if(c_error!=0)restart(c_error);else state<=CONFIG_CHECK;end
        CONFIG_CHECK: begin
          if(!desc_valid || desc_error)restart(8'h34);
          else state<=SET_CONFIG;
        end
        SET_CONFIG: if(c_start)state<=SET_CONFIG_WAIT;
        SET_CONFIG_WAIT: if(c_done)begin
          if(c_error!=0)restart(c_error);
          else begin state<=RUN;bulk_data1<=0;transfer_errors<=0;poll_delay<=0;end
        end
        RUN: if(x_done && owner==OWNER_POLL)begin
          poll_inflight<=0;poll_delay<=16'(POLL_CYCLES);
          if(x_result==0)begin
            bulk_data1<=!bulk_data1;transfer_errors<=0;
            if(x_rx_len>desc_mps)restart(8'h41);
          end else if(x_result==1 || x_result==5)transfer_errors<=0;
          else if(x_result==2)begin state<=CLEAR_HALT;flush_only<=1;end
          else if(transfer_errors==7)restart({5'b01000,x_result});
          else transfer_errors<=transfer_errors+1'b1;
        end
        CLEAR_HALT: if(c_start)state<=CLEAR_HALT_WAIT;
        CLEAR_HALT_WAIT: if(c_done)begin
          if(c_error!=0)restart(c_error);
          else begin state<=RUN;bulk_data1<=0;transfer_errors<=0;error_code<=0;end
        end
        ERROR: begin
          // An attached device/PHY may hold K or RX-active indefinitely.
          // Do not require a fresh J attach indication before forcing reset.
          if(delay_count==24'(ATTACH_CYCLES-1))begin state<=RESET;delay_count<=0;cancel<=1;end
          else delay_count<=delay_count+1'b1;
        end
        default:restart(8'h7f);
      endcase
      if(format_error) restart(8'h45);
      if(link_active && link_watchdog>=24'(LINK_WATCHDOG_CYCLES-1))restart(8'h46);
      if(detached)begin state<=ATTACH;cancel<=1;link_active<=0;delay_count<=0;poll_inflight<=0;end
    end
  end
endmodule
