// rxchain: what to build. The only file you edit.

// SMALL for simulation, LARGE for the FPGA.
#define SIZE SMALL

// RUN_ALL: the whole application, checked against its golden checksum.
// Otherwise the calls to run alone, on synthetic data, OR-ed together:
//   regions  R_NCO R_MIXER R_FIR R_DECIMATE R_MAGNITUDE R_DETECT
//   calls    C_NCO C_MIX_I C_MIX_Q C_FIR_I C_FIR_Q C_DEC_I C_DEC_Q C_MAG C_DETECT
#define RUN RUN_ALL

// 1: the kernels mapped below run on your accelerator.
#define USE_ACCEL 0

#if USE_ACCEL
// #include "my_accel.h"
// #define k_fir_i16 my_accel_fir_i16
#endif
