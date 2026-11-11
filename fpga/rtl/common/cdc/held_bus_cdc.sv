// SPDX-License-Identifier: MIT
// One bundled-data slot. The source holds the whole payload until the
// destination has accepted it; only the request and acknowledgement cross
// through synchronizers. Constrain the held bus relative to the request path.
module held_bus_cdc #(
    parameter integer WIDTH = 32,
    parameter bit DISTRIBUTE_SOURCE_RESET = 1'b0,
    parameter bit PRELOAD_WHEN_READY = 1'b0
) (
    input  logic                 src_clk_i,
    input  logic                 dst_clk_i,
    input  logic                 rst_n_i,
    input  logic [WIDTH-1:0]     src_data_i,
    input  logic                 src_valid_i,
    output logic                 src_ready_o,
    output logic [WIDTH-1:0]     dst_data_o,
    output logic                 dst_valid_o,
    input  logic                 dst_ready_i
);
    logic [WIDTH-1:0] held_data;
    logic req, ack;
    (* syn_preserve = 1, ASYNC_REG = "TRUE" *) logic src_reset_meta, src_reset_sync;
    (* syn_preserve = 1, ASYNC_REG = "TRUE" *) logic dst_reset_meta, dst_reset_sync;
    (* ASYNC_REG = "TRUE" *) logic ack_meta, ack_sync;
    (* ASYNC_REG = "TRUE" *) logic req_meta, req_sync;

    always_ff @(posedge src_clk_i or negedge rst_n_i) begin
        if (!rst_n_i) begin
            src_reset_meta <= 1'b0;
            src_reset_sync <= 1'b0;
        end else begin
            src_reset_meta <= 1'b1;
            src_reset_sync <= src_reset_meta;
        end
    end

    always_ff @(posedge dst_clk_i or negedge rst_n_i) begin
        if (!rst_n_i) begin
            dst_reset_meta <= 1'b0;
            dst_reset_sync <= 1'b0;
        end else begin
            dst_reset_meta <= 1'b1;
            dst_reset_sync <= dst_reset_meta;
        end
    end

    wire source_control_reset_n;
    generate if (DISTRIBUTE_SOURCE_RESET) begin : g_distributed_source_reset
        // Local asynchronous assertion and source-clock release at two levels.
        // The control branch waits exactly as long as every payload leaf.
        // Preserve these equivalent FFs with the Gowin SUG550 5.5 syntax.
        localparam integer BANK_BITS = 128;
        localparam integer LEAF_BITS = 16;
        localparam integer BANKS = (WIDTH + BANK_BITS - 1) / BANK_BITS;
        reg control_bank_reset_n /* synthesis syn_dont_touch = 1 */;
        reg control_reset_n /* synthesis syn_dont_touch = 1 */;
        always_ff @(posedge src_clk_i or negedge src_reset_sync)
            if (!src_reset_sync) control_bank_reset_n <= 1'b0;
            else control_bank_reset_n <= 1'b1;
        always_ff @(posedge src_clk_i or negedge control_bank_reset_n)
            if (!control_bank_reset_n) control_reset_n <= 1'b0;
            else control_reset_n <= 1'b1;
        assign source_control_reset_n = control_reset_n;

        for (genvar bank = 0; bank < BANKS; bank++) begin : banks
            localparam integer BANK_BASE = bank * BANK_BITS;
            localparam integer BANK_WIDTH = WIDTH - BANK_BASE < BANK_BITS ?
                                            WIDTH - BANK_BASE : BANK_BITS;
            localparam integer LEAVES = (BANK_WIDTH + LEAF_BITS - 1) / LEAF_BITS;
            reg bank_reset_n /* synthesis syn_dont_touch = 1 */;
            always_ff @(posedge src_clk_i or negedge src_reset_sync)
                if (!src_reset_sync) bank_reset_n <= 1'b0;
                else bank_reset_n <= 1'b1;
            for (genvar leaf = 0; leaf < LEAVES; leaf++) begin : leaves
                localparam integer BASE = BANK_BASE + leaf * LEAF_BITS;
                localparam integer BITS = WIDTH - BASE < LEAF_BITS ?
                                         WIDTH - BASE : LEAF_BITS;
                reg leaf_reset_n /* synthesis syn_dont_touch = 1 */;
                always_ff @(posedge src_clk_i or negedge bank_reset_n)
                    if (!bank_reset_n) leaf_reset_n <= 1'b0;
                    else leaf_reset_n <= 1'b1;
                // Keep the whole held_data vector and original bundle name.
                // Preloading is private; the public handshake still captures
                // that edge's payload before the request toggle launches.
                always_ff @(posedge src_clk_i or negedge leaf_reset_n) begin
                    if (!leaf_reset_n) held_data[BASE +: BITS] <= '0;
                    else if (src_ready_o && (PRELOAD_WHEN_READY || src_valid_i))
                        held_data[BASE +: BITS] <= src_data_i[BASE +: BITS];
                end
            end
        end
    end else begin : g_direct_source_reset
        assign source_control_reset_n = src_reset_sync;
        always_ff @(posedge src_clk_i or negedge src_reset_sync) begin
            if (!src_reset_sync) held_data <= '0;
            else if (src_ready_o && (PRELOAD_WHEN_READY || src_valid_i)) held_data <= src_data_i;
        end
    end endgenerate

    assign src_ready_o = source_control_reset_n && (req == ack_sync);

    always_ff @(posedge src_clk_i or negedge source_control_reset_n) begin
        if (!source_control_reset_n) begin
            req <= 1'b0;
            ack_meta <= 1'b0;
            ack_sync <= 1'b0;
        end else begin
            ack_meta <= ack;
            ack_sync <= ack_meta;
            if (src_valid_i && src_ready_o) begin
                req <= ~req;
            end
        end
    end

    always_ff @(posedge dst_clk_i or negedge dst_reset_sync) begin
        if (!dst_reset_sync) begin
            req_meta <= 1'b0;
            req_sync <= 1'b0;
            ack <= 1'b0;
            dst_data_o <= '0;
            dst_valid_o <= 1'b0;
        end else begin
            req_meta <= req;
            req_sync <= req_meta;
            if (dst_valid_o && dst_ready_i) begin
                dst_valid_o <= 1'b0;
                ack <= req_sync;
            end else if (!dst_valid_o && (req_sync != ack)) begin
                dst_data_o <= held_data;
                dst_valid_o <= 1'b1;
            end
        end
    end
endmodule
