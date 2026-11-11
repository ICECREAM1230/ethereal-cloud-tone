`default_nettype none
// Counted output events: actual serialized source/revision remain one coherent
// bundle. Computational status continues to use control_audio_bridge separately.
module pcm_output_status_bridge (
    input wire audio_clk_i, audio_rst_n_i, control_clk_i, control_rst_n_i,
    input wire frame_tick_i, tap_valid_i, busy_i, fm_active_i,
    input wire [2:0] tap_source_i,
    input wire [31:0] tap_revision_i,
    output logic sample_tick_o, played_tick_o,
    output wire busy_o, fm_active_o,
    output logic played_fm_o,
    output logic [2:0] played_source_o,
    output logic [31:0] played_revision_o,
    output wire [31:0] played_count_o
);
    wire reset_n = audio_rst_n_i && control_rst_n_i;
    wire audio_reset_n, control_reset_n;
    jt51_reset_sync u_audio_reset(.clk_i(audio_clk_i),.rst_n_i(reset_n),.rst_n_o(audio_reset_n));
    jt51_reset_sync u_control_reset(.clk_i(control_clk_i),.rst_n_i(reset_n),.rst_n_o(control_reset_n));
    logic [31:0] sample_count, played_count, revision;
    logic [2:0] source;
    always_ff @(posedge audio_clk_i or negedge audio_reset_n) begin
        if (!audio_reset_n) begin sample_count<=0;played_count<=0;revision<=0;source<=0;end
        else begin
            if (frame_tick_i) sample_count<=sample_count+32'd1;
            if (tap_valid_i) begin
                played_count<=played_count+32'd1;
                revision<=tap_revision_i;source<=tap_source_i;
            end
        end
    end
    wire [100:0] snapshot;
    wire snapshot_valid;
    held_bus_cdc #(.WIDTH(101)) u_snapshot (
        .src_clk_i(audio_clk_i),.dst_clk_i(control_clk_i),.rst_n_i(reset_n),
        .src_data_i({busy_i,fm_active_i,sample_count,played_count,source,revision}),
        .src_valid_i(1'b1),.src_ready_o(),.dst_data_o(snapshot),
        .dst_valid_o(snapshot_valid),.dst_ready_i(1'b1));
    wire [31:0] samples=snapshot[98:67];
    assign played_count_o=snapshot[66:35];
    assign busy_o=!control_reset_n || !seen_snapshot || snapshot[100];
    assign fm_active_o=snapshot[99];
    logic seen_snapshot;
    logic [31:0] previous_samples,previous_played;
    always_ff @(posedge control_clk_i or negedge control_reset_n) begin
        if(!control_reset_n)begin
            seen_snapshot<=0;previous_samples<=0;previous_played<=0;
            sample_tick_o<=0;played_tick_o<=0;played_fm_o<=0;
            played_source_o<=0;played_revision_o<=0;
        end else begin
            sample_tick_o<=0;played_tick_o<=0;
            if(snapshot_valid)begin
                seen_snapshot<=1;
                sample_tick_o<=samples!=previous_samples;
                previous_samples<=samples;previous_played<=played_count_o;
                if(played_count_o!=previous_played)begin
                    played_tick_o<=1;
                    played_source_o<=snapshot[34:32];
                    played_fm_o<=snapshot[34:32]==3'd2;
                    played_revision_o<=snapshot[31:0];
                end
            end
        end
    end
endmodule
`default_nettype wire
