# Lab 3 — Report

Fill every `____`. Every number must come from a report the flow writes or
from a simulation log: name the file when you are asked to. Numbers from
Labs 0, 1 and 2 are copied from your delivered reports (`lab1-delivery`,
`lab2-delivery`), not measured again.

| | |
|---|---|
| Group number | ____ |
| Members | ____, ____, ____ |
| Accelerator (top module) | ____ |
| Lab 2 repository / tag imported (`rev` in `<accel>.vendor.hjson`) | ____ / ____ |
| Hardware project (`<name>/`) | ____ |
| Application / kernel calls mapped to the accelerator | ____ / ____ |
| Clock period (`CLK_PERIOD`) | ____ ns |
| Release tag | `lab3-delivery` |
| Date | ____ |

---

## 1. What the earlier labs promised

| | value | from |
|---|---|---|
| Kernel share of runtime (Lab 0) | ____ % | Lab 1 REPORT.md §1 |
| Amdahl ceiling | ____ × | Lab 1 REPORT.md §1 |
| Kernel speedup, projected | ____ × | Lab 1 REPORT.md §7.1 |
| Application speedup, projected | ____ × | Lab 1 REPORT.md §7.1 |
| Clock period of the final design | ____ ns | Lab 2 REPORT.md §4.1 |
| Area, post-layout | ____ µm² | Lab 2 REPORT.md §4.1 |
| Cycles per item | ____ | Lab 2 REPORT.md §4.1 |
| Throughput | ____ items/s | Lab 2 REPORT.md §4.1 |
| Power, post-layout | ____ mW | Lab 2 REPORT.md §4.1 |
| Energy per item | ____ pJ | Lab 2 REPORT.md §4.1 |

---

## 2. Integration

| | |
|---|---|
| Changes to the Lab 2 RTL for this lab (which tags) | ____ |
| Registers the driver writes per call / reads while it waits | ____ / ____ |
| Data conversion in the driver (what it converts) | ____ |
| Items per accelerator run (chunking in the driver) | ____ |
| `RUN_ALL`, `USE_ACCEL 0` / `USE_ACCEL 1` (Verilator) | PASS / FAIL, PASS / FAIL |

What you had to change to make it work, and why. Two or three sentences.

> ____

---

## 3. Speedup

Verilator, `SIZE SMALL`, `RUN_ALL`: the `[profile]` table with `USE_ACCEL 0`
and with `USE_ACCEL 1`, one row per region, `total` last. Speedup = software
cycles / accelerator cycles. Say what it means: above 1, "N× faster"; below 1,
"N× slower", with N = accelerator cycles / software cycles. Put the regions
your accelerator replaces in **bold**.

| region | software cycles | accelerator cycles | speedup | |
|---|---|---|---|---|
| ____ | ____ | ____ | ____ | ____ × faster / slower |
| ____ | ____ | ____ | ____ | ____ × faster / slower |
| ____ | ____ | ____ | ____ | ____ × faster / slower |
| `total` | ____ | ____ | ____ | ____ × faster / slower |
| check | PASS / FAIL | PASS / FAIL | | |

> With our accelerator, the whole application (`SIZE SMALL`) runs ____ ×
> faster / slower; ____ (the kernel) runs ____ × faster / slower.

The region your accelerator replaces, alone (`RUN R_____`, synthetic data):

| | region cycles | checksum |
|---|---|---|
| software | ____ | `0x____` |
| accelerator | ____ | `0x____` |

| | measured | projected (Lab 1 §7.1) |
|---|---|---|
| Kernel speedup | ____ × | ____ × |
| Application speedup | ____ × | ____ × |
| Kernel share on this platform, `p` = kernel / total, software `RUN_ALL` | ____ % | ____ % (Lab 0) |
| Amdahl from the measured kernel speedup: 1 / ((1 − p) + p / S_kernel) | ____ × | — |

Gate-level runs of `R_____` with `USE_ACCEL 1`: same cycles and checksum as
Verilator? Post-synthesis ____, post-layout ____.

**3.1 Does Amdahl's law hold after the integration?** Compare the measured
application speedup with Amdahl's prediction from the measured kernel speedup,
and both with the ceiling of §1. The share `p` is not Lab 0's: the CPU here is
a cv32e40px, not Lab 0's cv32e20. Where the numbers differ, say where the
cycles went.

> ____

