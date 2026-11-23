// Lab 3 version of the Lab 0 tinyformer: same network, same arithmetic, plus
// config.h to pick the size, what runs, and which kernels go to your
// accelerator.
//
//   token ids -> embedding + positional encoding
//   (LARGE, a tiny ViT trained on MNIST by model/mnist/train.py: 28x28 image
//    -> SEQ patches of 7x7 pixels -> linear patch embedding + positional
//    encoding)
//   for each of NLAYER layers:
//       Q,K,V projections            (3 linear layers)
//       multi-head attention         (Q.K^T, softmax, weights.V, per head)
//       output projection + residual + layer norm
//       feed-forward: DM -> DFF -> DM, ReLU, + residual + layer norm
//   mean-pool over the sequence -> classifier head -> argmax

#include <stdint.h>
#include <stdio.h>
#include "kernels.h"
#include "profile.h"

#define C_LINEAR_Q     (1u << 0)
#define C_LINEAR_K     (1u << 1)
#define C_LINEAR_V     (1u << 2)
#define C_LINEAR_O     (1u << 3)
#define C_LINEAR_FF1   (1u << 4)
#define C_LINEAR_FF2   (1u << 5)
#define C_LINEAR_CLS   (1u << 6)
#define C_QK           (1u << 7)
#define C_SOFTMAX      (1u << 8)
#define C_AV           (1u << 9)
#define C_NORM         (1u << 10)
#define C_REQUANT_Q    (1u << 11)
#define C_REQUANT_K    (1u << 12)
#define C_REQUANT_V    (1u << 13)
#define C_REQUANT_NORM (1u << 14)
#define C_REQUANT_FF   (1u << 15)
#define C_REQUANT_POOL (1u << 16)
#define C_SHIFT        (1u << 17)
#define C_RELU_FF      (1u << 18)
#define C_PATCH        (1u << 19)

#define R_LINEAR  (C_LINEAR_Q | C_LINEAR_K | C_LINEAR_V | C_LINEAR_O | \
                   C_LINEAR_FF1 | C_LINEAR_FF2 | C_LINEAR_CLS | C_PATCH)
#define R_QK      C_QK
#define R_SOFTMAX C_SOFTMAX
#define R_AV      C_AV
#define R_NORM    C_NORM
#define R_REQUANT (C_REQUANT_Q | C_REQUANT_K | C_REQUANT_V | C_REQUANT_NORM | \
                   C_REQUANT_FF | C_REQUANT_POOL)

#include "run.h"

#if SIZE == LARGE
#define IMG     28                  // MNIST image
#define PATCH    7                  // cut into SEQ patches of PATCH x PATCH
#define PDIM    (PATCH * PATCH)     // pixels per patch
#define SEQ     16
#define DM      64
#define HEADS    4
#define DFF    128
#define NLAYER   1
#define NCLASS  10
#define GOLDEN 0xffff6d0fu
#else
#define SEQ      8
#define DM      16
#define HEADS    2
#define DFF     32
#define NLAYER   2
#define NCLASS   4
#define GOLDEN 0x0000016au
#endif

#define VOCAB   32                  // token vocabulary
#define DH      (DM / HEADS)        // per-head dimension
#define NINFER   1

// Fixed-point rescale shifts: int32 accumulator -> int8 activation.
#if SIZE == LARGE                   // trained weights use the whole int8 range
#define SH_PATCH  7                 // after the patch embedding
#define SH_QKV    9                 // after a DM-wide projection
#define SH_FF     9                 // after the DFF-wide hidden layer
#else
#define SH_QKV    5                 // after a DM-wide projection
#define SH_FF     5                 // after the DFF-wide hidden layer
#endif
#define SH_SCORE  7                 // stands in for the 1/sqrt(d_k) scaling
#define SH_CTX    8                 // undoes the Q8 softmax weights
#define SH_NORM   0                 // layer norm already outputs int8 range
#define SH_RES    5                 // residual -> accumulator scale (add_norm)
#define NORM_GAIN 64                // layer-norm output scale

