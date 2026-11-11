`default_nettype none
// Local control of the legacy power-up chord, independent of SPI/MIDI state.
// Key0 toggles the chord; Key1 emits one local engine-cycle request.
// Active-low keys must be released after reset. Long holds never repeat.
module preset_chord_keys #(
    parameter integer DEBOUNCE_CYCLES = 737342,
    parameter bit BOOT_PLAY = 1'b1
) (
    input wire clk_i, rst_n_i,
    input wire [1:0] keys_n_i,
    input wire frame_tick_i,
    output logic play_o,
    output wire display_toggle_o,
    output wire engine_cycle_o
);
    localparam integer CW = DEBOUNCE_CYCLES > 1 ? $clog2(DEBOUNCE_CYCLES) : 1;
    localparam [CW-1:0] LAST = CW'(DEBOUNCE_CYCLES-1);
    (* syn_preserve = 1, ASYNC_REG = "TRUE" *) logic [1:0] key_meta,key_sync;
    logic [1:0] input_valid,press_pulse;
    always_ff @(posedge clk_i or negedge rst_n_i) begin
        if(!rst_n_i)begin key_meta<=2'b11;key_sync<=2'b11;input_valid<=0;end
        else begin key_meta<=keys_n_i;key_sync<=key_meta;input_valid<={input_valid[0],1'b1};end
    end
    for(genvar k=0;k<2;k++)begin:g_key
        logic candidate,armed;
        logic [CW-1:0] stable_count;
        always_ff @(posedge clk_i or negedge rst_n_i) begin
            if(!rst_n_i)begin candidate<=1;armed<=0;stable_count<=0;press_pulse[k]<=0;end
            else begin
                press_pulse[k]<=0;
                if(!input_valid[1])begin candidate<=key_sync[k];stable_count<=0;armed<=0;end
                else if(key_sync[k]!=candidate)begin candidate<=key_sync[k];stable_count<=0;end
                else if(stable_count!=LAST)stable_count<=stable_count+1'b1;
                else if(candidate)armed<=1;
                else if(armed)begin press_pulse[k]<=1;armed<=0;end
            end
        end
    end
    assign display_toggle_o=press_pulse[0];
    assign engine_cycle_o=press_pulse[1];
    logic toggle_pending;
    always_ff @(posedge clk_i or negedge rst_n_i) begin
        if(!rst_n_i)begin play_o<=BOOT_PLAY;toggle_pending<=0;end
        else if(frame_tick_i)begin
            if(toggle_pending^press_pulse[0])play_o<=~play_o;
            toggle_pending<=0;
        end else begin
            toggle_pending<=toggle_pending^press_pulse[0];
        end
    end
`ifndef SYNTHESIS
    initial if(DEBOUNCE_CYCLES<1)$fatal(1,"preset_chord_keys requires a positive debounce interval");
`endif
endmodule
`default_nettype wire
