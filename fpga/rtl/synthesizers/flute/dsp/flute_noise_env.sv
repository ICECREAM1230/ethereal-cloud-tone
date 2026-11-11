`default_nettype none
module flute_noise_env (
    input wire clk_i, rst_n_i,
    input wire note_on_i, note_off_i, panic_i, sample_i,
    input wire [31:0] seed_i,
    input wire signed [31:0] attack_step_i,
    output logic signed [31:0] env_o, noise_o
);
    import flute_math_pkg::*;
    typedef enum logic [2:0] {QUIET, ATTACK, DECAY, HOLD, RELEASE} phase_t;
    phase_t phase;
    logic [31:0] rng;
    logic signed [31:0] attack_step;
    wire [31:0] next_rng = xorshift32(rng);
    always_ff @(posedge clk_i or negedge rst_n_i) begin
        if (!rst_n_i) begin
            phase <= QUIET; env_o <= 0; noise_o <= 0;
            rng <= 1; attack_step <= 0;
        end else if (panic_i) begin
            phase <= QUIET; env_o <= 0; noise_o <= 0; rng <= 1;
        end else if (note_on_i) begin
            phase <= ATTACK; env_o <= 0; noise_o <= 0;
            rng <= seed_i == 0 ? 32'd1 : seed_i;
            attack_step <= attack_step_i;
        end else if (note_off_i) begin
            phase <= RELEASE;
        end else if (sample_i) begin
            rng <= next_rng;
            noise_o <= $signed(next_rng) >>> 5;
            case (phase)
                ATTACK: begin
                    if (env_o >= ONE - attack_step) begin
                        env_o <= ONE; phase <= DECAY;
                    end else env_o <= env_o + attack_step;
                end
                DECAY: begin
                    if (env_o <= SUSTAIN + DECAY_STEP) begin
                        env_o <= SUSTAIN; phase <= HOLD;
                    end else env_o <= env_o - DECAY_STEP;
                end
                RELEASE: begin
                    if (env_o <= RELEASE_STEP) begin
                        env_o <= 0; phase <= QUIET;
                    end else env_o <= env_o - RELEASE_STEP;
                end
                default: begin end
            endcase
        end
    end
endmodule
`default_nettype wire
