# SUB_PATCH1 (approved 2026-10-30)

The SPI owner must observe `0x071E == 0x53554231` before emitting this
extension. A successful observation of zero means the original template ABI
remains usable; I/O failure is not capability discovery. CPU mode RPC version 1
(`B401` / `071D`) is independent. The existing 64 element words and complete
3084-byte in-memory image retain their sizes.

The immutable compile/cache input is `{ patch_t patch, int32 cutoff_offset,
int32 resonance_offset }`, plus the selected extension capability and the
existing KS decay/model generation. No compiler read of live owner globals is
permitted. The owner alone adopts file/CC state; SPI I/O remains in its link task.

| Word address | Value |
| --- | --- |
| 800D | magic `53554231` |
| 800E | version `00010000` |
| 800F | complete field mask `00000FFF` |
| 8015, 8016 | AMP `patch_adsr_rate1`, `patch_adsr_rate2` |
| 8017, 801D | MOD `patch_adsr_rate1`, `patch_adsr_rate2` |
| 801E | LFO0 config: `E \| shape<<4 \| 2<<6 \| rate<<16` |
| 801F | LFO0 depth, signed18 sign-extended to32 |
| 8025 | LFO1 config: `E \| shape<<4 \| bus<<6 \| rate<<16` |
| 8026 | LFO1 depth, signed18 sign-extended to32 |
| 8027 | gain base `-10240 + (patch.volume-207)*64`, signed18 |
| 802D | cutoff offset, signed18 sign-extended to32 |
| 802E | MOD destination, only cutoff (`0`) is supported |
| 802F | resonance offset, signed18 sign-extended to32 |

LFO1 destination 0/1/2/3 maps to bus1/3/2/4 respectively. LFO0 is fixed to
pitch bus2. Shapes 0..3 are accepted; existing negative-LFO-depth policy is
unchanged (rejected). AMP and MOD use the original native ADSR coefficient
helpers. The existing wheel/touch words `8005..8007`, note velocity tables,
KS/FM bytes and unused reserved words retain their prior meaning.

PWM CC25/85 uses the original Q0.24 offset:
`trunc((0.5 - 0.5*10^(-CC/127))*2^24)`.
LFO CC76/109 uses the original half-rate phase increment:
`round(0.03*1000^(CC/127)*2^24/48000)`.
CC0/64/127 anchors are PWM 0/5759836/7549747 and LFO 10/341/10486.

Optional HTTP metadata is `capabilities.sub_patch = {version:1, required:true,
supported:boolean}` and `state.sub_patch` with an additional `valid:boolean`.
Legacy/non-CPU endpoints omit both. CPU endpoints explicitly report unsupported
capability. The browser must gate complete-only CC on matching supported v1
metadata and a valid state observation. The CC list is 7,71,72,73,74,75,76,77,79,
87,102,103,104,105,106,107,109,110,111,112,113. HTTP and owner dispatch also
reject these controls while capability is unavailable. Notes, STOP, file
management and existing supported static controls remain usable.

A file-load job completes patch adoption after the existing all-source STOP
fence. A CC parameter sequence confirms owner adoption. Native template ACK and
the CPU's full GLOBAL activation/held-voice refresh are later, distinct stages;
neither HTTP receipt nor a cached desired value proves audible application.
Real full-parameter audio acceptance requires the matching CPU/native/CSP
implementation and capability. Raw MIDI CC7/10/11 channel gain policy, FM/KS
full parameter interfaces, arbitrary MOD matrix, arp and LFO0 other destinations
are outside this extension.
