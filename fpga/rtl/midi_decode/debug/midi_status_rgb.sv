// SPDX-License-Identifier: MIT
// One WS2812B status pixel, entirely in the USB clock domain.
// Green means MIDI class enumeration/configuration completed. Cyan is an
// accepted packet indication, not proof of semantic or PC capture correctness.
module midi_status_rgb #(
    parameter integer CLK_HZ = 48000000,
    parameter integer ACTIVITY_CYCLES = CLK_HZ/8
) (
    input  wire clk,
    input  wire rst,
    input  wire configured,
    input  wire [7:0] error_code,
    input  wire packet_valid,
    input  wire enumerating,
    input  wire overflow,
    input  wire session_flush,
    output reg led
);
    localparam integer BIT_CYCLES = CLK_HZ / 800000;
    localparam integer T0_HIGH = (CLK_HZ / 1000000) * 400 / 1000;
    localparam integer T1_HIGH = (CLK_HZ / 1000000) * 800 / 1000;
    localparam integer LATCH_CYCLES = CLK_HZ / 10000 * 3; // >=300 us low
    localparam integer BLINK_CYCLES = CLK_HZ / 2;
    localparam integer CW = $clog2(LATCH_CYCLES + 1);
    localparam integer BW = $clog2(BLINK_CYCLES + 1);

    reg [BW-1:0] blink_count;
    reg blink;
    localparam integer AW = $clog2(ACTIVITY_CYCLES+1);
    reg [AW-1:0] activity_count;

    always @(posedge clk) begin
        if (rst) begin
            blink_count <= 0;
            blink <= 1'b0;
            activity_count <= 0;
        end else begin
            if (blink_count == BW'(BLINK_CYCLES-1)) begin
                blink_count <= 0;
                blink <= !blink;
            end else
                blink_count <= blink_count + 1'b1;

            if (session_flush || !configured) begin
                activity_count <= 0;
            end else if (packet_valid) activity_count <= AW'(ACTIVITY_CYCLES);
            else if(activity_count!=0)activity_count<=activity_count-1'b1;
        end
    end

    reg [23:0] rgb;
    always @* begin
        rgb = blink ? 24'h000018 : 24'h000004; // waiting/enumeration
        if(enumerating)rgb=blink ? 24'h181000 : 24'h080400;
        if(configured)rgb=activity_count!=0 ? 24'h001818 : 24'h001800;
        if (error_code != 0 || overflow)
            rgb = blink ? 24'h200000 : 24'h040000;
    end

    reg [23:0] shift_grb;
    reg [4:0] bit_index;
    reg [CW-1:0] cycle_count;
    reg sending;
    always @(posedge clk) begin
        if (rst) begin
            shift_grb <= 0;
            bit_index <= 0;
            cycle_count <= 0;
            sending <= 1'b0;
            led <= 1'b0;
        end else if (!sending) begin
            led <= 1'b0;
            if (cycle_count == CW'(LATCH_CYCLES-1)) begin
                shift_grb <= {rgb[15:8], rgb[23:16], rgb[7:0]};
                bit_index <= 0;
                cycle_count <= 0;
                sending <= 1'b1;
            end else
                cycle_count <= cycle_count + 1'b1;
        end else begin
            led <= cycle_count < CW'(shift_grb[23] ? T1_HIGH : T0_HIGH);
            if (cycle_count == CW'(BIT_CYCLES-1)) begin
                shift_grb <= {shift_grb[22:0], 1'b0};
                cycle_count <= 0;
                if (bit_index == 5'd23)
                    sending <= 1'b0;
                else
                    bit_index <= bit_index + 1'b1;
            end else
                cycle_count <= cycle_count + 1'b1;
        end
    end
endmodule
