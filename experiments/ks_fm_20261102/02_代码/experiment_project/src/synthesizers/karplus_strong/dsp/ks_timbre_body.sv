// Four coupled-rotation modes, independently for left and right. All modes
// read committed state. A completed candidate remains speculative until the
// owning PCM frame publishes. Cancellation cannot advance the resonators.
`default_nettype none
module ks_timbre_body (
    input wire clk_i, rst_n_i, clear_i, cancel_i, start_i, commit_i,
    input wire [2047:0] words_i,
    input wire signed [31:0] left_i, right_i,
    output reg done_o,
    output reg signed [31:0] left_o, right_o
);
    reg busy;
    reg [2:0] index;
    reg signed [31:0] s1[0:7], s2[0:7], n1[0:7], n2[0:7];
    reg signed [31:0] in_l,in_r;
    reg signed [63:0] sum_l,sum_r;
    wire [1:0] mode_index=index[2:1];
    wire signed [17:0] c=$signed(words_i[(24+mode_index*4)*32+:18]);
    wire signed [17:0] s=$signed(words_i[(25+mode_index*4)*32+:18]);
    wire signed [18:0] g=$signed({1'b0,words_i[(26+mode_index*4)*32+:18]});
    wire signed [16:0] weight=$signed({1'b0,words_i[(27+mode_index*4)*32+:16]});
    wire signed [31:0] x=index[0] ? in_r : in_l;
    wire signed [63:0] p1=c*s1[index],p2=s*s2[index],p3=g*x;
    wire signed [63:0] p4=s*s1[index],p5=c*s2[index];
    function automatic signed [63:0] trunc0(input signed [63:0] v,input integer sh);
        trunc0=v<0 ? -((-v)>>>sh) : v>>>sh;
    endfunction
    function automatic signed [31:0] sat32(input signed [63:0] v);
        if(v>64'sd2147483647) sat32=32'sh7fffffff;
        else if(v< -64'sd2147483648) sat32=32'sh80000000;
        else sat32=v[31:0];
    endfunction
    wire signed [31:0] next1=sat32(trunc0(p1-p2+p3,17));
    wire signed [31:0] next2=sat32(trunc0(p4+p5,17));
    wire signed [63:0] contribution=next1*weight;
    wire signed [16:0] dry=$signed({1'b0,words_i[18*32+:16]});
    integer j;
    always @(posedge clk_i) begin
        if(!rst_n_i || clear_i) begin
            busy<=0;done_o<=0;index<=0;in_l<=0;in_r<=0;sum_l<=0;sum_r<=0;left_o<=0;right_o<=0;
            for(j=0;j<8;j=j+1) begin s1[j]<=0;s2[j]<=0;n1[j]<=0;n2[j]<=0;end
        end else begin
            done_o<=0;
            if(commit_i && !cancel_i) for(j=0;j<8;j=j+1) begin s1[j]<=n1[j];s2[j]<=n2[j];end
            if(cancel_i) busy<=0;
            else if(start_i) begin
                in_l<=left_i;in_r<=right_i;sum_l<=left_i*dry;sum_r<=right_i*dry;index<=0;busy<=1;
            end else if(busy) begin
                n1[index]<=next1;n2[index]<=next2;
                if(index[0]) sum_r<=sum_r+contribution; else sum_l<=sum_l+contribution;
                if(index==7) begin
                    left_o<=sat32(trunc0(sum_l,15));
                    right_o<=sat32(trunc0(sum_r+contribution,15));busy<=0;done_o<=1;
                end else index<=index+1'b1;
            end
        end
    end
endmodule
`default_nettype wire
