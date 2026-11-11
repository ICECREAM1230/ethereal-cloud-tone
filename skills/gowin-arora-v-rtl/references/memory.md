# Memory

## Choose the ports and resource

Define each array's read and write addresses, clocks, CE, read latency, OCE, byte enables, and collision behavior. A two-port BSRAM does not provide four independent addresses.

| Requirement | Candidate |
|---|---|
| Larger storage with clocked access | BSRAM |
| Small asynchronous-read table | SSRAM on supported parts, or LUTs/FFs |
| Short delay line with a slow SSRAM output | Registers, another partition, or an output stage |
| Unsupported port count or whole-array reset | Arbitration, banking, scheduling, or a register implementation |

Read replication can add read ports but does not solve arbitrary multiple writes.

## BSRAM modes

- SP, SDP, and ROM support `512 x 32` or `512 x 36` configurations.
- True dual-port modes support up to `1K x 16` or `1K x 18` per block. They do not provide a single-block `512 x 32/36` mode.
- The `512 x 72` configuration belongs to SDP36KE/ECC. Do not treat it as a general 36-Kbit mode.
- A `256 x 32` true dual-port array normally needs two blocks in parallel. A one-write/one-read SDP array can fit one 32-bit block. Confirm actual mapping.
- Bypass mode bypasses the extra output register; it does not make BSRAM an arbitrary asynchronous-read memory. Pipeline mode adds a cycle. Check CE/OCE behavior.

See [BSRAM configurations](manuals/markdown/UG300E.md#pdf-page-13) and [port modes](manuals/markdown/UG300E.md#pdf-page-15).

When instantiating primitives, map addresses and byte enables to the physical ports. Low address bits may control byte lanes. Do not connect a logical address without checking the selected mode.

## Device restrictions and collisions

Arora V 138K/75K/60K/25K/15K devices do not support Read-before-Write. Define the required result before choosing forwarding, arbitration, or another architecture. See [mode restrictions](manuals/markdown/UG300E.md#pdf-page-13).

Cross-port same-address writes and simultaneous cross-port read/write are prohibited for the described DP/SDP modes. The primitive restriction compares physical address bits `[13:4]`. Check address mapping, mixed widths, and clocks. Treat legal single-port write-through separately. See [DP collision rules](manuals/markdown/UG300E.md#pdf-page-15) and [SDP rules](manuals/markdown/UG300E.md#pdf-page-33).

These parts do not support `RAM16S1/2/4` or `RAM16SDP1/2/4`:

- `GW5AST-138B`, `GW5A-138B`, `GW5AT-138B`, `GW5AS-138B`
- `GW5AT-75B`, `GW5A-25A`, `GW5AS-25A`, `GW5AR-25A`

Do not rely on `distributed_ram` for these primitives. This restriction does not by itself exclude ROM16. See [SSRAM support](manuals/markdown/UG300E.md#pdf-page-54).

## Reset and initialization

BSRAM reset clears its output structure, not the memory array. Preserve visible clearing behavior with a clear state machine, valid bits, or generation tags. Update ready/busy and first-access behavior. Configuration initialization is separate from runtime reset. See [BSRAM output reset](manuals/markdown/UG300E.md#pdf-page-51).

Keep array writes separate from output-register reset so they can map to RAM. Arora V 15K A-version parts also restrict some RAM initialization and ROM primitives; check the selected primitive before using them.

## Attributes and inference

```verilog
reg [31:0] mem [0:255] /* synthesis syn_ramstyle = "block_ram" */;
```

| Attribute | Values |
|---|---|
| `syn_ramstyle` | `registers`, `block_ram`, `distributed_ram`; `rw_check` or `no_rw_check` for collision logic |
| `syn_romstyle` | `logic`, `block_rom`, `distributed_rom` |
| `syn_srlstyle` | `registers`, `block_ram`, `distributed_ram` |

`no_rw_check` is the default. `rw_check` adds surrounding logic; it does not guarantee arbitrary asynchronous collision behavior. Attributes cannot add ports or reset features. See [RAM attributes](manuals/markdown/SUG550.md#pdf-page-78).

Keep working inference. Use official IP for special modes, mixed widths, or unstable inference. Preserve its configuration and initialization inputs. Check actual primitives, block counts, output registers, and any unexpected LUT/FF growth.
