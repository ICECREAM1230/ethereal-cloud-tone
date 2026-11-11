`timescale 1ns/1ps
`default_nettype none
// Candidate only: all ports are synchronous to pclk_i. CDC and ingress
// arbitration belong outside this block. No PCM or voice allocation lives here.
module control_link_apb #(
    parameter integer RX_DEPTH=32,
    parameter integer TX_DEPTH=4,
    parameter integer FB_DEPTH=8
) (
    input wire pclk_i,preset_ni,
    input wire [31:0] paddr_i,pwdata_i,
    input wire [3:0] pstrb_i,
    input wire psel_i,penable_i,pwrite_i,
    output logic [31:0] prdata_o,
    output wire pready_o,
    output logic pslverr_o,
    input wire rx_valid_i,
    output wire rx_ready_o,
    input wire [1:0] rx_source_i,
    input wire [7:0] rx_data_i,
    input wire [63:0] rx_tick_i,
    input wire flush_valid_i,
    output wire flush_ready_o,
    input wire [3:0] flush_mask_i,
    input wire [63:0] flush_tick_i,
    output wire rx_overflow_o,rx_recovering_o,
    output wire cmd_valid_o,
    input wire cmd_ready_i,
    output wire [159:0] cmd_data_o,
    output wire [31:0] cmd_sequence_o,
    input wire fb_valid_i,
    output wire fb_ready_o,
    input wire [1:0] fb_engine_i,
    input wire [4:0] fb_voice_i,
    input wire [31:0] fb_generation_i,
    input wire [31:0] backend_status_i,backend_fault_i,cmd_result_i,
    output wire flush_done_valid_o,
    output wire [3:0] flush_done_mask_o,
    output wire [31:0] flush_done_sequence_o,
    output wire route_valid_o,
    input wire route_ready_i,
    output wire [1:0] route_o,
    input wire template_valid_i,
    output wire template_ready_o,
    input wire [15:0] template_addr_i,
    input wire [31:0] template_data_i,
    input wire [15:0] template_free_i,
    input wire template_overflow_i,
    output logic sub_patch1_supported_o,
    output logic [415:0] template_status_o
);
    localparam integer RCW=$clog2(RX_DEPTH+1),TCW=$clog2(TX_DEPTH+1),FCW=$clog2(FB_DEPTH+1);
    localparam integer RPW=RX_DEPTH>1 ? $clog2(RX_DEPTH) : 1;
    typedef struct packed {
        logic [63:0] tick;
        logic [1:0] source;
        logic [7:0] data;
    } raw_t;
    raw_t rx_q[0:RX_DEPTH-1],rx_head_value;
    // Selective flush invalidates narrow tags, never dynamically compacts the
    // entire 74-bit FIFO. Invalid head slots retire at one slot per PCLK.
    logic [RX_DEPTH-1:0] rx_live,rx_live_d;
    logic [RCW-1:0] rx_live_count;
    logic [RCW-1:0] rx_count,rx_count_d;
    logic [RPW-1:0] rx_head,rx_head_d,rx_tail,rx_tail_d;
    // Payload slots never move. APB capture only advances the head; ingress
    // writes the current tail, independently of this cycle's capture/count_d.
    assign rx_head_value=rx_q[rx_head];
    logic [159:0] tx_q[0:TX_DEPTH-1],tx_d[0:TX_DEPTH-1];
    logic [31:0] tx_seq[0:TX_DEPTH-1],tx_seq_d[0:TX_DEPTH-1];
    logic [TCW-1:0] tx_count,tx_count_d;
    logic [31:0] shadow[0:4],tx_ack,tx_result;
    logic route_busy;
    logic [1:0] route_request,route_ack;
    logic snapshot_valid,snapshot_valid_d;
    logic [31:0] snapshot_meta,snapshot_meta_d,snapshot_seq,snapshot_seq_d;
    logic [63:0] snapshot_tick,snapshot_tick_d;
    logic [3:0] flush_pending,flush_pending_d;
    logic [63:0] pending_tick,pending_tick_d;
    logic [31:0] flush_serial,flush_serial_d,pending_seq,pending_seq_d;
    logic pending_recovery,pending_recovery_d;
    logic recovering,recovering_d,overflow_sticky,overflow_sticky_d;
    logic resume_requested,resume_requested_d;
    logic [31:0] overflow_count,overflow_count_d;
    logic cancel_hold;
    raw_t cancelled_raw;
    logic stalled;
    raw_t stalled_raw;
    wire [15:0] offset=paddr_i[15:0];
    wire aligned=paddr_i[31:16]==16'hf801 && paddr_i[1:0]==0;
    wire access=psel_i && penable_i;
    wire flush_fire=flush_valid_i && flush_ready_o;
    // Keep fixed byte comparisons before the scalar protocol guard reduction.
    // No state or cycle is added to cancelled/stalled raw-record checking.
    wire [7:0] cancelled_tick_byte_diff /* synthesis syn_keep = 1 */;
    wire [7:0] stalled_tick_byte_diff /* synthesis syn_keep = 1 */;
    generate for(genvar raw_tick_byte=0;raw_tick_byte<8;raw_tick_byte++)begin:g_raw_tick_compare
        assign cancelled_tick_byte_diff[raw_tick_byte]=
            rx_tick_i[raw_tick_byte*8+:8]!=cancelled_raw.tick[raw_tick_byte*8+:8];
        assign stalled_tick_byte_diff[raw_tick_byte]=
            rx_tick_i[raw_tick_byte*8+:8]!=stalled_raw.tick[raw_tick_byte*8+:8];
    end endgenerate
    wire cancelled_changed=cancel_hold && rx_valid_i &&
        (rx_source_i!=cancelled_raw.source || rx_data_i!=cancelled_raw.data || (|cancelled_tick_byte_diff));
    wire raw_fault=!recovering && ((rx_valid_i && rx_source_i==2'd3) || cancelled_changed ||
        (stalled && !(flush_fire && flush_mask_i[stalled_raw.source]) &&
         (!rx_valid_i || rx_source_i!=stalled_raw.source ||
         rx_data_i!=stalled_raw.data || (|stalled_tick_byte_diff))));
    wire raw_fire=rx_valid_i && rx_ready_o && !raw_fault;
    wire cmd_fire=cmd_valid_o && cmd_ready_i;
    logic candidate_valid;
    logic [31:0] candidate_meta,candidate_seq;
    logic [63:0] candidate_tick;
    typedef struct packed {
        logic [31:0] generation;
        logic [4:0] voice;
        logic [1:0] engine;
    } feedback_t;
    feedback_t fb_q[0:FB_DEPTH-1],fb_d[0:FB_DEPTH-1],fb_snapshot;
    logic [FCW-1:0] fb_count,fb_count_d;
    logic fb_snapshot_valid;
    wire fb_legal=fb_engine_i<=2 &&
        ((fb_engine_i==0) || (fb_engine_i==1 && fb_voice_i<16) || (fb_engine_i==2));
    wire fb_fire=fb_valid_i && fb_ready_o;
    wire fb_read_address=offset==16'h0060 || offset==16'h0064;
    wire fb_capture=access && !pwrite_i && aligned && fb_read_address &&
        fb_available && !fb_snapshot_valid;
    wire fb_pop=write_access && offset==16'h0068;
    wire fb_available=fb_snapshot_valid || fb_count!=0;
    feedback_t fb_candidate;
    wire record_address=offset==16'h0010 || offset==16'h0014 ||
        offset==16'h0018 || offset==16'h0020;
    logic write_error;
    // Read candidate/fault decode must not feed command or other write CEs.
    // The externally visible error still selects the same read/write rules.
    wire write_access=access && pwrite_i && aligned && !write_error;
    // Firmware POPs a flush only after clearing its source owners and waiting
    // for all cleanup commands to be accepted by the real voice backends.
    assign flush_done_valid_o=write_access && offset==16'h001c && snapshot_meta[16];
    assign flush_done_mask_o=snapshot_meta[3:0];
    assign flush_done_sequence_o=snapshot_seq;
    assign route_valid_o=preset_ni && route_busy;
    assign route_o=route_request;
    wire capture=access && !pwrite_i && !pslverr_o && record_address && !snapshot_valid;
    // COMMIT depends only on its own error rules, not other write offsets.
    wire commit=access && pwrite_i && aligned && offset==16'h0054 && !commit_write_error;
    // ABI2 contains native prepared payload, never MIDI/source/channel fields.
    // The native executor owns flags, fragments, revisions and generations.
    wire prepared_global=shadow[0][23:16]==8'h15;
    wire prepared_opcode=shadow[0][23:16]==8'h01 || shadow[0][23:16]==8'h04 ||
        shadow[0][23:16]==8'h10 || shadow[0][23:16]==8'h11 ||
        shadow[0][23:16]==8'h12 || shadow[0][23:16]==8'h13 || prepared_global;
    wire payload_legal=shadow[0][7:0]<=2 && prepared_opcode &&
        (prepared_global ? (shadow[0][15:8]==0 && shadow[4]==0) :
         (shadow[4]!=0 && ((shadow[0][7:0]==0 && shadow[0][15:8]<32) ||
          (shadow[0][7:0]==1 && shadow[0][15:8]<16) ||
          (shadow[0][7:0]==2 && shadow[0][15:8]<32))));
    wire commit_write_error=pstrb_i!=4'hf || pwdata_i==0 || !payload_legal ||
        (tx_count==TCW'(TX_DEPTH) && !cmd_fire);
    assign pready_o=1'b1;
    assign flush_ready_o=preset_ni && (|flush_mask_i);
    assign rx_ready_o=preset_ni && !recovering && !raw_fault && !cancel_hold && rx_source_i!=2'd3 &&
        rx_count<RCW'(RX_DEPTH) && !flush_valid_i &&
        !flush_pending[rx_source_i] &&
        !(snapshot_valid && snapshot_meta[16] && snapshot_meta[5'(rx_source_i)]);
    assign rx_overflow_o=overflow_sticky;
    assign rx_recovering_o=recovering;
    assign cmd_valid_o=preset_ni && tx_count!=0;
    assign cmd_data_o=tx_count!=0 ? tx_q[0] : 160'd0;
    assign cmd_sequence_o=tx_count!=0 ? tx_seq[0] : 32'd0;
    assign fb_ready_o=preset_ni && fb_legal && fb_count<FCW'(FB_DEPTH);
    // The FIFO holds this head until an actual successful POP transfer.
    assign template_ready_o=preset_ni && write_access && offset==16'h0098;
    always_comb begin
        rx_live_count=0;
        for(integer k=0;k<RX_DEPTH;k=k+1)rx_live_count=rx_live_count+RCW'(rx_live[k]);
    end
    always_comb fb_candidate=fb_snapshot_valid ? fb_snapshot : fb_q[0];
    function automatic [31:0] merge_bytes(input [31:0] old_value,new_value,input [3:0] strobe);
        reg [31:0] value;
        begin
            value=old_value;
            for(integer b=0;b<4;b=b+1)if(strobe[b])value[8*b+:8]=new_value[8*b+:8];
            merge_bytes=value;
        end
    endfunction
    function automatic [31:0] next_serial(input [31:0] serial);
        next_serial=serial==32'hffffffff ? 32'd1 : serial+32'd1;
    endfunction
    function automatic [RPW-1:0] next_rx_slot(input [RPW-1:0] slot);
        next_rx_slot=slot==RPW'(RX_DEPTH-1) ? RPW'(0) : slot+1'b1;
    endfunction
    // A new flush outranks any unlatched byte. A previously latched snapshot
    // is immutable; its POP is followed by the flush before fresh source data.
    always_comb begin
        candidate_valid=1'b1;candidate_meta=0;candidate_tick=0;candidate_seq=0;
        if(snapshot_valid)begin
            candidate_meta=snapshot_meta;candidate_tick=snapshot_tick;candidate_seq=snapshot_seq;
        end else if(raw_fault)begin
            candidate_meta=32'h0003ff0f;candidate_tick=rx_tick_i;candidate_seq=next_serial(flush_serial);
        end else if(flush_fire || flush_pending!=0)begin
            candidate_meta={14'd0,pending_recovery,1'b1,8'hff,
                4'd0,(flush_pending | (flush_fire ? flush_mask_i : 4'd0))};
            candidate_tick=flush_pending!=0 ? pending_tick : flush_tick_i;
            candidate_seq=flush_fire ? next_serial(flush_serial) : pending_seq;
        end else if(rx_count!=0 && rx_live[rx_head])begin
            candidate_meta={16'd0,6'd0,rx_head_value.source,rx_head_value.data};candidate_tick=rx_head_value.tick;
        end else candidate_valid=1'b0;
    end
    always_comb begin
        write_error=1'b0;
        case(offset)
                16'h0040,16'h0044,16'h0048,16'h004c,16'h0050:begin end
                16'h001c:write_error=pstrb_i!=4'hf || pwdata_i!=1 || !snapshot_valid;
                16'h0068:write_error=pstrb_i!=4'hf || pwdata_i!=1 || !fb_snapshot_valid;
                16'h0028:write_error=pstrb_i!=4'hf || pwdata_i==0 || pwdata_i[31:2]!=0;
                16'h0054:write_error=commit_write_error;
                16'h0080:write_error=pstrb_i!=4'hf || pwdata_i>2 || route_busy;
                16'h00c0:write_error=pstrb_i!=4'hf;
                16'h00d0,16'h00d4,16'h00d8,16'h00dc,16'h00e0:write_error=pstrb_i!=4'hf;
                16'h0098:write_error=pstrb_i!=4'hf || pwdata_i!=1 || !template_valid_i;
                16'h00a0,16'h00a4,16'h00a8,16'h00ac,
                16'h00b0,16'h00b4,16'h00b8,16'h00bc:begin end
                default:write_error=1'b1;
        endcase
    end
    always_comb begin
        pslverr_o=1'b0;prdata_o=0;
        if(access)begin
            if(!aligned)pslverr_o=1'b1;
            else if(pwrite_i)pslverr_o=write_error;
            else case(offset)
                16'h0000:prdata_o=32'h43544c32;
                16'h0004:prdata_o=32'h00020000;
                16'h0008:prdata_o=32'(rx_live_count)+32'(snapshot_valid)+32'(flush_pending!=0);
                16'h000c:prdata_o={20'd0,resume_requested,fb_available,(fb_count==FCW'(FB_DEPTH)),cancel_hold,
                    (tx_count!=0),(tx_count==TCW'(TX_DEPTH)),
                    (flush_pending!=0),snapshot_valid,overflow_sticky,recovering,
                    (rx_count==RCW'(RX_DEPTH)),candidate_valid};
                16'h0010:begin prdata_o=candidate_meta;pslverr_o=!candidate_valid;end
                16'h0014:begin prdata_o=candidate_tick[31:0];pslverr_o=!candidate_valid;end
                16'h0018:begin prdata_o=candidate_tick[63:32];pslverr_o=!candidate_valid;end
                16'h0020:begin prdata_o=candidate_seq;pslverr_o=!candidate_valid;end
                16'h0024:prdata_o=overflow_count;
                16'h0040:prdata_o=shadow[0];
                16'h0044:prdata_o=shadow[1];
                16'h0048:prdata_o=shadow[2];
                16'h004c:prdata_o=shadow[3];
                16'h0050:prdata_o=shadow[4];
                16'h0058:prdata_o=tx_ack;
                16'h005c:prdata_o=32'(tx_count);
                16'h0060:begin
                    prdata_o={16'd0,3'd0,fb_candidate.voice,6'd0,fb_candidate.engine};pslverr_o=!fb_available;
                end
                16'h0064:begin prdata_o=fb_candidate.generation;pslverr_o=!fb_available;end
                16'h006c:prdata_o=32'(fb_count)+32'(fb_snapshot_valid);
                16'h0070:prdata_o=backend_status_i;
                16'h0074:prdata_o=backend_fault_i;
                16'h0078:prdata_o=tx_result;
                16'h0080:prdata_o={30'd0,route_request};
                16'h0084:prdata_o={23'd0,route_busy,6'd0,route_ack};
                16'h0090:prdata_o={template_valid_i,15'd0,template_valid_i ? template_addr_i : 16'd0};
                16'h0094:begin prdata_o=template_data_i;pslverr_o=!template_valid_i;end
                // Read-domain free is a delayed observation, not write credit.
                16'h009c:prdata_o={15'd0,template_overflow_i,template_free_i};
                16'h00d0,16'h00d4,16'h00d8,16'h00dc,16'h00e0:prdata_o=template_status_o[256+((offset-16'h00d0)>>2)*32+:32];
                16'h00c0:prdata_o=sub_patch1_supported_o?32'h53554231:32'd0;
                16'h00a0,16'h00a4,16'h00a8,16'h00ac,
                16'h00b0,16'h00b4,16'h00b8,16'h00bc:prdata_o=template_status_o[offset[4:2]*32+:32];
                default:pslverr_o=1'b1;
            endcase
        end
    end
    always_comb begin : next_rx
        logic retire_head;
        rx_count_d=rx_count;rx_live_d=rx_live;retire_head=0;snapshot_valid_d=snapshot_valid;
        rx_head_d=rx_head;rx_tail_d=rx_tail;
        snapshot_meta_d=snapshot_meta;snapshot_tick_d=snapshot_tick;snapshot_seq_d=snapshot_seq;
        flush_pending_d=flush_pending;pending_tick_d=pending_tick;pending_seq_d=pending_seq;
        flush_serial_d=flush_serial;pending_recovery_d=pending_recovery;
        recovering_d=recovering;resume_requested_d=resume_requested;
        overflow_sticky_d=overflow_sticky;overflow_count_d=overflow_count;
        if(raw_fault)begin
            rx_count_d=0;rx_live_d=0;recovering_d=1;resume_requested_d=0;
            rx_head_d=0;rx_tail_d=0;
            overflow_sticky_d=1;overflow_count_d=overflow_count+1;
            flush_pending_d=4'hf;pending_recovery_d=1;
            pending_tick_d=rx_tick_i;flush_serial_d=next_serial(flush_serial);pending_seq_d=flush_serial_d;
        end else if(flush_fire)begin
            // Preserve physical order. Clearing only per-slot valid bits
            // avoids the depth-squared, wide kept-index compaction network.
            for(integer k=0;k<RX_DEPTH;k=k+1)begin
                if(flush_mask_i[rx_q[k].source])rx_live_d[k]=0;
            end
            flush_pending_d=flush_pending|flush_mask_i;
            if(flush_pending==0)pending_tick_d=flush_tick_i;
            flush_serial_d=next_serial(flush_serial);pending_seq_d=flush_serial_d;
        end
        if(capture)begin
            snapshot_valid_d=1;snapshot_meta_d=candidate_meta;
            snapshot_tick_d=candidate_tick;snapshot_seq_d=candidate_seq;
            if(candidate_meta[16])begin flush_pending_d=0;pending_recovery_d=0;end
            else retire_head=1;
        end
        if(rx_count_d!=0 && !rx_live_d[rx_head])retire_head=1;
        if(retire_head)begin
            rx_live_d[rx_head]=0;rx_head_d=next_rx_slot(rx_head);
            rx_count_d=rx_count_d-1'b1;
        end
        if(write_access && offset==16'h001c)snapshot_valid_d=0;
        if(raw_fire)begin
            rx_live_d[rx_tail]=1;rx_tail_d=next_rx_slot(rx_tail);
            rx_count_d=rx_count_d+1'b1;
        end
        if(write_access && offset==16'h0028)begin
            // A newly arriving flush can race the preceding software STATUS
            // read. Accept the legal resume request and defer its effect,
            // instead of turning normal session concurrency into CPU PSLVERR.
            if(pwdata_i[0] && !raw_fault)resume_requested_d=1;
            if(pwdata_i[1] && !raw_fault)overflow_sticky_d=0;
        end
        if(resume_requested_d && !snapshot_valid_d && flush_pending_d==0 &&
           rx_count_d==0 && !flush_valid_i && !raw_fault && !cancel_hold)begin
            recovering_d=0;resume_requested_d=0;
        end
    end
    always_comb begin
        for(integer k=0;k<TX_DEPTH;k=k+1)begin tx_d[k]=tx_q[k];tx_seq_d[k]=tx_seq[k];end
        tx_count_d=tx_count;
        if(cmd_fire)begin
            for(integer k=0;k<TX_DEPTH-1;k=k+1)begin tx_d[k]=tx_q[k+1];tx_seq_d[k]=tx_seq[k+1];end
            tx_count_d=tx_count_d-1'b1;
        end
        if(commit)begin
            tx_d[integer'(tx_count_d)]={shadow[4],shadow[3],shadow[2],shadow[1],shadow[0]};
            tx_seq_d[integer'(tx_count_d)]=pwdata_i;tx_count_d=tx_count_d+1'b1;
        end
    end
    always_comb begin
        for(integer k=0;k<FB_DEPTH;k=k+1)fb_d[k]=fb_q[k];
        fb_count_d=fb_count;
        if(fb_capture)begin
            for(integer k=0;k<FB_DEPTH-1;k=k+1)fb_d[k]=fb_q[k+1];
            fb_count_d=fb_count_d-1'b1;
        end
        if(fb_fire)begin
            fb_d[integer'(fb_count_d)]='{generation:fb_generation_i,voice:fb_voice_i,engine:fb_engine_i};
            fb_count_d=fb_count_d+1'b1;
        end
    end
    always_ff @(posedge pclk_i or negedge preset_ni)begin
        if(!preset_ni)begin
            rx_count<=0;rx_live<=0;rx_head<=0;rx_tail<=0;tx_count<=0;tx_ack<=0;tx_result<=0;
            route_busy<=0;route_request<=0;route_ack<=0;
            template_status_o<=0;sub_patch1_supported_o<=0;
            stalled<=0;stalled_raw<='0;cancel_hold<=0;cancelled_raw<='0;
            fb_count<=0;fb_snapshot_valid<=0;fb_snapshot<='0;
            snapshot_valid<=0;snapshot_meta<=0;snapshot_tick<=0;snapshot_seq<=0;
            flush_pending<=0;pending_tick<=0;pending_seq<=0;flush_serial<=0;pending_recovery<=0;
            recovering<=0;resume_requested<=0;overflow_sticky<=0;overflow_count<=0;
            for(integer k=0;k<RX_DEPTH;k=k+1)rx_q[k]<='0;
            for(integer k=0;k<TX_DEPTH;k=k+1)begin tx_q[k]<=0;tx_seq[k]<=0;end
            for(integer k=0;k<FB_DEPTH;k=k+1)fb_q[k]<='0;
            for(integer k=0;k<5;k=k+1)shadow[k]<=0;
        end else begin
            rx_count<=rx_count_d;rx_live<=rx_live_d;tx_count<=tx_count_d;
            rx_head<=rx_head_d;rx_tail<=rx_tail_d;
            snapshot_valid<=snapshot_valid_d;snapshot_meta<=snapshot_meta_d;
            snapshot_tick<=snapshot_tick_d;snapshot_seq<=snapshot_seq_d;
            flush_pending<=flush_pending_d;pending_tick<=pending_tick_d;pending_seq<=pending_seq_d;
            flush_serial<=flush_serial_d;pending_recovery<=pending_recovery_d;
            recovering<=recovering_d;resume_requested<=resume_requested_d;
            overflow_sticky<=overflow_sticky_d;overflow_count<=overflow_count_d;
            // A flush cancels an unaccepted upstream record. Require a valid
            // low bubble before accepting another record, so it cannot replay.
            if(!rx_valid_i)cancel_hold<=0;
            if(raw_fault && rx_valid_i)begin
                cancel_hold<=1;cancelled_raw<='{tick:rx_tick_i,source:rx_source_i,data:rx_data_i};
            end
            if(flush_fire && rx_valid_i && flush_mask_i[rx_source_i])begin
                cancel_hold<=1;cancelled_raw<='{tick:rx_tick_i,source:rx_source_i,data:rx_data_i};
            end
            stalled<=!recovering && !raw_fault && !cancel_hold && rx_valid_i && !rx_ready_o &&
                !(flush_fire && flush_mask_i[rx_source_i]);
            stalled_raw<='{tick:rx_tick_i,source:rx_source_i,data:rx_data_i};
            for(integer k=0;k<RX_DEPTH;k=k+1)begin
                if(raw_fire && rx_tail==RPW'(k))
                    rx_q[k]<='{tick:rx_tick_i,source:rx_source_i,data:rx_data_i};
            end
            for(integer k=0;k<TX_DEPTH;k=k+1)begin tx_q[k]<=tx_d[k];tx_seq[k]<=tx_seq_d[k];end
            fb_count<=fb_count_d;
            for(integer k=0;k<FB_DEPTH;k=k+1)fb_q[k]<=fb_d[k];
            if(fb_capture)begin fb_snapshot<=fb_candidate;fb_snapshot_valid<=1;end
            if(fb_pop)fb_snapshot_valid<=0;
            if(cmd_fire)begin tx_ack<=tx_seq[0];tx_result<=cmd_result_i;end
            if(route_valid_o && route_ready_i)begin route_busy<=0;route_ack<=route_request;end
            if(write_access)case(offset)
                16'h0040:shadow[0]<=merge_bytes(shadow[0],pwdata_i,pstrb_i);
                16'h0044:shadow[1]<=merge_bytes(shadow[1],pwdata_i,pstrb_i);
                16'h0048:shadow[2]<=merge_bytes(shadow[2],pwdata_i,pstrb_i);
                16'h004c:shadow[3]<=merge_bytes(shadow[3],pwdata_i,pstrb_i);
                16'h0050:shadow[4]<=merge_bytes(shadow[4],pwdata_i,pstrb_i);
                16'h0080:begin route_request<=pwdata_i[1:0];route_busy<=1;end
                16'h00c0:sub_patch1_supported_o<=pwdata_i[0];
                16'h00a0,16'h00a4,16'h00a8,16'h00ac,
                16'h00b0,16'h00b4,16'h00b8,16'h00bc:
                    template_status_o[offset[4:2]*32+:32]<=merge_bytes(template_status_o[offset[4:2]*32+:32],pwdata_i,pstrb_i);
                16'h00d0,16'h00d4,16'h00d8,16'h00dc,16'h00e0:
                    template_status_o[256+((offset-16'h00d0)>>2)*32+:32]<=pwdata_i;
                default:begin end
            endcase
        end
    end
endmodule
`default_nettype wire
