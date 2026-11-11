`default_nettype none
module control_input_transport #(
    parameter bit ENABLE_MIDI=1'b1, AUTONOMOUS_USB_MIDI=1'b1,
    parameter bit RAW_SOURCE3_ENABLE=1'b1,
    parameter bit AE350_CONTROL=1'b0, AE350_FDC=1'b0
) (
    input wire sysclk,rst_n,sclk,cs,mosi,
    input wire ae350_flush_done_valid_i,
    input wire [3:0] ae350_flush_done_mask_i,
    input wire ae350_sub_patch1_supported_i,
    input wire [415:0] ae350_template_status_i,
    input wire [31:0] ae350_fdc_status_i,
    input wire [127:0] ae350_fdc_telemetry_i,
    input wire [15:0] ae350_template_free_i,
    input wire ae350_template_overflow_i,
    output wire ae350_template_write_o,
    output wire miso,
    output wire cfg_write_o,
    output wire [15:0] cfg_addr_o,
    output wire [31:0] cfg_data_o,
    output wire cfg_read_req_toggle_o,
    output wire [15:0] cfg_read_addr_o,
    input wire [31:0] cfg_read_data_i,
    input wire usb_packet_valid,usb_configured,usb_error,usb_flush_busy,
    output wire usb_packet_ready,
    input wire source_packet_ready,source_raw_ready,local_switch_mute,
    output wire [1:0] raw_source,
    output wire [7:0] raw_data,
    output wire raw_valid,raw_input_enabled,
    output wire [3:0] mailbox_flush,
    output wire [31:0] mailbox_read,
    input wire [31:0] event_count
);
    // A control reset aborts a partial SPI frame even while SCLK is stopped.
    // After release, a fresh CS-framed word is required for RVR1 shadow_valid.
    wire spi_write;
    wire template_address=(cfg_addr_o>=16'h8000 && cfg_addr_o<=16'h8041) || cfg_addr_o==16'h80ff ||
        (cfg_addr_o>=16'h8100 && cfg_addr_o<=16'h81ff) ||
        (cfg_addr_o>=16'hb000 && cfg_addr_o<=16'hb0ff) ||
        (cfg_addr_o>=16'hb200 && cfg_addr_o<=16'hb27f) ||
        (cfg_addr_o>=16'hb300 && cfg_addr_o<=16'hb33f) ||
        cfg_addr_o==16'hb3fe || cfg_addr_o==16'hb3ff || cfg_addr_o==16'hb400 || cfg_addr_o==16'hb401 ||
        (cfg_addr_o>=16'hb410 && cfg_addr_o<=16'hb41c) || cfg_addr_o==16'hb41f ||
        (AE350_FDC && (cfg_addr_o==16'hb500 || cfg_addr_o==16'hb501 ||
        (cfg_addr_o>=16'hb510 && cfg_addr_o<=16'hb515) || cfg_addr_o==16'hb51e));
    wire reverb_write_address=cfg_addr_o==16'h0725 || cfg_addr_o==16'h0726;
    wire output_route_write_address=cfg_addr_o==16'h07b5;
    wire cpu_write_allowed=cfg_addr_o==16'h0701 || template_address || reverb_write_address || output_route_write_address;
    // Prepared CPU mode owns templates; legacy register mirrors must not
    // also consume the same write. RAW remains handled by the MID1 mailbox.
    // RVR1 shadow/COMMIT control the shared effect, not MIDI voice ownership.
    assign cfg_write_o=spi_write && (!AE350_CONTROL || cfg_addr_o==16'h0701 || reverb_write_address || output_route_write_address);
    assign ae350_template_write_o=AE350_CONTROL && spi_write && template_address;
    logic [31:0] reject_count,reject_gray;
    logic [15:0] last_reject_addr;
    (* ASYNC_REG="TRUE" *) logic [31:0] reject_meta,reject_sync;
    logic [31:0] reject_binary;
    logic read_meta,read_sync,read_previous,read_pending,cpu_selected;
    logic [31:0] cpu_read;
    always_ff @(posedge sclk or negedge rst_n) begin
        if(!rst_n)begin reject_count<=0;reject_gray<=0;last_reject_addr<=0;end
        else if(AE350_CONTROL && spi_write && !cpu_write_allowed)begin
            reject_count<=reject_count+32'd1;
            reject_gray<=((reject_count+32'd1)>>1)^(reject_count+32'd1);
            last_reject_addr<=cfg_addr_o;
        end
    end
    function automatic logic [31:0] gray_decode(input logic [31:0] value);
        logic [31:0] result;
        result[31]=value[31];
        for(integer g=30;g>=0;g=g-1)result[g]=result[g+1]^value[g];
        return result;
    endfunction
    assign reject_binary=gray_decode(reject_sync);
    // Last address is a diagnostic held register; software reads it only after
    // stopping rejected writes and observing a stable reject count. Count uses
    // Gray CDC; constrain its bus skew to one SCLK period.
    always_ff @(posedge sysclk or negedge rst_n) begin
        if(!rst_n)begin
            reject_meta<=0;reject_sync<=0;read_meta<=0;read_sync<=0;read_previous<=0;
            read_pending<=0;cpu_selected<=0;cpu_read<=0;
        end else begin
            reject_meta<=reject_gray;reject_sync<=reject_meta;
            read_meta<=cfg_read_req_toggle_o;read_sync<=read_meta;read_previous<=read_sync;
            read_pending<=read_sync!=read_previous;
            if(read_pending)begin
                cpu_selected<=AE350_CONTROL && cfg_read_addr_o[15:2]==14'h1c6;
                case(cfg_read_addr_o[1:0])
                    0:cpu_read<=32'h43505531;
                    1:cpu_read<=32'd2;
                    2:cpu_read<=reject_binary;
                    3:cpu_read<={16'd0,last_reject_addr};
                endcase
            end
        end
    end
    // Credits are sampled in their owning SPI clock domain. During a read
    // frame no producer writes occur, so a later software burst can reserve
    // no more than this conservative free count. Do not cross its binary
    // value through the unrelated 48MHz status path.
    reg credit_seen;reg [31:0] credit_snapshot;
    always @(posedge sclk or negedge rst_n)begin
        if(!rst_n)begin credit_seen<=0;credit_snapshot<=0;end
        else if(cfg_read_req_toggle_o!=credit_seen)begin
            credit_seen<=cfg_read_req_toggle_o;
            credit_snapshot<={15'd0,ae350_template_overflow_i,ae350_template_free_i};
        end
    end
    // FMC status has its own read-only window; never shadow RVR1 0720..0727.
    localparam logic [15:0] FMC_STATUS_SPI = 16'h07c0;
    reg template_selected;reg [31:0] template_read;
    always @*begin
        template_selected=AE350_CONTROL;template_read=0;
        case(cfg_read_addr_o)
            16'h0707:template_read=ae350_template_status_i[0+:32];
            16'h0708:template_read=ae350_template_status_i[32+:32];
            16'h055c:template_read=ae350_template_status_i[64+:32];
            16'h055d:template_read=ae350_template_status_i[96+:32];
            16'h055e:template_read=ae350_template_status_i[128+:32];
            16'h055b:template_read=ae350_template_status_i[160+:32];
            16'h071d:template_read=ae350_template_status_i[224+:32];
            16'h071e:template_read=ae350_sub_patch1_supported_i?32'h53554231:32'd0;
            16'h071f:template_read=AE350_FDC?ae350_fdc_status_i:32'd0;
            FMC_STATUS_SPI+16'd0:template_read=ae350_template_status_i[256+:32];
            FMC_STATUS_SPI+16'd1:template_read=ae350_template_status_i[288+:32];
            FMC_STATUS_SPI+16'd2:template_read=ae350_template_status_i[320+:32];
            FMC_STATUS_SPI+16'd3:template_read=ae350_template_status_i[352+:32];
            FMC_STATUS_SPI+16'd4:template_read=ae350_template_status_i[384+:32];
            16'h0730:template_read=AE350_FDC?32'h46445231:32'd0; // FDR1, readonly
            16'h0731:template_read=AE350_FDC?ae350_fdc_telemetry_i[0+:32]:32'd0;
            16'h0732:template_read=AE350_FDC?ae350_fdc_telemetry_i[32+:32]:32'd0;
            16'h0733:template_read=AE350_FDC?ae350_fdc_telemetry_i[64+:32]:32'd0;
            16'h0734:template_read=AE350_FDC?ae350_fdc_telemetry_i[96+:32]:32'd0;
            default:template_selected=0;
        endcase
    end
    wire [31:0] spi_read=(AE350_CONTROL&&cfg_read_addr_o==16'h071c)?credit_snapshot:
        template_selected?template_read:cpu_selected?cpu_read:cfg_read_data_i;
    spi_transport u_spi_transport(.sclk,.cs(cs || !rst_n),.mosi,.miso,.cfg_write_o(spi_write),.cfg_addr_o,.cfg_data_o,
        .cfg_read_req_toggle_o,.cfg_read_addr_o,.cfg_read_data_i(spi_read));
    wire midi_write_enable=spi_write && !(AE350_FDC && cfg_addr_o>=16'h0730 && cfg_addr_o<=16'h0734) && cfg_addr_o[15:8]==8'h07 && cfg_addr_o[7:3]!=5'h04;
    wire [7:0] midi_write_addr=cfg_addr_o[7:0],midi_read_addr=cfg_read_addr_o[7:0];
    wire [31:0] midi_write_data=cfg_data_o;
    assign usb_packet_ready=(!AE350_CONTROL && local_switch_mute) ? rst_n : source_packet_ready;
    generate if(ENABLE_MIDI)begin:g_midi
        wire raw_ready=(!AE350_CONTROL && local_switch_mute) ? rst_n : source_raw_ready;
        midi_raw_mailbox #(.WAIT_EXTERNAL_FLUSH(1'b1),.INDEPENDENT_USB(AUTONOMOUS_USB_MIDI),
            .RAW_SOURCE3_ENABLE(RAW_SOURCE3_ENABLE),.AE350_CONTROL(AE350_CONTROL)) u_raw_mailbox (
            .flush_ready(!usb_flush_busy),.ae350_flush_done_valid_i,.ae350_flush_done_mask_i,
            .sclk,.clk(sysclk),.rst_n,.write_enable(midi_write_enable),
            .write_addr(midi_write_addr),.write_data(midi_write_data),
            .read_addr(midi_read_addr),.read_data(mailbox_read),
            .raw_source,.raw_data,.raw_valid,.raw_ready,.flush(mailbox_flush),
            .enabled(raw_input_enabled),.event_count,.usb_configured,.usb_error
        );
    end else begin:g_no_midi
        assign raw_source=0;assign raw_data=0;assign raw_valid=0;assign raw_input_enabled=0;
        assign mailbox_flush=0;assign mailbox_read=0;
    end endgenerate

endmodule
`default_nettype wire
