`default_nettype none
// SNS1: SPI shadow + immutable COMMIT slot, consumed in the 50MHz sensor domain.
// This configures an extra modulation dimension, never a MIDI source/voice.
module sensor_control_registers (
    input wire cfg_clk_i, clk_i, rst_ni, cfg_cs_ni,
    input wire write_i,
    input wire [15:0] addr_i,
    input wire [31:0] data_i,
    input wire read_toggle_i,
    input wire [15:0] read_addr_i,
    output logic read_selected_o,
    output logic [31:0] read_data_o,
    output logic enable_o, source_o, clear_o,
    output wire cfg_valid_o,
    input wire cfg_ready_i,
    output wire cfg_source_o,
    output wire [27:0] pitch_far_o, pitch_near_o, volume_far_o, volume_near_o,
    output wire [13:0] pitch_min_o, pitch_max_o, reference_o,
    input wire [1:0] cfg_applied_i, cfg_error_i, calibrated_i,
    input wire active_i, stale_i, ready_i, fault_i,
    input wire signed [17:0] delta_i,
    input wire [16:0] gain_i,
    input wire [27:0] fdc0_i, fdc1_i,
    input wire [1:0][15:0] distance_i,
    input wire [2:0] configured_i, device_fault_i,
    input wire sample_error_i, overrun_i, expired_i
);
    (* ASYNC_REG = "TRUE" *) logic cfg_reset_meta, cfg_reset_sync;
    (* ASYNC_REG = "TRUE" *) logic reset_meta, reset_sync;
    always_ff @(posedge cfg_clk_i or negedge rst_ni)
        if (!rst_ni) begin cfg_reset_meta<=0; cfg_reset_sync<=0; end
        else begin cfg_reset_meta<=1; cfg_reset_sync<=cfg_reset_meta; end
    always_ff @(posedge clk_i or negedge rst_ni)
        if (!rst_ni) begin reset_meta<=0; reset_sync<=0; end
        else begin reset_meta<=1; reset_sync<=reset_meta; end

    // A fresh CS boundary after reset is required, including reset mid-burst.
    (* ASYNC_REG = "TRUE" *) logic cs_meta, cs_sync;
    logic frame_armed;
    always_ff @(posedge clk_i or negedge reset_sync)
        if (!reset_sync) begin cs_meta<=0; cs_sync<=0; frame_armed<=0; end
        else begin cs_meta<=cfg_cs_ni; cs_sync<=cs_meta; if(cs_sync) frame_armed<=1; end

    // Seven complete words, including reserved bits for validation at COMMIT.
    logic [223:0] shadow, held;
    logic [31:0] held_command;
    logic request_q, acknowledge_q, rejected_q;
    (* ASYNC_REG = "TRUE" *) logic ack_meta, ack_sync, armed_meta, armed_sync;
    (* ASYNC_REG = "TRUE" *) logic req_meta, req_sync, reject_meta, reject_sync;
    always_ff @(posedge cfg_clk_i or negedge cfg_reset_sync) begin
        if (!cfg_reset_sync) begin
            shadow<='0; held<='0; held_command<=0; request_q<=0; rejected_q<=0;
            ack_meta<=0; ack_sync<=0; armed_meta<=0; armed_sync<=0;
        end else begin
            ack_meta<=acknowledge_q; ack_sync<=ack_meta;
            armed_meta<=frame_armed; armed_sync<=armed_meta;
            if (write_i && addr_i>=16'h0741 && addr_i<=16'h0747)
                shadow[(addr_i-16'h0741)*32 +: 32] <= data_i;
            if (write_i && addr_i==16'h0748) begin
                if (armed_sync && request_q==ack_sync) begin
                    held<=shadow; held_command<=data_i; request_q<=~request_q;
                end else rejected_q<=1;
            end
        end
    end
    always_ff @(posedge clk_i or negedge reset_sync)
        if (!reset_sync) begin req_meta<=0; req_sync<=0; reject_meta<=0; reject_sync<=0; end
        else begin req_meta<=request_q; req_sync<=req_meta; reject_meta<=rejected_q; reject_sync<=reject_meta; end

    assign pitch_far_o=held[32 +: 28];
    assign pitch_near_o=held[64 +: 28];
    assign volume_far_o=held[96 +: 28];
    assign volume_near_o=held[128 +: 28];
    assign pitch_min_o=held[160 +: 14];
    assign pitch_max_o=held[176 +: 14];
    assign reference_o=held[192 +: 14];
    assign cfg_source_o=held[208];
    wire legal_header=held_command[31:18]==0 && held_command[15:0]!=0 &&
                      !(held_command[16] && held_command[17]) && held[31:2]==0;
    wire legal_calibration=held[63:60]==0 && held[95:92]==0 &&
                          held[127:124]==0 && held[159:156]==0 &&
                          held[175:174]==0 && held[191:190]==0 &&
                          held[207:206]==0 && held[223:209]==0;
    typedef enum logic [1:0] {IDLE, CONFIGURE, RESULT} state_t;
    state_t state;
    logic [15:0] last_sequence;
    logic [7:0] last_result;
    logic [2:0] event_sticky;
    assign cfg_valid_o=state==CONFIGURE;
    always_ff @(posedge clk_i or negedge reset_sync) begin
        if (!reset_sync) begin
            state<=IDLE; acknowledge_q<=0; last_sequence<=0; last_result<=0;
            enable_o<=0; source_o<=0; clear_o<=0; event_sticky<=0;
        end else begin
            clear_o<=0;
            event_sticky<=event_sticky | {expired_i,overrun_i,sample_error_i};
            case (state)
                IDLE: if (frame_armed && req_sync!=acknowledge_q) begin
                    if (!legal_header || (held_command[16] && !legal_calibration)) begin
                        last_sequence<=held_command[15:0]; last_result<=1; acknowledge_q<=req_sync;
                    end else if (held_command[16]) state<=CONFIGURE;
                    else begin
                        enable_o<=held[0]; source_o<=held[1]; clear_o<=held_command[17];
                        last_sequence<=held_command[15:0]; last_result<=0; acknowledge_q<=req_sync;
                        if (held_command[17]) event_sticky<=0;
                    end
                end
                CONFIGURE: if (cfg_ready_i) state<=RESULT;
                RESULT: if (cfg_applied_i[cfg_source_o] || cfg_error_i[cfg_source_o]) begin
                    last_sequence<=held_command[15:0];
                    last_result<=cfg_error_i[cfg_source_o] ? 8'd2 : 8'd0;
                    if (!cfg_error_i[cfg_source_o]) begin enable_o<=held[0]; source_o<=held[1]; end
                    acknowledge_q<=req_sync; state<=IDLE;
                end
                default: state<=IDLE;
            endcase
        end
    end

    // Capture selection together with data; an SPI burst may prefetch a
    // different window while the previous word is still streaming.
    (* ASYNC_REG = "TRUE" *) logic read_meta;
    logic read_sync, read_previous, read_pending;
    always_ff @(posedge clk_i or negedge reset_sync) begin
        if (!reset_sync) begin
            read_meta<=0; read_sync<=0; read_previous<=0; read_pending<=0;
            read_selected_o<=0; read_data_o<=0;
        end else begin
            read_meta<=read_toggle_i; read_sync<=read_meta; read_previous<=read_sync;
            read_pending<=read_sync!=read_previous;
            if (read_pending) begin
                read_selected_o<=read_addr_i[15:5]==11'h03a;
                case (read_addr_i[4:0])
                    5'h00: read_data_o<=32'h534e5331;
                    5'h09: read_data_o<={22'd0,reject_sync,calibrated_i,fault_i,ready_i,stale_i,
                                       active_i,source_o,enable_o,(req_sync!=acknowledge_q)};
                    5'h0a: read_data_o<={last_result,8'd0,last_sequence};
                    5'h0b: read_data_o<={{14{delta_i[17]}},delta_i};
                    5'h0c: read_data_o<={15'd0,gain_i};
                    5'h0d: read_data_o<={4'd0,fdc0_i};
                    5'h0e: read_data_o<={4'd0,fdc1_i};
                    5'h0f: read_data_o<=distance_i;
                    5'h10: read_data_o<={26'd0,device_fault_i,configured_i};
                    5'h11: read_data_o<={29'd0,event_sticky};
                    5'h14: read_data_o<=32'h00010001; // ABI v1, acquisition compiled
                    default: read_data_o<=0;
                endcase
            end
        end
    end
endmodule
`default_nettype wire
