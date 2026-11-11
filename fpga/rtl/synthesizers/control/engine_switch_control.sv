`default_nettype none
// One local mode transaction owns the engine selectors and the DAC
// handover. Backend acceptance is distinct from application/completion.
module engine_switch_control #(
    parameter integer ENGINE_COUNT = 4,
    parameter logic [ENGINE_COUNT-1:0] ENGINE_MASK = {ENGINE_COUNT{1'b1}},
    parameter integer TIMEOUT_CYCLES = 7373418,
    parameter bit KS_CLOCK_GUARD = 1'b0,
    parameter integer MUTE_BRANCH_COUNT = 1,
    // Optional centralized-control barriers; legacy callers ignore new inputs.
    parameter bit WAIT_CANCEL_ACK = 1'b0
) (
    input wire clk_i, rst_n_i, cycle_i, remote_release_i,
    input wire [ENGINE_COUNT-2:0] selected_i, // KS, FM, additive, optional flute
    input wire fm_clock_alive_i, usb_flush_busy_i, ks_clock_alive_i,
    input wire dac_busy_i, dac_fm_active_i,dac_frame_tick_i,dac_frame_fm_i,
    input wire cancel_ready_i, output_state_applied_i, domain_fault_i,
    input wire fm_reset_cancelled_i,
    output wire [ENGINE_COUNT-2:0] select_valid_o, select_bind_o,
    output wire [31:0] select_id_o,
    input wire [ENGINE_COUNT-2:0] select_ready_i, select_done_i,
    input wire [(ENGINE_COUNT-1)*32-1:0] select_done_id_i, select_result_i,
    output wire [(ENGINE_COUNT>2?$clog2(ENGINE_COUNT):1)-1:0] effective_engine_o,
    output wire [ENGINE_COUNT-1:0] effective_selection_o,
    output wire busy_o, owner_o, mute_o,
    output logic flush_o,
    output wire [31:0] status_o,
    output logic [31:0] requested_revision_o, completed_revision_o, fault_o,
    output wire [MUTE_BRANCH_COUNT-1:0] mute_branches_o
);
    localparam logic [3:0] IDLE=0, CLEAR=1, PREPARE=2, ISSUE=3,
        WAIT_ACK=4, SET_ROUTE=5, WAIT_OUTPUT=6, FAULT=7;
    localparam integer TW=TIMEOUT_CYCLES>1?$clog2(TIMEOUT_CYCLES):1;
    localparam integer ROUTE_WIDTH=ENGINE_COUNT>2?$clog2(ENGINE_COUNT):1;
    localparam integer SELECT_COUNT=ENGINE_COUNT-1;
    localparam integer SLOT_WIDTH=SELECT_COUNT>1?$clog2(SELECT_COUNT):1;
    logic [3:0] state;
    // A same-edge mute register cuts the distributed cancellation network.
    // Its update depends only on actions that change mute, not on intermediate
    // transitions between states which are all already muted.
    logic local_mute_q /* synthesis syn_dont_touch = 1 */;
    // Physical consumer branches take the same next value on the same edge.
    // Keep each FF distinct; sampling local_mute_q here would delay cancellation.
    logic [MUTE_BRANCH_COUNT-1:0] local_mute_branch_q /* synthesis syn_dont_touch = 1 */;
    logic [ROUTE_WIDTH-1:0] requested, applied, target, route;
    logic [SLOT_WIDTH-1:0] command_slot;
    logic command_bind, local_owner, release_pending;
    logic [31:0] command_id;
    logic [TW-1:0] elapsed;
    logic [2:0] clear_wait;
    logic output_frame_seen;
    function automatic [ROUTE_WIDTH-1:0] observe(input [SELECT_COUNT-1:0] selected);
        observe='0;
        for(integer n=0;n<SELECT_COUNT;n=n+1)
            if(selected[n])observe=ROUTE_WIDTH'(n+1);
    endfunction
    wire [ROUTE_WIDTH-1:0] observed=observe(selected_i);
    wire local_busy=state!=IDLE && state!=FAULT;
    wire [SELECT_COUNT-1:0] target_selection=target==0?'0:SELECT_COUNT'(1)<<(target-1'b1);
    wire [31:0] result=select_result_i[command_slot*32+:32];
    wire [31:0] done_id=select_done_id_i[command_slot*32+:32];
    function automatic [31:0] advance(input [31:0] value);
        advance=value==32'hffffffff ? 32'd1 : value+1'b1;
    endfunction
    function automatic [ROUTE_WIDTH-1:0] next_engine(input [ROUTE_WIDTH-1:0] value);
        logic [ROUTE_WIDTH-1:0] candidate;
        begin
            candidate=value;
            for(integer n=0;n<ENGINE_COUNT;n=n+1)begin
                candidate=candidate==ROUTE_WIDTH'(ENGINE_COUNT-1)?'0:candidate+1'b1;
                if(ENGINE_MASK[candidate])return candidate;
            end
            return value;
        end
    endfunction
    wire [ROUTE_WIDTH-1:0] latest_target=cycle_i?next_engine(local_busy?requested:observed):requested;
    wire [31:0] latest_revision=cycle_i?advance(requested_revision_o):requested_revision_o;
    wire deadline=local_busy && elapsed==TW'(TIMEOUT_CYCLES-1);
    // Qualified by the caller after native-clock startup. A live fault level
    // also fences IDLE/FAULT user presses below, so they cannot bypass mute.
    wire cancel_domain_fault=WAIT_CANCEL_ACK && domain_fault_i;
    wire clock_lost=local_busy &&
        ((!fm_clock_alive_i &&
          (((state==ISSUE || state==WAIT_ACK) && command_slot==1 &&
            !(WAIT_CANCEL_ACK && !command_bind && fm_reset_cancelled_i)) ||
           (state==WAIT_OUTPUT && target==2))) ||
         (KS_CLOCK_GUARD && !ks_clock_alive_i &&
          (((state==ISSUE || state==WAIT_ACK) && command_slot==0) ||
           (state==WAIT_OUTPUT && target==1))));
    assign busy_o=local_busy;
    assign owner_o=local_owner;
    assign mute_o=local_mute_q;
    assign mute_branches_o=local_mute_branch_q;
    assign effective_engine_o=local_owner?route:observed;
    // Direct priority selection avoids a binary encode/mux/decode round trip
    // at the MIDI boundary. The public binary route remains unchanged.
    function automatic [ENGINE_COUNT-1:0] observe_selection(input [SELECT_COUNT-1:0] selected);
        observe_selection=ENGINE_COUNT'(1);
        for(integer n=0;n<SELECT_COUNT;n=n+1)begin
            if(selected[n])begin
                observe_selection='0;
                observe_selection[n+1]=1'b1;
            end
        end
    endfunction
    wire [ENGINE_COUNT-1:0] observed_selection=observe_selection(selected_i);
    wire [ENGINE_COUNT-1:0] owned_selection;
    for(genvar n=0;n<ENGINE_COUNT;n=n+1)begin: g_owned_selection
        assign owned_selection[n]=route==ROUTE_WIDTH'(n);
    end
    // An unbound system keeps binary route 0; the compiled mask fences it.
    assign effective_selection_o=(local_owner?owned_selection:observed_selection) & ENGINE_MASK;
    assign select_valid_o=state==ISSUE?(SELECT_COUNT'(1)<<command_slot):'0;
    assign select_bind_o=command_bind?(SELECT_COUNT'(1)<<command_slot):'0;
    assign select_id_o=command_id;
    wire ready=state==IDLE && !dac_busy_i && fault_o==0 &&
        (!local_owner || (observed==applied && selected_i==(applied==0?SELECT_COUNT'(0):SELECT_COUNT'(1)<<(applied-1'b1))));
    // ENG1 070C: requested/applied, local busy/fault/owner, real DAC status,
    // readiness, and compiled engines. 070D/E are request/completion revisions.
    // Existing low fields and capability positions are stable for ENG1.
    // Bits 10/11 extend the two mode fields, bit 20 extends the mask.
    assign status_o=(32'(ENGINE_MASK)<<16) | (32'(requested)&32'h3) |
        ((32'(applied)&32'h3)<<2) | (32'(local_busy)<<4) |
        (32'(fault_o!=0)<<5) | (32'(local_owner)<<6) |
        (32'(dac_busy_i)<<7) | (32'(dac_fm_active_i)<<8) |
        (32'(ready)<<9) | (((32'(requested)>>2)&1)<<10) |
        (((32'(applied)>>2)&1)<<11);
    // CLEAR/PREPARE/ISSUE/WAIT_ACK/SET_ROUTE never unmute. In particular,
    // cancel_ready_i can move CLEAR to PREPARE without entering this FF's D
    // cone. Keep the original new-press, fault and completion edge priorities.
    always_ff @(posedge clk_i or negedge rst_n_i)begin
        if(!rst_n_i){local_mute_q,local_mute_branch_q}<={(MUTE_BRANCH_COUNT+1){1'b0}};
        else if(cycle_i && (state==IDLE || state==FAULT) && !cancel_domain_fault)
            {local_mute_q,local_mute_branch_q}<={(MUTE_BRANCH_COUNT+1){1'b1}};
        else if(clock_lost || cancel_domain_fault || deadline)
            {local_mute_q,local_mute_branch_q}<={(MUTE_BRANCH_COUNT+1){1'b1}};
        else case(state)
            IDLE:;
            FAULT:if(remote_release_i || release_pending)
                {local_mute_q,local_mute_branch_q}<={(MUTE_BRANCH_COUNT+1){1'b0}};
            WAIT_OUTPUT:if(selected_i==target_selection && !usb_flush_busy_i && (!WAIT_CANCEL_ACK || output_state_applied_i) &&
                !dac_busy_i && dac_fm_active_i==(target==2) &&
                dac_frame_tick_i && dac_frame_fm_i==(target==2) && output_frame_seen)begin
                if((release_pending || remote_release_i) && !cycle_i)
                    {local_mute_q,local_mute_branch_q}<={(MUTE_BRANCH_COUNT+1){1'b0}};
                else if(latest_target!=target)
                    {local_mute_q,local_mute_branch_q}<={(MUTE_BRANCH_COUNT+1){1'b1}};
                else {local_mute_q,local_mute_branch_q}<={(MUTE_BRANCH_COUNT+1){1'b0}};
            end
            default:{local_mute_q,local_mute_branch_q}<={(MUTE_BRANCH_COUNT+1){1'b1}};
        endcase
    end
    always_ff @(posedge clk_i or negedge rst_n_i)begin
        if(!rst_n_i)begin
            state<=IDLE;requested<=0;applied<=0;target<=0;route<=0;
            command_slot<=0;command_bind<=0;command_id<=0;elapsed<=0;
            local_owner<=0;release_pending<=0;clear_wait<=0;flush_o<=0;output_frame_seen<=0;
            requested_revision_o<=0;completed_revision_o<=0;fault_o<=0;
        end else begin
            flush_o<=0;
            if(local_busy)begin
                if(elapsed!=TW'(TIMEOUT_CYCLES-1))elapsed<=elapsed+1'b1;
            end else elapsed<=0;
            if(remote_release_i)release_pending<=1;
            // Faults take precedence over completion, including a DAC frame
            // on the deadline. Never publish both completed revision and fault.
            if(clock_lost || cancel_domain_fault)begin fault_o<=5;state<=FAULT;end
            else if(deadline)begin fault_o<=1;state<=FAULT;end
            else case(state)
                IDLE:begin
                    if(!local_owner || observed!=applied)begin
                        requested<=observed;applied<=observed;route<=observed;
                        if(observed!=applied)local_owner<=0;
                    end
                    if(remote_release_i || release_pending)begin
                        local_owner<=0;release_pending<=0;
                        requested<=observed;applied<=observed;route<=observed;
                    end
                end
                CLEAR:begin
                    if(clear_wait==0)flush_o<=1;
                    if(clear_wait!=7)clear_wait<=clear_wait+1'b1;
                    else if(!usb_flush_busy_i && (!WAIT_CANCEL_ACK || cancel_ready_i))begin state<=PREPARE;end
                end
                PREPARE:begin
                    command_bind<=0;
                    if(|selected_i)begin command_slot<=SLOT_WIDTH'(observed-1'b1);command_id<=advance(command_id);state<=ISSUE;end
                    else if(target==0)begin state<=SET_ROUTE;end
                    else begin
                        command_slot<=SLOT_WIDTH'(target-1'b1);command_bind<=1;
                        command_id<=advance(command_id);state<=ISSUE;
                    end
                end
                ISSUE:if(select_ready_i[command_slot])begin state<=WAIT_ACK;end
                WAIT_ACK:if(select_done_i[command_slot] && done_id==command_id)begin
                    if(result!=0)begin fault_o<=32'd2+32'(command_slot);state<=FAULT;end
                    else if(command_bind)begin state<=SET_ROUTE;end
                    else begin state<=PREPARE;end
                end
                SET_ROUTE:begin route<=target;output_frame_seen<=0;state<=WAIT_OUTPUT;end
                WAIT_OUTPUT:if(selected_i==target_selection && !usb_flush_busy_i && (!WAIT_CANCEL_ACK || output_state_applied_i) &&
                    !dac_busy_i && dac_fm_active_i==(target==2) &&
                    dac_frame_tick_i && dac_frame_fm_i==(target==2))begin
                    // The first tap may have been captured at the route edge
                    // or delivered late through FM's tap CDC. A second frame
                    // also covers synth-to-synth switches without gate changes.
                    output_frame_seen<=1;
                    if(!output_frame_seen)begin end
                    else if((release_pending || remote_release_i) && !cycle_i)begin
                        local_owner<=0;release_pending<=0;state<=IDLE;
                        requested<=observed;applied<=observed;route<=observed;
                    end else if(latest_target!=target)begin
                        target<=latest_target;state<=CLEAR;clear_wait<=0;elapsed<=0;
                    end else begin
                        applied<=target;completed_revision_o<=latest_revision;state<=IDLE;
                    end
                end
                FAULT:if(remote_release_i || release_pending)begin
                    local_owner<=0;release_pending<=0;fault_o<=0;state<=IDLE;
                    requested<=observed;applied<=observed;route<=observed;
                end
                default:begin state<=FAULT;fault_o<=32'd6;end
            endcase
            // New presses coalesce the desired mode, never an in-flight payload.
            if(cycle_i)begin
                requested<=latest_target;requested_revision_o<=latest_revision;
                local_owner<=1;release_pending<=0;
                if((state==IDLE || state==FAULT) && !cancel_domain_fault)begin
                    target<=latest_target;route<=observed;fault_o<=0;
                    clear_wait<=0;elapsed<=0;state<=CLEAR;
                end
            end
        end
    end
`ifndef SYNTHESIS
    initial begin
        if(TIMEOUT_CYCLES<2 || MUTE_BRANCH_COUNT<1 || ENGINE_COUNT<2 || ENGINE_COUNT>5)$fatal(1,"invalid engine_switch_control configuration");
    end
`endif
endmodule
`default_nettype wire
