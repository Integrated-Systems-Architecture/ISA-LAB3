redirect -tee $REPORT_DIR/02_floorplan.log {
  log_stage "Floorplan"
  set_db design_bottom_routing_layer $design(route_bottom_layer)
  set_db design_top_routing_layer    $design(route_top_layer)
  delete_relative_floorplan -all
  unplace_obj -all

  # Die, pad ring (180 um) and core.
  set W $design(die_w)
  set H $design(die_h)
  set c [expr {180 + $design(io_to_core)}]
  create_floorplan -site $design(site) -box_size [list 0 0 $W $H 0 0 $W $H $c $c [expr {$W - $c}] [expr {$H - $c}]]

  # Pad ring: the signal pads of the netlist, plus VDD, VSS, IOVDD, IOVSS on every side.
  set pads [lsort [get_db [get_db insts -if {.base_cell.name == sg13g2_IOPad*}] .name]]
  set per_side [expr {int(ceil([llength $pads] / 4.0))}]
  set io artefacts/pads.io
  set f [open $io w]
  puts $f "(globals\n  version = 3\n  io_order = clockwise\n)\n(iopad"
  foreach corner {topleft topright bottomright bottomleft} {
    puts $f "  ($corner (inst name=\"corner_$corner\" cell=\"sg13g2_Corner\"))"
  }
  set i 0
  foreach side {top right bottom left} {
    set mine [lrange $pads [expr {$i * $per_side}] [expr {($i + 1) * $per_side - 1}]]
    set half [expr {[llength $mine] / 2}]
    puts $f "  ($side"
    foreach p [lrange $mine 0 [expr {$half - 1}]] { puts $f "    (inst name=\"$p\")" }
    foreach {n cell} {vdd sg13g2_IOPadVdd vss sg13g2_IOPadVss iovdd sg13g2_IOPadIOVdd iovss sg13g2_IOPadIOVss} {
      puts $f "    (inst name=\"pad_${n}_$side\" cell=\"$cell\")"
    }
    foreach p [lrange $mine $half end] { puts $f "    (inst name=\"$p\")" }
    puts $f "  )"
    incr i
  }
  puts $f ")"
  close $f
  read_io_file $io -no_die_size_adjust
  snap_floorplan -io_pad
  add_io_fillers -cells $design(io_fillers) -prefix io_filler

  # Macros (the SRAMs, yours included) in rows from the top of the core.
  set h $design(macro_halo)
  set core [lindex [get_db current_design .core_bbox] 0]
  lassign $core cx0 cy0 cx1 cy1
  set x [expr {$cx0 + $h}]
  set top [expr {$cy1 - $h}]
  set row_h 0
  foreach m [lsort [get_db [get_db insts -if {.base_cell.class == block}] .name]] {
    set mw [get_db inst:$m .base_cell.bbox.dx]
    set mh [get_db inst:$m .base_cell.bbox.dy]
    if {$x + $mw > $cx1 - $h} {
      set x [expr {$cx0 + $h}]
      set top [expr {$top - $row_h - 2 * $h}]
      set row_h 0
    }
    place_inst $m $x [expr {$top - $mh}] R0 -fixed
    create_place_halo -insts $m -halo_deltas [list $h $h $h $h]
    set x [expr {$x + $mw + 2 * $h}]
    set row_h [expr {max($row_h, $mh)}]
  }
  log FPLAN "macros down to y = [expr {$top - $row_h}] um, core from $cy0 to $cy1 um"
  if {$top - $row_h < $cy0} { error "the macros do not fit: raise PNR_DIE_W / PNR_DIE_H" }

  snap_floorplan -all
  report_area > $REPORT_DIR/02_floorplan.area.rpt
  write_db $design(DB_DIR)/post_floorplan.db
}
