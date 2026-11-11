`timescale 1ns/1ps

// Digital low-intent routing only. The physical top implements open-drain 0/Z.
// Unselected or invalid channels cannot drive low or affect master feedback.
module i2c_bus_mux #(
    parameter integer BUSES = 3,
    parameter integer BUS_W = (BUSES <= 1) ? 1 : $clog2(BUSES)
) (
    input  logic enable_i,
    input  logic [BUS_W-1:0] select_i,
    input  logic master_scl_low_i,
    input  logic master_sda_low_i,
    input  logic [BUSES-1:0] scl_i,
    input  logic [BUSES-1:0] sda_i,
    output logic master_scl_o,
    output logic master_sda_o,
    output logic [BUSES-1:0] scl_low_o,
    output logic [BUSES-1:0] sda_low_o
);
    always_comb begin
        scl_low_o = '0;
        sda_low_o = '0;
        master_scl_o = 1'b1;
        master_sda_o = 1'b1;
        for (integer i = 0; i < BUSES; i = i + 1) begin
            if (enable_i && int'(select_i) == i) begin
                scl_low_o[i] = master_scl_low_i;
                sda_low_o[i] = master_sda_low_i;
                master_scl_o = scl_i[i];
                master_sda_o = sda_i[i];
            end
        end
    end
endmodule
