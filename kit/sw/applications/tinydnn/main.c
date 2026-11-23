// Lab 3 version of the Lab 0 tinydnn: same network, same arithmetic, plus
// config.h to pick the size, what runs, and which kernels go to your
// accelerator.
//
//   int8 image -> conv 3x3, ReLU -> maxpool 2x2 -> conv 3x3, ReLU
//              -> maxpool 2x2 -> FC, ReLU -> FC -> argmax
//
// SMALL runs pseudo-random weights; LARGE runs weights trained on MNIST
// (model/mnist/train.py) on a real test image.

#include <stdint.h>
#include <stdio.h>
#include "kernels.h"
#include "profile.h"

#define C_CONV1         (1u << 0)
#define C_POOL1         (1u << 1)
#define C_REQUANT_CONV1 (1u << 2)
#define C_CONV2         (1u << 3)
#define C_POOL2         (1u << 4)
#define C_REQUANT_CONV2 (1u << 5)
#define C_FC1           (1u << 6)
#define C_RELU_FC1      (1u << 7)
#define C_REQUANT_FC1   (1u << 8)
#define C_FC2           (1u << 9)
#define C_ARGMAX        (1u << 10)

#define R_CONV    (C_CONV1 | C_CONV2)
#define R_POOL    (C_POOL1 | C_POOL2)
#define R_LINEAR  (C_FC1 | C_FC2)
#define R_REQUANT (C_REQUANT_CONV1 | C_REQUANT_CONV2 | C_REQUANT_FC1)

#include "run.h"

#if SIZE == LARGE
#define IMG    28
#define C1      8
#define C2     16
#define FC1    64
#define NCLASS 10
#define GOLDEN 0xffffae0du
#else
#define IMG    16
#define C1      4
#define C2      8
#define FC1    16
#define NCLASS 10
#define GOLDEN 0xffffff90u
#endif

#define KS     3                    // 3x3 kernels throughout
#define H1    (IMG - KS + 1)        // conv1 output
#define P1    (H1 / 2)              // pool1 output
#define H2    (P1 - KS + 1)         // conv2 output
#define P2    (H2 / 2)              // pool2 output
#define FLAT  (C2 * P2 * P2)        // flattened features

#define NINFER 1

// Requantization shifts: int32 accumulator -> int8 activation. Picked so the
// activations use the int8 range without saturating everything to +/-127.
#if SIZE == LARGE                   // trained weights use the whole int8 range
#define SH_CONV1 7
#define SH_CONV2 8
#define SH_FC1  10
#else
#define SH_CONV1 4
#define SH_CONV2 3
#define SH_FC1   4
#endif

#if SIZE == LARGE
// --- Parameters (trained on MNIST) and the test image ----------------------
#include "tinydnn_mnist.h"
#else
// --- Parameters (pseudo-random, fixed seed => reproducible everywhere) ------
static int8_t  w1[C1 * 1 * KS * KS], w2[C2 * C1 * KS * KS];
static int8_t  wf1[FC1 * FLAT],      wf2[NCLASS * FC1];
static int32_t b1[C1], b2[C2], bf1[FC1], bf2[NCLASS];
#endif

// --- Activations ------------------------------------------------------------
static int8_t  img[1 * IMG * IMG];
static int32_t acc1[C1 * H1 * H1], pool1[C1 * P1 * P1];
static int8_t  a1[C1 * P1 * P1];
static int32_t acc2[C2 * H2 * H2], pool2[C2 * P2 * P2];
static int8_t  a2[FLAT];
static int32_t accf1[FC1];
static int8_t  af1[FC1];
static int32_t logits[NCLASS];
static int     pred;

// One accumulating counter per kernel class, plus one around the whole
// inference. The per-kernel counters nest inside `total`, so their percentages
// tell you what fraction of the network each accelerator candidate covers.
PROFILE_ACC_DECL(total);
PROFILE_ACC_DECL(conv);
PROFILE_ACC_DECL(pool);
PROFILE_ACC_DECL(linear);
PROFILE_ACC_DECL(requant);

#if SIZE == LARGE
static void init_params(void) {}    // trained, in tinydnn_mnist.h

