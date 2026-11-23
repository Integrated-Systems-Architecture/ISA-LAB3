// tinyformer: what to build. The only file you edit.

// SMALL for simulation, LARGE for the FPGA.
#define SIZE SMALL

// RUN_ALL: the whole application, checked against its golden checksum.
// Otherwise the calls to run alone, on synthetic data, OR-ed together:
//   regions  R_LINEAR R_QK R_SOFTMAX R_AV R_NORM R_REQUANT
//   calls    C_LINEAR_Q C_LINEAR_K C_LINEAR_V C_LINEAR_O C_LINEAR_FF1
//            C_LINEAR_FF2 C_LINEAR_CLS C_QK C_SOFTMAX C_AV C_NORM
//            C_REQUANT_Q C_REQUANT_K C_REQUANT_V C_REQUANT_NORM
//            C_REQUANT_FF C_REQUANT_POOL C_SHIFT C_RELU_FF
//            C_PATCH (LARGE: the patch embedding)
#define RUN RUN_ALL

// 1 (LARGE and RUN_ALL only): classify MNIST images received over the UART,
// one `pred=<k> cycles=<c>` line each. util/mnist_uart.py sends them.
#define MNIST_UART 0

// 1: the kernels mapped below run on your accelerator.
#define USE_ACCEL 0

#if USE_ACCEL
// #include "my_accel.h"
// #define k_linear_i8 my_accel_linear_i8
#endif
