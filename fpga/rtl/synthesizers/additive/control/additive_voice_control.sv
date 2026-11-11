// Copyright (c) 2026 ICECREAM1230
// Standalone additive MIDI ownership and four envelope groups per voice.
// SPDX-License-Identifier: MIT
//
// No subtractive/CSP state is used. The envelope law is a saturating linear
// increment per audio sample; zero rates are coerced to one LSB so release
// always terminates. Output levels include velocity, CC7 and CC11, all linear.
// CC123 behaves as simultaneous key-up and honors CC64; CC120 and source
// flush immediately clear their matching owners. Disabled events are drained.
//
// Each frame begins at slot 0. A serialized 8-voice scan finishes at slot 249
// and publishes one complete snapshot, stable over slots 256..767 except for
// an explicit hard mute. MIDI accepts only in slots 256..735; the bounded
// event scan then finishes before the next frame. Flush cancels an unfinished
// scan, clears matching owners and their controllers, and cannot resurrect a
// voice from stale pending work.
//
// ROM words 0..383 are UQ0.32 phase steps for MIDI pitches -128..255 at
// Fs=91015625/948 Hz (clamped to Nyquist). Words 384..511 are UQ1.23 factors
// 2**(j/(128*12)). Fractional semitones use all 13 bend fraction bits by linear
// interpolation. The final base*ratio conversion rounds to UQ0.24.
`default_nettype none
module additive_voice_control #(
    parameter integer VOICES=8,
    parameter NOTE_ROM_FILE="rtl/synthesizers/additive/data/additive_note_steps.hex",
    parameter integer VW=(VOICES>1)?$clog2(VOICES):1
)(
    input wire clk,rst_n,enable,
    input wire [9:0] slot,
    input midi_pkg::midi_event_t midi_event,
    input wire midi_event_valid,
    output wire midi_event_ready,
    input wire [3:0] midi_flush,
    input wire [63:0] attack_inc,decay_inc,sustain_level,release_inc,
    input wire [6:0] bend_range,
    output logic [VOICES*24-1:0] voice_steps,
    output logic [VOICES*4*16-1:0] voice_envelopes,
    output logic [VOICES*16-1:0] voice_pan,
    output logic [VOICES*8-1:0] voice_generation,
    output logic [VOICES-1:0] voice_active
);
    localparam [1:0] ENV_IDLE=0,ENV_ATTACK=1,ENV_DECAY=2,ENV_RELEASE=3;
    localparam [23:0] ONE_SEMITONE=24'd8887421;
    logic [VOICES-1:0] active,gate,down;
    logic [1:0] owner_source[0:VOICES-1];
    logic [3:0] owner_channel[0:VOICES-1];
    logic [6:0] note[0:VOICES-1],velocity[0:VOICES-1];
    logic [31:0] birth[0:VOICES-1];
    logic [7:0] generation[0:VOICES-1];
    logic [15:0] envelope[0:VOICES-1][0:3];
    logic [1:0] envelope_stage[0:VOICES-1][0:3];
    logic [15:0] pending_envelope[0:VOICES-1][0:3];
    logic [1:0] pending_stage[0:VOICES-1][0:3];
    logic [31:0] age;
    logic sustain_raw[0:63];
    logic [13:0] bend_raw[0:63];
    logic [6:0] volume_raw[0:63],expression_raw[0:63];
    logic [15:0] pan_raw[0:63];
    logic sustain_valid[0:63],bend_valid[0:63],volume_valid[0:63];
    logic expression_valid[0:63],pan_valid[0:63];
    wire sustain[0:63];
    wire [13:0] bend[0:63];
    wire [6:0] volume[0:63],expression[0:63];
    wire [15:0] pan[0:63];

    typedef enum logic [4:0] {
        IDLE,EVENT_DECODE,EVENT_SCAN,EVENT_NOTE_ON,EVENT_APPLY,
        FRAME_OWNER,FRAME_SELECT,FRAME_BEND,FRAME_PITCH,FRAME_INDEX,FRAME_BASE_WAIT,
        FRAME_BASE_CAPTURE,FRAME_FRAC_WAIT,FRAME_FRAC_CAPTURE,
        FRAME_NEXT_WAIT,FRAME_NEXT_CAPTURE,FRAME_RATIO,FRAME_MULT,
        FRAME_STEP,FRAME_ENV_READ,FRAME_ENV_CALC,FRAME_ENV_MULT,FRAME_ENV_STORE,
        FRAME_NEXT_VOICE,FRAME_PUBLISH
    } state_t;
    state_t state;
    typedef struct packed {
        logic [7:0] status,data1,data2;
        logic [1:0] nbytes;
        logic [7:0] source;
    } event_header_t;
    event_header_t event_q;
    wire [5:0] event_owner={event_q.source[1:0],event_q.status[3:0]};
    wire event_note_on=(event_q.status[7:4]==4'h9)&&(event_q.data2[6:0]!=0);
    wire event_note_off=(event_q.status[7:4]==4'h8)||
                        ((event_q.status[7:4]==4'h9)&&(event_q.data2[6:0]==0));
    wire event_header_ok=event_q.source<4&&event_q.nbytes==3&&
                         !event_q.data1[7]&&!event_q.data2[7];
    assign midi_event_ready=!enable ||
        (state==IDLE&&slot>=256&&slot<736&&midi_flush==0);

    logic [VW-1:0] event_index,best_index;
    logic best_valid;
    logic [1:0] best_rank,candidate_rank;
    logic [31:0] best_birth;
    wire same_note=active[event_index]&&
        owner_source[event_index]==event_q.source[1:0]&&
        owner_channel[event_index]==event_q.status[3:0]&&
        note[event_index]==event_q.data1[6:0];
    always_comb begin
        if(same_note) candidate_rank=0;
        else if(!active[event_index]) candidate_rank=1;
        else if(!gate[event_index]) candidate_rank=2;
        else candidate_rank=3;
    end
    wire better_candidate=!best_valid||candidate_rank<best_rank||
        (candidate_rank==best_rank&&candidate_rank>=2&&
         $signed(birth[event_index]-best_birth)<0);

    function automatic logic [15:0] expand7(input logic [6:0] value);
        return {value,value,value[6:5]};
    endfunction
    // Exact floor(product/65535), using one bounded fold after the registered
    // multiply. This preserves full scale when each input is 65535.
    function automatic logic [15:0] gain_fold(input logic [31:0] product);
        logic [16:0] folded;
        folded={1'b0,product[31:16]}+{1'b0,product[15:0]};
        return product[31:16]+16'(folded>=17'd65535);
    endfunction

    logic [VW-1:0] scan_voice;
    logic [1:0] scan_group;
    logic [5:0] scan_owner;
    logic signed [21:0] bend_product,pitch_q;
    logic signed [14:0] bend_delta;
    logic [5:0] fraction_low;
    logic [6:0] fraction_index;
    logic [31:0] note_base,rom_q;
    logic [8:0] rom_address;
    logic [31:0] note_rom[0:511];
    initial $readmemh(NOTE_ROM_FILE,note_rom);
    always_ff @(posedge clk) rom_q<=note_rom[rom_address];
    logic [23:0] ratio_low,ratio_high,ratio_q;
    wire [23:0] ratio_difference=ratio_high-ratio_low;
    wire [29:0] ratio_fraction_product=ratio_difference*fraction_low;
    logic [55:0] step_product;
    wire [56:0] rounded_step_product={1'b0,step_product}+57'd1073741824;
    logic [31:0] gain_product0,gain_product1,envelope_product;
    logic [15:0] voice_gain_q,next_level,velocity_q,volume_q,expression_q,volume_gain_q;
    logic [1:0] next_stage;
    logic frame_any_level;
    logic [VOICES*24-1:0] frame_steps;
    logic [VOICES*4*16-1:0] frame_envelopes;
    logic [VOICES*16-1:0] frame_pan;
    logic [VOICES*8-1:0] frame_generation;
    logic [VOICES-1:0] frame_active;

    // Keep the voice/group read muxes off the envelope arithmetic path.
    logic [15:0] current_level,attack_rate,decay_rate,release_rate,sustain_target;
    logic [1:0] current_stage;
    logic env_active_q,env_gate_q;
    wire [15:0] effective_attack=attack_rate==0?16'd1:attack_rate;
    wire [15:0] effective_decay=decay_rate==0?16'd1:decay_rate;
    wire [15:0] effective_release=release_rate==0?16'd1:release_rate;
    wire [16:0] attack_sum={1'b0,current_level}+{1'b0,effective_attack};
    logic [15:0] calculated_level;
    logic [1:0] calculated_stage;
    always_comb begin
        calculated_level=0; calculated_stage=ENV_IDLE;
        if(env_active_q) begin
            if(!env_gate_q) begin
                if(current_level>effective_release) begin
                    calculated_level=current_level-effective_release;
                    calculated_stage=ENV_RELEASE;
                end
            end else if(current_stage==ENV_ATTACK) begin
                calculated_level=attack_sum>=17'd65535 ? 16'hffff:attack_sum[15:0];
                calculated_stage=attack_sum>=17'd65535 ? ENV_DECAY:ENV_ATTACK;
            end else begin
                calculated_stage=ENV_DECAY;
                if(current_level>sustain_target)
                    calculated_level=(current_level-sustain_target<=effective_decay)?
                                     sustain_target:current_level-effective_decay;
                else
                    calculated_level=(sustain_target-current_level<=effective_decay)?
                                     sustain_target:current_level+effective_decay;
            end
        end
    end

    // Only validity carries controller reset/cancellation. A surviving event
    // is replayed before another event or frame can observe a speculative raw
    // write made on an unrelated source's cancellation edge.
    always_ff @(posedge clk) begin
        if(state==EVENT_APPLY) begin
            if(event_q.status[7:4]==4'he)
                bend_raw[event_owner]<={event_q.data2[6:0],event_q.data1[6:0]};
            else if(event_q.status[7:4]==4'hb) begin
                case(event_q.data1[6:0])
                    7:volume_raw[event_owner]<=event_q.data2[6:0];
                    11:expression_raw[event_owner]<=event_q.data2[6:0];
                    10:pan_raw[event_owner]<=expand7(event_q.data2[6:0]);
                    64:sustain_raw[event_owner]<=event_q.data2[6];
                    default:;
                endcase
            end
        end
    end
    for(genvar c=0;c<64;c=c+1) begin:g_controller
        assign sustain[c]=sustain_valid[c] ? sustain_raw[c]:1'b0;
        assign bend[c]=bend_valid[c] ? bend_raw[c]:14'd8192;
        assign volume[c]=volume_valid[c] ? volume_raw[c]:7'd127;
        assign expression[c]=expression_valid[c] ? expression_raw[c]:7'd127;
        assign pan[c]=pan_valid[c] ? pan_raw[c]:16'd32768;
        always_ff @(posedge clk or negedge rst_n) begin
            if(!rst_n) begin
                sustain_valid[c]<=0;bend_valid[c]<=0;volume_valid[c]<=0;
                expression_valid[c]<=0;pan_valid[c]<=0;
            end else if(!enable || midi_flush[c/16]) begin
                sustain_valid[c]<=0;bend_valid[c]<=0;volume_valid[c]<=0;
                expression_valid[c]<=0;pan_valid[c]<=0;
            end else if(midi_flush==0 && state==EVENT_APPLY && event_owner==6'(c)) begin
                if(event_q.status[7:4]==4'he) bend_valid[c]<=1;
                else if(event_q.status[7:4]==4'hb) begin
                    case(event_q.data1[6:0])
                        7:volume_valid[c]<=1;
                        11:expression_valid[c]<=1;
                        10:pan_valid[c]<=1;
                        64:sustain_valid[c]<=1;
                        default:;
                    endcase
                end
            end
        end
    end

    // Every group is scanned before PUBLISH. Pending data needs no reset:
    // canceling the FSM discards the whole scan, and the next scan overwrites
    // every entry before it can commit. Matching owners still clear directly.
    for(genvar v=0;v<VOICES;v=v+1) begin:g_envelope_voice
        for(genvar g=0;g<4;g=g+1) begin:g_envelope_group
            always_ff @(posedge clk) begin
                if(state==FRAME_ENV_STORE &&
                   scan_voice==VW'(v) && scan_group==2'(g)) begin
                    pending_envelope[v][g]<=next_level;pending_stage[v][g]<=next_stage;
                end
            end
            always_ff @(posedge clk or negedge rst_n) begin
                if(!rst_n) begin
                    envelope[v][g]<=0;envelope_stage[v][g]<=ENV_IDLE;
                end else if(!enable || midi_flush[owner_source[v]]) begin
                    envelope[v][g]<=0;envelope_stage[v][g]<=ENV_IDLE;
                end else if(midi_flush==0) begin
                    if((state==EVENT_NOTE_ON && best_index==VW'(v)) ||
                       (state==EVENT_APPLY && event_q.status[7:4]==4'hb &&
                        event_q.data1[6:0]==120 && active[v] &&
                        {owner_source[v],owner_channel[v]}==event_owner)) begin
                        envelope[v][g]<=0;
                        envelope_stage[v][g]<=state==EVENT_NOTE_ON ? ENV_ATTACK:ENV_IDLE;
                    end else if(state==FRAME_PUBLISH) begin
                        envelope[v][g]<=pending_envelope[v][g];envelope_stage[v][g]<=pending_stage[v][g];
                    end
                end
            end
        end
    end

    // Frame buffers are private until PUBLISH. A canceled edge may write
    // discarded data; the next complete scan overwrites every entry.
    always_ff @(posedge clk) begin
        case(state)
            FRAME_SELECT: begin
                frame_pan[scan_voice*16+:16]<=pan[scan_owner];
                frame_generation[scan_voice*8+:8]<=generation[scan_voice];
            end
            FRAME_STEP: begin
                frame_steps[scan_voice*24+:24]<=!active[scan_voice]?24'd0:
                    rounded_step_product[56:31]>=26'd8388608 ? 24'h800000:rounded_step_product[54:31];
            end
            FRAME_ENV_STORE: begin
                frame_envelopes[(scan_voice*4+32'(scan_group))*16+:16]<=gain_fold(envelope_product);
                if(scan_group==3)
                    frame_active[scan_voice]<=active[scan_voice]&&(gate[scan_voice]||frame_any_level||next_level!=0);
            end
            default:;
        endcase
    end

    always_ff @(posedge clk or negedge rst_n) begin
        if(!rst_n) begin
            state<=IDLE; active<=0;gate<=0;down<=0;age<=0;event_q<='0;
            event_index<=0;best_index<=0;best_valid<=0;best_rank<=3;best_birth<=0;
            scan_voice<=0;scan_group<=0;scan_owner<=0;bend_product<=0;pitch_q<=0;
            current_level<=0;current_stage<=ENV_IDLE;env_active_q<=0;env_gate_q<=0;
            attack_rate<=0;decay_rate<=0;release_rate<=0;sustain_target<=0;
            bend_delta<=0;velocity_q<=0;volume_q<=0;expression_q<=0;volume_gain_q<=0;
            fraction_low<=0;fraction_index<=0;note_base<=0;rom_address<=0;
            ratio_low<=0;ratio_high<=0;ratio_q<=0;step_product<=0;
            gain_product0<=0;gain_product1<=0;envelope_product<=0;
            voice_gain_q<=0;next_level<=0;next_stage<=ENV_IDLE;frame_any_level<=0;
            voice_steps<=0;voice_envelopes<=0;voice_pan<=0;voice_generation<=0;voice_active<=0;
            for(integer v=0;v<VOICES;v=v+1) begin
                owner_source[v]<=0;owner_channel[v]<=0;note[v]<=0;velocity[v]<=0;
                birth[v]<=0;generation[v]<=0;
            end
        end else if(!enable) begin
            state<=IDLE;active<=0;gate<=0;down<=0;age<=0;
            voice_steps<=0;voice_envelopes<=0;voice_pan<=0;voice_generation<=0;voice_active<=0;
            for(integer v=0;v<VOICES;v=v+1) begin
                generation[v]<=0;velocity[v]<=0;birth[v]<=0;
            end
        end else if(midi_flush!=0) begin
            // Allocation may already have acknowledged an unrelated source.
            // Keep that transaction and re-scan after the flushed owners are
            // removed; its previous best candidate may no longer be best.
            if((state==EVENT_DECODE||state==EVENT_SCAN||state==EVENT_NOTE_ON||state==EVENT_APPLY)&&
               event_q.source<4&&!midi_flush[event_q.source[1:0]]) state<=EVENT_DECODE;
            else state<=IDLE;
            for(integer v=0;v<VOICES;v=v+1) if(midi_flush[owner_source[v]]) begin
                active[v]<=0;gate[v]<=0;down[v]<=0;velocity[v]<=0;
                voice_active[v]<=0;voice_steps[v*24+:24]<=0;
                voice_envelopes[v*64+:64]<=0;
            end
        end else case(state)
            IDLE: begin
                if(slot==0) begin scan_voice<=0; state<=FRAME_OWNER;end
                else if(midi_event_valid&&midi_event_ready) begin
                    event_q<={midi_event.status,midi_event.data1,midi_event.data2,midi_event.nbytes,midi_event.source};
                    state<=EVENT_DECODE;
                end
            end
            EVENT_DECODE: begin
                if(!event_header_ok) state<=IDLE;
                else if(event_note_on) begin
                    event_index<=0;best_valid<=0;best_rank<=3;state<=EVENT_SCAN;
                end else state<=EVENT_APPLY;
            end
            EVENT_SCAN: begin
                if(better_candidate) begin
                    best_valid<=1;best_index<=event_index;best_rank<=candidate_rank;best_birth<=birth[event_index];
                end
                if(event_index==VW'(VOICES-1)) state<=EVENT_NOTE_ON;
                else event_index<=event_index+1'b1;
            end
            EVENT_NOTE_ON: begin
                active[best_index]<=1;gate[best_index]<=1;down[best_index]<=1;
                owner_source[best_index]<=event_q.source[1:0];owner_channel[best_index]<=event_q.status[3:0];
                note[best_index]<=event_q.data1[6:0];velocity[best_index]<=event_q.data2[6:0];
                generation[best_index]<=generation[best_index]+1'b1;birth[best_index]<=age;age<=age+1'b1;
                state<=IDLE;
            end
            EVENT_APPLY: begin
                if(event_note_off) begin
                    for(integer v=0;v<VOICES;v=v+1)
                        if(active[v]&&owner_source[v]==event_q.source[1:0]&&
                           owner_channel[v]==event_q.status[3:0]&&note[v]==event_q.data1[6:0]) begin
                            down[v]<=0;
                            if(!sustain[event_owner]) gate[v]<=0;
                        end
                end else if(event_q.status[7:4]==4'hb) begin
                    case(event_q.data1[6:0])
                        64: begin
                            if(!event_q.data2[6]) for(integer v=0;v<VOICES;v=v+1)
                                if(active[v]&&!down[v]&&{owner_source[v],owner_channel[v]}==event_owner) gate[v]<=0;
                        end
                        120,123: for(integer v=0;v<VOICES;v=v+1)
                            if(active[v]&&{owner_source[v],owner_channel[v]}==event_owner) begin
                                down[v]<=0;gate[v]<=event_q.data1[6:0]==123&&sustain[event_owner];
                                if(event_q.data1[6:0]==120) begin
                                    active[v]<=0;voice_active[v]<=0;voice_steps[v*24+:24]<=0;voice_envelopes[v*64+:64]<=0;
                                end
                            end
                        default:;
                    endcase
                end
                state<=IDLE;
            end
            FRAME_OWNER: begin
                scan_owner<={owner_source[scan_voice],owner_channel[scan_voice]};
                state<=FRAME_SELECT;
            end
            FRAME_SELECT: begin
                bend_delta<=$signed({1'b0,bend[scan_owner]})-15'sd8192;
                velocity_q<=expand7(velocity[scan_voice]);volume_q<=expand7(volume[scan_owner]);
                expression_q<=expand7(expression[scan_owner]);
                frame_any_level<=0;scan_group<=0;state<=FRAME_BEND;
            end
            FRAME_BEND: begin
                bend_product<=bend_delta*$signed({1'b0,bend_range});
                gain_product0<=velocity_q*volume_q;state<=FRAME_PITCH;
            end
            FRAME_PITCH: begin
                pitch_q<=22'({note[scan_voice],13'b0})+bend_product;
                volume_gain_q<=gain_fold(gain_product0);
                state<=FRAME_INDEX;
            end
            FRAME_INDEX: begin
                rom_address<=9'(($signed(pitch_q)>>>13)+22'sd128);
                fraction_index<=pitch_q[12:6];fraction_low<=pitch_q[5:0];
                gain_product1<=volume_gain_q*expression_q;state<=FRAME_BASE_WAIT;
            end
            FRAME_BASE_WAIT:begin voice_gain_q<=gain_fold(gain_product1);state<=FRAME_BASE_CAPTURE;end
            FRAME_BASE_CAPTURE: begin
                note_base<=rom_q;rom_address<=9'd384+9'(fraction_index);state<=FRAME_FRAC_WAIT;
            end
            FRAME_FRAC_WAIT:state<=FRAME_FRAC_CAPTURE;
            FRAME_FRAC_CAPTURE: begin
                ratio_low<=rom_q[23:0];rom_address<=9'd385+9'(fraction_index);state<=FRAME_NEXT_WAIT;
            end
            FRAME_NEXT_WAIT:state<=FRAME_NEXT_CAPTURE;
            FRAME_NEXT_CAPTURE: begin
                ratio_high<=fraction_index==127?ONE_SEMITONE:rom_q[23:0];state<=FRAME_RATIO;
            end
            FRAME_RATIO: begin
                ratio_q<=ratio_low+24'((ratio_fraction_product+30'd32)>>6);state<=FRAME_MULT;
            end
            FRAME_MULT: begin step_product<=note_base*ratio_q;state<=FRAME_STEP;end
            FRAME_STEP: begin
                state<=FRAME_ENV_READ;
            end
            FRAME_ENV_READ: begin
                current_level<=envelope[scan_voice][scan_group];
                current_stage<=envelope_stage[scan_voice][scan_group];
                env_active_q<=active[scan_voice];env_gate_q<=gate[scan_voice];
                attack_rate<=attack_inc[scan_group*16+:16];
                decay_rate<=decay_inc[scan_group*16+:16];
                release_rate<=release_inc[scan_group*16+:16];
                sustain_target<=sustain_level[scan_group*16+:16];
                state<=FRAME_ENV_CALC;
            end
            FRAME_ENV_CALC: begin next_level<=calculated_level;next_stage<=calculated_stage;state<=FRAME_ENV_MULT;end
            FRAME_ENV_MULT: begin envelope_product<=next_level*voice_gain_q;state<=FRAME_ENV_STORE;end
            FRAME_ENV_STORE: begin
                frame_any_level<=frame_any_level||(next_level!=0);
                if(scan_group==3) begin
                    state<=FRAME_NEXT_VOICE;
                end else begin scan_group<=scan_group+1'b1;state<=FRAME_ENV_READ;end
            end
            FRAME_NEXT_VOICE: begin
                if(scan_voice==VW'(VOICES-1)) state<=FRAME_PUBLISH;
                else begin scan_voice<=scan_voice+1'b1;state<=FRAME_OWNER;end
            end
            FRAME_PUBLISH: begin
                active<=frame_active;
                voice_steps<=frame_steps;voice_envelopes<=frame_envelopes;
                voice_pan<=frame_pan;voice_generation<=frame_generation;voice_active<=frame_active;
                state<=IDLE;
            end
            default: state<=IDLE;
        endcase
    end
endmodule
`default_nettype wire
