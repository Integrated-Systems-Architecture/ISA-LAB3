// pqcrypto: what to build. The only file you edit.

// SMALL for simulation, LARGE for the FPGA.
#define SIZE SMALL

// RUN_ALL: the whole application, checked against its golden checksum.
// Otherwise the calls to run alone, on synthetic data, OR-ed together:
//   regions  R_NTT R_INTT R_POINTWISE R_POLYADD R_SAMPLE R_COMPRESS R_MESSAGE
//   calls    C_NTT_S C_NTT_E C_NTT_R C_NTT_U C_INTT_U C_INTT_V C_INTT_US
//            C_PWM_AS C_PWM_AR C_PWM_TR C_PWM_US
//            C_ADD_E C_ADD_E1 C_ADD_E2 C_ADD_M C_SUB_US
//            C_SAMPLE_A C_SAMPLE_S C_SAMPLE_E C_SAMPLE_R C_SAMPLE_E1 C_SAMPLE_E2
//            C_COMP_U C_COMP_V C_DECOMP_U C_DECOMP_V C_ENCODE C_DECODE
#define RUN RUN_ALL

// 1: the kernels mapped below run on your accelerator.
#define USE_ACCEL 0

#if USE_ACCEL
// #include "my_accel.h"
// #define k_ntt_fwd my_accel_ntt_fwd
#endif
