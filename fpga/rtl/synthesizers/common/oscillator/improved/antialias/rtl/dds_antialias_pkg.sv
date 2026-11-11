package dds_antialias_pkg;
    localparam int PHASE_LATENCY = 2;
    localparam int BLEP_LATENCY = 8;
    localparam int WAVE_LATENCY = BLEP_LATENCY + 1;
    localparam int OSCILLATOR_LATENCY = PHASE_LATENCY + WAVE_LATENCY;
endpackage
