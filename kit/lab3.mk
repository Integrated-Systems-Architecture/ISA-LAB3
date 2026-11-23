# Lab 3 targets. ACCEL is the name of your accelerator: hw/vendor/$(ACCEL).vendor.hjson.
ACCEL ?= my_accel

ACCEL_DIR   = hw/vendor/$(ACCEL)
ACCEL_HJSON = $(firstword $(wildcard $(ACCEL_DIR)/data/*.hjson))
REGTOOL     = $(X_HEEP_DIR)/hw/vendor/pulp_platform/register_interface/vendor/lowrisc_opentitan/util/regtool.py

## Import the accelerator (vendor) and generate its registers
.PHONY: accel accel-regs
accel:
	$(MAKE) vendor-update MODULE_NAME=$(ACCEL)
	$(MAKE) accel-regs

## Generate the register block and the C header from the vendored hjson
accel-regs:
	$(PYTHON) $(REGTOOL) -r -t $(ACCEL_DIR)/rtl $(ACCEL_HJSON)
	mkdir -p sw/external/lib/drivers/$(ACCEL)
	$(PYTHON) $(REGTOOL) -D -o sw/external/lib/drivers/$(ACCEL)/$(ACCEL)_regs.h $(ACCEL_HJSON)

## Link the IHP PDK into the tech core (simulation models)
.PHONY: pdk-link
pdk-link:
	ln -sfn $(IHP_PDK_ROOT) hw/asic/sg13g2/pdk

## Synthesis of the whole SoC with Design Compiler (CLK_PERIOD in ns)
.PHONY: synth
synth: pdk-link
	CLK_PERIOD=$(CLK_PERIOD) $(FUSESOC) --cores-root . run --no-export --target=asic_synthesis --build x-heep:systems:gr-heep 2>&1 | tee synth.log
CLK_PERIOD ?= 20

## Place and route of the whole SoC with Innovus (die size in um: PNR_DIE_W, PNR_DIE_H)
PNR_DIE_W ?= 3800
PNR_DIE_H ?= 3000
.PHONY: pnr
pnr:
	mkdir -p implementation/innovus/artefacts
	-cd implementation/innovus && env -u DISPLAY TOP=gr_heep IHP_PDK_ROOT=$(IHP_PDK_ROOT) \
	  PNR_DIE_W=$(PNR_DIE_W) PNR_DIE_H=$(PNR_DIE_H) \
	  innovus -stylus -batch -files scripts/run_pnr_flow.tcl -log artefacts/innovus
	@test -e implementation/innovus/artefacts/db/post_route.db || { \
	  echo "place and route did not finish: see implementation/innovus/artefacts/innovus.log"; exit 1; }

## RTL simulation with Questa (app PROJECT, VCD_MODE 0 or 2: VCD of the dump_on()/dump_off() windows)
VCD_MODE ?= 0
.PHONY: questasim
questasim:
	$(MAKE) app PROJECT=$(PROJECT)
	$(FUSESOC) --cores-root . run --no-export --target=sim --tool=modelsim --build x-heep:systems:gr-heep
	$(MAKE) -C build/x-heep_systems_gr-heep_0/sim-modelsim run \
	  PLUSARGS="c firmware=../../../sw/build/main.hex vcd_mode=$(VCD_MODE) $(SIM_ARGS)"

## Gate-level simulation with the SDF delays at CLK_PERIOD (app PROJECT, VCD_MODE 0 or 2):
## questasim-postsynth runs the synthesis netlist, questasim-postlayout the place and route one
.PHONY: questasim-postsynth questasim-postlayout
questasim-postsynth: GATE = postsynthesis
questasim-postlayout: GATE = postlayout
questasim-postsynth questasim-postlayout: pdk-link
	$(MAKE) app PROJECT=$(PROJECT)
	$(FUSESOC) --cores-root . run --no-export --target=sim_$(GATE) --tool=modelsim --build x-heep:systems:gr-heep
	$(MAKE) -C build/x-heep_systems_gr-heep_0/sim_$(GATE)-modelsim run PARAMETERS=CLK_PERIOD_NS=$(CLK_PERIOD) \
	  PLUSARGS="c firmware=../../../sw/build/main.hex vcd_mode=$(VCD_MODE) $(SIM_ARGS)"

## Power of a VCD window with PrimePower: STAGE postsynthesis or postlayout (run its simulation first), WINDOW = waves-<N>.vcd
STAGE  ?= postlayout
WINDOW ?= 0
.PHONY: power
# pt_shell needs libodbc.so.2: on the ISA server it is only inside VCS
ODBC ?= $(firstword $(wildcard /eda/synopsys/*/RHELx86/VCS_*/doc/UserGuide/python*_smartsearch/lib/libodbc.so.2))
power:
	mkdir -p implementation/primepower/lib
	$(if $(ODBC),ln -sf $(ODBC) implementation/primepower/lib/libodbc.so.2)
	cd implementation/primepower && LD_LIBRARY_PATH=$(CURDIR)/implementation/primepower/lib:$$LD_LIBRARY_PATH TOP=gr_heep STAGE=$(STAGE) \
	  VCD=$(CURDIR)/build/x-heep_systems_gr-heep_0/sim_$(STAGE)-modelsim/waves-$(WINDOW).vcd \
	  pt_shell -f power.tcl -output_log_file $(STAGE)_power.log
