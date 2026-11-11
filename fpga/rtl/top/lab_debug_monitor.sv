`timescale 1ns/1ps
`default_nettype none
// Passive APB observation. Never drives the bus or resets from CPU PRESETN.
module lab_debug_monitor #(
    parameter integer SAMPLE_CYCLES=1_000_000
) (
    input wire clk_i,rst_ni,apb_clk_i,
    input wire core_lock_i,video_lock_i,preset_ni,
    input wire [31:0] paddr_i,pwdata_i,prdata_i,
    input wire psel_i,penable_i,pwrite_i,pready_i,pslverr_i,
    output wire [31:0] status_o,boot_stage_o,apb_count_o,last_addr_o,
    output wire [31:0] last_wdata_o,last_rdata_o,trap_cause_o,trap_pc_o,trap_value_o,
    output wire [31:0] display_ack_o
);
    localparam integer TIMER_WIDTH=SAMPLE_CYCLES>1?$clog2(SAMPLE_CYCLES):1;
    logic [TIMER_WIDTH-1:0] sample_count;
    logic request_level,request_busy,snapshot_ack;
    (* async_reg="true" *) logic request_meta,request_sync,ack_meta,ack_sync;
    (* async_reg="true" *) logic [2:0] flags_meta,flags_sync;
    logic [319:0] payload_hold,payload_sys;
    logic [31:0] boot_stage,apb_count,last_addr,last_wdata,last_rdata;
    logic [31:0] trap_cause,trap_pc,trap_value;
    logic [31:0] display_ack;
    logic seen_access,seen_write,seen_error,seen_publish,trap_complete;
    logic [1:0] trap_word;
    wire access_complete=psel_i && penable_i && pready_i;
    wire [31:0] apb_flags={22'd0,trap_complete,1'b1,seen_publish,
        seen_error,seen_write,seen_access,4'd0};
    always_ff @(posedge apb_clk_i or negedge rst_ni) begin
        if(!rst_ni) begin
            boot_stage<=0;apb_count<=0;last_addr<=0;last_wdata<=0;last_rdata<=0;
            trap_cause<=0;trap_pc<=0;trap_value<=0;trap_word<=0;
            display_ack<=0;
            seen_access<=0;seen_write<=0;seen_error<=0;seen_publish<=0;trap_complete<=0;
            request_meta<=0;request_sync<=0;snapshot_ack<=0;payload_hold<=0;
        end else begin
            request_meta<=request_level;request_sync<=request_meta;
            if(request_sync!=snapshot_ack) begin
                payload_hold<={apb_flags,boot_stage,apb_count,last_addr,last_wdata,
                    last_rdata,trap_cause,trap_pc,trap_value,display_ack};
                snapshot_ack<=request_sync;
            end
            if(access_complete) begin
                apb_count<=apb_count+1'b1;last_addr<=paddr_i;seen_access<=1;
                if(pslverr_i)seen_error<=1;
                if(!pwrite_i) begin
                    last_rdata<=prdata_i;
                    // Only successful CPU reads of the display ACK count as
                    // evidence that the running firmware observed a frame.
                    if(!pslverr_i && paddr_i[15:0]==16'h0054)display_ack<=prdata_i;
                end
                else begin
                    last_wdata<=pwdata_i;seen_write<=1;
                    if(paddr_i[15:0]==16'h0050)seen_publish<=1;
                    // Observe requests even when PSLVERR is set: the raw
                    // address and sticky error flag retain that distinction.
                    if(paddr_i[15:0]==16'h001c) begin
                        if(trap_word!=0) begin
                            case(trap_word)
                                1:trap_cause<=pwdata_i;
                                2:trap_pc<=pwdata_i;
                                3:begin trap_value<=pwdata_i;trap_complete<=1;end
                                default:begin end
                            endcase
                            trap_word<=trap_word==3?2'd0:trap_word+1'b1;
                        end else if(pwdata_i==32'hdead3500) begin
                            trap_word<=1;trap_complete<=0;
                        end else if(pwdata_i[31:16]==16'hae35)boot_stage<=pwdata_i;
                    end
                end
            end
        end
    end
    // Payload remains stable until the next request, after the destination
    // has observed the two-stage synchronized acknowledgement.
    always_ff @(posedge clk_i or negedge rst_ni) begin
        if(!rst_ni) begin
            sample_count<=0;request_level<=0;request_busy<=0;
            ack_meta<=0;ack_sync<=0;flags_meta<=0;flags_sync<=0;payload_sys<=0;
        end else begin
            ack_meta<=snapshot_ack;ack_sync<=ack_meta;
            flags_meta<={preset_ni,video_lock_i,core_lock_i};flags_sync<=flags_meta;
            if(sample_count!=TIMER_WIDTH'(SAMPLE_CYCLES-1))sample_count<=sample_count+1'b1;
            else if(!request_busy) begin
                sample_count<=0;request_level<=~request_level;request_busy<=1;
            end
            if(request_busy && ack_sync==request_level) begin
                payload_sys<=payload_hold;request_busy<=0;
            end
        end
    end
    assign {boot_stage_o,apb_count_o,last_addr_o,last_wdata_o,last_rdata_o,
        trap_cause_o,trap_pc_o,trap_value_o,display_ack_o}=payload_sys[287:0];
    assign status_o=payload_sys[319:288] | {28'd0,flags_sync[2],rst_ni,flags_sync[1:0]};
endmodule
`default_nettype wire
