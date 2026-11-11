// SPDX-License-Identifier: MIT
// Streaming USB configuration walker; no fixed interface/endpoint offsets.
module midi_descriptor #(
  parameter integer MAX_CONFIG_BYTES = 1024,
  parameter bit USE_RAW_BITMAP_VALID = 1'b0
)(
  input wire clk, rst, start, finish,
  input wire [7:0] byte_data,
  input wire byte_valid,
  output wire byte_ready,
  output reg valid, error,
  output reg [7:0] configuration_value, interface_number,
  output reg [3:0] endpoint_number,
  output reg [6:0] max_packet_size,
  output reg [4:0] cable_count,
  input wire bitmap_byte_valid
);
  reg active, current_midi, midi1_header, pending_ep, found, saw_config;
  reg [7:0] pos, desc_len, current_interface;
  reg [3:0] candidate_ep;
  reg [6:0] candidate_mps;
  reg [15:0] count, total;
  reg [255:0] embedded_out_jacks;
  reg [7:0] header[0:8];
  wire [7:0] d[0:8];
  for (genvar g=0;g<9;g=g+1) begin : current_byte
    assign d[g] = pos == g ? byte_data : header[g];
  end
  assign byte_ready = active && !rst && !start && !finish;
  wire bitmap_valid = USE_RAW_BITMAP_VALID ? bitmap_byte_valid : byte_valid;
  // Every bitmap lookup requires pending_ep. That flag can only follow a
  // MIDI interface descriptor, which clears all 256 bits here before any
  // lookup. Reset/start keep resetting the flags in the original block.
  // Extra private writes on a cancellation edge are discarded by that next
  // interface clear; no raw cancellation net needs to enter this write cone.
  always @(posedge clk) begin
    if (active && bitmap_valid && pos == desc_len - 1'b1) begin
      case (d[1])
        8'h04: embedded_out_jacks <= 0;
        8'h24: begin
          if (current_midi && d[2] == 3 && desc_len >= 9 && d[3] == 1 &&
              d[4] != 0 && d[5] != 0 && {1'b0,desc_len} == 9'd7 + ({1'b0,d[5]} << 1))
            embedded_out_jacks[d[4]] <= 1;
        end
        default: begin end
      endcase
    end
  end
  always @(posedge clk) begin
    if (rst || start) begin
      active <= !rst;
      valid <= 0; error <= 0; count <= 0; total <= 0;
      pos <= 0; desc_len <= 0; current_midi <= 0; midi1_header <= 0;
      pending_ep <= 0; found <= 0; saw_config <= 0;
      current_interface <= 0; candidate_ep <= 0; candidate_mps <= 0;
      configuration_value <= 0; interface_number <= 0;
      endpoint_number <= 0; max_packet_size <= 0; cable_count <= 0;
      for (integer i=0;i<9;i=i+1) header[i] <= 0;
    end else if (finish) begin
      active <= 0;
      valid <= !error && found && saw_config && count == total && pos == 0;
      if (!found || !saw_config || count != total || pos != 0) error <= 1;
    end else if (byte_valid && byte_ready) begin
      count <= count + 1'b1;
      if (saw_config && count >= total) error <= 1;
      if (pos < 9) header[pos[3:0]] <= byte_data;
      if (pos == 0) begin
        desc_len <= byte_data;
        pos <= 1;
        if (byte_data < 2 || (saw_config && count + {8'b0,byte_data} > total)) error <= 1;
      end else if (pos == desc_len - 1'b1) begin
        pos <= 0;
        case (d[1])
          8'h02: begin
            if (saw_config || count != 8 || desc_len != 9 || d[5] == 0 ||
                {d[3],d[2]} < 9 || {d[3],d[2]} > 16'(MAX_CONFIG_BYTES)) error <= 1;
            total <= {d[3],d[2]};
            configuration_value <= d[5];
            saw_config <= 1;
          end
          8'h04: begin
            if (desc_len < 9) error <= 1;
            current_midi <= desc_len >= 9 && d[3] == 0 && d[5] == 1 && d[6] == 3 && d[7] == 0;
            current_interface <= d[2];
            midi1_header <= 0; pending_ep <= 0;
          end
          8'h24: begin
            if (current_midi && d[2] == 1) begin
              midi1_header <= desc_len >= 7 && d[3] == 0 && d[4] == 1;
            end
          end
          8'h05: begin
            pending_ep <= 0;
            if (desc_len < 7) error <= 1;
            if (current_midi && midi1_header && !found && desc_len >= 7 &&
                d[2][7:4] == 8 && d[2][3:0] != 0 && d[3][1:0] == 2 &&
                d[5] == 0 && (d[4] == 8 || d[4] == 16 || d[4] == 32 || d[4] == 64)) begin
              pending_ep <= 1;
              candidate_ep <= d[2][3:0]; candidate_mps <= d[4][6:0];
            end
          end
          8'h25: begin
            if (pending_ep && !found && desc_len >= 5 && d[2] == 1 &&
                d[3] == 1 && desc_len == 5 && embedded_out_jacks[d[4]]) begin
              found <= 1; endpoint_number <= candidate_ep;
              max_packet_size <= candidate_mps; cable_count <= d[3][4:0];
              interface_number <= current_interface;
            end
            pending_ep <= 0;
          end
          default: pending_ep <= 0;
        endcase
        if (!saw_config && d[1] != 2) error <= 1;
      end else pos <= pos + 1'b1;
    end
  end
endmodule
