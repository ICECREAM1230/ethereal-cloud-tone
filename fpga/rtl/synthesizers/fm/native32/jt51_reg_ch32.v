// FM32 local fork: 32 voices/128 slots; upstream source is unchanged.
/*  This file is part of JT51.

    JT51 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    JT51 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with JT51.  If not, see <http://www.gnu.org/licenses/>.
    
    Author: Jose Tejada Gomez. Twitter: @topapate
    Version: 1.0
    Date: 23-10-2019
    */

// Channel data is not stored in a CSR as operators
// Proof of that is the Splatter House arcade writes
// channel and operator data in two consequitive accesses
// without enough time in between to have the eight
// channels go through the CSR. So the channel data
// cannot be CSR, but regular registers.
module jt51_reg_ch32(
    input         rst,
    input         clk,
    input         cen,
    input  [ 7:0] din,

    input  [ 4:0] up_ch,
    input         up_rl,
    input         up_kc,
    input         up_kf,
    input         up_pms,

    input      [4:0] ch, // next active channel
    output reg [1:0] rl,
    output reg [2:0] fb_II,
    output reg [2:0] con,
    output reg [6:0] kc,
    output reg [5:0] kf,
    output reg [1:0] ams_VII,
    output reg [2:0] pms
);

wire    [1:0]   rl_in   = din[7:6];
wire    [2:0]   fb_in   = din[5:3];
wire    [2:0]   con_in  = din[2:0];
wire    [6:0]   kc_in   = din[6:0];
wire    [5:0]   kf_in   = din[7:2];
wire    [1:0]   ams_in  = din[1:0];
wire    [2:0]   pms_in  = din[6:4];

reg [1:0] reg_rl[0:31];
reg [2:0] reg_fb[0:31];
reg [2:0] reg_con[0:31];
reg [6:0] reg_kc[0:31];
reg [5:0] reg_kf[0:31];
reg [1:0] reg_ams[0:31];
reg [2:0] reg_pms[0:31];

// Reset only the write-group validity, not the distributed data RAMs.  Keep
// these bitmaps in FFs so reset does not infer auxiliary distributed RAMs.
reg [31:0] valid_rl  /* synthesis syn_ramstyle = "registers" */;
reg [31:0] valid_kc  /* synthesis syn_ramstyle = "registers" */;
reg [31:0] valid_kf  /* synthesis syn_ramstyle = "registers" */;
reg [31:0] valid_pms /* synthesis syn_ramstyle = "registers" */;

wire [4:0] fb_ch  = ch-5'd1;
wire [4:0] ams_ch = ch-5'd6;

always @(posedge clk) if(cen) begin
    // Mask inside the existing output register stage: rst or a stopped cen
    // must not asynchronously change a value already in the pipeline.
    rl      <= valid_rl[ch]      ? reg_rl[ch]      : 2'd0;
    fb_II   <= valid_rl[fb_ch]   ? reg_fb[fb_ch]   : 3'd0;
    con     <= valid_rl[ch]      ? reg_con[ch]     : 3'd0;
    kc      <= valid_kc[ch]      ? reg_kc[ch]      : 7'd0;
    kf      <= valid_kf[ch]      ? reg_kf[ch]      : 6'd0;
    ams_VII <= valid_pms[ams_ch] ? reg_ams[ams_ch] : 2'd0;
    pms     <= valid_pms[ch]     ? reg_pms[ch]     : 3'd0;
end

always @(posedge clk, posedge rst) begin
    if( rst ) begin
        valid_rl  <= 32'd0;
        valid_kc  <= 32'd0;
        valid_kf  <= 32'd0;
        valid_pms <= 32'd0;
    end else begin
        // Writes remain independent of cen, as in the original parameter bank.
        if( up_rl  ) valid_rl[up_ch]  <= 1'b1;
        if( up_kc  ) valid_kc[up_ch]  <= 1'b1;
        if( up_kf  ) valid_kf[up_ch]  <= 1'b1;
        if( up_pms ) valid_pms[up_ch] <= 1'b1;
    end
end

// Invalid data is hidden until its entire write group has been written.  With
// nonblocking updates, same-edge reads still observe the old data/valid bits.
always @(posedge clk) if(!rst) begin
        if( up_rl  ) begin
            reg_rl[up_ch]  <= rl_in;
            reg_fb[up_ch]  <= fb_in;
            reg_con[up_ch] <= con_in;
        end
        if( up_kc  ) reg_kc[up_ch]  <= kc_in;
        if( up_kf  ) reg_kf[up_ch]  <= kf_in;
        if( up_pms ) begin
            reg_ams[up_ch] <= ams_in;
            reg_pms[up_ch] <= pms_in;
        end
end

endmodule
