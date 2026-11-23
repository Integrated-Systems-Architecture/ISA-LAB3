#ifndef VCD_DUMP_H
#define VCD_DUMP_H

// GPIO 0 high: the testbench dumps the VCD (+vcd_mode=2). Low: it stops.
void dump_on(void);
void dump_off(void);

#endif
