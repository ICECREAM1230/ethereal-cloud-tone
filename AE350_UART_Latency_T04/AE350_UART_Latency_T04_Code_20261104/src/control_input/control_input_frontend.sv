`default_nettype none
module control_input_frontend #(
    parameter bit ENABLE_MIDI_PCM_LATENCY_TEST=1'b0,
    parameter bit ENABLE_MIDI=1'b1, AUTONOMOUS_USB_MIDI=1'b1, ENABLE_MIDI_STATUS_LED=1'b1,
    parameter bit ENABLE_SENSOR_I2C=1'b0,
    parameter bit AE350_CONTROL=1'b0,
    parameter integer SENSOR_I2C_HZ=100_000
) (
    input wire sysclk,rst_n,sclk,cs,mosi,
    input wire ae350_flush_done_valid_i,
    input wire [3:0] ae350_flush_done_mask_i,
    input wire [255:0] ae350_template_status_i,
    input wire [15:0] ae350_template_free_i,
    input wire ae350_template_overflow_i,
    output wire ae350_template_write_o,
    output wire latency_raw_accept_o,
    output wire [1:0] latency_raw_source_o,
    output wire [7:0] latency_raw_data_o,
    output wire latency_usb_accept_o,latency_usb_rst_no,
    output wire [31:0] latency_usb_packet_o,
    output wire miso,
    output wire cfg_write_o,
    output wire [15:0] cfg_addr_o,
    output wire [31:0] cfg_data_o,
    output wire cfg_read_req_toggle_o,
    output wire [15:0] cfg_read_addr_o,
    input wire [31:0] cfg_read_data_i,
    output wire usb_packet_valid,usb_configured,usb_error,usb_flush_busy,
    output wire usb_packet_ready,
    input wire source_packet_ready,source_raw_ready,local_switch_mute,
    output wire [1:0] raw_source,
    output wire [7:0] raw_data,
    output wire raw_valid,raw_input_enabled,
    output wire [3:0] mailbox_flush,
    output wire [31:0] mailbox_read,
    input wire [31:0] event_count,
    input wire sys_clk,sys_rst_n,usb_local_flush,
    inout wire usb_dp,usb_dm,
    output wire [31:0] usb_packet_data,
    output wire usb_session_flush,status_led,
    output wire usb_clk_o,usb_pll_locked_o,
    output wire modulation_valid_o,modulation_active_o,modulation_source_o,
    output wire signed [17:0] modulation_pitch_delta_o,
    output wire [16:0] modulation_gain_o,
    // Fourth logical input: native sensor controls. source_id is always 3;
    // group_select is internal to that source (0 FDC, 1 dual ToF).
    output wire sensor_controls_valid_o,sensor_active_o,sensor_group_o,
    output wire [1:0] sensor_source_id_o,
    output wire [13:0] sensor_pitch_o,
    output wire [16:0] sensor_gain_o
`ifdef SYNTH_SENSOR_I2C_PORTS
    ,inout wire scl_io,sda_io,
    output wire fdc_addr_o,fdc_sd_o,
    input wire fdc_int_ni,
    inout wire tof1_scl_io,tof1_sda_io,tof1_xshut_io,
    input wire tof1_int_ni,
    inout wire tof2_scl_io,tof2_sda_io,tof2_xshut_io,
    input wire tof2_int_ni
`endif
);
    wire sensor_selected;
    wire [31:0] sensor_read;
    assign sensor_source_id_o=2'd3;
    assign sensor_controls_valid_o=modulation_valid_o;
    assign sensor_active_o=modulation_active_o;
    assign sensor_group_o=modulation_source_o;
    // A native performance source is silent when inactive. Keep historical
    // modulation outputs as aliases for compatibility, including neutral unity.
    assign sensor_gain_o=modulation_active_o ? modulation_gain_o : 17'd0;
    control_input_sensor #(.ENABLE(ENABLE_SENSOR_I2C),.I2C_HZ(SENSOR_I2C_HZ)) u_sensor (
        .sys_clk_i(sys_clk),.core_clk_i(sysclk),.rst_ni(sys_rst_n && rst_n),
        .cfg_clk_i(sclk),.cfg_cs_ni(cs),.cfg_write_i(cfg_write_o),.cfg_addr_i(cfg_addr_o),.cfg_data_i(cfg_data_o),
        .read_toggle_i(cfg_read_req_toggle_o),.read_addr_i(cfg_read_addr_o),
        .read_selected_o(sensor_selected),.read_data_o(sensor_read),
        .controls_valid_o(modulation_valid_o),.active_o(modulation_active_o),.source_o(modulation_source_o),
        .pitch_code_o(sensor_pitch_o),.pitch_delta_o(modulation_pitch_delta_o),.expression_gain_o(modulation_gain_o)
`ifdef SYNTH_SENSOR_I2C_PORTS
        ,.scl_io,.sda_io,.fdc_addr_o,.fdc_sd_o,.fdc_int_ni,
        .tof1_scl_io,.tof1_sda_io,.tof1_xshut_io,.tof1_int_ni,
        .tof2_scl_io,.tof2_sda_io,.tof2_xshut_io,.tof2_int_ni
