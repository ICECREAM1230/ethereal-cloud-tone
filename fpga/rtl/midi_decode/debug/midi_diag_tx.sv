// SPDX-License-Identifier: MIT
module midi_diag_tx #(parameter integer CLK_HZ=50000000, BAUD=115200)(input clk,rstn,input [7:0] data,input valid,output wire ready,output wire tx);
localparam integer DIV=CLK_HZ/BAUD;
localparam integer TICK_W=(DIV>1)?$clog2(DIV):1;
reg [9:0] shift;reg [3:0] bits;reg [TICK_W-1:0] tick;
// Preserve the original power-up idle values without declaration/process writes.
initial begin shift=10'h3ff;bits=0;tick=0;end
assign ready=(bits==0);assign tx=(bits==0)?1'b1:shift[0];
always @(posedge clk) begin
 if(!rstn) begin bits<=0;tick<=0;shift<=10'h3ff;end
 else if(bits==0) begin
  if(valid) begin shift<={1'b1,data,1'b0};bits<=10;tick<=TICK_W'(DIV-1);end
 end else if(tick==0) begin shift<={1'b1,shift[9:1]};bits<=bits-1;tick<=TICK_W'(DIV-1);end
 else tick<=tick-1;
end
endmodule
