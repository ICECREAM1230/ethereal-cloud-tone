// Fixed contracts for the separate 24-bit Tang Mega DDS candidate.
package dds_improve_pkg;
    localparam int PHASE_LATENCY = 2;
    localparam int SINE_LATENCY = 9;
    localparam int OSCILLATOR_LATENCY = PHASE_LATENCY + SINE_LATENCY;
endpackage