typedef struct {
    int8_t  wq[DM * DM], wk[DM * DM], wv[DM * DM], wo[DM * DM];
    int8_t  w1[DFF * DM], w2[DM * DFF];
    int32_t bq[DM], bk[DM], bv[DM], bo[DM], b1[DFF], b2[DM];
} layer_t;

#if SIZE == LARGE
// Parameters trained on MNIST, and the test image
#include "tinyformer_mnist.h"
#else
static layer_t layers[NLAYER];
static int8_t  embed[VOCAB * DM];
static int8_t  pos[SEQ * DM];
static int8_t  wcls[NCLASS * DM];
static int32_t bcls[NCLASS];
#endif

// Activations
#if SIZE == LARGE
static int8_t  patches[SEQ * PDIM];         // the image, one patch per row
#else
static int32_t tokens[SEQ];
#endif
static int8_t  x[SEQ * DM];                 // the residual stream, int8
static int8_t  xres[SEQ * DM];              // copy kept for the residual add
static int32_t q32[SEQ * DM], k32[SEQ * DM], v32[SEQ * DM];
static int8_t  q8[SEQ * DM],  k8[SEQ * DM],  v8[SEQ * DM];
static int8_t  qh[SEQ * DH],  kh[SEQ * DH],  vh[SEQ * DH];
static int32_t scores[SEQ * SEQ];
static int32_t ctx[SEQ * DH];
static int8_t  ctxcat[SEQ * DM];            // heads concatenated back to DM
static int32_t proj[SEQ * DM];
static int32_t ff1[SEQ * DFF];
static int8_t  ff1_8[SEQ * DFF];
static int32_t ff2[SEQ * DM];
static int32_t pooled[DM];
static int8_t  pooled8[DM];
static int32_t logits[NCLASS];

// Per-kernel-class counters. `total` wraps the whole forward pass, so every
// other line's percentage is its share of one complete inference.
PROFILE_ACC_DECL(total);
PROFILE_ACC_DECL(linear);     // Q/K/V, output projection, FFN, classifier
PROFILE_ACC_DECL(qk);         // Q . K^T
PROFILE_ACC_DECL(softmax);
PROFILE_ACC_DECL(av);         // weights . V
PROFILE_ACC_DECL(norm);       // layer norm (mean, variance, isqrt, divide)
PROFILE_ACC_DECL(requant);

#if SIZE == LARGE
static void init_params(void) {}    // trained, in tinyformer_mnist.h
#else
static void init_params(void) {
    uint32_t s = 0xBEEF01u;
    k_fill_i8(embed, VOCAB * DM, 32, &s);
    k_fill_i8(pos,   SEQ * DM,    8, &s);
    for (int l = 0; l < NLAYER; l++) {
        layer_t *L = &layers[l];
        k_fill_i8(L->wq, DM * DM,  4, &s);
        k_fill_i8(L->wk, DM * DM,  4, &s);
        k_fill_i8(L->wv, DM * DM,  4, &s);
        k_fill_i8(L->wo, DM * DM,  4, &s);
        k_fill_i8(L->w1, DFF * DM, 4, &s);
        k_fill_i8(L->w2, DM * DFF, 4, &s);
        for (int i = 0; i < DM;  i++) {
            L->bq[i] = (int32_t)(k_rand(&s) % 32) - 16;
            L->bk[i] = (int32_t)(k_rand(&s) % 32) - 16;
            L->bv[i] = (int32_t)(k_rand(&s) % 32) - 16;
            L->bo[i] = (int32_t)(k_rand(&s) % 32) - 16;
            L->b2[i] = (int32_t)(k_rand(&s) % 32) - 16;
        }
        for (int i = 0; i < DFF; i++)
            L->b1[i] = (int32_t)(k_rand(&s) % 32) - 16;
    }
    k_fill_i8(wcls, NCLASS * DM, 4, &s);
    for (int i = 0; i < NCLASS; i++) bcls[i] = (int32_t)(k_rand(&s) % 32) - 16;
}
#endif

