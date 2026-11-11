`timescale 1ns/1ps
`default_nettype none
// The only menu semantics live in C. This module is an APB peripheral,
// double-buffered character store, key sampler and a deliberately virtual device.
module lab_apb #(
    parameter integer BUS_HZ=50_000_000,
    parameter integer DEBOUNCE_CYCLES=500_000,
    parameter integer ACK_CYCLES=200_000
) (
    input wire pclk_i, preset_ni,
    input wire [31:0] paddr_i, pwdata_i,
    input wire [3:0] pstrb_i,
    input wire psel_i, penable_i, pwrite_i,
    output logic [31:0] prdata_o,
    output wire pready_o, output logic pslverr_o,
    input wire [2:0] keys_ni,
    input wire pixel_clk_i,pixel_rst_ni,vblank_i,
    input wire [9:0] text_addr_i,
    output logic [7:0] text_data_o,
    output logic display_valid_o
);
    (* async_reg="true" *) logic [2:0] key_meta,key_sync;
    logic [2:0] key_stable;
    logic [31:0] key_debounce[0:2],key_count[0:2];
    logic [31:0] ms_div,ms_count,scratch,virtual_sample;
    logic [31:0] shadow_gain,shadow_cutoff,shadow_mode;
    logic [31:0] pending_gain,pending_cutoff,pending_mode,pending_seq;
    logic [31:0] applied_gain,applied_cutoff,applied_mode,ack_seq,result_code;
    logic [31:0] mock_count;
    logic mock_busy;
    logic [7:0] text_mem[0:2047];
    // Contents are intentionally uninitialized. CPU fills the complete back
    // bank before publishing; the renderer ignores RAM until the first ACK.
    logic write_bank,bank_hold,pixel_bank;
    logic publish_request,ack_level,publish_busy,ack_seen;
    logic [31:0] publish_seq_hold,display_ack;
    (* async_reg="true" *) logic request_meta,request_sync,ack_meta,ack_sync;
    wire [15:0] offset=paddr_i[15:0];
    wire aligned=paddr_i[1:0]==0 && paddr_i[31:16]==16'hF800;
    wire text_address=offset>=16'h1000 && offset<16'h1c80;
    wire access=psel_i && penable_i;
    wire write_access=access && pwrite_i && !pslverr_o;
    assign pready_o=1'b1;
    function automatic [31:0] merge_bytes(input [31:0] old_value,new_value,input [3:0] strobe);
        reg [31:0] v;
        begin
            v=old_value;
            for (integer b=0;b<4;b=b+1) if(strobe[b]) v[b*8+:8]=new_value[b*8+:8];
            merge_bytes=v;
        end
    endfunction
    always_comb begin
        pslverr_o=1'b0;
        if(access) begin
            if(!aligned) pslverr_o=1'b1;
            else if(pwrite_i) begin
                if(text_address) pslverr_o=publish_busy;
                else case(offset)
                    16'h001c,16'h0030,16'h0034,16'h0038: pslverr_o=1'b0;
                    16'h0024: pslverr_o=mock_busy || pwdata_i==0 || pstrb_i!=4'hf;
                    16'h0050: pslverr_o=publish_busy || pwdata_i==0 || pstrb_i!=4'hf;
                    default: pslverr_o=1'b1;
                endcase
            end else case(offset)
                16'h0000,16'h0004,16'h0008,16'h000c,16'h0010,16'h0014,16'h0018,
                16'h001c,16'h0020,16'h0028,16'h002c,16'h0030,16'h0034,16'h0038,
                16'h003c,16'h0040,16'h0044,16'h0048,16'h0054: pslverr_o=1'b0;
                default: pslverr_o=1'b1;
            endcase
        end
    end
    // One synchronous APB setup read; data is stable for the access phase.
    always_ff @(posedge pclk_i) begin
        if(!preset_ni) prdata_o<=0;
        else if(psel_i && !penable_i && !pwrite_i) begin
            if(!aligned) prdata_o<=0;
            else case(offset)
                16'h0000:prdata_o<=32'h4d454e55;
                16'h0004:prdata_o<=32'h00010000;
                16'h0008:prdata_o<=ms_count;
                16'h000c:prdata_o<={30'd0,mock_busy,publish_busy};
                16'h0010:prdata_o<=key_count[0];
                16'h0014:prdata_o<=key_count[1];
                16'h0018:prdata_o<=key_count[2];
                16'h001c:prdata_o<=scratch;
                16'h0020:prdata_o<={scratch[24:0],scratch[31:25]}^32'ha535138c;
                16'h0028:prdata_o<=ack_seq;
                16'h002c:prdata_o<=result_code;
                16'h0030:prdata_o<=shadow_gain;
                16'h0034:prdata_o<=shadow_cutoff;
                16'h0038:prdata_o<=shadow_mode;
                16'h003c:prdata_o<=applied_gain;
                16'h0040:prdata_o<=applied_cutoff;
                16'h0044:prdata_o<=applied_mode;
                16'h0048:prdata_o<=virtual_sample;
                16'h0054:prdata_o<=display_ack;
                default:prdata_o<=0;
            endcase
        end
    end
    always_ff @(posedge pclk_i or negedge preset_ni) begin
        if(!preset_ni) begin
            key_meta<=3'b111;key_sync<=3'b111;key_stable<=3'b111;
            for(integer k=0;k<3;k=k+1)begin key_debounce[k]<=0;key_count[k]<=0;end
            ms_div<=0;ms_count<=0;virtual_sample<=0;scratch<=0;
            shadow_gain<=50;shadow_cutoff<=1000;shadow_mode<=0;
            pending_gain<=0;pending_cutoff<=0;pending_mode<=0;pending_seq<=0;
            applied_gain<=50;applied_cutoff<=1000;applied_mode<=0;
            ack_seq<=0;result_code<=0;mock_count<=0;mock_busy<=0;
            write_bank<=1;bank_hold<=0;publish_request<=0;ack_seen<=0;
            publish_seq_hold<=0;display_ack<=0;publish_busy<=0;ack_meta<=0;ack_sync<=0;
        end else begin
            key_meta<=keys_ni;key_sync<=key_meta;
            for(integer k=0;k<3;k=k+1)begin
                if(key_sync[k]==key_stable[k])key_debounce[k]<=0;
                else if(key_debounce[k]>=DEBOUNCE_CYCLES-1)begin
                    key_debounce[k]<=0;key_stable[k]<=key_sync[k];
                    if(!key_sync[k])key_count[k]<=key_count[k]+1;
                end else key_debounce[k]<=key_debounce[k]+1;
            end
            if(ms_div==BUS_HZ/1000-1)begin
                ms_div<=0;ms_count<=ms_count+1;virtual_sample<=virtual_sample+1;
            end else ms_div<=ms_div+1;
            ack_meta<=ack_level;ack_sync<=ack_meta;
            // Four-phase level handshake. A receiver-only reset cannot turn
            // an unaccepted request into a successful ACK by clearing a toggle.
            if(publish_busy && !ack_seen && ack_sync)begin
                ack_seen<=1;publish_request<=0;display_ack<=publish_seq_hold;
            end else if(publish_busy && ack_seen && !ack_sync)begin
                publish_busy<=0;write_bank<=~write_bank;ack_seen<=0;
            end
            if(mock_busy)begin
                if(mock_count==0)begin
                    mock_busy<=0;ack_seq<=pending_seq;
                    if(pending_gain>100 || pending_cutoff<20 || pending_cutoff>20000 || pending_mode>2)
                        result_code<=1;
                    else begin
                        result_code<=0;applied_gain<=pending_gain;
                        applied_cutoff<=pending_cutoff;applied_mode<=pending_mode;
                    end
                end else mock_count<=mock_count-1;
            end
            if(write_access)case(offset)
                16'h001c:scratch<=merge_bytes(scratch,pwdata_i,pstrb_i);
                16'h0030:shadow_gain<=merge_bytes(shadow_gain,pwdata_i,pstrb_i);
                16'h0034:shadow_cutoff<=merge_bytes(shadow_cutoff,pwdata_i,pstrb_i);
                16'h0038:shadow_mode<=merge_bytes(shadow_mode,pwdata_i,pstrb_i);
                16'h0024:begin
                    pending_gain<=shadow_gain;pending_cutoff<=shadow_cutoff;pending_mode<=shadow_mode;
                    pending_seq<=pwdata_i;mock_busy<=1;mock_count<=ACK_CYCLES-1;
                end
                16'h0050:begin
                    publish_seq_hold<=pwdata_i;bank_hold<=write_bank;
                    publish_request<=1;publish_busy<=1;ack_seen<=0;
                end
                default:begin end
            endcase
        end
    end
    // True independent-clock RAM ports. No bus read port, no array reset.
    always_ff @(posedge pclk_i)begin
        if(preset_ni && write_access && text_address && pstrb_i[0])
            text_mem[{write_bank,paddr_i[11:2]}]<=pwdata_i[7:0];
    end
    always_ff @(posedge pixel_clk_i) text_data_o<=text_mem[{pixel_bank,text_addr_i}];
    always_ff @(posedge pixel_clk_i or negedge pixel_rst_ni)begin
        if(!pixel_rst_ni)begin
            request_meta<=0;request_sync<=0;ack_level<=0;pixel_bank<=0;display_valid_o<=0;
        end else begin
            request_meta<=publish_request;request_sync<=request_meta;
            if(!request_sync)ack_level<=0;
            else if(vblank_i && !ack_level)begin
                pixel_bank<=bank_hold;ack_level<=1;display_valid_o<=1;
            end
        end
    end
endmodule
`default_nettype wire
