# Place and route of the whole SoC, IHP SG13G2: every setting in one place.
# make pnr sets TOP, IHP_PDK_ROOT and the PNR_* knobs below.

global design

proc env_or {name default} { expr {[info exists ::env($name)] ? [string trim $::env($name)] : $default} }

set design(TOPLEVEL)  [env_or TOP gr_heep]
set design(FLOW_ROOT) [file normalize ../..]
set design(SYN_DIR)   $design(FLOW_ROOT)/implementation/synthesis/last_output/report
set design(netlist)     $design(SYN_DIR)/netlist.v
set design(default_sdc) $design(SYN_DIR)/netlist.sdc

set design(PDK_LIB) [env_or IHP_PDK_ROOT /oss-tools/pdk/ihp-sg13g2]/libs.ref
set STD  $design(PDK_LIB)/sg13g2_stdcell
set IO   $design(PDK_LIB)/sg13g2_io
set SRAM $design(PDK_LIB)/sg13g2_sram

# The SRAM macros the netlist uses.
set design(SRAMS) [lsort -unique [regexp -all -inline {RM_IHPSG13_\w+} [exec grep -o {RM_IHPSG13_[A-Za-z0-9_]*} $design(netlist)]]]

# Corners: libraries of standard cells, pads and SRAMs.
foreach {corner std io sram} {
  slow sg13g2_stdcell_slow_1p08V_125C sg13g2_io_slow_1p08V_3p0V_125C slow_1p08V_125C
  typ  sg13g2_stdcell_typ_1p20V_25C   sg13g2_io_typ_1p2V_3p3V_25C    typ_1p20V_25C
  fast sg13g2_stdcell_fast_1p32V_m40C sg13g2_io_fast_1p32V_3p6V_m40C fast_1p32V_m55C
} {
  set design(libs_$corner) [list $STD/lib/$std.lib $IO/lib/$io.lib]
  foreach m $design(SRAMS) { lappend design(libs_$corner) $SRAM/lib/${m}_${sram}.lib }
}
set design(lib_slow) [lindex $design(libs_slow) 0]
set design(lib_typ)  [lindex $design(libs_typ) 0]
set design(lib_fast) [lindex $design(libs_fast) 0]
set design(opcond_slow) {1.08 125}
set design(opcond_typ)  {1.20  25}
set design(opcond_fast) {1.32 -40}
set design(mmmc_view_file) [file normalize [file dirname [info script]]/soc.view]

set design(ALL_LEFS) [list $STD/lef/sg13g2_tech.lef $STD/lef/sg13g2_stdcell.lef $IO/lef/sg13g2_io_notracks.lef]
set design(ALL_GDS)  [list $STD/gds/sg13g2_stdcell.gds $IO/gds/sg13g2_io.gds]
foreach m $design(SRAMS) {
  lappend design(ALL_LEFS) $SRAM/lef/$m.lef
  lappend design(ALL_GDS)  $SRAM/gds/$m.gds
}

set design(all_power_nets)  {VDD IOVDD}
set design(all_ground_nets) {VSS IOVSS}

set design(fillers)   {sg13g2_fill_8 sg13g2_fill_4 sg13g2_fill_2 sg13g2_fill_1}
set design(tie_cells) {sg13g2_tiehi:L_HI sg13g2_tielo:L_LO}
set design(io_fillers) {sg13g2_Filler10000 sg13g2_Filler4000 sg13g2_Filler2000 sg13g2_Filler1000 sg13g2_Filler400 sg13g2_Filler200}
set design(cts_buffers)   {sg13g2_buf_4 sg13g2_buf_8 sg13g2_buf_16}
set design(cts_inverters) {sg13g2_inv_4 sg13g2_inv_8 sg13g2_inv_16}
set design(cts_target_skew)      0.20
set design(cts_target_max_trans) 0.50

set design(site) CoreSite
set design(route_bottom_layer) Metal1
set design(route_top_layer)    Metal5

# Die, in um, and core distance from the die edge (pads are 180 um tall).
set design(die_w)        [env_or PNR_DIE_W 3800]
set design(die_h)        [env_or PNR_DIE_H 3000]
set design(io_to_core)   [env_or PNR_IO_TO_CORE 60]
set design(macro_halo)   20

# Power: ring on TopMetal1/2 around the core, stripes on TopMetal1 (vertical) and Metal5 (horizontal).
set design(pg_ring_width)     10.0
set design(pg_ring_spacing)    5.0
set design(pg_stripe_width)    5.0
set design(pg_stripe_spacing)  5.0
set design(pg_stripe_pitch)  150.0

set design(REPORT_DIR) artefacts/reports
set design(DB_DIR)     artefacts/db
set design(EXPORT_DIR) artefacts/export
set design(METRIC_DIR) artefacts/metrics