// Inputs of an isolated run: every intermediate tensor pseudo-random, with the
// scores as Q8 weights and the layer-norm inputs small enough not to overflow.
static void init_synthetic(void) {
    fill_rand(x, sizeof(x), 1);
    fill_rand(xres, sizeof(xres), 2);
    fill_rand(q32, sizeof(q32), 3);
    fill_rand(k32, sizeof(k32), 4);
    fill_rand(v32, sizeof(v32), 5);
    fill_rand(q8, sizeof(q8), 6);
    fill_rand(k8, sizeof(k8), 7);
    fill_rand(v8, sizeof(v8), 8);
    fill_rand(qh, sizeof(qh), 9);
    fill_rand(kh, sizeof(kh), 10);
    fill_rand(vh, sizeof(vh), 11);
    fill_rand(scores, sizeof(scores), 12);
    fill_rand(ctx, sizeof(ctx), 13);
    fill_rand(ctxcat, sizeof(ctxcat), 14);
    fill_rand(proj, sizeof(proj), 15);
    fill_rand(ff1, sizeof(ff1), 16);
    fill_rand(ff1_8, sizeof(ff1_8), 17);
    fill_rand(ff2, sizeof(ff2), 18);
    fill_rand(pooled, sizeof(pooled), 19);
    fill_rand(pooled8, sizeof(pooled8), 20);
    fill_rand(logits, sizeof(logits), 21);
#if SIZE == LARGE
    fill_rand(patches, sizeof(patches), 22);
#endif
    for (int i = 0; i < SEQ * SEQ; i++) scores[i] &= K_SOFTMAX_ONE - 1;
    for (int i = 0; i < SEQ * DM; i++) { proj[i] >>= 19; ff2[i] >>= 19; }
}

static uint32_t checksum_all(void) {
    uint32_t c = CHECKSUM_INIT;
    c = checksum_bytes(c, x, sizeof(x));
    c = checksum_bytes(c, q32, sizeof(q32));
    c = checksum_bytes(c, k32, sizeof(k32));
    c = checksum_bytes(c, v32, sizeof(v32));
    c = checksum_bytes(c, q8, sizeof(q8));
    c = checksum_bytes(c, k8, sizeof(k8));
    c = checksum_bytes(c, v8, sizeof(v8));
    c = checksum_bytes(c, scores, sizeof(scores));
    c = checksum_bytes(c, ctx, sizeof(ctx));
    c = checksum_bytes(c, proj, sizeof(proj));
    c = checksum_bytes(c, ff1, sizeof(ff1));
    c = checksum_bytes(c, ff1_8, sizeof(ff1_8));
    c = checksum_bytes(c, ff2, sizeof(ff2));
    c = checksum_bytes(c, pooled8, sizeof(pooled8));
    return checksum_bytes(c, logits, sizeof(logits));
}

#if SIZE == LARGE
// The MNIST image as SEQ patches, preprocessed as in training: uint8 pixel >> 1.
static void init_patches(void) {
    for (int t = 0; t < SEQ; t++)
        for (int i = 0; i < PDIM; i++) {
            int row = (t / (IMG / PATCH)) * PATCH + i / PATCH;
            int col = (t % (IMG / PATCH)) * PATCH + i % PATCH;
            patches[t * PDIM + i] = (int8_t)(mnist_img[row * IMG + col] >> 1);
        }
}

// Patch embedding (into proj, free at this point) + additive positional
// encoding, clamped back to int8.
static void embed_patches(void) {
    PROFILE_ACC_START(linear);
    if (RUNS(C_PATCH)) k_linear_i8(patches, wpatch, bpatch, proj, SEQ, PDIM, DM);
    PROFILE_ACC_END(linear);

    if (!ISOLATED)
        for (int i = 0; i < SEQ * DM; i++) {
            int32_t v = (proj[i] >> SH_PATCH) + (int32_t)pos[i];
            if (v >  127) v =  127;
            if (v < -128) v = -128;
            x[i] = (int8_t)v;
        }
}
#else
// Embedding lookup + additive positional encoding, clamped back to int8.
static void embed_tokens(void) {
    for (int t = 0; t < SEQ; t++)
        for (int d = 0; d < DM; d++) {
            int32_t v = (int32_t)embed[tokens[t] * DM + d] +
                        (int32_t)pos[t * DM + d];
            if (v >  127) v =  127;
            if (v < -128) v = -128;
            x[t * DM + d] = (int8_t)v;
        }
}
#endif

