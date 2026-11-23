set REPORT_DIR $design(REPORT_DIR)
redirect -tee $REPORT_DIR/01_init.log {
  log_stage "Setup libraries and read design"
  check_inputs
  set_db init_power_nets  $design(all_power_nets)
  set_db init_ground_nets $design(all_ground_nets)
  set_db design_process_node 130
  set_multi_cpu_usage -local_cpu 4
  read_mmmc $design(mmmc_view_file)
  read_physical -lefs $design(ALL_LEFS)
  read_netlist $design(netlist)
  set_db design_flow_effort standard
  set_db init_design_uniquify true
  init_design
  delete_empty_hinsts
  delete_assigns -add_buffer
  check_design -type all -out_file $REPORT_DIR/01_init.check_design.rpt
  check_timing -verbose > $REPORT_DIR/01_init.check_timing.rpt
  log INIT "Import complete: [llength [get_db insts]] instances, SRAMs: $design(SRAMS)"
}
