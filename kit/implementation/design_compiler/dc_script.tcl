# Whole-SoC synthesis, pad ring included, IHP SG13G2 (from the polheepo flow).
# make synth: FuseSoC sets TOP_MODULE, READ_SOURCES, SCRIPT_DIR, REPORT_DIR.

set_host_options -max_cores 4

proc env_or {name default} { expr {[info exists ::env($name)] ? $::env($name) : $default} }

remove_design -all
source ${SCRIPT_DIR}/set_libs.tcl

set power_preserve_rtl_hier_names true
define_design_lib WORK -path ./work
source ${READ_SOURCES}.tcl
elaborate ${TOP_MODULE}
link

source ${SCRIPT_DIR}/set_constraints.tcl
report_clocks -attributes -skew > ${REPORT_DIR}/clocks.rpt
check_design > ${REPORT_DIR}/check_design_elaborate.rpt
check_timing > ${REPORT_DIR}/check_timing_elaborate.rpt

# The library picks its wire load by design area: for a whole SoC that is the 500k model, wires
# of millimetres on every net. Use a light model everywhere; PnR sees the real wires.
set auto_wire_load_selection false
set_wire_load_mode top
set_wire_load_model -name 1k -library [get_object_name [index_collection [get_libs sg13g2_stdcell*] 0]]

set_clock_gating_style -minimum_bitwidth 3 -positive_edge_logic integrated:sg13g2_slgcp_1 -control_point before
compile_ultra -no_autoungroup -no_boundary_optimization -gate_clock

check_design > ${REPORT_DIR}/check_design_compile.rpt
check_timing > ${REPORT_DIR}/check_timing_compile.rpt
write -f ddc -hierarchy -output ${REPORT_DIR}/compiled.ddc
change_names -rules verilog -hier
uniquify
write -format verilog -hier -o ${REPORT_DIR}/netlist.v
report_timing -nosplit -max_paths 10 > ${REPORT_DIR}/timing.rpt
# The accelerator inside the SoC: worst path ending in it, starting in it, inside it.
set acc_d [filter_collection [all_registers -data_pins] "full_name =~ *_peripherals_i/*"]
set acc_c [filter_collection [all_registers -clock_pins] "full_name =~ *_peripherals_i/*"]
report_timing -nosplit -to $acc_d > ${REPORT_DIR}/accel_timing.rpt
report_timing -nosplit -from $acc_c >> ${REPORT_DIR}/accel_timing.rpt
report_timing -nosplit -from $acc_c -to $acc_d >> ${REPORT_DIR}/accel_timing.rpt
report_area -hier -nosplit > ${REPORT_DIR}/area.rpt
report_constraints > ${REPORT_DIR}/constraints.rpt
report_clock_gating > ${REPORT_DIR}/clock_gating.rpt
report_power > ${REPORT_DIR}/power.rpt
report_qor > ${REPORT_DIR}/qor.rpt

# SDF for the gate-level simulation. There is no clock tree yet: clock cells (pads, clock gates,
# tc_clk_*) get no delay, as in the timing above, or the gated clocks would arrive nanoseconds late.
set clk_cells [get_cells -hier -filter "ref_name =~ sg13g2_slgcp*"]
set clk_cells [add_to_collection -unique $clk_cells [get_cells -hier -filter "full_name =~ */i_cell"]]
set clk_cells [add_to_collection -unique $clk_cells [get_cells -of_objects [get_attribute [get_clocks] sources]]]
foreach_in_collection c $clk_cells {
  set_annotated_delay -cell 0 -from [get_pins -of_objects $c -filter "pin_direction == in"] \
    -to [get_pins -of_objects $c -filter "pin_direction == out"]
}
write_sdf ${REPORT_DIR}/netlist.sdf

# Innovus builds the clock tree and buffers the reset: no ideal networks in its constraints.
remove_ideal_network -all
write_sdc -version 1.7 ${REPORT_DIR}/netlist.sdc

set LAST_DIR ${SCRIPT_DIR}/../synthesis/last_output
sh rm -rf ${LAST_DIR}
file mkdir ${LAST_DIR}
sh cp -R ${REPORT_DIR} ${LAST_DIR}
exit
