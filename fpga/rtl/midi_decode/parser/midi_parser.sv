// SPDX-License-Identifier: MIT
module midi_parser (
  input logic clk, rst, flush,
  input logic [63:0] tick,
  input logic [7:0] source, byte_data,
  input logic byte_valid,
  output logic byte_ready,
  output midi_pkg::midi_event_t event_data,
  output logic event_valid,
  input logic event_ready,
  output midi_pkg::midi_sysex_t sysex_data,
  output logic sysex_valid,
  input logic sysex_ready
);
  // Independent protocol state for one source. Based on audioxtreamer's
  // three-byte packet boundary; no USB framing is exposed at this interface.
  logic [7:0] running_status, active_status, first_data;
  logic [1:0] remaining;
  logic have_first, in_sysex, sysex_first;
  logic [23:0] sysex_buffer;
  logic [1:0] sysex_count;
  logic [63:0] short_start, sysex_start;
  logic [31:0] next_seq;
  logic abort_sysex;
  logic needs_event, needs_sysex;
  logic event_valid_i, sysex_valid_i;
  logic [23:0] appended;

  // Reserve only the outputs this byte can produce. A held realtime event
  // must not prevent an independent SysEx completion (or the converse).
  always_comb begin
    needs_event=0; needs_sysex=0;
    if (byte_data>=8'hF8)
      needs_event=byte_data!=8'hF9 && byte_data!=8'hFD;
    else if (byte_data[7]) begin
      needs_sysex=in_sysex;
      needs_event=byte_data==8'hF6;
    end else if (in_sysex) needs_sysex=sysex_count==2;
    else needs_event=remaining==1 || (remaining==0 && running_status!=0 && short_type(running_status));
  end
  assign event_valid=event_valid_i && !rst && !flush;
  assign sysex_valid=sysex_valid_i && !rst && !flush;
  assign byte_ready = !rst && !flush && (!needs_event || !event_valid_i || event_ready) &&
                      (!needs_sysex || !sysex_valid_i || sysex_ready);
  assign abort_sysex = in_sysex && byte_data[7] && byte_data<8'hF8 && byte_data!=8'hF7;
  always_comb begin
    appended=sysex_buffer;
    case (sysex_count)
      0: appended[7:0]=byte_data;
      1: appended[15:8]=byte_data;
      default: appended[23:16]=byte_data;
    endcase
  end
  function automatic logic short_type(input logic [7:0] status);
    return status[7:4]==4'hC || status[7:4]==4'hD || status==8'hF1 || status==8'hF3;
  endfunction
  function automatic midi_pkg::midi_event_t make_event(
    input logic [7:0] st,d1,d2, input logic [1:0] len,
    input logic [63:0] start_at, input logic [31:0] seq_at);
    midi_pkg::midi_event_t e;
    e.status=st; e.data1=d1; e.data2=d2; e.nbytes=len; e.source=source;
    e.seq=seq_at; e.start_tick=start_at; e.end_tick=tick;
    return e;
  endfunction
  function automatic midi_pkg::midi_sysex_t make_sysex(
    input logic [23:0] data, input logic [1:0] count,
    input logic last_chunk, aborted);
    midi_pkg::midi_sysex_t s;
    s.data=data; s.count=count; s.first=sysex_first; s.last=last_chunk;
    s.aborted=aborted; s.source=source; s.seq=next_seq;
    s.start_tick=sysex_start; s.end_tick=tick;
    return s;
  endfunction
  always_ff @(posedge clk) begin
    if (rst) begin
      running_status<=0; active_status<=0; first_data<=0; remaining<=0;
      have_first<=0; in_sysex<=0; sysex_first<=0; sysex_buffer<=0; sysex_count<=0;
      short_start<=0; sysex_start<=0; next_seq<=0;
      event_data<='0; event_valid_i<=0; sysex_data<='0; sysex_valid_i<=0;
    end else if (flush) begin
      running_status<=0; active_status<=0; first_data<=0; remaining<=0;
      have_first<=0; in_sysex<=0; sysex_first<=0; sysex_buffer<=0; sysex_count<=0;
      event_valid_i<=0; sysex_valid_i<=0;
      // Keep next_seq: discarded pending outputs leave observable sequence gaps.
    end else begin
      if (event_ready) event_valid_i<=0;
      if (sysex_ready) sysex_valid_i<=0;
      if (byte_valid && byte_ready) begin
        if (byte_data>=8'hF8) begin
          // All real-time statuses are transparent to pending and running state.
          if (byte_data!=8'hF9 && byte_data!=8'hFD) begin
            event_data<=make_event(byte_data,0,0,1,tick,next_seq);
            event_valid_i<=1; next_seq<=next_seq+1;
          end
        end else if (byte_data[7]) begin
          if (abort_sysex) begin
            sysex_data<=make_sysex(sysex_buffer,sysex_count,1,1);
            sysex_valid_i<=1; next_seq<=next_seq+1;
          end
          remaining<=0; have_first<=0; in_sysex<=0;
          sysex_buffer<=0; sysex_count<=0;
          if (byte_data<8'hF0) begin
            running_status<=byte_data; active_status<=byte_data;
            remaining<=short_type(byte_data) ? 1 : 2;
            short_start<=tick;
          end else begin
            running_status<=0;
            case (byte_data)
              8'hF0: begin
                in_sysex<=1; sysex_first<=1; sysex_buffer<=24'h0000F0;
                sysex_count<=1; sysex_start<=tick;
              end
              8'hF1,8'hF2,8'hF3: begin
                active_status<=byte_data;
                remaining<=short_type(byte_data) ? 1 : 2;
                short_start<=tick;
              end
              8'hF6: begin
                event_data<=make_event(byte_data,0,0,1,tick,next_seq+(abort_sysex ? 32'd1 : 32'd0));
                event_valid_i<=1; next_seq<=next_seq+(abort_sysex ? 32'd2 : 32'd1);
              end
              8'hF7: if (in_sysex) begin
                sysex_data<=make_sysex(appended,sysex_count+2'd1,1,0);
                sysex_valid_i<=1; next_seq<=next_seq+1;
              end
              default: begin end // Reserved common cancels pending/running state.
            endcase
          end
        end else if (in_sysex) begin
          if (sysex_count==2) begin
            sysex_data<=make_sysex(appended,3,0,0); sysex_valid_i<=1;
            next_seq<=next_seq+1; sysex_buffer<=0; sysex_count<=0; sysex_first<=0;
          end else begin sysex_buffer<=appended; sysex_count<=sysex_count+2'd1; end
        end else if (remaining!=0) begin
          if (remaining==1) begin
            event_data<=make_event(active_status,have_first ? first_data : byte_data,
                have_first ? byte_data : 8'd0, have_first ? 2'd3 : 2'd2,short_start,next_seq);
            event_valid_i<=1; next_seq<=next_seq+1; remaining<=0; have_first<=0;
          end else begin first_data<=byte_data; have_first<=1; remaining<=1; end
        end else if (running_status!=0) begin
          if (short_type(running_status)) begin
            event_data<=make_event(running_status,byte_data,0,2,tick,next_seq);
            event_valid_i<=1; next_seq<=next_seq+1;
          end else begin
            active_status<=running_status; first_data<=byte_data; have_first<=1;
            remaining<=1; short_start<=tick;
          end
        end
      end
    end
  end
endmodule
