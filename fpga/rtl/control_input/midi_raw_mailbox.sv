// SPDX-License-Identifier: MIT
// Frozen MID1 0x07xx contract. Source payload is bundled data: it remains
// unchanged from request launch until the acknowledged request returns through
// two SCLK registers. The destination samples it only after two clk registers.
module midi_raw_mailbox #(
    parameter bit WAIT_EXTERNAL_FLUSH = 0,
    parameter bit AE350_CONTROL = 0,
    parameter bit INDEPENDENT_USB = 0,
    parameter bit RAW_SOURCE3_ENABLE = 1 // Generic compatibility; production reserves 3 for sensors.
) (
    input  logic        sclk,
    input  logic        clk,
    input  logic        rst_n,
    input  logic        write_enable,
    input  logic [7:0]  write_addr,
    input  logic [31:0] write_data,
    input  logic [7:0]  read_addr,
    output logic [31:0] read_data,
    output logic [1:0]  raw_source,
    output logic [7:0]  raw_data,
    output logic        raw_valid,
    input  logic        raw_ready,
    input  logic        flush_ready,
    input  logic        ae350_flush_done_valid_i,
    input  logic [3:0]  ae350_flush_done_mask_i,
    output logic [3:0]  flush,
    output logic        enabled,
    input  logic [31:0] event_count,
    input  logic        usb_configured,
    input  logic        usb_error
);
    logic [31:0] payload;
    logic request_toggle, acknowledge_toggle;
    (* ASYNC_REG = "TRUE" *) logic ack_meta, ack_sync;
    (* ASYNC_REG = "TRUE" *) logic req_meta, req_sync;
    logic [31:0] busy_reject_count, busy_reject_gray;
    (* ASYNC_REG = "TRUE" *) logic [31:0] reject_gray_meta, reject_gray_sync;
    logic [31:0] busy_reject_decoded;
    logic [31:0] accept_count, invalid_reject_count;
    logic accept_pulse_q;
    logic pending, legal_raw, wait_flush;
    logic wait_external, external_sampled;
    logic [3:0] cpu_wait_mask;
    logic cpu_flush_complete;

    function automatic logic [31:0] gray_to_binary(input logic [31:0] gray);
        logic [31:0] binary;
        binary[31] = gray[31];
        for (integer i = 30; i >= 0; i = i - 1)
            binary[i] = binary[i+1] ^ gray[i];
        return binary;
    endfunction

    // Only registered Gray crosses for the potentially repeated busy rejections.
    // Physical implementation must constrain Gray bus skew to one SCLK period.
    always_ff @(posedge sclk or negedge rst_n) begin
        if (!rst_n) begin
            payload <= 0;
            request_toggle <= 0;
            ack_meta <= 0;
            ack_sync <= 0;
            busy_reject_count <= 0;
            busy_reject_gray <= 0;
        end else begin
            ack_meta <= acknowledge_toggle;
            ack_sync <= ack_meta;
            if (write_enable && write_addr == 8'h01) begin
                if (request_toggle != ack_sync) begin
                    busy_reject_count <= busy_reject_count + 32'd1;
                    busy_reject_gray <= ((busy_reject_count + 32'd1) >> 1)
                                      ^ (busy_reject_count + 32'd1);
                end else begin
                    payload <= write_data;
                    request_toggle <= ~request_toggle;
                end
            end
        end
    end

    assign pending = req_sync != acknowledge_toggle;
    localparam logic [3:0] RAW_MASK = RAW_SOURCE3_ENABLE ? 4'b1110 : 4'b0110;
    localparam logic [3:0] FLUSH_ALL_MASK = RAW_MASK | (INDEPENDENT_USB ? 4'b0000 : 4'b0001);
    assign legal_raw = payload[31:24] == 8'h01 && payload[15:8] == 0
                    && payload[23:16] >= 1 && payload[23:16] <= (RAW_SOURCE3_ENABLE ? 8'd3 : 8'd2);
    assign raw_valid = pending && !wait_flush && legal_raw && enabled;
    assign raw_source = payload[17:16];
    assign raw_data = payload[7:0];
    // Register only the synchronized Gray decoding. The sum at 0x04 stays
    // combinational, so Gray-to-binary and the counter adder use separate cycles.
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) busy_reject_decoded <= 0;
        else busy_reject_decoded <= gray_to_binary(reject_gray_sync);
    end

    always_comb begin
        case (read_addr)
            8'h00: read_data = 32'h4d494431;
            8'h02: read_data = {30'b0, enabled, pending};
            8'h03: read_data = accept_count;
            8'h04: read_data = invalid_reject_count + busy_reject_decoded;
            8'h05: read_data = event_count;
            8'h06: read_data = {30'b0, usb_error, usb_configured};
            // Bit 0: ENABLE and FLUSH FF affect only SPI-owned RAW sources.
            // [1]: source3 is native sensors, not a RAW MIDI/DIN stream.
            8'h0a: read_data = {29'b0, AE350_CONTROL, !RAW_SOURCE3_ENABLE, INDEPENDENT_USB};
            default: read_data = 0;
        endcase
    end

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            req_meta <= 0;
            req_sync <= 0;
            reject_gray_meta <= 0;
            reject_gray_sync <= 0;
            acknowledge_toggle <= 0;
            accept_count <= 0;
            accept_pulse_q <= 0;
            invalid_reject_count <= 0;
            enabled <= 0;
            flush <= 0;
            wait_flush <= 0;
            wait_external <= 0;
            external_sampled <= 0;
            cpu_wait_mask <= 0;cpu_flush_complete <= 0;
        end else begin
            accept_pulse_q <= 0;
            if (accept_pulse_q) accept_count <= accept_count + 32'd1;
            req_meta <= request_toggle;
            req_sync <= req_meta;
            reject_gray_meta <= busy_reject_gray;
            reject_gray_sync <= reject_gray_meta;
            flush <= 0;
            if (AE350_CONTROL && pending && !wait_flush && legal_raw &&
                ae350_flush_done_valid_i && ae350_flush_done_mask_i[raw_source]) begin
                // A global recovery cancels an unaccepted old SPI byte too.
                acknowledge_toggle <= req_sync;
                invalid_reject_count <= invalid_reject_count + 32'd1;
            end else if (wait_flush) begin
                if (AE350_CONTROL && ae350_flush_done_valid_i &&
                    (ae350_flush_done_mask_i & cpu_wait_mask)==cpu_wait_mask)
                    cpu_flush_complete <= 1;
                // The first edge after assertion is when the downstream sees
                // flush. Its busy register changes only AFTER that edge, so an
                // external USB barrier must not inspect the old ready there.
                if (wait_external && !external_sampled)
                    external_sampled <= 1;
                else if ((!wait_external || flush_ready) &&
                         (!AE350_CONTROL || cpu_flush_complete)) begin
                    wait_flush <= 0;
                    acknowledge_toggle <= req_sync;
                    accept_pulse_q <= 1'b1;
                end
            end else if (pending) begin
                if (legal_raw && enabled) begin
                    if (raw_ready) begin
                        acknowledge_toggle <= req_sync;
                        accept_pulse_q <= 1'b1;
                    end
                end else if (payload[31:24] == 8'h02 && payload[15:0] == 0
                          && (payload[23:16] <= (RAW_SOURCE3_ENABLE ? 8'd3 : 8'd2) || payload[23:16] == 8'hff)) begin
                    flush <= payload[23:16] == 8'hff ? FLUSH_ALL_MASK
                           : (4'b0001 << payload[17:16]);
                    wait_flush <= 1;
                    cpu_wait_mask <= payload[23:16] == 8'hff ? FLUSH_ALL_MASK : (4'b0001 << payload[17:16]);
                    cpu_flush_complete <= 0;
                    wait_external <= WAIT_EXTERNAL_FLUSH
                                  && (payload[23:16] == 0 ||
                                      (payload[23:16] == 8'hff && !INDEPENDENT_USB));
                    external_sampled <= 0;
                end else if (payload[31:24] == 8'h03 && payload[23:16] == 8'hff
                          && payload[15:1] == 0) begin
                    if (enabled != payload[0]) begin
                        enabled <= payload[0];
                        flush <= FLUSH_ALL_MASK;
                        wait_flush <= 1;
                        cpu_wait_mask <= FLUSH_ALL_MASK;cpu_flush_complete <= 0;
                        wait_external <= WAIT_EXTERNAL_FLUSH && !INDEPENDENT_USB;
                        external_sampled <= 0;
                    end else begin
                        acknowledge_toggle <= req_sync;
                        accept_pulse_q <= 1'b1;
                    end
                end else begin
                    // Includes valid-looking RAW while disabled: release the
                    // slot rather than waiting forever for an unavailable sink.
                    acknowledge_toggle <= req_sync;
                    invalid_reject_count <= invalid_reject_count + 32'd1;
                end
            end
            // Internal APB recovery uses all four bits (normal production
            // source boundaries use 0..2). Revoke RAW even if this mailbox was
            // empty: the S3 must observe disabled and retire its unsent queue,
            // rather than replaying pre-recovery bytes after the CPU panic.
            if(AE350_CONTROL && ae350_flush_done_valid_i && ae350_flush_done_mask_i==4'hf)
                enabled<=0;
        end
    end
endmodule