// acc += residual, then row-wise layer norm, then back to int8 in `dst`.
// The residual stream is int8 and the accumulator is int32, so the residual is
// shifted up to put both on a comparable scale; the layer norm renormalizes
// straight afterwards, so this constant only sets how much weight the residual
// keeps relative to the sub-layer output.
static void add_norm(int32_t *acc, const int8_t *res, int8_t *dst, int width) {
    for (int t = 0; t < SEQ; t++) {
        if (!ISOLATED)
            for (int d = 0; d < width; d++)
                acc[t * width + d] += (int32_t)res[t * width + d] << SH_RES;

        PROFILE_ACC_START(norm);
        if (RUNS(C_NORM)) k_layernorm_i32(&acc[t * width], width, NORM_GAIN);
        PROFILE_ACC_END(norm);
    }
    PROFILE_ACC_START(requant);
    if (RUNS(C_REQUANT_NORM)) k_requant_i8(acc, dst, SEQ * width, SH_NORM);
    PROFILE_ACC_END(requant);
}

static void encoder_layer(const layer_t *L) {
    // --- self-attention ------------------------------------------------
    PROFILE_ACC_START(linear);
    if (RUNS(C_LINEAR_Q)) k_linear_i8(x, L->wq, L->bq, q32, SEQ, DM, DM);
    if (RUNS(C_LINEAR_K)) k_linear_i8(x, L->wk, L->bk, k32, SEQ, DM, DM);
    if (RUNS(C_LINEAR_V)) k_linear_i8(x, L->wv, L->bv, v32, SEQ, DM, DM);
    PROFILE_ACC_END(linear);

    PROFILE_ACC_START(requant);
    if (RUNS(C_REQUANT_Q)) k_requant_i8(q32, q8, SEQ * DM, SH_QKV);
    if (RUNS(C_REQUANT_K)) k_requant_i8(k32, k8, SEQ * DM, SH_QKV);
    if (RUNS(C_REQUANT_V)) k_requant_i8(v32, v8, SEQ * DM, SH_QKV);
    PROFILE_ACC_END(requant);

    for (int h = 0; h < HEADS; h++) {
        // Gather this head's slice: head h owns columns [h*DH, h*DH+DH) of the
        // DM-wide projections, which are not contiguous in memory.
        if (!ISOLATED)
            for (int t = 0; t < SEQ; t++)
                for (int d = 0; d < DH; d++) {
                    qh[t * DH + d] = q8[t * DM + h * DH + d];
                    kh[t * DH + d] = k8[t * DM + h * DH + d];
                    vh[t * DH + d] = v8[t * DM + h * DH + d];
                }

        PROFILE_ACC_START(qk);
        if (RUNS(C_QK)) k_matmul_i8_bt(qh, kh, scores, SEQ, DH, SEQ);   // scores = Qh . Kh^T
        PROFILE_ACC_END(qk);

        if (RUNS(C_SHIFT)) k_shift_i32(scores, SEQ * SEQ, SH_SCORE);       // the 1/sqrt(d_k) scale

        PROFILE_ACC_START(softmax);
        if (RUNS(C_SOFTMAX)) k_softmax_q8(scores, SEQ, SEQ);
        PROFILE_ACC_END(softmax);

        PROFILE_ACC_START(av);
        if (RUNS(C_AV)) k_matmul_q8_i8(scores, vh, ctx, SEQ, SEQ, DH, SH_CTX);
        PROFILE_ACC_END(av);

        // Scatter the head back into the concatenated DM-wide context.
        if (!ISOLATED)
            for (int t = 0; t < SEQ; t++)
                for (int d = 0; d < DH; d++) {
                    int32_t v = ctx[t * DH + d];
                    if (v >  127) v =  127;
                    if (v < -128) v = -128;
                    ctxcat[t * DM + h * DH + d] = (int8_t)v;
                }
    }

    if (!ISOLATED)
        for (int i = 0; i < SEQ * DM; i++) xres[i] = x[i];   // keep the residual

    PROFILE_ACC_START(linear);
    if (RUNS(C_LINEAR_O)) k_linear_i8(ctxcat, L->wo, L->bo, proj, SEQ, DM, DM);
    PROFILE_ACC_END(linear);

    add_norm(proj, xres, x, DM);

    // --- feed-forward ---------------------------------------------------
    if (!ISOLATED)
        for (int i = 0; i < SEQ * DM; i++) xres[i] = x[i];

    PROFILE_ACC_START(linear);
    if (RUNS(C_LINEAR_FF1)) k_linear_i8(x, L->w1, L->b1, ff1, SEQ, DM, DFF);
    PROFILE_ACC_END(linear);

    if (RUNS(C_RELU_FF)) k_relu_i32(ff1, SEQ * DFF);

    PROFILE_ACC_START(requant);
    if (RUNS(C_REQUANT_FF)) k_requant_i8(ff1, ff1_8, SEQ * DFF, SH_FF);
    PROFILE_ACC_END(requant);

    PROFILE_ACC_START(linear);
    if (RUNS(C_LINEAR_FF2)) k_linear_i8(ff1_8, L->w2, L->b2, ff2, SEQ, DFF, DM);
    PROFILE_ACC_END(linear);

    add_norm(ff2, xres, x, DM);
}

