`timescale 1ns/1ps
`default_nettype none
// PCLK-only registered APB bridge. The downstream menu needs a complete SETUP
// cycle before ACCESS. CPU responses have no combinational slave return path.
module lab_apb_pipeline(
    input wire pclk_i,preset_ni,
    input wire [31:0] paddr_i,pwdata_i,
    input wire [3:0] pstrb_i,
    input wire psel_i,penable_i,pwrite_i,control_available_i,
    input wire [31:0] menu_rdata_i,control_rdata_i,
    input wire menu_ready_i,menu_error_i,control_ready_i,control_error_i,
    output wire [31:0] prdata_o,
    output wire pready_o,pslverr_o,
    output wire [31:0] slave_addr_o,slave_wdata_o,
    output wire [3:0] slave_strb_o,
    output wire slave_write_o,slave_enable_o,menu_sel_o,control_sel_o
);
    typedef enum logic[1:0] {IDLE,SETUP,ACCESS,RESP} state_t;
    state_t state;
    localparam [1:0] NO_PAGE=0,MENU_PAGE=1,CONTROL_PAGE=2;
    reg [1:0] page;
    reg [31:0] address,wdata,response_data;
    reg [3:0] strobe;
    reg write_request,invalid_request,response_error;
    wire unavailable=page==CONTROL_PAGE && !control_available_i;
    wire downstream_active=preset_ni && psel_i && !invalid_request && !unavailable &&
        (state==SETUP || state==ACCESS);
    assign slave_addr_o=address;
    assign slave_wdata_o=wdata;
    assign slave_strb_o=strobe;
    assign slave_write_o=write_request;
    assign menu_sel_o=downstream_active && page==MENU_PAGE;
    assign control_sel_o=downstream_active && page==CONTROL_PAGE;
    assign slave_enable_o=downstream_active && state==ACCESS && penable_i;
    assign prdata_o=response_data;
    assign pready_o=state==RESP;
    assign pslverr_o=state==RESP && response_error;

    // Synchronous CPU reset: an unexecuted request is discarded at this edge.
    // Once a downstream ACCESS has completed, its effects cannot be rolled
    // back by an upstream cancellation during the registered response cycle.
    always @(posedge pclk_i)begin
        if(!preset_ni)begin
            state<=IDLE;page<=NO_PAGE;address<=0;wdata<=0;strobe<=0;
            write_request<=0;invalid_request<=0;response_data<=0;response_error<=0;
        end else case(state)
            IDLE:if(psel_i && !penable_i)begin
                address<=paddr_i;wdata<=pwdata_i;strobe<=pstrb_i;write_request<=pwrite_i;
                if(paddr_i[31:16]==16'hf800)begin page<=MENU_PAGE;invalid_request<=0;end
                else if(paddr_i[31:16]==16'hf801)begin page<=CONTROL_PAGE;invalid_request<=!control_available_i;end
                else begin page<=NO_PAGE;invalid_request<=1;end
                state<=SETUP;
            end
            SETUP:begin
                if(!psel_i)state<=IDLE;
                else begin
                    // Any observed owner loss retires this captured session,
                    // even if availability returns before external ACCESS.
                    if(unavailable)invalid_request<=1;
                    if(penable_i)state<=ACCESS;
                end
            end
            ACCESS:begin
                if(!psel_i || !penable_i)state<=IDLE;
                else if(invalid_request || unavailable)begin
                    response_data<=0;response_error<=1;state<=RESP;
                end else if(page==MENU_PAGE && menu_ready_i)begin
                    response_data<=menu_rdata_i;response_error<=menu_error_i;state<=RESP;
                end else if(page==CONTROL_PAGE && control_ready_i)begin
                    response_data<=control_rdata_i;response_error<=control_error_i;state<=RESP;
                end
            end
            RESP:state<=IDLE;
            default:state<=IDLE;
        endcase
    end
endmodule
`default_nettype wire
