redirect -tee $REPORT_DIR/03_powergrid.log {
  log_stage "Power plan"

  foreach {net pins} {
    VDD   {VDD VDD! VDDARRAY! vdd}
    VSS   {VSS VSS! vss}
    IOVDD {iovdd}
    IOVSS {iovss}
  } {
    foreach p $pins { connect_global_net $net -type pg_pin -pin_base_name $p -all }
  }
  connect_global_net VDD -type tie_hi -all
  connect_global_net VSS -type tie_lo -all

  add_ring -nets {VDD VSS} -type core_rings -follow core \
    -layer {top TopMetal2 bottom TopMetal2 left TopMetal1 right TopMetal1} \
    -width $design(pg_ring_width) -spacing $design(pg_ring_spacing) -offset $design(pg_ring_spacing)

  set_db add_stripes_stacked_via_bottom_layer Metal1
  set_db add_stripes_stacked_via_top_layer    TopMetal2
  foreach {layer dir} {TopMetal1 vertical TopMetal2 horizontal} {
    add_stripes -nets {VDD VSS} -layer $layer -direction $dir \
      -width $design(pg_stripe_width) -spacing $design(pg_stripe_spacing) \
      -set_to_set_distance $design(pg_stripe_pitch) -start_offset $design(pg_stripe_pitch)
  }

  # Pads to the core ring, SRAM pins (Metal4) and standard-cell rows to the stripes.
  route_special -connect {pad_pin block_pin core_pin} -nets {VDD VSS} \
    -allow_jogging 1 -allow_layer_change 1 -layer_change_range {Metal1 TopMetal2} \
    -block_pin_target nearest_target -pad_pin_port_connect {all_port one_geom} \
    -core_pin_target first_after_row_end

  check_connectivity -type special -nets {VDD VSS} -out_file $REPORT_DIR/03_powergrid.connectivity.rpt
  check_drc -check_only special -limit 100000 -out_file $REPORT_DIR/03_powergrid.drc.rpt
  write_db $design(DB_DIR)/post_powergrid.db
}
