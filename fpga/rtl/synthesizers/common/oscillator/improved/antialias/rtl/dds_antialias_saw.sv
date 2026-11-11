// Increasing saw: original signed phase minus 2^23 * PolyBLEP at its wrap.
// Preserve nominal +/-0.5 Q2.16 scale. Nine registered stages, II=1.
`default_nettype none
module dds_antialias_saw (
    input wire clk, rst_n, in_valid,
    input wire signed [23:0] phase_in,
    input wire [23:0] step,
    // Shared R is aligned to edge stage C4, consumed by PolyBLEP at C5.
    input wire [29:0] reciprocal,
    output logic out_valid,
    output logic signed [17:0] sample_out
);
    import dds_antialias_pkg::*;
    wire edge_valid;
    wire signed [50:0] blep;
    logic signed [23:0] base_pipe [0:BLEP_LATENCY-1];
    // Only bits 50:33 reach the sample. Preserve the discarded low bits'
    // borrow exactly: the base's low 25 bits are zero, so only an 8-bit
    // comparison and a lower-bit nonzero test are needed before subtraction.
    wire [7:0] base_low = base_pipe[BLEP_LATENCY-1][7:0];
    wire low_borrow = (base_low < blep[32:25]) ||
        ((base_low == blep[32:25]) && (|blep[24:0]));
    wire signed [17:0] base_high =
        {{2{base_pipe[BLEP_LATENCY-1][23]}}, base_pipe[BLEP_LATENCY-1][23:8]};
    wire [17:0] sample_next = $unsigned(base_high) - blep[50:33] -
        {17'd0, low_borrow};
    dds_antialias_polyblep u_wrap_edge (
        .clk(clk), .rst_n(rst_n), .in_valid(in_valid),
        .edge_phase($unsigned(phase_in) ^ 24'h800000), .step(step),
        .reciprocal(reciprocal), .out_valid(edge_valid), .correction(blep)
    );
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            out_valid <= 1'b0; sample_out <= '0;
            for (int i=0; i<BLEP_LATENCY; i++) base_pipe[i] <= '0;
        end else begin
            base_pipe[0] <= phase_in;
            for (int i=1; i<BLEP_LATENCY; i++) base_pipe[i] <= base_pipe[i-1];
            out_valid <= edge_valid;
            if (edge_valid) sample_out <= sample_next;
        end
    end
endmodule
`default_nettype wire
