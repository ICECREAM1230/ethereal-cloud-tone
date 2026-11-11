`default_nettype none
package flute_math_pkg;
    // S32/F26. All intermediates explicitly sign extended before arithmetic.
    localparam logic signed [31:0] ONE = 32'sd67108864;
    localparam logic signed [31:0] POLE = 32'sd43893391;
    localparam logic signed [31:0] DC_POLE = 32'sd66437775;
    localparam logic signed [31:0] OUTPUT_GAIN = 32'sd14112994;
    localparam logic signed [31:0] SUSTAIN = 32'sd53687091;
    localparam logic signed [31:0] DECAY_STEP = 32'sd5592;
    localparam logic signed [31:0] RELEASE_STEP = 32'sd13981;

    function automatic logic signed [64:0] wide(input logic signed [31:0] a);
        return {{33{a[31]}}, a};
    endfunction
    function automatic logic signed [31:0] sat32(input logic signed [64:0] a);
        if (a > 65'sd2147483647) return 32'sh7fffffff;
        if (a < -65'sd2147483648) return 32'sh80000000;
        return a[31:0];
    endfunction
    function automatic logic signed [23:0] sat24(input logic signed [64:0] a);
        if (a > 65'sd8388607) return 24'sh7fffff;
        if (a < -65'sd8388608) return 24'sh800000;
        return a[23:0];
    endfunction
    function automatic logic [31:0] xorshift32(input logic [31:0] a);
        logic [31:0] b, c;
        b = a ^ (a << 13);
        c = b ^ (b >> 17);
        return c ^ (c << 5);
    endfunction
endpackage
`default_nettype wire