// The MNIST image, preprocessed as in training: uint8 pixel >> 1.
static void init_image(int n) {
    (void)n;
    for (int i = 0; i < IMG * IMG; i++) img[i] = (int8_t)(mnist_img[i] >> 1);
}
#else
static void init_params(void) {
    uint32_t s = 0xC0FFEEu;
    k_fill_i8(w1,  C1 * KS * KS,      4, &s);
    k_fill_i8(w2,  C2 * C1 * KS * KS, 4, &s);
    k_fill_i8(wf1, FC1 * FLAT,        4, &s);
    k_fill_i8(wf2, NCLASS * FC1,      4, &s);
    for (int i = 0; i < C1;     i++) b1[i]  = (int32_t)(k_rand(&s) % 64) - 32;
    for (int i = 0; i < C2;     i++) b2[i]  = (int32_t)(k_rand(&s) % 64) - 32;
    for (int i = 0; i < FC1;    i++) bf1[i] = (int32_t)(k_rand(&s) % 64) - 32;
    for (int i = 0; i < NCLASS; i++) bf2[i] = (int32_t)(k_rand(&s) % 64) - 32;
}

// A different image per inference, so NINFER > 1 is not the same work repeated
// out of the same cache lines.
static void init_image(int n) {
    uint32_t s = 0x1234u + (uint32_t)n;
    k_fill_i8(img, IMG * IMG, 64, &s);
}
#endif

#if SIZE == LARGE && MNIST_UART
#ifdef __riscv
#include "uart.h"
#include "soc_ctrl.h"
#include "x-heep.h"

// One raw image from the UART, blocking. The uart_t is set up the way X-HEEP's
// printf sets up its own (runtime/syscalls.c, _write).
static int read_image(uint8_t *buf) {
    soc_ctrl_t soc_ctrl;
    soc_ctrl.base_addr = mmio_region_from_addr((uintptr_t)SOC_CTRL_START_ADDRESS);
    uart_t uart;
    uart.base_addr   = mmio_region_from_addr((uintptr_t)UART_START_ADDRESS);
    uart.baudrate    = UART_BAUDRATE;
    uart.clk_freq_hz = soc_ctrl_get_frequency(&soc_ctrl);
#ifdef UART_NCO
    uart.nco         = UART_NCO;
#else
    uart.nco         = ((uint64_t)uart.baudrate << (NCO_WIDTH + 4)) / uart.clk_freq_hz;
#endif
    uart_init(&uart);
    uart_read(&uart, buf, IMG * IMG);
    return 1;
}
#else
// Host build: images on stdin, stop at EOF.
static int read_image(uint8_t *buf) {
    return fread(buf, 1, IMG * IMG, stdin) == IMG * IMG;
}
#endif
#endif

// Inputs of an isolated run: every activation pseudo-random.
static void init_synthetic(void) {
    fill_rand(img, sizeof(img), 1);
    fill_rand(acc1, sizeof(acc1), 2);
    fill_rand(pool1, sizeof(pool1), 3);
    fill_rand(a1, sizeof(a1), 4);
    fill_rand(acc2, sizeof(acc2), 5);
    fill_rand(pool2, sizeof(pool2), 6);
    fill_rand(a2, sizeof(a2), 7);
    fill_rand(accf1, sizeof(accf1), 8);
    fill_rand(af1, sizeof(af1), 9);
    fill_rand(logits, sizeof(logits), 10);
}

static uint32_t checksum_all(void) {
    uint32_t c = CHECKSUM_INIT;
    c = checksum_bytes(c, acc1, sizeof(acc1));
    c = checksum_bytes(c, pool1, sizeof(pool1));
    c = checksum_bytes(c, a1, sizeof(a1));
    c = checksum_bytes(c, acc2, sizeof(acc2));
    c = checksum_bytes(c, pool2, sizeof(pool2));
    c = checksum_bytes(c, a2, sizeof(a2));
    c = checksum_bytes(c, accf1, sizeof(accf1));
    c = checksum_bytes(c, af1, sizeof(af1));
    c = checksum_bytes(c, logits, sizeof(logits));
    return checksum_bytes(c, &pred, sizeof(pred));
}

