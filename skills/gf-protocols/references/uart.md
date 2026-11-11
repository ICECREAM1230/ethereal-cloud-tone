# UART Protocol Reference

## Signals
| Signal | Direction (TX) | Description |
|--------|---------------|-------------|
| TX | output | Serial data output |
| RX | input | Serial data input |

## Frame Format (8N1)
```
IDLE ──┐   ┌─D0─D1─D2─D3─D4─D5─D6─D7─┐   ┌── IDLE
       └───┘ START                      └───┘ STOP
```
- Start bit: 1 low bit
- Data: 8 bits, LSB first
- Parity: none (8N1)
- Stop bit: 1 high bit

## Baud Rate
Clock divider = CLK_FREQ / BAUD_RATE

| Baud Rate | Divider (100MHz) |
|-----------|-----------------|
| 9600 | 10417 |
| 115200 | 868 |
| 921600 | 109 |

## Receiver Oversampling
Sample at 16x baud rate, check middle of each bit for noise immunity.
