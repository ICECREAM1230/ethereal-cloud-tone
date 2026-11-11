`default_nettype none
// One write + synchronous read port. No reset on memory/read data registers.
// Start edge t0 writes/reads; valid rises at t2, parent consumes at t3.
module flute_delay_pair (
    input wire clk_i, rst_n_i,
    input wire clear_i,
    input wire [8:0] clear_addr_i,
    input wire start_i,
    input wire signed [31:0] sample_i,
    input wire [8:0] delay_int_i,
    output logic signed [31:0] newer_o, older_o,
    output logic valid_o
);
    logic signed [31:0] mem [0:511];
    logic signed [31:0] read_data;
    logic [8:0] cursor, older_addr;
    logic [1:0] phase;
    wire [8:0] newer_addr = cursor - delay_int_i;
    // Delay >= 2 ensures that both reads are distinct from this cycle's write.
    always_ff @(posedge clk_i) begin
        if (clear_i) mem[clear_addr_i] <= 0;
        else if (start_i) mem[cursor] <= sample_i;
        if (start_i && !clear_i) read_data <= mem[newer_addr];
        else if (phase == 1 && !clear_i) read_data <= mem[older_addr];
    end
    always_ff @(posedge clk_i or negedge rst_n_i) begin
        if (!rst_n_i) begin
            cursor <= 0; older_addr <= 0; phase <= 0;
            newer_o <= 0; older_o <= 0; valid_o <= 0;
        end else begin
            valid_o <= 0;
            if (clear_i) begin cursor <= 0; phase <= 0; end
            else if (start_i) begin
                older_addr <= newer_addr - 9'd1;
                cursor <= cursor + 9'd1;
                phase <= 1;
            end else if (phase == 1) begin
                newer_o <= read_data; phase <= 2;
            end else if (phase == 2) begin
                older_o <= read_data; phase <= 0; valid_o <= 1;
            end
        end
    end
endmodule
`default_nettype wire