**3.2 Against Lab 1's projection.** Lab 1 ignored the cost of moving data.
Cycles per item in the SoC (accelerator cycles of `R_____` / items) against
your Lab 2 testbench, and what makes the difference: the driver's register
writes and polling, the data conversion, the bus and the memory banks shared
with the CPU.

| | Lab 2 testbench | in the SoC |
|---|---|---|
| Cycles per item | ____ | ____ |

> ____

**3.3 On the FPGA.** The same application with `SIZE LARGE` and `RUN_ALL` on
the AUP-ZU3 (README section 9), the `[profile]` table read from the UART.

| run | kernel cycles | `total` cycles | check |
|---|---|---|---|
| `SIZE LARGE`, software | ____ | ____ | ____ |
| `SIZE LARGE`, accelerator | ____ | ____ | ____ |

| | `SMALL`, Verilator | `LARGE`, FPGA |
|---|---|---|
| Kernel speedup | ____ × | ____ × |
| Application speedup | ____ × | ____ × |
| Kernel share `p`, software | ____ % | ____ % |

Do the speedups change with the size? Why?

> ____

---

## 4. Area

| | post-synthesis | post-layout |
|---|---|---|
| Accelerator, `<name>_peripherals_i/<accel>_i` | ____ µm² | ____ µm² |
| CPU, `core_v_mini_mcu_i/cpu_subsystem_i` | ____ µm² | ____ µm² |
| Memory, `core_v_mini_mcu_i/memory_subsystem_i` | ____ µm² | ____ µm² |
| Whole SoC, cell area | ____ µm² | ____ µm² |
| Accelerator / SoC | ____ % | ____ % |
| Accelerator / CPU | ____ × | ____ × |
| Die (`PNR_DIE_W` × `PNR_DIE_H`) | — | ____ mm² |

From `implementation/synthesis/last_output/report/area.rpt` and
`implementation/innovus/artefacts/reports/08_export.area.rpt`.

**4.1** Accelerator area here against Lab 2 (§1): what changed, and why.
Then: is the area worth it? Its share of the SoC against the application
speedup of §3.

> ____

---

## 5. Timing

At `CLK_PERIOD`, slow corner. Paths that end in your accelerator, start in it,
or stay inside it are in `accel_timing.rpt` (synthesis report directory) and
`08_export.accel_timing.rpt` (place and route).

| | post-synthesis | post-layout |
|---|---|---|
| SoC worst setup slack (`SYSTEM_CLK`) | ____ ns | ____ ns |
| SoC critical path: start → end | ____ → ____ | ____ → ____ |
| Worst path ending in the accelerator: start → end, slack | ____ → ____, ____ ns | ____ → ____, ____ ns |
| Worst path starting in the accelerator: start → end, slack | ____ → ____, ____ ns | ____ → ____, ____ ns |
| Worst path inside the accelerator: slack | ____ ns | ____ ns |

SoC numbers: `timing.rpt` (path group `SYSTEM_CLK`) and `qor.rpt` after
synthesis, `08_export.timing_setup.rpt` after place and route.

**5.1 Where is the critical path?** Is it in your accelerator? If not, where
is it, and why there? Your Lab 2 design closed at the clock period of §1, alone:
what did that buy inside this SoC, and what would have to change for your
accelerator to set the SoC's clock?

> ____

---

## 6. Throughput, power, efficiency

The `R_____` window of §3 with `USE_ACCEL 1`: `make questasim-postlayout ...
VCD_MODE=2`, then `make power STAGE=postlayout` (typical corner).

| | value | from |
|---|---|---|
| Items processed in the window | ____ | your application |
| Cycles of the window | ____ | simulation log |
| Throughput, items / (cycles × `CLK_PERIOD`) | ____ items/s | computed |
| Accelerator power, post-layout | ____ mW | `postlayout_power_hier.rpt`, line `<accel>_i` |
| Throughput per watt | ____ items/s/W | computed |
| Throughput per mm² (accelerator, post-layout area) | ____ items/s/mm² | computed |
| Energy per item | ____ pJ | power × cycles × `CLK_PERIOD` / items |
| Software, same window: throughput | ____ items/s | `USE_ACCEL 0`, Verilator |
| Software: throughput per mm² of the CPU (`cpu_subsystem_i`) | ____ items/s/mm² | computed |

**6.1** Throughput and energy per item here against Lab 2 (§1), and the
accelerator against the CPU per mm². Why do they differ?

> ____

---

## 7. Reproducibility

| | |
|---|---|
| Commands to rebuild every number here | ____ |
| Anything not reproducible with those commands | ____ |
| Known-broken or skipped steps | ____ |
