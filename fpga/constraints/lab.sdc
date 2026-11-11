# Actual clocks from generated PLL configurations. No global STA disable.
create_clock -name board_ref_clk -period 20.000 [get_ports {sys_clk}]
create_generated_clock -name cpu_core_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 16 [get_pins {u_core_pll/u_pll/PLL_inst/CLKOUT1}]
create_generated_clock -name dtcm_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 1 [get_pins {u_core_pll/u_pll/PLL_inst/CLKOUT0}]
create_generated_clock -name ahb_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 1 [get_pins {u_core_pll/u_pll/PLL_inst/CLKOUT2}]
create_generated_clock -name apb_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 1 [get_pins {u_core_pll/u_pll/PLL_inst/CLKOUT3}]
create_generated_clock -name rtc_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -divide_by 5 [get_pins {u_core_pll/u_pll/PLL_inst/CLKOUT4}]
# Reused main project video PLL: 50*22.25/15=74.1666667MHz; serial is exactly 5x.
create_generated_clock -name video_pixel_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 89 -divide_by 60 [get_pins {u_video_pll/u_pll/PLL_inst/CLKOUT0}]
create_generated_clock -name video_serial_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 89 -divide_by 12 [get_pins {u_video_pll/u_pll/PLL_inst/CLKOUT1}]
# Only synchronizer first-stage D endpoints are excepted.
set_false_path -to [get_pins {u_apb/request_meta_s0/D u_apb/ack_meta_s0/D}]
set_false_path -from [get_ports {keys_n[*]}] -to [get_pins {u_apb/key_meta*_s0/D}]
# The bank payload is held before request propagation until ACK returns.
set_max_delay -from [get_pins {u_apb/bank_hold_s0/Q}] -to [get_pins {u_apb/pixel_bank_s0/D}] 20.000
set_false_path -hold -from [get_pins {u_apb/bank_hold_s0/Q}] -to [get_pins {u_apb/pixel_bank_s0/D}]
# 50MHz DTCM/AHB/APB retain one common PLL and in-phase 1:1 AHB/APB.
# Recovery/removal and video serializer timing remain enabled for review.
# UART debug monitor: only first synchronizer D endpoints are excepted.
set_false_path -to [get_pins {u_debug_monitor/request_meta_s0/D u_debug_monitor/ack_meta_s0/D u_debug_monitor/flags_meta*_s0/D}]
# Snapshot is held through request/ack; constrain its held mailbox to one 50MHz cycle.
set_max_delay -from [get_pins {u_debug_monitor/payload_hold*_s0/Q}] -to [get_pins {u_debug_monitor/payload_sys*_s0/D}] 20.000
set_false_path -hold -from [get_pins {u_debug_monitor/payload_hold*_s0/Q}] -to [get_pins {u_debug_monitor/payload_sys*_s0/D}]
