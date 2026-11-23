# PrimePower: average power of one dump_on()/dump_off() window, typical corner.
# Run through `make power` (STAGE = postsynthesis or postlayout, WINDOW = waves-<N>.vcd).
proc env_or {name default} {
  if {[info exists ::env($name)]} { return $::env($name) }
  return $default
}
set TOP   [env_or TOP gr_heep]
set STAGE [env_or STAGE postlayout]
set VCD   $::env(VCD)
set SCRIPT_DIR [file normalize [file dirname [info script]]/../design_compiler]
set REPORT_DIR [file normalize [file dirname [info script]]]/reports
file mkdir $REPORT_DIR

set ::env(SG13G2_CORNER)    typ_1p20V_25C
set ::env(SG13G2_IO_CORNER) typ_1p2V_3p3V_25C
source ${SCRIPT_DIR}/set_libs.tcl
set link_path [concat "*" $DB_STDCELLS $DB_PADS $DB_MEMORIES]

if {$STAGE eq "postlayout"} {
  set EXPORT ${SCRIPT_DIR}/../innovus/artefacts/export
  set NETLIST ${EXPORT}/${TOP}_pnr.v
  set SDC     ${EXPORT}/${TOP}_pnr.sdc
  set SPEF    ${EXPORT}/${TOP}_pnr.spef
} else {
  set NETLIST ${SCRIPT_DIR}/../synthesis/last_output/report/netlist.v
  set SDC     ${SCRIPT_DIR}/../synthesis/last_output/report/netlist.sdc
}

set power_enable_analysis true
set power_analysis_mode averaged

read_verilog $NETLIST
current_design $TOP
link_design
# The wire load model in the synthesis SDC names the slow library, not loaded here; read_sdc rejects
# the current_design line of the Innovus SDC.
set fi [open $SDC]
set fo [open ${REPORT_DIR}/${STAGE}.sdc w]
foreach l [split [read $fi] \n] {
  if {![string match set_wire_load* $l] && ![string match current_design* $l]} { puts $fo $l }
}
close $fi
close $fo
read_sdc ${REPORT_DIR}/${STAGE}.sdc
if {[info exists SPEF]} { read_parasitics -format spef $SPEF }

# The VCD holds only the scopes chosen in vcd_scopes() (tb/tb_top.sv): the rest gets default activity.
read_vcd $VCD -strip_path tb_top/testharness_i/${TOP}_i
update_power
report_power -nosplit > ${REPORT_DIR}/${STAGE}_power.rpt
report_power -nosplit -hierarchy -levels 4 > ${REPORT_DIR}/${STAGE}_power_hier.rpt
report_switching_activity > ${REPORT_DIR}/${STAGE}_activity.rpt
puts "reports: ${REPORT_DIR}/${STAGE}_power*.rpt"
exit
