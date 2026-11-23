#!/usr/bin/env python3
"""Install the Lab 3 kit into a hardware project created with X-HEEP IDE (New Hardware Project).

    python3 kit/install.py <project folder>

Copies the applications, drivers, testbench, SG13G2 tech core and implementation scripts,
renamed after the project, and wires them into its Makefile, .core and configuration.
Run it once, right after creating the project.
"""
import subprocess
import sys
from pathlib import Path

KIT = Path(__file__).resolve().parent

COPY = [
    "lab3.mk",
    "sw/applications",
    "sw/external/lib/kernels",
    "sw/external/lib/drivers/vcd_dump",
    "tb/tb_top.sv",
    "tb/tb_util.svh.tpl",
    "tb/testharness.sv",
    "hw/asic",
    "hw/ip",
    "hw/vendor/patches/x-heep/0001-aup-zu3-sram-ip.patch",
    "implementation",
]

MEMORY = """    memory_ss = MemorySS()
    memory_ss.add_ram_banks([32] * 4)
    memory_ss.add_linker_section(LinkerSection.by_size("code", 0, 0x00008000))
    memory_ss.add_linker_section(LinkerSection("data", 0x00008000, None))
"""

GITIGNORE = """
# Lab 3: PDK link and tool outputs
hw/asic/sg13g2/pdk
implementation/design_compiler/db/
implementation/synthesis/
implementation/innovus/artefacts/
implementation/primepower/reports/
implementation/primepower/lib/
*.vcd
"""

CORE_FILESETS = """  asic-sg13g2:
    depend:
    - isa:lab3:prim-sg13g2

  postsynthesis-netlist:
    depend:
    - x-heep::packages
    files:
    - implementation/synthesis/last_output/report/netlist.v
    file_type: verilogSource

  postlayout-netlist:
    depend:
    - x-heep::packages
    files:
    - implementation/innovus/artefacts/export/gr_heep_pnr.v
    file_type: verilogSource

"""

CORE_PARAMETERS = """  GATE_LEVEL:
    datatype: bool
    paramtype: vlogdefine
    default: false
"""

CORE_TARGETS = """  asic_synthesis:
    <<: *default_target
    default_tool: design_compiler
    filesets_append:
    - asic-sg13g2
    parameters_append:
    - SYNTHESIS=true
    tools:
      design_compiler:
        script_dir: ../../../implementation/design_compiler
        dc_script: dc_script.tcl
        report_dir: report

  sim_postsynthesis:
    default_tool: modelsim
    filesets:
    - postsynthesis-netlist
    - asic-sg13g2
    - tb-harness
    - tb-sv
    - systemverilog_only_uart
    - systemverilog_only_simjtag
    - pre_build_remote_bitbang
    - pre_build_uartdpi
    - pre_patch_modelsim_Makefile
    toplevel: [tb_top]
    hooks:
      pre_build:
        - pre_build_uartdpi
        - pre_build_remote_bitbang
        - pre_patch_modelsim_Makefile
      pre_run:
        - remove_uart_log
      post_run:
        - print_uart_log
    parameters:
    - GATE_LEVEL=true
    tools:
      modelsim:
        vlog_options:
        - -override_timescale 1ns/1ps
        - -suppress vlog-2583
        - -suppress vlog-2577
        - -suppress vlog-2720
        - -define MODELSIM
        vsim_options:
        - -suppress 3111
        - -sv_lib ../../../hw/vendor/x-heep/hw/vendor/lowrisc/opentitan/hw/dv/dpi/uartdpi/uartdpi
        - -sv_lib ../../../hw/vendor/x-heep/hw/vendor/pulp_platform/pulpissimo/rtl/tb/remote_bitbang/librbs
        - -voptargs=+acc=npr
        - -sdfmax /tb_top/testharness_i/gr_heep_i=../../../implementation/synthesis/last_output/report/netlist.sdf
        - -sdfnoerror
        # Timing checks stay on, silent and without notifiers: +notimingchecks leaves the delayed
        # nets of the IHP cell models half driven, and clock gates stop passing edges.
        - +no_notifier
        - +no_tchk_msg

"""
CORE_TARGETS += (CORE_TARGETS[CORE_TARGETS.index("  sim_postsynthesis:"):]
                 .replace("sim_postsynthesis", "sim_postlayout").replace("postsynthesis-netlist", "postlayout-netlist")
                 .replace("synthesis/last_output/report/netlist.sdf", "innovus/artefacts/export/gr_heep_pnr.sdf"))


