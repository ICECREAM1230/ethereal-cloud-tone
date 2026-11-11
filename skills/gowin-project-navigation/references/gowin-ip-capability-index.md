# Gowin IP Capability Index

Read this reference only to discover what kinds of Gowin IP exist or to match an unfamiliar function to a possible official IP.

Do not read it when the required IP type is already known. For ROM/pROM, RAM, FIFO, PLL/OSC/CLKDIV, multiplier/DSP, or a known PHY, go directly to [gowin-ip-generation](../../gowin-ip-generation/SKILL.md) and the relevant IP integration checks.

## Scope and limits

This index summarizes capability groups observed on 2026-09-22 in:

- Gowin Software V1.9.12.03 under `IDE/ipcore`;
- the local `Gowin_IP资料` documentation collection.

It is a discovery index, not proof that an IP is available for the selected device. Availability can depend on device family, package, tool version, and license. Confirm the exact core in Gowin IP Core Generator for the device selected by the `.gprj`.

Do not select entries from `Deprecated` for a new design. A reference design demonstrates an example system; it does not by itself prove that every included block is a supported standalone IP.

## Capability groups

| Group | Representative IP types and search terms |
|---|---|
| On-chip memory and buffering | Single-, simple-dual-, and dual-port RAM; pROM/ROM; distributed SSRAM; RAM-based shift register; synchronous and asynchronous FIFO; high-speed FIFO; AXI Stream FIFO |
| External memory and storage | DDR/DDR2/DDR3/LPDDR; SDRAM; PSRAM and UHS PSRAM; HyperRAM/RPC memory; SPI flash interfaces; flash controller |
| Clock and timing | PLL/rPLL, advanced PLL, SSCPLL; OSC/EOSC; CLKDIV; clock switching and enable blocks; DLL and delay resources |
| DSP, mathematics, coding, and control | Integer and floating-point arithmetic; multiplier, divider, MAC and DSP blocks; FIR/IIR/CIC and adaptive filters; FFT; CORDIC; DDS; BCH and Reed-Solomon; PID/FOC; correlation, AEC, and AGC |
| Processor systems and buses | Gowin EMPU and RISC-V systems; AHB, APB, AXI, and Wishbone bridges; bus matrix and arbiter; GPIO, timer, RTC, watchdog, UART, SPI, and I2C peripherals |
| Interfaces and interconnect | UART, SPI, I2C, I3C, CAN, HDLC, RFFE, and SDIO; Ethernet MAC; USB 1.1/2.0 controllers and SoftPHY; MIPI converters and DSI/CSI-2; PCI/PCIe SGDMA; LTPI; PWM and TDC |
| Multimedia and audio | AE/AWB, black-level and color processing, CFA, gamma and CSC; DVI and eDP; MJPEG; scaler; video frame buffer and warping; SDI; PDM-to-PCM, ASRC, SPDIF, and equalizer |
| SerDes and high-speed links | Serial Ethernet; CPRI; JESD204B; PCI Express; USB 3.x PHY; QSGMII; eDP, SDI, and SLVS-EC PHY; Serial RapidIO; RoraLink; customized PHY |
| AI acceleration | Gowin NPU |
| Configuration and reliability | goConfig over I2C, JTAG, SPI, or UART; SEU Handler |
| Device-specific hard resources | Hardened MIPI D-PHY RX, hardened JPEG+, ADC, bandgap, and other family-specific hard blocks exposed by the installed tool |

The groups overlap. Use the names as search terms, not as evidence of device support.

## Minimal discovery procedure

1. Map the unfamiliar requirement to one capability group and a few search terms.
2. Check those terms in IP Core Generator with the selected `.gprj` device.
3. Read the exact IP guide only when interface, configuration, latency, license, or device support is still unclear.

Once the IP type is known, stop using this index and continue with [gowin-ip-generation](../../gowin-ip-generation/SKILL.md) and the relevant Gowin validation workflow.