`endif
    );
    // MOD1 is the common core-domain view, and the consumption boundary for
    // future voice adapters. Observability keeps this path independently usable.
    logic mod_read_meta,mod_read_sync,mod_read_previous,mod_read_pending,mod_selected;
    logic [31:0] mod_read;
    always_ff @(posedge sysclk or negedge rst_n) begin
        if (!rst_n) begin
            mod_read_meta<=0;mod_read_sync<=0;mod_read_previous<=0;mod_read_pending<=0;mod_selected<=0;mod_read<=0;
        end else begin
            mod_read_meta<=cfg_read_req_toggle_o;mod_read_sync<=mod_read_meta;mod_read_previous<=mod_read_sync;
            mod_read_pending<=mod_read_sync!=mod_read_previous;
            if (mod_read_pending) begin
                mod_selected<=cfg_read_addr_o[15:3]==13'h0ec;
                case(cfg_read_addr_o[2:0])
                    0:mod_read<=32'h4d4f4431;
                    1:mod_read<={30'd0,modulation_source_o,modulation_active_o};
                    2:mod_read<={{14{modulation_pitch_delta_o[17]}},modulation_pitch_delta_o};
                    3:mod_read<={15'd0,modulation_gain_o};
                    4:mod_read<={18'd0,sensor_pitch_o};
                    5:mod_read<={30'd0,sensor_source_id_o};
                    6:mod_read<={15'd0,sensor_gain_o};
                    default:mod_read<=0;
                endcase
            end
        end
    end
    wire [31:0] shared_read=mod_selected ? mod_read : sensor_selected ? sensor_read : cfg_read_data_i;
    midi_host_frontend #(.ENABLE_STATUS_LED(ENABLE_MIDI_STATUS_LED),
        .ENABLE_MIDI_PCM_LATENCY_TEST(ENABLE_MIDI_PCM_LATENCY_TEST)) u_usb_midi (
        .sys_clk,.sys_rst_n,.core_clk(sysclk),.core_rst_n(rst_n),.usb_dp,.usb_dm,
        .packet_data(usb_packet_data),.packet_valid(usb_packet_valid),.packet_ready(usb_packet_ready),
        .session_flush(usb_session_flush),.usb_configured,.usb_error,
        .latency_usb_accept_o,.latency_usb_packet_o,.latency_usb_rst_no,
        .local_flush(usb_local_flush),.local_flush_busy(usb_flush_busy),.status_led,
        .usb_clk_o,.usb_pll_locked_o);
    control_input_transport #(.ENABLE_MIDI_PCM_LATENCY_TEST(ENABLE_MIDI_PCM_LATENCY_TEST),.ENABLE_MIDI(ENABLE_MIDI),.AUTONOMOUS_USB_MIDI(AUTONOMOUS_USB_MIDI),
        .RAW_SOURCE3_ENABLE(1'b0),.AE350_CONTROL(AE350_CONTROL)) u_transport (
        .latency_raw_accept_o,.latency_raw_source_o,.latency_raw_data_o,
        .ae350_flush_done_valid_i,.ae350_flush_done_mask_i,
        .ae350_template_status_i,.ae350_template_free_i,.ae350_template_overflow_i,.ae350_template_write_o,
        .sysclk(sysclk),
        .rst_n(rst_n),
        .sclk(sclk),
        .cs(cs),
        .mosi(mosi),
        .miso(miso),
        .cfg_write_o(cfg_write_o),
        .cfg_addr_o(cfg_addr_o),
        .cfg_data_o(cfg_data_o),
        .cfg_read_req_toggle_o(cfg_read_req_toggle_o),
        .cfg_read_addr_o(cfg_read_addr_o),
        .cfg_read_data_i(shared_read),
        .usb_packet_valid(usb_packet_valid),
        .usb_configured(usb_configured),
        .usb_error(usb_error),
        .usb_flush_busy(usb_flush_busy),
        .usb_packet_ready(usb_packet_ready),
        .source_packet_ready(source_packet_ready),
        .source_raw_ready(source_raw_ready),
        .local_switch_mute(local_switch_mute),
        .raw_source(raw_source),
        .raw_data(raw_data),
        .raw_valid(raw_valid),
        .raw_input_enabled(raw_input_enabled),
        .mailbox_flush(mailbox_flush),
        .mailbox_read(mailbox_read),
        .event_count(event_count));

endmodule
`default_nettype wire
