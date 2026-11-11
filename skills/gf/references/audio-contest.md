# Gowin contest audio profile

Load this reference only for the contest's real-time multi-timbre synthesizer or closely related FPGA audio work. Recheck local official PDFs before treating a requirement as authoritative; keep official text, organizer commentary, design choices and verified results labeled separately.

## Requirement mapping

Track the baseline targets (at least four voices, at least two independent control dimensions, HDL real-time audio, pitch/ADSR/mixing, and no more than 10 ms latency) separately from extension goals such as 32 voices, 32 additive components per note, or parallel physical modeling, plus expression, multiple timbres, effects, no more than 5 ms latency, hardware visualization and repertoire guidance. The selected design need not implement every extension. Cite the local official contest PDF for these requirements; treat the supplement and Markdown summaries as navigation/commentary when they differ.

## Review checklist

- Clocks: distinguish target, calculated and measured sample rate; retain oscillator/PLL units and exact parameters.
- Serial audio: identify I2S, left/right justified, or PT8211-style LSBJ; verify slot/word widths, WS alignment, MSB timing and launch/capture edges.
- DDS/ADSR: use the actual update enable, phase width/table index, time units, retrigger/release rules and saturation behavior.
- Fixed point/mixing: track signedness, product widths, rounding/truncation, guard bits and saturation.
- Polyphony/effects: account for per-voice state, allocation/stealing, memory ports, initiation interval, worst-case cycles per sample, delay RAM and feedback stability.
- Sensors/latency: include sampling, filtering/debounce, scheduling and audio output; state both endpoints of every latency claim.
- Spectrum: use actual sample rate, window and resolution; do not confuse harmonics or aliases with oscillators.
- USB/debug/visualization: packetize with sequence/format metadata and buffering; loss or host stalls must not block audio. Confirm whether hardware visualization requirements are met independently. A Lissajous display needs two synchronously sampled signals with stated sources; identical left/right channels normally collapse to a line.

## Current audio example regression facts

These are source-derived regression expectations for `阶段1/audio_i2s/eda_proj/audio.gprj`, not universal defaults or new board measurements. Recompute from current source before reporting them:

- 210 MHz / 4 produces 52.5 MHz; the documented PLL ratio gives about 6.357421875 MHz; divide-by-four gives BCLK about 1.58935546875 MHz.
- With 32 BCLK per stereo frame, per-channel sample rate is about 49.6673583984375 kHz. A request every 16 BCLK alternates channel words and is not the per-channel rate.
- The 256-entry signed sine table advances two entries per channel frame in the observed example, about 388.026 Hz.
- Simultaneous WS and new-MSB updates at the observed counters are not standard I2S's one-bit WS lead.
- Source can establish `PA_EN=0`; board schematic/manual is needed to establish amplifier polarity.

Hearing audio does not establish serial-interface timing, analog quality, exact sample rate, or contest compliance.
