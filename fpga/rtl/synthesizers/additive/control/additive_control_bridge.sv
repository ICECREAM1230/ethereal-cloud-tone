// Copyright (c) 2026 ICECREAM1230
// SPDX-License-Identifier: MIT
// SPI bundled-data COMMIT mailbox. Shadow remains immutable until ACK.
// Only this engine owns 0xA000..0xA1FF. No subtractive/CSP state dependency.
`default_nettype none
module additive_control_bridge #(
    parameter bit ENABLE_LOCAL_SELECT=0
) (
    input wire clk,sclk,rst_n,sample_tick,
    input wire perf_write_enable,
    input wire [15:0] perf_write_addr,
    input wire [31:0] perf_write_data,
    output wire selected,bypass_tilt,
    output wire [12:0] centroid,
    output wire [15:0] slope,comb_depth,master_gain,master_pan,
    output wire [4:0] comb_period,
    output wire [6:0] bend_range,
    output wire [63:0] attack_inc,decay_inc,sustain_level,release_inc,
    output wire [767:0] ratios,
    output wire [511:0] weights,
    output wire [63:0] groups,
    output wire [31:0] status,
    input wire local_select_valid_i,local_select_bind_i,
    input wire [31:0] local_select_id_i,
    input wire local_switch_busy_i,
    output wire local_select_ready_o,
    output logic local_select_done_o,
    output logic [31:0] local_select_done_id_o,local_select_result_o
);
    logic [31:0] shadow_control[0:31],active_control[0:31];
    logic [23:0] shadow_ratio[0:31],active_ratio[0:31];
    logic [17:0] shadow_weight[0:31],active_weight[0:31];
    logic request_spi,ack_clk;
    logic [30:0] sequence_spi;
    (* async_reg="true" *) logic ack_meta,ack_sync,req_meta,req_sync;
    wire busy=request_spi!=ack_sync;
    logic select_pending,select_bind,spi_local_override;
    logic [31:0] select_id;
    logic [2:0] switch_tail;
    assign local_select_ready_o=ENABLE_LOCAL_SELECT&&!select_pending;
    wire select_accept=local_select_valid_i&&local_select_ready_o;
    wire local_fence=ENABLE_LOCAL_SELECT&&(local_switch_busy_i||select_pending||select_accept||(|switch_tail));
    assign status={busy,sequence_spi};
    assign selected=active_control[0][0];
    assign bypass_tilt=active_control[0][1];
    assign centroid=active_control[1][12:0];
    assign slope=active_control[1][31:16];
    assign comb_period=active_control[2][4:0];
    assign comb_depth=active_control[2][31:16];
    assign master_gain=active_control[3][15:0];
    assign master_pan=active_control[3][31:16];
    assign bend_range=active_control[4][6:0];
    for(genvar g=0;g<4;g++)begin:g_rates
        assign attack_inc[g*16+:16]=active_control[16+g][15:0];
        assign decay_inc[g*16+:16]=active_control[16+g][31:16];
        assign sustain_level[g*16+:16]=active_control[20+g][15:0];
        assign release_inc[g*16+:16]=active_control[20+g][31:16];
    end
    for(genvar p=0;p<32;p++)begin:g_partials
        assign ratios[p*24+:24]=active_ratio[p];
        assign weights[p*16+:16]=active_weight[p][15:0];
        assign groups[p*2+:2]=active_weight[p][17:16];
    end
    function automatic [31:0] control_default(input integer index);
        case(index)
            0:control_default=32'd2; // additive disabled; its output tilt bypass available.
            3:control_default={16'd32768,16'd16384};
            4:control_default=32'd2;
            16,17,18,19:control_default={16'd8,16'd2048};
            20:control_default={16'd8,16'd49152};
            21:control_default={16'd12,16'd32768};
            22:control_default={16'd20,16'd16384};
            23:control_default={16'd32,16'd8192};
            default:control_default=32'd0;
        endcase
    endfunction
    always_ff @(posedge sclk or negedge rst_n)begin
        if(!rst_n)begin
            request_spi<=0;ack_meta<=0;ack_sync<=0;sequence_spi<=0;
            for(int i=0;i<32;i++)begin
                shadow_control[i]<=control_default(i);
                shadow_ratio[i]<=24'((i+1)*65536);
                shadow_weight[i]<={2'(i/8),16'(65535/(i+1))};
            end
        end else begin
            ack_meta<=ack_clk;ack_sync<=ack_meta;
            if(perf_write_enable && !busy)begin
                if(perf_write_addr>=16'ha000 && perf_write_addr<16'ha020)
                    shadow_control[perf_write_addr[4:0]]<=perf_write_data;
                if(perf_write_addr>=16'ha100 && perf_write_addr<16'ha180)begin
                    case(perf_write_addr[1:0])
                        0:shadow_ratio[perf_write_addr[6:2]]<=perf_write_data[23:0];
                        1:shadow_weight[perf_write_addr[6:2]]<=perf_write_data[17:0];
                        default:begin end
                    endcase
                end
                if(perf_write_addr==16'ha0ff && perf_write_data[30:0]!=0)begin
                    sequence_spi<=perf_write_data[30:0];request_spi<=~request_spi;
                end
            end
        end
    end
    // Keep the frame-boundary write enable local to four parameter rows.
    // Separate preserved nets prevent synthesis from rebuilding one CE net
    // for the complete active bank. Publication and ACK keep their old edge.
    for(genvar bank=0;bank<8;bank++)begin:g_active_bank
        wire bank_commit /* synthesis syn_dont_touch = 1 */;
        assign bank_commit=sample_tick && req_sync!=ack_clk;
        always_ff @(posedge clk or negedge rst_n)begin
            if(!rst_n)begin
                for(int i=bank*4;i<bank*4+4;i++)begin
                    active_control[i]<=control_default(i);
                    active_ratio[i]<=24'((i+1)*65536);
                    active_weight[i]<={2'(i/8),16'(65535/(i+1))};
                end
            end else begin
                if(bank_commit)begin
                    for(int i=bank*4;i<bank*4+4;i++)begin
                        active_control[i]<=shadow_control[i];
                        active_ratio[i]<=shadow_ratio[i];
                        active_weight[i]<=shadow_weight[i];
                    end
                end
                // Local selection overlays only this bit of an otherwise
                // atomic SPI package, including a simultaneous COMMIT.
                if(bank==0 && sample_tick &&
                   (local_fence||(req_sync!=ack_clk&&spi_local_override)))
                    active_control[0][0]<=select_accept?local_select_bind_i:select_bind;
            end
        end
    end
    always_ff @(posedge clk or negedge rst_n)begin
        if(!rst_n)begin
            req_meta<=0;req_sync<=0;ack_clk<=0;
            select_pending<=0;select_bind<=0;select_id<=0;switch_tail<=0;spi_local_override<=0;
            local_select_done_o<=0;local_select_done_id_o<=0;local_select_result_o<=0;
        end else begin
            req_meta<=request_spi;req_sync<=req_meta;
            switch_tail<={switch_tail[1:0],ENABLE_LOCAL_SELECT&&(local_switch_busy_i||select_pending||select_accept)};
            local_select_done_o<=0;
            if(select_accept)begin
                select_pending<=1;select_bind<=local_select_bind_i;select_id<=local_select_id_i;
            end
            // Remember ownership when the request reaches this domain. Its
            // sample-boundary commit may occur after the busy fence ends.
            if(req_sync!=ack_clk&&local_fence)spi_local_override<=1;
            if(sample_tick && req_sync!=ack_clk)begin
                ack_clk<=req_sync;
                spi_local_override<=0;
            end
            // A complete SPI package still commits and receives its normal
            // ACK. During local ownership only its selected bit is overlaid;
            // all ratios, weights, envelopes and other controls remain intact.
            if(sample_tick&&select_pending)begin
                select_pending<=0;local_select_done_o<=1;
                local_select_done_id_o<=select_id;local_select_result_o<=0;
            end
        end
    end
endmodule
`default_nettype wire
