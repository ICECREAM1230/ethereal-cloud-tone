# cam2dvi IIC reference

Copied without modification from `/home/ice/Project/visual_theremin/cam2dvi/src/i2c_master`, per user request on 2026-10-31. Original copyright and disclaimers are retained.

The active byte/bit controllers and their two includes are byte-identical to the reference. FDC uses `rtl/fdc2214/fdc_cam_i2c_reg16_master.sv` to adapt the camera transaction sequencer to an 8-bit pointer and 16-bit big-endian data, including ACK checking, repeated START, high-byte ACK/low-byte NACK and bounded timeout recovery.

The reference camera LUT and its 8-bit data sequencer are archived read-only in `sim/results/fdc_cam_iic_20261031/reference-readonly.tar.gz`; they are not a valid FDC register table.
