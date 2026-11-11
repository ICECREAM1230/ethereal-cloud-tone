// SPDX-License-Identifier: MIT
module midi_hub #(
  parameter int PORTS=4,
  parameter int DEPTH=4,
  parameter bit DISTRIBUTED_SOURCE_RESET=1'b0
) (
  input logic clk, rst,
  // Optional phase-aligned local reset branches; flush remains independent.
  input logic [PORTS-1:0] source_rst_i,
  input logic [PORTS-1:0][7:0] byte_data,
  input logic [PORTS-1:0] byte_valid, flush,
  output logic [PORTS-1:0] byte_ready,
  output midi_pkg::midi_event_t event_data,
  output logic event_valid,
  input logic event_ready,
  output midi_pkg::midi_sysex_t sysex_data,
  output logic sysex_valid,
  input logic sysex_ready,
  input logic master_enable,
  input logic [7:0] master_source,
  output logic event_is_master, master_clock_pulse
);
  import midi_pkg::*;
  localparam int INDEX_W=PORTS>1 ? $clog2(PORTS) : 1;
  logic [63:0] tick;
  midi_event_t parser_event[PORTS], queued_event[PORTS];
  midi_sysex_t parser_sysex[PORTS], queued_sysex[PORTS];
  logic [PORTS-1:0] pe_valid, pe_ready, ps_valid, ps_ready;
  logic [PORTS-1:0] qe_valid, qe_ready, qs_valid, qs_ready;
  logic [INDEX_W-1:0] event_rr, sysex_rr, event_grant, sysex_grant;
  logic event_found, sysex_found, event_valid_i, sysex_valid_i;
  logic master_i;
  logic [PORTS-1:0] queued_master;
  logic [PORTS-1:0] event_available, sysex_available;
  logic [PORTS-1:0] event_selected, sysex_selected;

  always_ff @(posedge clk) if (rst) tick<=0; else tick<=tick+1;
  for (genvar p=0; p<PORTS; p++) begin : sources
    wire lane_rst=DISTRIBUTED_SOURCE_RESET ? source_rst_i[p] : rst;
    // Classify before the grant mux. A valid queue entry has this fixed
    // source, so this preserves slot-load sampling while shortening timing.
    // A scalar alias also supports Icarus12's struct-array elaborator.
    // This is the exact same held FIFO payload, without new state or logic.
    midi_event_t queued_event_p;
    assign queued_event[p]=queued_event_p;
    assign queued_master[p]=master_enable && master_source==8'(p) &&
      (queued_event_p.status==8'hF8 || queued_event_p.status==8'hFA ||
       queued_event_p.status==8'hFB || queued_event_p.status==8'hFC);
    midi_parser parser(.clk,.rst(lane_rst),.flush(flush[p]),.tick,.source(8'(p)),
      .byte_data(byte_data[p]),.byte_valid(byte_valid[p]),.byte_ready(byte_ready[p]),
      .event_data(parser_event[p]),.event_valid(pe_valid[p]),.event_ready(pe_ready[p]),
      .sysex_data(parser_sysex[p]),.sysex_valid(ps_valid[p]),.sysex_ready(ps_ready[p]));
    stream_fifo #(.WIDTH($bits(midi_event_t)),.DEPTH(DEPTH)) events(
      .clk,.rst(lane_rst||flush[p]),.in_data(parser_event[p]),.in_valid(pe_valid[p]),.in_ready(pe_ready[p]),
      .out_data(queued_event_p),.out_valid(qe_valid[p]),.out_ready(qe_ready[p]));
    stream_fifo #(.WIDTH($bits(midi_sysex_t)),.DEPTH(DEPTH)) sysex(
      .clk,.rst(lane_rst||flush[p]),.in_data(parser_sysex[p]),.in_valid(ps_valid[p]),.in_ready(ps_ready[p]),
      .out_data(queued_sysex[p]),.out_valid(qs_valid[p]),.out_ready(qs_ready[p]));
  end

  // Each lane has a stable output slot. Arbitration may only replace a slot
  // when empty or accepted; newly arriving sources cannot change a held item.
  // Compute every grant in parallel. For q<p, q precedes p unless rr is
  // between them; for q>p it precedes p only when the search wraps past q.
  // rr is initialized to zero and is always advanced to a legal lane.
  assign event_available=qe_valid & ~flush;
  assign sysex_available=qs_valid & ~flush;
  for (genvar p=0; p<PORTS; p++) begin : arbitration
    wire [PORTS-1:0] event_precedes, sysex_precedes;
    for (genvar q=0; q<PORTS; q++) begin : priority_mask
      if(q<p) begin : lower_lane
        assign event_precedes[q]=(event_rr<=INDEX_W'(q)) || (event_rr>INDEX_W'(p));
        assign sysex_precedes[q]=(sysex_rr<=INDEX_W'(q)) || (sysex_rr>INDEX_W'(p));
      end else if(q>p) begin : upper_lane
        assign event_precedes[q]=(event_rr<=INDEX_W'(q)) && (event_rr>INDEX_W'(p));
        assign sysex_precedes[q]=(sysex_rr<=INDEX_W'(q)) && (sysex_rr>INDEX_W'(p));
      end else begin : same_lane
        assign event_precedes[q]=1'b0;
        assign sysex_precedes[q]=1'b0;
      end
    end
    assign event_selected[p]=event_available[p] && !(|(event_available & event_precedes));
    assign sysex_selected[p]=sysex_available[p] && !(|(sysex_available & sysex_precedes));
    // Keep the FIFO-pop path one-hot: it no longer goes through a binary
    // grant encoder and a second variable-index decoder.
    assign qe_ready[p]=event_selected[p] && (!event_valid_i || event_ready) && !rst;
    assign qs_ready[p]=sysex_selected[p] && (!sysex_valid_i || sysex_ready) && !rst;
  end
  always_comb begin
    event_found=|event_available; sysex_found=|sysex_available;
    event_grant=0; sysex_grant=0;
    for (int p=0; p<PORTS; p++) begin
      event_grant=event_grant | (INDEX_W'(p) & {INDEX_W{event_selected[p]}});
      sysex_grant=sysex_grant | (INDEX_W'(p) & {INDEX_W{sysex_selected[p]}});
    end
  end
  assign event_valid=event_valid_i && !rst;
  assign sysex_valid=sysex_valid_i && !rst;
  assign event_is_master=master_i;
  assign master_clock_pulse=event_valid && event_ready && master_i && event_data.status==8'hF8;
  always_ff @(posedge clk) begin
    if(rst) begin
      event_rr<=0; sysex_rr<=0;
      event_valid_i<=0; sysex_valid_i<=0;
      event_data<='0; sysex_data<='0; master_i<=0;
    end else begin
      if(!event_valid_i || event_ready) begin
        event_valid_i<=event_found;
        if(event_found) begin
          event_data<=queued_event[event_grant];
          master_i<=queued_master[event_grant];
          event_rr<=event_grant==INDEX_W'(PORTS-1) ? INDEX_W'(0) : event_grant+INDEX_W'(1);
        end else master_i<=0;
      end
      if(!sysex_valid_i || sysex_ready) begin
        sysex_valid_i<=sysex_found;
        if(sysex_found) begin
          sysex_data<=queued_sysex[sysex_grant];
          sysex_rr<=sysex_grant==INDEX_W'(PORTS-1) ? INDEX_W'(0) : sysex_grant+INDEX_W'(1);
        end
      end
    end
  end
endmodule
