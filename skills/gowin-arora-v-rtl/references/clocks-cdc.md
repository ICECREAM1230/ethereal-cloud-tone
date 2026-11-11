# Clocks and CDC

## Clock resources

Arora V provides global and regional clock resources, HCLK, PLLs, and DQS clocking. The described architecture has 16 global clocks and four HCLKs per IO bank. Check the part, bank, placement, connections, and current use. See [clock resources](manuals/markdown/UG306E.md#pdf-page-15).

Use dedicated low-skew networks for high-fanout core clocks. Use HCLK for suitable source-synchronous IO. Routing clocks through ordinary logic can cause skew and hold failures. Routing control signals through clock resources consumes capacity too.

## Enable and switching

Use register CE to pause logic. For network-level clock gating, use a supported DCE and verify its control timing and restart behavior.

DCS supports four clock inputs and glitchless or non-glitchless switching. `SELFORCE=1` selects non-glitchless operation. Check `DCS_MODE`, select encoding, switch timing, and stopped-clock behavior. See [DCE](manuals/markdown/UG306E.md#pdf-page-19), [DCS](manuals/markdown/UG306E.md#pdf-page-22), and [DCS control ports](manuals/markdown/UG306E.md#pdf-page-23).

## PLLs

Check the `.ipc`, generated wrapper, device revision, and tool model for the PLL type, frequency, phase, duty cycle, feedback, reset, and LOCK behavior. Keep required initialization files. An ideal simulation clock does not validate the PLL.

Verify the generated-clock relationships in Clock Summary. Avoid duplicate clocks, lost phase relationships, or independent base clocks on related PLL outputs. Changing an SDC period does not change the hardware clock.

## CDC and reset

Choose the crossing circuit from the signal's behavior:

- For a stable single-bit level, use an appropriate synchronizer.
- For a pulse or event, use a handshake, toggle transfer, or pulse extension that prevents missed events.
- For a bus, use an asynchronous FIFO, stable-data handshake, or a proven Gray-code transfer. Separate bit synchronizers do not preserve a bus transaction.

Synchronizers reduce metastability risk; they do not remove it.

Check reset assertion and release in every domain, including PLL startup, stopped clocks, and restart. GSR routing alone does not prove synchronous release.

Use clock-group exceptions only for real asynchronous or exclusive relationships. Constraints do not replace CDC circuits. Keep normal destination-domain paths timed.

Check routed clocks, clock resource use, skew, setup/hold, pulse width, CDC behavior, and reset restart.