def fail(msg):
    sys.exit(f"install.py: {msg}")


def insert(text, anchor, new, before=True, what=""):
    if anchor not in text:
        fail(f"cannot find {anchor.strip()!r} ({what}): has gr-heep changed? Ask in the course forum.")
    i = text.index(anchor) + (0 if before else len(anchor))
    return text[:i] + new + text[i:]


def main():
    if len(sys.argv) != 2:
        fail("usage: python3 kit/install.py <project folder>")
    proj = Path(sys.argv[1]).resolve()
    cores = [c for c in proj.glob("*.core")]
    if len(cores) != 1 or not (proj / "hw/vendor/x-heep").is_dir():
        fail(f"{proj} is not a project created with New Hardware Project")
    core = cores[0]
    name = core.stem
    words = name.replace("-", "_").split("_")

    def rename(text):
        return (text.replace("gr-heep", "-".join(words)).replace("gr_heep", "_".join(words))
                .replace("GR-HEEP", "-".join(words).upper()).replace("GR_HEEP", "_".join(words).upper()))

    if "lab3.mk" in (proj / "Makefile").read_text():
        fail(f"the kit is already installed in {proj}")

    # Files, renamed after the project.
    for item in COPY:
        src = KIT / item
        for f in ([src] if src.is_file() else sorted(p for p in src.rglob("*") if p.is_file())):
            if "__pycache__" in f.parts:
                continue
            dst = proj / f.relative_to(KIT)
            dst.parent.mkdir(parents=True, exist_ok=True)
            if dst.is_symlink():
                dst.unlink()
            data = f.read_bytes()
            try:
                dst.write_text(rename(data.decode()))
            except UnicodeDecodeError:
                dst.write_bytes(data)
            dst.chmod(f.stat().st_mode)

    # X-HEEP fix for the AUP-ZU3 memory IP.
    patch = proj / "hw/vendor/patches/x-heep/0001-aup-zu3-sram-ip.patch"
    subprocess.run(["patch", "-p1", "-N", "-s", "-d", str(proj / "hw/vendor/x-heep"), "-i", str(patch)], check=False)

    # Makefile
    mk = proj / "Makefile"
    mk.write_text(mk.read_text() + "\n# Lab 3\ninclude lab3.mk\n")
    gi = proj / ".gitignore"
    gi.write_text(gi.read_text() + GITIGNORE)

    # Memory: 128 KiB, 32 KiB of code and 96 KiB of data.
    cfg = proj / "config/mcu-gen-config.py"
    text = cfg.read_text()
    start, end = "    memory_ss = MemorySS()\n", "    system.set_memory_ss(memory_ss)\n"
    if start not in text or end not in text:
        fail("cannot find the memory subsystem in config/mcu-gen-config.py")
    cfg.write_text(text[:text.index(start)] + MEMORY + text[text.index(end):])

    # FuseSoC core: tech core, netlist, gate-level define, two-port SRAM model, new targets.
    text = core.read_text()
    text = insert(text, "    - openhwgroup.org:systems:core-v-mini-mcu\n", "    - isa:lab3:sram_wrapper_2p\n",
                  before=False, what="files_rtl_generic")
    text = insert(text, "  files_verilator_waiver:\n", rename(CORE_FILESETS), what="filesets")
    text = insert(text, "parameters:\n", CORE_PARAMETERS, before=False, what="parameters")
    text = insert(text, "        vsim_options:\n", "        - -suppress 3111\n", before=False, what="sim target")
    text = insert(text, "  sim:\n    <<: *default_target\n", rename(CORE_TARGETS), what="sim target")
    core.write_text(text)

    print(f"Lab 3 kit installed in {proj}.")
    print("Next: import your accelerator (README, section 4), then Run any application.")


if __name__ == "__main__":
    main()
