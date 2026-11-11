// SPDX-License-Identifier: MIT
// Preserves the tested Host/PHY and official 48 MHz PLL; packets cross as a
// held complete payload with a separate acknowledged session-flush channel.
module midi_host_frontend #(parameter bit ENABLE_STATUS_LED=1'b0) (
    input wire sys_clk,sys_rst_n,core_clk,core_rst_n,
    inout wire usb_dp,usb_dm,
    output wire [31:0] packet_data,
    output wire packet_valid,
    input wire packet_ready,
    output wire session_flush,
    input wire local_flush,
    output wire local_flush_busy,
    output wire usb_configured,usb_error,
    output wire status_led,
    output wire usb_clk_o,usb_pll_locked_o
);
    wire usb_clk;
    wire pll_locked;
    // Shared free-running clock; USB bus/session reset never gates this net.
    assign usb_clk_o = usb_clk;
    assign usb_pll_locked_o = pll_locked;
    USB48_PLL usb_pll (
        .clkin(sys_clk),
        .init_clk(sys_clk),
        .clkout0(usb_clk),
        .lock(pll_locked)
    );

    // Asynchronous assertion also releases the pads when the clock is absent.
    // Three destination-clock stages qualify reset removal after PLL lock.
    wire common_rst_n = sys_rst_n && core_rst_n && pll_locked;
    wire reset_async = !common_rst_n;
    // The consumer-side readiness/flush network is ordinary core-clock
    // data. Keep raw PLL/reset removal out of it, including when core_clk
    // is stopped. USB pad assertion below still uses reset_async directly.
    wire core_reset_n;
    jt51_reset_sync u_core_reset(
        .clk_i(core_clk),.rst_n_i(common_rst_n),.rst_n_o(core_reset_n));
    (* syn_preserve = 1 *) reg [2:0] reset_release;
    always @(posedge usb_clk or posedge reset_async) begin
        if (reset_async)
            reset_release <= 3'b111;
        else
            reset_release <= {reset_release[1:0], 1'b0};
    end
    wire rst = reset_release[2];

    wire [7:0] tx_data;
    wire tx_valid, tx_ready;
    wire [7:0] rx_data;
    wire rx_valid, rx_active, rx_error;
    wire [1:0] line_state;
    wire bus_reset;
    wire tx_dp, tx_dm, tx_oen_n;

    // Host pulldowns must exist on the physical path; actual population is
    // unverified. UTMI controls do not create resistors inside this soft PHY.
    usb_fs_phy phy (
        .clk_i(usb_clk),
        .rst_i(rst),
        .utmi_data_out_i(tx_data),
        .utmi_txvalid_i(tx_valid),
        .utmi_op_mode_i(2'b00),
        .utmi_xcvrselect_i(2'b01),
        .utmi_termselect_i(1'b1),
        .utmi_dppulldown_i(1'b1),
        .utmi_dmpulldown_i(1'b1),
        // Experimental GPIO receiver: the recovered differential data input
        // uses D+. Electrical USB FS compliance requires separate board tests.
        .usb_rx_rcv_i(usb_dp),
        .usb_rx_dp_i(usb_dp),
        .usb_rx_dn_i(usb_dm),
        .usb_reset_assert_i(bus_reset),
        .utmi_data_in_o(rx_data),
        .utmi_txready_o(tx_ready),
        .utmi_rxvalid_o(rx_valid),
        .utmi_rxactive_o(rx_active),
        .utmi_rxerror_o(rx_error),
        .utmi_linestate_o(line_state),
        .usb_tx_dp_o(tx_dp),
        .usb_tx_dn_o(tx_dm),
        .usb_tx_oen_o(tx_oen_n),
        .usb_reset_detect_o(),
        .usb_en_o()
    );
    assign usb_dp = (!reset_async && !rst && !tx_oen_n) ? tx_dp : 1'bz;
    assign usb_dm = (!reset_async && !rst && !tx_oen_n) ? tx_dm : 1'bz;

    wire host_flush_request,host_flush_done;
    wire src_packet_valid,src_packet_ready;
    wire [31:0] src_packet_data;
    wire src_session_flush, configured, enumerating;
    wire [7:0] error_code;
    wire [7:0] midi_interface;
    wire [3:0] midi_endpoint;
    wire [6:0] midi_max_packet_size;
    wire [4:0] midi_cables;
    wire trace_start,trace_start_ready,trace_byte_valid,trace_byte_ready,trace_done;
    wire [7:0] trace_type,trace_byte_data,trace_error;
    wire [15:0] trace_request_len,trace_index,trace_byte_offset,trace_received;
    wire [6:0] trace_address,trace_ep0_mps;
    usb_midi_host #(.TRACE_ENABLE(1'b0),.ENABLE_SINK_FLUSH(1'b1)) host (
        .clk(usb_clk), .rst(rst),
        .line_state(line_state), .bus_reset(bus_reset),
        .utmi_tx_data(tx_data), .utmi_tx_valid(tx_valid),
        .utmi_tx_ready(tx_ready), .utmi_rx_data(rx_data),
        .utmi_rx_valid(rx_valid), .utmi_rx_active(rx_active),
        .utmi_rx_error(rx_error),
        .packet_valid(src_packet_valid), .packet_ready(src_packet_ready),
        .packet_data(src_packet_data), .session_flush(src_session_flush),
        .sink_flush_request(host_flush_request),.sink_flush_done(host_flush_done),
        .configured(configured), .error_code(error_code),
        .midi_interface,.midi_endpoint,.midi_max_packet_size,.midi_cables,.enumerating(enumerating),
        .trace_start,.trace_start_ready,.trace_type,.trace_request_len,.trace_index,
        .trace_address,.trace_ep0_mps,.trace_byte_data,.trace_byte_offset,
        .trace_byte_valid,.trace_byte_ready,.trace_done,.trace_error,.trace_received
    );

    assign trace_start_ready=1'b1;
    assign trace_byte_ready=1'b1;
    // Local sink barriers have a request/done toggle pair plus an explicit
    // release toggle. The Host remains quiesced after done until core has seen
    // the new CDC epoch. Rearming inserts a real low request edge at the Host;
    // a following local pulse cannot be mistaken for the previous request.
    logic local_busy, local_request_toggle, local_release_toggle;
    logic host_done_toggle;
    (* ASYNC_REG="TRUE" *) logic host_done_meta,host_done_sync;
    (* ASYNC_REG="TRUE" *) logic local_req_meta,local_req_sync;
    (* ASYNC_REG="TRUE" *) logic local_release_meta,local_release_sync;
    logic [2:0] barrier_state;
    logic active_request,barrier_epoch_flush;
    wire cdc_src_ready,cdc_valid,cdc_session_flush;
    wire [31:0] cdc_data;
    localparam [2:0] B_IDLE=0,B_HOST=1,B_CDC_IDLE=2,B_CDC_LAUNCH=3,
                     B_RELEASE=4,B_REARM=5;
    wire barrier_active=barrier_state!=B_IDLE;
    assign host_flush_request=barrier_state!=B_IDLE && barrier_state!=B_REARM;
    assign local_flush_busy=!core_reset_n || local_busy;
    assign packet_valid=cdc_valid && !local_flush_busy && !local_flush;
    assign packet_data=cdc_data;
    assign session_flush=cdc_session_flush;
    assign src_packet_ready=cdc_src_ready && !barrier_active;

    always @(posedge core_clk or negedge core_reset_n) begin
        if(!core_reset_n) begin
            local_busy<=0;local_request_toggle<=0;local_release_toggle<=0;
            host_done_meta<=0;host_done_sync<=0;
        end else begin
            host_done_meta<=host_done_toggle;host_done_sync<=host_done_meta;
            if(local_busy) begin
                if(host_done_sync==local_request_toggle && cdc_session_flush) begin
                    local_release_toggle<=local_request_toggle;
                    if(local_flush) begin
                        // A pulse on the finishing edge belongs to the next
                        // barrier. Busy stays high while that request rearms.
                        local_request_toggle<=!local_request_toggle;
                    end else local_busy<=0;
                end
            end else if(local_flush) begin
                local_request_toggle<=!local_request_toggle;
                local_busy<=1;
            end
        end
    end
    // Share the Host's existing asynchronous assertion and third-USB-edge
    // release so raw PLL/reset removal cannot reach USB barrier business CLR.
    always @(posedge usb_clk or posedge rst) begin
        if(rst) begin
            local_req_meta<=0;local_req_sync<=0;
            local_release_meta<=0;local_release_sync<=0;
            host_done_toggle<=0;active_request<=0;
            barrier_state<=B_IDLE;barrier_epoch_flush<=0;
        end else begin
            local_req_meta<=local_request_toggle;local_req_sync<=local_req_meta;
            local_release_meta<=local_release_toggle;local_release_sync<=local_release_meta;
            barrier_epoch_flush<=0;
            case(barrier_state)
                B_IDLE: if(local_req_sync!=host_done_toggle) begin
                    active_request<=local_req_sync;barrier_state<=B_HOST;
                end
                B_HOST: if(host_flush_done) barrier_state<=B_CDC_IDLE;
                B_CDC_IDLE: if(cdc_src_ready && !src_session_flush) begin
                    // Prior remote flushes must finish first, so this distinct
                    // epoch marker cannot coalesce with an earlier request.
                    barrier_epoch_flush<=1;barrier_state<=B_CDC_LAUNCH;
                end
                B_CDC_LAUNCH: begin
                    // CDC samples the marker on this very edge. Done follows
                    // the same two-stage core crossing as its request, so the
                    // destination flush pulse arrives after done is visible.
                    host_done_toggle<=active_request;barrier_state<=B_RELEASE;
                end
                B_RELEASE: if(local_release_sync==active_request)
                    barrier_state<=B_REARM;
                B_REARM: begin
                    // Host samples request low here to reset its serviced bit.
                    // Keep packet capture gated until the next request is known.
                    if(local_req_sync!=host_done_toggle) begin
                        active_request<=local_req_sync;barrier_state<=B_HOST;
                    end else barrier_state<=B_IDLE;
                end
                default:barrier_state<=B_IDLE;
            endcase
        end
    end
    midi_packet_cdc transfer (
        .src_clk(usb_clk),.dst_clk(core_clk),.rst_n(core_reset_n),
        .src_data(src_packet_data),.src_valid(src_packet_valid && !barrier_active),.src_ready(cdc_src_ready),
        .src_flush(src_session_flush || barrier_epoch_flush),.dst_data(cdc_data),.dst_valid(cdc_valid),
        .dst_ready((local_flush_busy || local_flush) ? 1'b1 : packet_ready),.dst_flush(cdc_session_flush)
    );
    (* syn_preserve=1 *) reg [1:0] configured_sync,error_sync;
    always @(posedge core_clk or negedge core_reset_n) begin
        if(!core_reset_n) begin configured_sync<=0;error_sync<=0;end
        else begin configured_sync<={configured_sync[0],configured};error_sync<={error_sync[0],|error_code};end
    end
    generate if(ENABLE_STATUS_LED) begin : g_status_led
        wire drive;
        midi_status_rgb #(.CLK_HZ(48000000)) pixel (
            .clk(usb_clk),.rst,.configured,.error_code,.enumerating,
            .packet_valid(src_packet_valid && src_packet_ready),
            .session_flush(src_session_flush || barrier_epoch_flush),
            .overflow(1'b0),.led(drive)
        );
        assign status_led=reset_async || rst ? 1'b0 : drive;
    end else begin : g_no_status_led
        assign status_led=1'b0;
    end endgenerate
    assign usb_configured=configured_sync[1];
    assign usb_error=error_sync[1];
endmodule
