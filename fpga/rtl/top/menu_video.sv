`timescale 1ns/1ps
`default_nettype none
module menu_video (
    input wire clk_i,rst_ni,display_valid_i,
    output wire [9:0] text_addr_o,
    input wire [7:0] text_data_i,
    output wire vblank_o,
    output logic [23:0] rgb_o,
    output logic de_o,hs_o,vs_o
);
    logic [10:0] x;
    logic [9:0] y;
    wire active=x<1280 && y<720;
    wire box=x>=320 && x<960 && y>=40 && y<680;
    wire [8:0] dx=9'(x[10:1]-10'd160);
    wire [7:0] dy=y[9:2]-8'd10;
    wire [5:0] column=dx[8:3];
    wire [4:0] row=dy[7:3];
    assign text_addr_o=box ? 10'(row*40+column) : 10'd0;
    assign vblank_o=x==0 && y==720;
    logic active_q,hs_q,vs_q,box_q,valid_q;
    logic [2:0] glyph_x_q,glyph_y_q;
    logic [5:0] column_q;
    logic [4:0] row_q;
    logic [7:0] character;
    wire [34:0] bits=glyph(character);
    wire ink=glyph_x_q<5 && glyph_y_q<7 && bits[34-5*int'(glyph_y_q)-int'(glyph_x_q)];
    function automatic [7:0] waiting_char(input [4:0] r,input [5:0] c);
        reg [127:0] s;
        begin
            s=r==7 ? "FPGA VIDEO READY" : r==9 ? "WAIT AE350      " : "                ";
            waiting_char=(c>=11 && c<27) ? 8'(s>>(8*(26-c))) : 8'h20;
        end
    endfunction
    always_comb character=valid_q ? text_data_i : waiting_char(row_q,column_q);
    always_ff @(posedge clk_i or negedge rst_ni)begin
        if(!rst_ni)begin
            x<=0;y<=0;active_q<=0;hs_q<=0;vs_q<=0;box_q<=0;valid_q<=0;
            glyph_x_q<=0;glyph_y_q<=0;column_q<=0;row_q<=0;
            rgb_o<=0;de_o<=0;hs_o<=0;vs_o<=0;
        end else begin
            if(x==1649)begin x<=0;if(y==749)y<=0;else y<=y+1;end else x<=x+1;
            active_q<=active;hs_q<=x>=1390 && x<1430;vs_q<=y>=725 && y<730;
            box_q<=box;valid_q<=display_valid_i;
            glyph_x_q<=dx[2:0];glyph_y_q<=dy[2:0];column_q<=column;row_q<=row;
            de_o<=active_q;hs_o<=hs_q;vs_o<=vs_q;
            if(!active_q)rgb_o<=0;
            else if(box_q && ink)rgb_o<=character==8'h3e ? 24'h54e9c5 : 24'he4eef4;
            else rgb_o<=24'h091522;
        end
    end
    // Compact 5x7 ASCII font; geometry is hardware, strings/values are CPU data.
    function automatic [34:0] glyph(input [7:0] c);
        case(c)
            "0":glyph=35'b01110_10001_10011_10101_11001_10001_01110;
            "1":glyph=35'b00100_01100_00100_00100_00100_00100_01110;
            "2":glyph=35'b01110_10001_00001_00010_00100_01000_11111;
            "3":glyph=35'b11110_00001_00001_01110_00001_00001_11110;
            "4":glyph=35'b00010_00110_01010_10010_11111_00010_00010;
            "5":glyph=35'b11111_10000_10000_11110_00001_00001_11110;
            "6":glyph=35'b01110_10000_10000_11110_10001_10001_01110;
            "7":glyph=35'b11111_00001_00010_00100_01000_01000_01000;
            "8":glyph=35'b01110_10001_10001_01110_10001_10001_01110;
            "9":glyph=35'b01110_10001_10001_01111_00001_00001_01110;
            "A":glyph=35'b01110_10001_10001_11111_10001_10001_10001;
            "B":glyph=35'b11110_10001_10001_11110_10001_10001_11110;
            "C":glyph=35'b01111_10000_10000_10000_10000_10000_01111;
            "D":glyph=35'b11110_10001_10001_10001_10001_10001_11110;
            "E":glyph=35'b11111_10000_10000_11110_10000_10000_11111;
            "F":glyph=35'b11111_10000_10000_11110_10000_10000_10000;
            "G":glyph=35'b01111_10000_10000_10111_10001_10001_01111;
            "H":glyph=35'b10001_10001_10001_11111_10001_10001_10001;
            "I":glyph=35'b11111_00100_00100_00100_00100_00100_11111;
            "J":glyph=35'b00111_00010_00010_00010_10010_10010_01100;
            "K":glyph=35'b10001_10010_10100_11000_10100_10010_10001;
            "L":glyph=35'b10000_10000_10000_10000_10000_10000_11111;
            "M":glyph=35'b10001_11011_10101_10101_10001_10001_10001;
            "N":glyph=35'b10001_11001_11001_10101_10011_10011_10001;
            "O":glyph=35'b01110_10001_10001_10001_10001_10001_01110;
            "P":glyph=35'b11110_10001_10001_11110_10000_10000_10000;
            "Q":glyph=35'b01110_10001_10001_10001_10101_10010_01101;
            "R":glyph=35'b11110_10001_10001_11110_10100_10010_10001;
            "S":glyph=35'b01111_10000_10000_01110_00001_00001_11110;
            "T":glyph=35'b11111_00100_00100_00100_00100_00100_00100;
            "U":glyph=35'b10001_10001_10001_10001_10001_10001_01110;
            "V":glyph=35'b10001_10001_10001_10001_10001_01010_00100;
            "W":glyph=35'b10001_10001_10001_10101_10101_10101_01010;
            "X":glyph=35'b10001_10001_01010_00100_01010_10001_10001;
            "Y":glyph=35'b10001_10001_01010_00100_00100_00100_00100;
            "Z":glyph=35'b11111_00001_00010_00100_01000_10000_11111;
            ">":glyph=35'b00000_10000_01000_00100_01000_10000_00000;
            "+":glyph=35'b00000_00100_00100_11111_00100_00100_00000;
            "-":glyph=35'b00000_00000_00000_11111_00000_00000_00000;
            "/":glyph=35'b00001_00001_00010_00100_01000_10000_10000;
            default:glyph=0;
        endcase
    endfunction
endmodule
`default_nettype wire
