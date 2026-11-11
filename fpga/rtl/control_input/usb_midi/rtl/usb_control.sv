// SPDX-License-Identifier: MIT
// EP0 control-read and no-data requests. OUT data stages are deliberately rejected.
module usb_control #(
  parameter integer TIMEOUT_CYCLES = 24000000,
  parameter integer RETRY_LIMIT = 4095
)(
  input wire clk, rst, start,
  input wire [63:0] setup,
  input wire [6:0] device_address, ep0_mps,
  output wire busy,
  output reg done,
  output reg [7:0] error_code,
  output wire [7:0] data_byte,
  output wire [15:0] data_offset,
  output wire data_valid,
  input wire data_ready,
  output wire req_valid,
  input wire req_ready,
  output wire [7:0] req_pid,
  output wire [6:0] req_addr,
  output wire [3:0] req_ep,
  output wire req_data1,
  output wire [6:0] req_len,
  output wire [511:0] req_payload,
  input wire x_done,
  input wire [2:0] x_result,
  input wire [6:0] x_rx_len,
  input wire [511:0] x_rx_payload,
  // Private storage may observe a held byte during cancellation. Publication
  // continues to use data_valid with its original same-cycle reset guard.
  output wire data_pending
);
  localparam [3:0] IDLE=0, SETUP_REQ=1, SETUP_WAIT=2, DATA_REQ=3, DATA_WAIT=4,
    EMIT=5, STATUS_REQ=6, STATUS_WAIT=7;
  reg [3:0] state;
  reg [63:0] setup_hold;
  reg [6:0] address_hold,mps;
  reg data1, status_in;
  reg [15:0] received, wanted;
  reg [6:0] emit_index,chunk_len;
  reg [511:0] chunk;
  reg [24:0] elapsed;
  reg [11:0] retries;
  assign busy = state != IDLE;
  assign req_valid = !rst && (state==SETUP_REQ || state==DATA_REQ || state==STATUS_REQ);
  assign req_pid = state==SETUP_REQ ? 8'h2d : ((state==DATA_REQ || status_in) ? 8'h69 : 8'he1);
  assign req_addr = address_hold;
  assign req_ep = 0;
  assign req_data1 = state==SETUP_REQ ? 1'b0 : (state==DATA_REQ ? data1 : 1'b1);
  wire [15:0] remaining = wanted-received;
  wire [6:0] read_limit = remaining < {9'b0,mps} ? remaining[6:0] : mps;
  assign req_len = state==SETUP_REQ ? 7'd8 : (state==DATA_REQ ? read_limit : 7'd0);
  assign req_payload = {448'b0,setup_hold};
  assign data_valid = state==EMIT && !rst;
  assign data_pending = state==EMIT;
  assign data_byte = chunk[7:0];
  assign data_offset = received + {9'b0,emit_index};
  task fail(input [7:0] code);
    begin error_code<=code;done<=1;state<=IDLE;end
  endtask
  always @(posedge clk) begin
    if (rst) begin
      state<=IDLE;done<=0;error_code<=0;setup_hold<=0;address_hold<=0;mps<=8;
      data1<=1;status_in<=0;received<=0;wanted<=0;emit_index<=0;chunk_len<=0;
      chunk<=0;elapsed<=0;retries<=0;
    end else begin
      done<=0;
      if (state!=IDLE) elapsed<=elapsed+1'b1;
      case (state)
        IDLE: if (start) begin
          setup_hold<=setup;address_hold<=device_address;mps<=ep0_mps;
          wanted<=setup[63:48];received<=0;data1<=1;elapsed<=0;retries<=0;error_code<=0;
          status_in<=setup[63:48]==0;
          if (!(ep0_mps==8 || ep0_mps==16 || ep0_mps==32 || ep0_mps==64)) fail(8'h24);
          else if (setup[63:48]!=0 && !setup[7]) fail(8'h20);
          else state<=SETUP_REQ;
        end
        SETUP_REQ: if(req_ready) state<=SETUP_WAIT;
        DATA_REQ: if(req_ready) state<=DATA_WAIT;
        STATUS_REQ: if(req_ready) state<=STATUS_WAIT;
        SETUP_WAIT,DATA_WAIT,STATUS_WAIT: if(x_done) begin
          if(x_result==0) begin
            retries<=0;
            if(state==SETUP_WAIT) state<=wanted==0 ? STATUS_REQ : DATA_REQ;
            else if(state==STATUS_WAIT) begin
              if(status_in && x_rx_len!=0) fail(8'h21);
              else begin state<=IDLE;done<=1;end
            end else if(x_rx_len>mps || {9'b0,x_rx_len}>wanted-received) fail(8'h21);
            else begin
              chunk<=x_rx_payload;chunk_len<=x_rx_len;emit_index<=0;data1<=!data1;
              if(x_rx_len==0) state<=STATUS_REQ;
              else state<=EMIT;
            end
          end else if(x_result==2) fail(8'h02);
          else if(retries==12'(RETRY_LIMIT)) fail(8'h22);
          else begin
            retries<=retries+1'b1;
            if(state==SETUP_WAIT) state<=SETUP_REQ;
            else if(state==DATA_WAIT) state<=DATA_REQ;
            else state<=STATUS_REQ;
          end
        end
        EMIT: if(data_ready) begin
          chunk<=chunk>>8;
          emit_index<=emit_index+1'b1;
          if(emit_index+1'b1==chunk_len) begin
            received<=received+{9'b0,chunk_len};
            state <= chunk_len<mps || received+{9'b0,chunk_len}==wanted ? STATUS_REQ : DATA_REQ;
          end
        end
        default: state<=IDLE;
      endcase
      if(state!=IDLE && elapsed>=25'(TIMEOUT_CYCLES-1)) fail(8'h23);
    end
  end
endmodule
