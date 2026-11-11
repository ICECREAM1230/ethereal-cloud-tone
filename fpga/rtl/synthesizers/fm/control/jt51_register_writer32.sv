// SPDX-License-Identifier: GPL-3.0-or-later
// FM32 host transaction: bank/address/data are one immutable accepted job.
// The one-cycle data strobe is qualified by the actual II1 operator enable.
// Acknowledgement waits for the full 128-slot MMR update to become visible.
module jt51_register_writer32(
  input logic clk_i,rst_i,valid_i,step_i,
  input logic [1:0] bank_i,
  input logic [7:0] addr_i,data_i,status_i,
  output logic ready_o,done_o,fault_o,
  output logic cs_n_o,wr_n_o,a0_o,
  output logic [1:0] bank_o,
  output logic [7:0] data_o
);
  typedef enum logic [2:0] {IDLE,ADDRESS,GAP,DATA,WAIT_BUSY,WAIT_CLEAR} state_t;
  state_t state;
  logic [1:0] bank;
  logic [7:0] address,data;
  logic [9:0] timeout_count;
  always_comb begin
    ready_o=state==IDLE && !fault_o && step_i;
    cs_n_o=1;wr_n_o=1;a0_o=0;data_o=address;bank_o=bank;
    // The core may park between sample requests. Never stretch a write
    // strobe across that idle interval, even though the job stays pending.
    if(step_i && state==ADDRESS)begin cs_n_o=0;wr_n_o=0;end
    if(step_i && state==DATA)begin cs_n_o=0;wr_n_o=0;a0_o=1;data_o=data;end
  end
  always_ff @(posedge clk_i or posedge rst_i)begin
    if(rst_i)begin
      state<=IDLE;bank<=0;address<=0;data<=0;timeout_count<=0;
      done_o<=0;fault_o<=0;
    end else if(step_i)begin
      done_o<=0;
      case(state)
        IDLE:if(valid_i&&ready_o)begin bank<=bank_i;address<=addr_i;data<=data_i;state<=ADDRESS;end
        ADDRESS:state<=GAP;
        GAP:state<=DATA;
        DATA:begin state<=WAIT_BUSY;timeout_count<=0;end
        WAIT_BUSY:if(status_i[7])begin state<=WAIT_CLEAR;timeout_count<=0;end
          else if(timeout_count==1023)begin fault_o<=1;state<=IDLE;end
          else timeout_count<=timeout_count+1'b1;
        WAIT_CLEAR:if(!status_i[7])begin done_o<=1;state<=IDLE;end
          else if(timeout_count==1023)begin fault_o<=1;state<=IDLE;end
          else timeout_count<=timeout_count+1'b1;
        default:state<=IDLE;
      endcase
    end
  end
endmodule
