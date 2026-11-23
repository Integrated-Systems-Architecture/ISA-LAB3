# CLK_PERIOD (ns) is the system clock: make synth CLK_PERIOD=...
set SYSTEM_CLK_PERIOD [env_or CLK_PERIOD 20.0]
set JTAG_CLK_PERIOD 100
set SPI_SLAVE_CLK_PERIOD [expr {$SYSTEM_CLK_PERIOD * 2}]

create_clock -name SYSTEM_CLK    -period $SYSTEM_CLK_PERIOD    [get_pins pad_ring_i/u_pad_clk/i_pad/p2c]
create_clock -name JTAG_CLK      -period $JTAG_CLK_PERIOD      [get_pins pad_ring_i/u_pad_jtag_tck/i_pad/p2c]
create_clock -name SPI_SLAVE_CLK -period $SPI_SLAVE_CLK_PERIOD [get_pins pad_ring_i/u_pad_spi_slave_sck/i_pad/p2c]
create_clock -name DDR_RCV_CLK   -period $SYSTEM_CLK_PERIOD    [get_pins pad_ring_i/u_pad_ddr_rcv_clk/i_pad/p2c]
set_clock_uncertainty 0.5 [all_clocks]
set_clock_groups -asynchronous -group SYSTEM_CLK -group JTAG_CLK -group SPI_SLAVE_CLK -group DDR_RCV_CLK

set_false_path -from [get_ports rst_ni]
set_false_path -from [get_ports boot_select_i]
set_false_path -from [get_ports jtag_trst_ni]

set_input_delay  -clock JTAG_CLK [expr {$JTAG_CLK_PERIOD * 0.5}] [get_ports {jtag_tms_i jtag_tdi_i}]
set_output_delay -clock JTAG_CLK [expr {$JTAG_CLK_PERIOD * 0.3}] [get_ports jtag_tdo_o]

set clk_ports [get_ports {clk_i rst_ni boot_select_i jtag_* spi_slave_sck_io ddr_rcv_clk_i}]
set_input_delay  -clock SYSTEM_CLK [expr {$SYSTEM_CLK_PERIOD * 0.3}] [remove_from_collection [all_inputs] $clk_ports]
set_output_delay -clock SYSTEM_CLK [expr {$SYSTEM_CLK_PERIOD * 0.3}] [remove_from_collection [all_outputs] [get_ports jtag_tdo_o]]

# Clock and reset trees are built in PnR: keep them ideal here, also after the clock
# cells instantiated in the RTL (tc_clk_*, pulp_clock_*, cv32e40px_clock_gate).
set clk_cells [get_cells -hier -filter "full_name=~*/i_cell"]
set_ideal_network [get_pins -of_objects $clk_cells -filter "direction==out"]
set_ideal_network [get_pins pad_ring_i/u_pad_rst/i_pad/p2c]

# Clocks sent out of the chip are not data.
set_false_path -to [get_ports {i2s_sck_io spi_sck_io spi_flash_sck_io spi2_sck_io pdm2pcm_clk_io ddr_snd_clk_o}]