static int infer(void) {
    PROFILE_ACC_START(total);

    PROFILE_ACC_START(conv);
    if (RUNS(C_CONV1)) k_conv2d_relu_i8(img, w1, b1, acc1, 1, C1, IMG, IMG, KS);
    PROFILE_ACC_END(conv);

    PROFILE_ACC_START(pool);
    if (RUNS(C_POOL1)) k_maxpool2x2_i32(acc1, pool1, C1, H1, H1);
    PROFILE_ACC_END(pool);

    PROFILE_ACC_START(requant);
    if (RUNS(C_REQUANT_CONV1)) k_requant_i8(pool1, a1, C1 * P1 * P1, SH_CONV1);
    PROFILE_ACC_END(requant);

    PROFILE_ACC_START(conv);
    if (RUNS(C_CONV2)) k_conv2d_relu_i8(a1, w2, b2, acc2, C1, C2, P1, P1, KS);
    PROFILE_ACC_END(conv);

    PROFILE_ACC_START(pool);
    if (RUNS(C_POOL2)) k_maxpool2x2_i32(acc2, pool2, C2, H2, H2);
    PROFILE_ACC_END(pool);

    PROFILE_ACC_START(requant);
    if (RUNS(C_REQUANT_CONV2)) k_requant_i8(pool2, a2, FLAT, SH_CONV2);   // flatten is just a reinterpret
    PROFILE_ACC_END(requant);

    PROFILE_ACC_START(linear);
    if (RUNS(C_FC1)) k_linear_i8(a2, wf1, bf1, accf1, 1, FLAT, FC1);
    PROFILE_ACC_END(linear);

    if (RUNS(C_RELU_FC1)) k_relu_i32(accf1, FC1);

    PROFILE_ACC_START(requant);
    if (RUNS(C_REQUANT_FC1)) k_requant_i8(accf1, af1, FC1, SH_FC1);
    PROFILE_ACC_END(requant);

    PROFILE_ACC_START(linear);
    if (RUNS(C_FC2)) k_linear_i8(af1, wf2, bf2, logits, 1, FC1, NCLASS);
    PROFILE_ACC_END(linear);

    PROFILE_ACC_END(total);
    if (RUNS(C_ARGMAX)) pred = k_argmax_i32(logits, NCLASS);
    return pred;
}

static void report(void) {
    PROFILE_PRINTF("tinydnn %dx%d -> %d classes, %d inference(s)\n",
           IMG, IMG, NCLASS, NINFER);
    PROFILE_PRINTF("[profile] %-12s %10s %10s %6s %6s\n",
           "kernel", "cycles", "instr", "calls", "share");
    PROFILE_ACC_REPORT_OF(total,   PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(conv,    PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(pool,    PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(linear,  PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(requant, PROFILE_ACC_CYCLES(total));
}

int main(void) {
    init_params();

    if (ISOLATED) {
        init_synthetic();
        dump_on();
        infer();
        dump_off();
        report();
        printf("tinydnn: isolated run 0x%x, checksum=0x%08x\n", (unsigned)RUN, (unsigned)checksum_all());
        return 0;
    }

#if SIZE == LARGE && MNIST_UART
    // Classify the images util/mnist_uart.py sends, one line each.
    while (read_image(mnist_img)) {
        init_image(0);
        uint32_t c0 = read_mcycle();
        int cls = infer();
        printf("pred=%d cycles=%u\n", cls, (unsigned)(read_mcycle() - c0));
        fflush(stdout);
    }
    return 0;
#endif

    uint32_t sum = 0;
    for (int n = 0; n < NINFER; n++) {
        init_image(n);
        int cls = infer();
#if SIZE == LARGE
        printf("pred=%d label=%d\n", cls, MNIST_LABEL);
#endif
        sum += (uint32_t)(cls + 1) * (uint32_t)(n + 1);
        sum += k_checksum_i32(logits, NCLASS);
    }

    report();
    return check_result("tinydnn", sum, GOLDEN);
}
