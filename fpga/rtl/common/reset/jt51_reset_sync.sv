// SPDX-License-Identifier: GPL-3.0-or-later
// Assert without a clock; release only after three native rising edges.
module jt51_reset_sync(input logic clk_i,rst_n_i,output logic rst_n_o);
  (* ASYNC_REG="TRUE" *) logic [2:0] release_pipe;
  always_ff @(posedge clk_i or negedge rst_n_i)
    if(!rst_n_i)release_pipe<=3'b000;
    else release_pipe<={release_pipe[1:0],1'b1};
  assign rst_n_o=release_pipe[2];
endmodule
