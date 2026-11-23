# IHP SG13G2 libraries: the .lib files are compiled to .db once, in db/.
set PDK       [env_or IHP_PDK_ROOT /oss-tools/pdk/ihp-sg13g2]/libs.ref
set CORNER    [env_or SG13G2_CORNER slow_1p08V_125C]
set IO_CORNER [env_or SG13G2_IO_CORNER slow_1p08V_3p0V_125C]
set DB_DIR    ${SCRIPT_DIR}/db
file mkdir ${DB_DIR}

proc lib2db {lib} {
  set db ${::DB_DIR}/[file rootname [file tail $lib]].db
  if {![file exists $db]} {
    set name [exec sed -n {s/^ *library *( *"\{0,1\}\([^ ")]*\).*/\1/p} $lib]
    sh lc_shell -no_init -x "read_lib $lib; write_lib [lindex $name 0] -format db -output $db; quit"
  }
  return $db
}

set DB_STDCELLS [lib2db ${PDK}/sg13g2_stdcell/lib/sg13g2_stdcell_${CORNER}.lib]
set DB_PADS     [lib2db ${PDK}/sg13g2_io/lib/sg13g2_io_${IO_CORNER}.lib]
set DB_MEMORIES [list]
foreach lib [glob ${PDK}/sg13g2_sram/lib/RM_IHPSG13_*_${CORNER}.lib] {
  lappend DB_MEMORIES [lib2db $lib]
}

set target_library [list ${DB_STDCELLS}]
set synthetic_library [list dw_foundation.sldb]
set link_library [concat "*" ${DB_STDCELLS} ${DB_PADS} ${DB_MEMORIES} dw_foundation.sldb]
puts "USED LIBRARIES: $link_library"
