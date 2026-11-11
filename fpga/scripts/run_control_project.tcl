set root [file dirname [file dirname [file normalize [info script]]]]
if {[llength $argv] != 1 || [lsearch -exact {syn pnr config} [lindex $argv 0]] < 0} {
    error "Usage: gw_sh scripts/run_control_project.tcl syn|pnr|config"
}
# Selection happens before the runner freezes inputs. Direct gw_sh calls must
# reject stale mode selection without mutating any frozen build input.
if {[catch {exec /usr/bin/python3 -I [file join $root scripts sync_control_uart_sdc.py] --check >@stdout 2>@stderr} message]} {
    puts stderr "UART SDC consistency check failed: $message"
    exit 1
}
# Run config persistence after vendor shutdown; keep one GUI/CLI writer.
if {![info exists ::env(GOWIN_GUI_SYNC_SESSION)]} {
    set command [list /usr/bin/python3 -I [file join $root scripts run_gowin_synced.py] --project [file join $root ethereal_cloud_tone.gprj] --script [file normalize [info script]]]
    if {[lindex $argv 0] eq "config"} {lappend command --config-only}
    lappend command -- {*}$argv
    if {[catch {exec {*}$command >@stdout 2>@stderr} message details]} {
        if {[lindex [dict get $details -errorcode] 0] eq "CHILDSTATUS"} {exit [lindex [dict get $details -errorcode] 2]}
        puts stderr $message
        exit 1
    }
    exit 0
}
cd $root
open_project [file join $root ethereal_cloud_tone.gprj]
set_option -top_module control_lab_top
set_option -verilog_std sysv2017
set_option -output_base_name ethereal_cloud_tone
set_option -opt_goal timing
set_option -place_option 3
set_option -route_option 2
set_option -replicate_resources 0
# CPU here is the FPGA parallel configuration port, not the AE350 core.
# MUG1031-1.3.1 p50 recommends its data pins be reused as regular I/O.
set_option -use_cpu_as_gpio 1
set_option -use_jtag_as_gpio 0
set_option -use_mspi_as_gpio 0
# Preserve the active synth project's SSPI-pad reuse for its board I/O.
set_option -use_sspi_as_gpio 1
set_option -timing_driven 1
set_option -gen_verilog_sim_netlist 1
set_option -gen_sdf 1
if {[lindex $argv 0] ne "config"} {run [lindex $argv 0]}
run close
