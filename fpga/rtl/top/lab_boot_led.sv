`timescale 1ns/1ps
`default_nettype none
// AE350 startup/liveness indication in the independent board-reference domain.
// Require two distinct successful display ACK reads, then continued progress.
module lab_boot_led #(
    parameter integer CLK_HZ=50_000_000,
    parameter integer STARTUP_TIMEOUT_CYCLES=CLK_HZ*3,
    parameter integer HEARTBEAT_TIMEOUT_CYCLES=CLK_HZ
) (
    input wire clk_i,rst_ni,
    input wire [31:0] status_i,boot_stage_i,display_ack_i,
    output wire led_o
);
    localparam integer SW=$clog2(STARTUP_TIMEOUT_CYCLES+1);
    localparam integer HW=$clog2(HEARTBEAT_TIMEOUT_CYCLES+1);
    reg [SW-1:0] startup_count;
    reg [HW-1:0] quiet_count;
    reg [31:0] previous_ack;
    reg [1:0] ack_progress;
    reg seen_start,stalled;
    // Other shared UART status bits do not participate in CPU health.
    wire unused_status=^{status_i[31:10],status_i[8:7],status_i[5:4]};
    wire cpu_ready=status_i[0] && status_i[2] && status_i[3];
    wire boot_seen=boot_stage_i[31:8]==24'hae3500 && boot_stage_i[7:0]!=0;
    wire ack_advance=display_ack_i!=0 && display_ack_i!=previous_ack;
    wire explicit_fault=status_i[6] || status_i[9] || boot_stage_i==32'hae3500e1;
    wire healthy=cpu_ready && status_i[1] && boot_stage_i==32'hae350021 &&
        ack_progress==2 && !stalled && !explicit_fault;
    wire failure=explicit_fault || stalled || (seen_start && !cpu_ready) ||
        (startup_count==SW'(STARTUP_TIMEOUT_CYCLES) && !healthy);
    always @(posedge clk_i) begin
        if(!rst_ni) begin
            startup_count<=0;quiet_count<=0;previous_ack<=0;
            ack_progress<=0;seen_start<=0;stalled<=0;
        end else begin
            previous_ack<=display_ack_i;
            if(boot_seen)seen_start<=1;
            if(startup_count<SW'(STARTUP_TIMEOUT_CYCLES))startup_count<=startup_count+1'b1;
            if(!cpu_ready || display_ack_i==0) begin
                ack_progress<=0;quiet_count<=0;
                if(seen_start && !cpu_ready)stalled<=1;
            end else if(ack_advance) begin
                quiet_count<=0;
                if(ack_progress<2)ack_progress<=ack_progress+1'b1;
                // A stopped CPU needs two fresh advances to regain green.
                if(stalled && ack_progress==1)stalled<=0;
            end else if(quiet_count<HW'(HEARTBEAT_TIMEOUT_CYCLES)) begin
                quiet_count<=quiet_count+1'b1;
                if(quiet_count==HW'(HEARTBEAT_TIMEOUT_CYCLES-1)) begin
                    ack_progress<=0;stalled<=1;
                end
            end
        end
    end
    // Preserve the main-project WS2812 GRB timing and low-brightness palette.
    midi_status_rgb #(.CLK_HZ(CLK_HZ)) u_pixel(
        .clk(clk_i),.rst(!rst_ni),.configured(healthy && !failure),
        .error_code({7'd0,failure}),.packet_valid(1'b0),
        .enumerating(boot_seen || seen_start),.overflow(1'b0),.session_flush(1'b0),.led(led_o));
endmodule
`default_nettype wire
