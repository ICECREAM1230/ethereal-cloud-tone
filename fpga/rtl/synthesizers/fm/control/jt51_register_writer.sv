// SPDX-License-Identifier: GPL-3.0-or-later
// Native-domain host transaction. Four data clocks include exactly one cen_p1
// tick, so the unmodified MMR busy circuit always sees the write. Wait for the
// entire native 32-slot update before acknowledging or starting another write.
module jt51_register_writer #(
  parameter bit CLOCK_ENABLE=1'b0
)(
  input logic clk_i,rst_i,valid_i,
  input logic step_i,
  input logic [7:0] addr_i,data_i,status_i,
  output logic ready_o,done_o,fault_o,
  output logic cs_n_o,wr_n_o,a0_o,
  output logic [7:0] data_o
);
  typedef enum logic [2:0] {IDLE,ADDRESS,GAP,DATA,WAIT_BUSY,WAIT_CLEAR} state_t;
  state_t state;
  logic [7:0] address,data;
  logic [1:0] ticks;
  logic [9:0] timeout_count;
  always_comb begin
    ready_o=state==IDLE && !fault_o && (!CLOCK_ENABLE || step_i);
    cs_n_o=1;wr_n_o=1;a0_o=0;data_o=address;
    if(state==ADDRESS)begin cs_n_o=0;wr_n_o=0;end
    if(state==DATA)begin cs_n_o=0;wr_n_o=0;a0_o=1;data_o=data;end
  end
  always_ff @(posedge clk_i or posedge rst_i) begin
    if(rst_i)begin state<=IDLE;address<=0;data<=0;ticks<=0;timeout_count<=0;done_o<=0;fault_o<=0;end
    else if(!CLOCK_ENABLE || step_i)begin
      done_o<=0;
      case(state)
        IDLE:if(valid_i && ready_o)begin address<=addr_i;data<=data_i;state<=ADDRESS;end
        ADDRESS:state<=GAP;
        GAP:begin ticks<=0;state<=DATA;end
        DATA:if(ticks==3)begin state<=WAIT_BUSY;timeout_count<=0;end else ticks<=ticks+1'b1;
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