static int forward(void) {
    PROFILE_ACC_START(total);

#if SIZE == LARGE
    embed_patches();
#else
    if (!ISOLATED) embed_tokens();
#endif
    for (int l = 0; l < NLAYER; l++)
        encoder_layer(&layers[l]);

    // Mean-pool over the sequence, then the classifier head.
    if (!ISOLATED)
        for (int d = 0; d < DM; d++) {
            int32_t s = 0;
            for (int t = 0; t < SEQ; t++) s += (int32_t)x[t * DM + d];
            pooled[d] = s / SEQ;
        }
    PROFILE_ACC_START(requant);
    if (RUNS(C_REQUANT_POOL)) k_requant_i8(pooled, pooled8, DM, 0);
    PROFILE_ACC_END(requant);

    PROFILE_ACC_START(linear);
    if (RUNS(C_LINEAR_CLS)) k_linear_i8(pooled8, wcls, bcls, logits, 1, DM, NCLASS);
    PROFILE_ACC_END(linear);

    PROFILE_ACC_END(total);
    return k_argmax_i32(logits, NCLASS);
}

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

static void report(void) {
    PROFILE_PRINTF("tinyformer seq=%d dm=%d heads=%d dff=%d layers=%d, %d pass(es)\n",
           SEQ, DM, HEADS, DFF, NLAYER, NINFER);
    PROFILE_PRINTF("[profile] %-12s %10s %10s %6s %6s\n",
           "kernel", "cycles", "instr", "calls", "share");
    PROFILE_ACC_REPORT_OF(total,   PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(linear,  PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(qk,      PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(softmax, PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(av,      PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(norm,    PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(requant, PROFILE_ACC_CYCLES(total));
}

int main(void) {
    init_params();

    if (ISOLATED) {
        init_synthetic();
        dump_on();
        forward();
        dump_off();
        report();
        printf("tinyformer: isolated run 0x%x, checksum=0x%08x\n", (unsigned)RUN, (unsigned)checksum_all());
        return 0;
    }

#if SIZE == LARGE && MNIST_UART
    // Classify the images util/mnist_uart.py sends, one line each.
    while (read_image(mnist_img)) {
        init_patches();
        uint32_t c0 = read_mcycle();
        int cls = forward();
        printf("pred=%d cycles=%u\n", cls, (unsigned)(read_mcycle() - c0));
        fflush(stdout);
    }
    return 0;
#endif

    uint32_t sum = 0;
    for (int n = 0; n < NINFER; n++) {
#if SIZE == LARGE
        init_patches();
#else
        uint32_t s = 0x51EEDu + (uint32_t)n;
        for (int t = 0; t < SEQ; t++)
            tokens[t] = (int32_t)(k_rand(&s) % VOCAB);
#endif

        int cls = forward();
#if SIZE == LARGE
        printf("pred=%d label=%d\n", cls, MNIST_LABEL);
#endif
        sum += (uint32_t)(cls + 1) * (uint32_t)(n + 1);
        sum += k_checksum_i32(logits, NCLASS);
    }

    report();
    return check_result("tinyformer", sum, GOLDEN);
}
