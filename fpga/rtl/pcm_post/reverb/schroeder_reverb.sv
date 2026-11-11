// Schroeder 1962 Fig.6, fixed-point stereo adaptation. See docs/数值与接口规格.md.
// Physical RAM uses official generated SDP IP (SYNC, one-cycle read).
// Full fallback preserves the frozen AE350 trim29184 allocation/scrub guard.
// COMPACT retains independent stereo and both allpasses, using combs 0 and 3.
// It reduces delay RAM capacity; fewer echo trains trade late-tail density.
module schroeder_reverb #(
    parameter bit COMPACT = 1'b0
) (
    input  logic clk,
    input  logic rst_n,
    input  logic clear,
    input  logic sample_tick,
    input  logic signed [23:0] pcm_l_in,
    input  logic signed [23:0] pcm_r_in,
    input  logic bypass,
    input  logic mute,
    input  logic [15:0] wet_q15,
    input  logic [15:0] damp_q15,
    input  logic [15:0] feedback0_q15,
    input  logic [15:0] feedback1_q15,
    input  logic [15:0] feedback2_q15,
    input  logic [15:0] feedback3_q15,
    output logic signed [23:0] pcm_l_out,
    output logic signed [23:0] pcm_r_out,
    output logic out_valid,
    output wire ready,
    output wire busy,
    output logic late,
    output logic clipped
);
    localparam logic [15:0] AP_GAIN = 16'd22938;
    localparam logic [15:0] G_MAX = 16'd32112;
    localparam logic [15:0] D_MAX = 16'd28672;
    localparam logic [15:0] UNITY = 16'd32768;

    typedef enum logic [4:0] {
        SCRUB, IDLE, READ_ADDR, READ_RAM, READ_WAIT, READ_LAUNCH, LP_WAIT, LP_DONE,
        FB_LAUNCH, FB_WAIT, FB_DONE, WRITE_COMB,
        AP_WAIT, AP_STATE, AP_OUT_LAUNCH, AP_OUT_WAIT, AP_OUTPUT, WRITE_AP,
        MIX_L_LAUNCH, MIX_L_WAIT, MIX_L_DONE,
        MIX_R_LAUNCH, MIX_R_WAIT, MIX_R_DONE, MAKEUP_WAIT, MAKEUP_DONE
    } state_t;
    state_t state;
    logic [14:0] scrub_addr;
    logic channel;
    logic [2:0] stage;
    localparam integer POINTER_COUNT = COMPACT ? 8 : 12;
    localparam integer LP_COUNT = COMPACT ? 4 : 8;
    localparam integer POINTER_INDEX_BITS = $clog2(POINTER_COUNT);
    localparam integer LP_INDEX_BITS = $clog2(LP_COUNT);
    localparam integer RAM_ADDRESS_BITS = COMPACT ? 14 : 15;
    logic [12:0] ptr [0:POINTER_COUNT-1];
    logic signed [31:0] lp [0:LP_COUNT-1];
    logic [15:0] gain [0:3];
    logic [15:0] wet_hold, damp_hold;
    logic bypass_hold, mute_hold;
    logic signed [23:0] dry_l, dry_r, mixed_l;
    logic signed [31:0] injected;
    logic signed [31:0] delayed, lp_value, ap_signal, wet_l, wet_r;
    logic signed [31:0] lp_read;
    logic signed [33:0] comb_sum;

    // Explicitly shared registered multiplier. Operands launch -> wait -> consume.
    logic signed [32:0] mul_a;
    logic [15:0] mul_b;
    wire signed [49:0] mul_product;
    // Preserve the original registered 33x17 product, async reset and clear
    // hold. Q15 truncation must precede each saturating add/sub below.
    post_reverb_mult u_shared_mult (
        .clk(clk), .rst_n(rst_n), .ce(!clear),
        .a(mul_a), .b(mul_b), .product(mul_product)
    );
    wire signed [34:0] scaled_product = trunc_q15(mul_product);

    wire [1:0] compact_stage = stage==0 ? 2'd0 : stage==3 ? 2'd1 : stage==4 ? 2'd2 : 2'd3;
    wire [POINTER_INDEX_BITS-1:0] pointer_index = POINTER_INDEX_BITS'(COMPACT ? {1'b0,channel,compact_stage} :
                                                   (channel ? 4'd6 : 4'd0) + {1'b0,stage});
    wire [LP_INDEX_BITS-1:0] lp_index = LP_INDEX_BITS'(COMPACT ? {1'b0,channel,(stage==3)} : {channel,stage[1:0]});
    wire [14:0] address = base_address(channel,stage) + {2'b0,ptr[pointer_index]};
    logic [14:0] delay_address;
    wire signed [31:0] dry_current = channel ? extend24(dry_r) : extend24(dry_l);
    wire signed [35:0] product_wide = {scaled_product[34],scaled_product};
    wire signed [35:0] delayed_wide = {{4{delayed[31]}},delayed};
    wire signed [35:0] calc_lp = delayed_wide + product_wide;
    wire signed [35:0] calc_feedback = {{4{injected[31]}},injected} + product_wide;
    wire signed [35:0] calc_ap_state = {{4{ap_signal[31]}},ap_signal} + product_wide;
    wire signed [35:0] calc_ap_output = delayed_wide - product_wide;
    wire signed [35:0] calc_makeup = $signed({{4{ap_signal[31]}},ap_signal}) <<< 1;
    wire signed [35:0] calc_mix = {{4{dry_current[31]}},dry_current} + product_wide;

    logic ram_write, ram_read;
    logic signed [31:0] ram_din;
    logic signed [31:0] write_data;
    logic [RAM_ADDRESS_BITS-1:0] ram_wa;
    wire signed [31:0] ram_dout;

    generate if (COMPACT) begin : g_compact_ram
        schroeder_delay_ram_compact u_delay_ram (
            .dout(ram_dout), .clka(clk), .cea(ram_write),
            .clkb(clk), .ceb(ram_read), .oce(1'b1), .reset(!rst_n),
            .ada(ram_wa), .din(ram_din), .adb(delay_address[13:0])
        );
    end else begin : g_full_ram
    schroeder_delay_ram_trim u_delay_ram (
        .dout(ram_dout), .clka(clk), .cea(ram_write),
        .clkb(clk), .ceb(ram_read), .oce(1'b1), .reset(!rst_n),
        .ada(ram_wa), .din(ram_din), .adb(delay_address)
    );
    end endgenerate

    assign ready = rst_n && !clear && (state == IDLE);
    assign busy = !ready;

    always_comb begin
        ram_write = 1'b0;
        ram_read = 1'b0;
        ram_din = write_data;
        ram_wa = RAM_ADDRESS_BITS'(delay_address);
        if (rst_n && !clear) begin
            case (state)
                SCRUB: begin ram_write=COMPACT || (scrub_addr<15'd29184); ram_wa=RAM_ADDRESS_BITS'(scrub_addr); ram_din=32'sd0; end
                READ_RAM: ram_read=1'b1;
                WRITE_COMB, WRITE_AP: ram_write=1'b1;
                default: begin end
            endcase
        end
    end

    function automatic logic signed [31:0] extend24(input logic signed [23:0] v);
        extend24 = {{8{v[23]}},v};
    endfunction
    function automatic logic signed [34:0] trunc_q15(input logic signed [49:0] p);
        logic signed [49:0] corrected;
        begin
            corrected = p < 0 ? p + 50'sd32767 : p;
            trunc_q15 = 35'(corrected >>> 15);
        end
    endfunction
    function automatic logic signed [31:0] divide4(input logic signed [33:0] v);
        logic signed [33:0] corrected;
        begin
            corrected = v < 0 ? v + 34'sd3 : v;
            divide4 = 32'(corrected >>> 2);
        end
    endfunction
    function automatic logic signed [31:0] average_combs(input logic signed [33:0] v);
        logic signed [33:0] corrected;
        begin
            corrected = v < 0 ? v + 34'sd1 : v;
            average_combs = COMPACT ? 32'(corrected >>> 1) : divide4(v);
        end
    endfunction
    function automatic logic signed [31:0] sat32(input logic signed [35:0] v);
        if (v > 36'sd2147483647) sat32=32'sh7fffffff;
        else if (v < -36'sd2147483648) sat32=32'sh80000000;
        else sat32=v[31:0];
    endfunction
    function automatic logic signed [23:0] sat24(input logic signed [35:0] v);
        if (v > 36'sd8388607) sat24=24'sh7fffff;
        else if (v < -36'sd8388608) sat24=24'sh800000;
        else sat24=v[23:0];
    endfunction
    function automatic logic over32(input logic signed [35:0] v);
        over32 = v > 36'sd2147483647 || v < -36'sd2147483648;
    endfunction
    function automatic logic over24(input logic signed [35:0] v);
        over24 = v > 36'sd8388607 || v < -36'sd8388608;
    endfunction
    function automatic logic [14:0] base_address(input logic ch, input logic [2:0] s);
        if (COMPACT) begin
            if (!ch) begin
                case (s)
                    0: base_address=15'd0;
                    3: base_address=15'd2887;
                    4: base_address=15'd6980;
                    default: base_address=15'd7459;
                endcase
            end else begin
                case (s)
                    0: base_address=15'd7622;
                    3: base_address=15'd10525;
                    4: base_address=15'd14636;
                    default: base_address=15'd15123;
                endcase
            end
        end else if (!ch) begin
            case (s)
                0: base_address=15'd0;
                1: base_address=15'd2887;
                2: base_address=15'd6140;
                3: base_address=15'd9763;
                4: base_address=15'd13856;
                default: base_address=15'd14335;
            endcase
        end else begin
            case (s)
                0: base_address=15'd14498;
                1: base_address=15'd17401;
                2: base_address=15'd20672;
                3: base_address=15'd24315;
                4: base_address=15'd28426;
                default: base_address=15'd28913;
            endcase
        end
    endfunction
    function automatic logic [12:0] delay_length(input logic ch, input logic [2:0] s);
        if (!ch) begin
            case (s)
                0: delay_length=13'd2887;
                1: delay_length=13'd3253;
                2: delay_length=13'd3623;
                3: delay_length=13'd4093;
                4: delay_length=13'd479;
                default: delay_length=13'd163;
            endcase
        end else begin
            case (s)
                0: delay_length=13'd2903;
                1: delay_length=13'd3271;
                2: delay_length=13'd3643;
                3: delay_length=13'd4111;
                4: delay_length=13'd487;
                default: delay_length=13'd167;
            endcase
        end
    endfunction

    // Each tap owns its pointer and its constant wrap comparison. A dynamic
    // array update otherwise feeds a 12:1 pointer mux and shared increment
    // back into every pointer register, putting channel on the carry path.
    genvar tap;
    generate for (tap=0; tap<POINTER_COUNT; tap=tap+1) begin: tap_pointer
        localparam logic TAP_CHANNEL = tap >= (COMPACT ? 4 : 6);
        localparam logic [2:0] TAP_STAGE = COMPACT ?
            ((tap%4)==0 ? 3'd0 : (tap%4)==1 ? 3'd3 : (tap%4)==2 ? 3'd4 : 3'd5) : 3'(tap % 6);
        localparam logic [12:0] LAST = delay_length(TAP_CHANNEL,TAP_STAGE)-13'd1;
        always_ff @(posedge clk or negedge rst_n) begin
            if (!rst_n) ptr[tap]<=13'd0;
            else if (clear) ptr[tap]<=13'd0;
            else if ((state==WRITE_COMB || state==WRITE_AP) &&
                     channel==TAP_CHANNEL && stage==TAP_STAGE)
                ptr[tap]<=ptr[tap]==LAST ? 13'd0 : ptr[tap]+13'd1;
        end
    end endgenerate

    integer i;
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state<=SCRUB; scrub_addr<=15'd0; channel<=1'b0; stage<=3'd0;
            out_valid<=1'b0; pcm_l_out<=24'sd0; pcm_r_out<=24'sd0;
            late<=1'b0; clipped<=1'b0;
            dry_l<=24'sd0; dry_r<=24'sd0; mixed_l<=24'sd0;
            injected<=32'sd0; delayed<=32'sd0; lp_value<=32'sd0;
            lp_read<=32'sd0; delay_address<=15'd0;
            ap_signal<=32'sd0; wet_l<=32'sd0; wet_r<=32'sd0;
            comb_sum<=34'sd0; write_data<=32'sd0;
            mul_a<=33'sd0; mul_b<=16'd0;
            wet_hold<=16'd0; damp_hold<=16'd0; bypass_hold<=1'b0; mute_hold<=1'b0;
            for (i=0;i<LP_COUNT;i=i+1) lp[i]<=32'sd0;
            for (i=0;i<4;i=i+1) gain[i]<=16'd0;
        end else if (clear) begin
            state<=SCRUB; scrub_addr<=15'd0; channel<=1'b0; stage<=3'd0;
            out_valid<=1'b0; pcm_l_out<=24'sd0; pcm_r_out<=24'sd0;
            late<=1'b0; clipped<=1'b0; comb_sum<=34'sd0;
            for (i=0;i<LP_COUNT;i=i+1) lp[i]<=32'sd0;
        end else begin
            out_valid<=1'b0;
            if (sample_tick && state!=IDLE && state!=SCRUB) late<=1'b1;
            case (state)
                SCRUB: begin
                    // Preserve the full version's startup/clear duration.
                    // Compact RAM is scrubbed twice; every access is in range.
                    if (scrub_addr==15'd32767) state<=IDLE;
                    else scrub_addr<=scrub_addr+15'd1;
                end
                IDLE: if (sample_tick) begin
                    dry_l<=pcm_l_in; dry_r<=pcm_r_in;
                    injected<=mute ? 32'sd0 : divide4({{10{pcm_l_in[23]}},pcm_l_in});
                    mute_hold<=mute; bypass_hold<=bypass;
                    wet_hold<=wet_q15>UNITY ? UNITY : wet_q15;
                    damp_hold<=damp_q15>D_MAX ? D_MAX : damp_q15;
                    gain[0]<=feedback0_q15>G_MAX ? G_MAX : feedback0_q15;
                    gain[1]<=feedback1_q15>G_MAX ? G_MAX : feedback1_q15;
                    gain[2]<=feedback2_q15>G_MAX ? G_MAX : feedback2_q15;
                    gain[3]<=feedback3_q15>G_MAX ? G_MAX : feedback3_q15;
                    channel<=1'b0; stage<=3'd0; comb_sum<=34'sd0; state<=READ_ADDR;
                end
                // Keep channel/stage selection, pointer mux and base addition
                // off the RAM bank-select pins. The same captured address is
                // retained through both the tap's read and its later write.
                READ_ADDR: begin delay_address<=address; state<=READ_RAM; end
                READ_RAM: state<=READ_WAIT;
                READ_WAIT: begin
                    // Capture the 58-block RAM output mux before subtraction.
                    // Two preparation clocks per tap plus four AP launch
                    // clocks make a 126-clock result, within the 128-clock
                    // module budget and the board's 768-clock frame.
                    delayed<=ram_dout;
                    lp_read<=lp[lp_index];
                    state<=READ_LAUNCH;
                end
                READ_LAUNCH: begin
                    if (stage<3'd4) begin
                        mul_a<=$signed({lp_read[31],lp_read})-$signed({delayed[31],delayed});
                        mul_b<=damp_hold; state<=LP_WAIT;
                    end else begin
                        mul_a<={delayed[31],delayed}; mul_b<=AP_GAIN; state<=AP_WAIT;
                    end
                end
                LP_WAIT: state<=LP_DONE;
                LP_DONE: begin
                    lp_value<=sat32(calc_lp); lp[lp_index]<=sat32(calc_lp);
                    if (over32(calc_lp)) clipped<=1'b1;
                    comb_sum<=comb_sum+$signed({{2{delayed[31]}},delayed});
                    state<=FB_LAUNCH;
                end
                FB_LAUNCH: begin
                    mul_a<={lp_value[31],lp_value};
                    mul_b<=COMPACT ? (stage==0 ? gain[0] : gain[3]) : gain[stage[1:0]];
                    state<=FB_WAIT;
                end
                FB_WAIT: state<=FB_DONE;
                FB_DONE: begin
                    write_data<=sat32(calc_feedback);
                    if (over32(calc_feedback)) clipped<=1'b1;
                    state<=WRITE_COMB;
                end
                WRITE_COMB: begin
                    if (stage==3'd3) begin ap_signal<=average_combs(comb_sum); stage<=3'd4; end
                    else stage<=COMPACT ? 3'd3 : stage+3'd1;
                    state<=READ_ADDR;
                end
                AP_WAIT: state<=AP_STATE;
                AP_STATE: begin
                    write_data<=sat32(calc_ap_state);
                    if (over32(calc_ap_state)) clipped<=1'b1;
                    state<=AP_OUT_LAUNCH;
                end
                // Break DSP output -> rounding -> add -> saturation -> DSP
                // input at the already required saturated delay-write value.
                AP_OUT_LAUNCH: begin
                    mul_a<={write_data[31],write_data};
                    mul_b<=AP_GAIN;
                    state<=AP_OUT_WAIT;
                end
                AP_OUT_WAIT: state<=AP_OUTPUT;
                AP_OUTPUT: begin
                    ap_signal<=sat32(calc_ap_output);
                    if (over32(calc_ap_output)) clipped<=1'b1;
                    state<=WRITE_AP;
                end
                WRITE_AP: begin
                    if (stage==3'd4) begin stage<=3'd5; state<=READ_ADDR; end
                    else if (COMPACT) begin
                        // Two rather than four echo trains: sqrt(2) makeup,
                        // using the existing DSP, approximately preserves wet
                        // energy without a further 3dB loudness advantage.
                        mul_a<={ap_signal[31],ap_signal};mul_b<=16'd46341;
                        state<=MAKEUP_WAIT;
                    end
                    else begin
                        if (over32(calc_makeup)) clipped<=1'b1;
                        if (!channel) begin
                            wet_l<=sat32(calc_makeup); channel<=1'b1; stage<=3'd0;
                            injected<=mute_hold ? 32'sd0 : divide4({{10{dry_r[23]}},dry_r});
                            comb_sum<=34'sd0; state<=READ_ADDR;
                        end else begin wet_r<=sat32(calc_makeup); state<=MIX_L_LAUNCH; end
                    end
                end
                MAKEUP_WAIT: state<=MAKEUP_DONE;
                MAKEUP_DONE: begin
                    if (over32(product_wide)) clipped<=1'b1;
                    if (!channel) begin
                        wet_l<=sat32(product_wide);channel<=1'b1;stage<=3'd0;
                        injected<=mute_hold ? 32'sd0 : divide4({{10{dry_r[23]}},dry_r});
                        comb_sum<=34'sd0;state<=READ_ADDR;
                    end else begin wet_r<=sat32(product_wide);state<=MIX_L_LAUNCH;end
                end
                MIX_L_LAUNCH: begin
                    channel<=1'b0;
                    mul_a<=$signed({wet_l[31],wet_l})-$signed({{9{dry_l[23]}},dry_l});
                    mul_b<=wet_hold; state<=MIX_L_WAIT;
                end
                MIX_L_WAIT: state<=MIX_L_DONE;
                MIX_L_DONE: begin
                    mixed_l<=mute_hold ? 24'sd0 : bypass_hold ? dry_l : sat24(calc_mix);
                    if (over24(calc_mix)) clipped<=1'b1;
                    state<=MIX_R_LAUNCH;
                end
                MIX_R_LAUNCH: begin
                    channel<=1'b1;
                    mul_a<=$signed({wet_r[31],wet_r})-$signed({{9{dry_r[23]}},dry_r});
                    mul_b<=wet_hold; state<=MIX_R_WAIT;
                end
                MIX_R_WAIT: state<=MIX_R_DONE;
                MIX_R_DONE: begin
                    pcm_l_out<=mixed_l;
                    pcm_r_out<=mute_hold ? 24'sd0 : bypass_hold ? dry_r : sat24(calc_mix);
                    if (over24(calc_mix)) clipped<=1'b1;
                    out_valid<=1'b1; state<=IDLE;
                end
                default: begin state<=SCRUB; scrub_addr<=15'd0; end
            endcase
        end
    end
endmodule
