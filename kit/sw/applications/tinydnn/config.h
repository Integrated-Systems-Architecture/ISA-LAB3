// tinydnn: what to build. The only file you edit.

// SMALL for simulation, LARGE for the FPGA.
#define SIZE SMALL

// RUN_ALL: the whole application, checked against its golden checksum.
// Otherwise the calls to run alone, on synthetic data, OR-ed together:
//   regions  R_CONV R_POOL R_LINEAR R_REQUANT
//   calls    C_CONV1 C_POOL1 C_REQUANT_CONV1 C_CONV2 C_POOL2 C_REQUANT_CONV2
//            C_FC1 C_RELU_FC1 C_REQUANT_FC1 C_FC2 C_ARGMAX
#define RUN RUN_ALL

// 1 (LARGE and RUN_ALL only): classify MNIST images received over the UART,
// one `pred=<k> cycles=<c>` line each. util/mnist_uart.py sends them.
#define MNIST_UART 0

// 1: the kernels mapped below run on your accelerator.
#define USE_ACCEL 0

#if USE_ACCEL
// #include "my_accel.h"
// #define k_conv2d_relu_i8 my_accel_conv2d_relu_i8
#endif
