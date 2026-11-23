// Lab 3 version of the Lab 0 pqcrypto: same round, same arithmetic, plus
// config.h to pick the size, what runs, and which kernels go to your
// accelerator.
//
//   keygen    a_hat = uniform poly           (already in the NTT domain)
//             s, e  = small noise polys      (centered binomial)
//             t_hat = a_hat . NTT(s) + NTT(e)
//   encrypt   r, e1, e2 = noise
//             u = INTT(a_hat . NTT(r)) + e1
//             v = INTT(t_hat . NTT(r)) + e2 + encode(message)
//             ciphertext = compress(u, 10 bits) , compress(v, 4 bits)
//   decrypt   m' = decode( v - INTT(NTT(u) . NTT(s)) )
//             check m' == m

#include <stdint.h>
#include <stdio.h>
#include "kernels.h"
#include "profile.h"

#define C_NTT_S     (1u << 0)
#define C_NTT_E     (1u << 1)
#define C_NTT_R     (1u << 2)
#define C_NTT_U     (1u << 3)
#define C_INTT_U    (1u << 4)
#define C_INTT_V    (1u << 5)
#define C_INTT_US   (1u << 6)
#define C_PWM_AS    (1u << 7)
#define C_PWM_AR    (1u << 8)
#define C_PWM_TR    (1u << 9)
#define C_PWM_US    (1u << 10)
#define C_ADD_E     (1u << 11)
#define C_ADD_E1    (1u << 12)
#define C_ADD_E2    (1u << 13)
#define C_ADD_M     (1u << 14)
#define C_SUB_US    (1u << 15)
#define C_SAMPLE_A  (1u << 16)
#define C_SAMPLE_S  (1u << 17)
#define C_SAMPLE_E  (1u << 18)
#define C_SAMPLE_R  (1u << 19)
#define C_SAMPLE_E1 (1u << 20)
#define C_SAMPLE_E2 (1u << 21)
#define C_COMP_U    (1u << 22)
#define C_COMP_V    (1u << 23)
#define C_DECOMP_U  (1u << 24)
#define C_DECOMP_V  (1u << 25)
#define C_ENCODE    (1u << 26)
#define C_DECODE    (1u << 27)

#define R_NTT       (C_NTT_S | C_NTT_E | C_NTT_R | C_NTT_U)
#define R_INTT      (C_INTT_U | C_INTT_V | C_INTT_US)
#define R_POINTWISE (C_PWM_AS | C_PWM_AR | C_PWM_TR | C_PWM_US)
#define R_POLYADD   (C_ADD_E | C_ADD_E1 | C_ADD_E2 | C_ADD_M | C_SUB_US)
#define R_SAMPLE    (C_SAMPLE_A | C_SAMPLE_S | C_SAMPLE_E | C_SAMPLE_R | \
                     C_SAMPLE_E1 | C_SAMPLE_E2)
#define R_COMPRESS  (C_COMP_U | C_COMP_V | C_DECOMP_U | C_DECOMP_V)
#define R_MESSAGE   (C_ENCODE | C_DECODE)

#include "run.h"

#if SIZE == LARGE
#define N      1024
#define GOLDEN 0x0007988bu
#else
#define N      256
#define GOLDEN 0x0001fbbau
#endif

#define NROUND 1                  // keygen+encrypt+decrypt rounds per run
#define ETA    2                  // noise parameter of the binomial sampler
#define DU     10                 // bits per coefficient of the u ciphertext
#define DV      4                 // bits per coefficient of the v ciphertext
#define QHALF  ((K_NTT_Q + 1) / 2)   // the "1" of the message encoding

// Twiddle tables: built once, shared by every transform (a hardware NTT holds
// them in a small ROM exactly like this).
static int32_t psi_rev[N], psi_inv_rev[N], n_inv;

// Key material and work polynomials.
static int32_t a_hat[N], s[N], s_hat[N], e[N], e_hat[N], t_hat[N];
static int32_t r[N], r_hat[N], e1[N], e2[N];
static int32_t u[N], v[N], tmp[N];
static int32_t cu[N], cv[N];              // the compressed ciphertext
static int32_t msg[N], dec[N];            // message bits in, bits out
static int errors;

PROFILE_ACC_DECL(total);
PROFILE_ACC_DECL(ntt);
PROFILE_ACC_DECL(intt);
PROFILE_ACC_DECL(pointwise);
PROFILE_ACC_DECL(polyadd);
PROFILE_ACC_DECL(sample);
PROFILE_ACC_DECL(compress);
PROFILE_ACC_DECL(message);

// One message bit per coefficient: 0 -> 0, 1 -> q/2. Half the ring apart is
// the largest distance available, which is exactly the noise budget: decryption
// succeeds while the accumulated noise stays below q/4.
static void encode_msg(const int32_t *bits, int32_t *poly) {
    for (int i = 0; i < N; i++)
        poly[i] = bits[i] ? QHALF : 0;
}

// Decode by distance: a coefficient nearer to q/2 than to 0 (or q) was a 1.
static void decode_msg(const int32_t *poly, int32_t *bits) {
    for (int i = 0; i < N; i++) {
        int32_t d = poly[i];
        if (d > K_NTT_Q / 2) d = K_NTT_Q - d;   // fold into [0, q/2]
        bits[i] = (d > K_NTT_Q / 4) ? 1 : 0;
    }
}

static void fill_mod(int32_t *p, uint32_t seed, uint32_t mod) {
    fill_rand(p, N * (int)sizeof(*p), seed);
    for (int i = 0; i < N; i++) p[i] = (int32_t)((uint32_t)p[i] % mod);
}

// Inputs of an isolated run: every intermediate polynomial pseudo-random, in
// the range its kernels expect.
static void init_synthetic(void) {
    fill_mod(a_hat, 1, K_NTT_Q);
    fill_mod(s, 2, K_NTT_Q);
    fill_mod(s_hat, 3, K_NTT_Q);
    fill_mod(e, 4, K_NTT_Q);
    fill_mod(e_hat, 5, K_NTT_Q);
    fill_mod(t_hat, 6, K_NTT_Q);
    fill_mod(r, 7, K_NTT_Q);
    fill_mod(r_hat, 8, K_NTT_Q);
    fill_mod(e1, 9, K_NTT_Q);
    fill_mod(e2, 10, K_NTT_Q);
    fill_mod(u, 11, K_NTT_Q);
    fill_mod(v, 12, K_NTT_Q);
    fill_mod(tmp, 13, K_NTT_Q);
    fill_mod(cu, 14, 1u << DU);
    fill_mod(cv, 15, 1u << DV);
    fill_mod(msg, 16, 2);
    fill_mod(dec, 17, 2);
}

static uint32_t checksum_all(void) {
    uint32_t c = CHECKSUM_INIT;
    c = checksum_bytes(c, a_hat, sizeof(a_hat));
    c = checksum_bytes(c, s, sizeof(s));
    c = checksum_bytes(c, s_hat, sizeof(s_hat));
    c = checksum_bytes(c, e, sizeof(e));
    c = checksum_bytes(c, e_hat, sizeof(e_hat));
    c = checksum_bytes(c, t_hat, sizeof(t_hat));
    c = checksum_bytes(c, r, sizeof(r));
    c = checksum_bytes(c, r_hat, sizeof(r_hat));
    c = checksum_bytes(c, e1, sizeof(e1));
    c = checksum_bytes(c, e2, sizeof(e2));
    c = checksum_bytes(c, u, sizeof(u));
    c = checksum_bytes(c, v, sizeof(v));
    c = checksum_bytes(c, tmp, sizeof(tmp));
    c = checksum_bytes(c, cu, sizeof(cu));
    c = checksum_bytes(c, cv, sizeof(cv));
    return checksum_bytes(c, dec, sizeof(dec));
}

// --- Key generation ---------------------------------------------------------
static void keygen(uint32_t *st) {
    PROFILE_ACC_START(sample);
    if (RUNS(C_SAMPLE_A)) k_poly_uniform_q(a_hat, N, st);      // the public "matrix", NTT domain
    if (RUNS(C_SAMPLE_S)) k_poly_cbd_q(s, N, ETA, st);         // the secret
    if (RUNS(C_SAMPLE_E)) k_poly_cbd_q(e, N, ETA, st);         // the error that hides it
    PROFILE_ACC_END(sample);

    if (!ISOLATED)
        for (int i = 0; i < N; i++) { s_hat[i] = s[i]; e_hat[i] = e[i]; }

    PROFILE_ACC_START(ntt);
    if (RUNS(C_NTT_S)) k_ntt_fwd(s_hat, N, psi_rev);
    if (RUNS(C_NTT_E)) k_ntt_fwd(e_hat, N, psi_rev);
    PROFILE_ACC_END(ntt);

    PROFILE_ACC_START(pointwise);
    if (RUNS(C_PWM_AS)) k_poly_pointwise_q(a_hat, s_hat, t_hat, N);
    PROFILE_ACC_END(pointwise);

    PROFILE_ACC_START(polyadd);
    if (RUNS(C_ADD_E)) k_poly_add_q(t_hat, e_hat, t_hat, N);
    PROFILE_ACC_END(polyadd);
}

// --- Encryption -------------------------------------------------------------
static void encrypt(uint32_t *st) {
    PROFILE_ACC_START(sample);
    if (RUNS(C_SAMPLE_R))  k_poly_cbd_q(r,  N, ETA, st);
    if (RUNS(C_SAMPLE_E1)) k_poly_cbd_q(e1, N, ETA, st);
    if (RUNS(C_SAMPLE_E2)) k_poly_cbd_q(e2, N, ETA, st);
    PROFILE_ACC_END(sample);

    if (!ISOLATED)
        for (int i = 0; i < N; i++) r_hat[i] = r[i];

    PROFILE_ACC_START(ntt);
    if (RUNS(C_NTT_R)) k_ntt_fwd(r_hat, N, psi_rev);
    PROFILE_ACC_END(ntt);

    // u = a * r + e1
    PROFILE_ACC_START(pointwise);
    if (RUNS(C_PWM_AR)) k_poly_pointwise_q(a_hat, r_hat, u, N);
    PROFILE_ACC_END(pointwise);
    PROFILE_ACC_START(intt);
    if (RUNS(C_INTT_U)) k_ntt_inv(u, N, psi_inv_rev, n_inv);
    PROFILE_ACC_END(intt);
    PROFILE_ACC_START(polyadd);
    if (RUNS(C_ADD_E1)) k_poly_add_q(u, e1, u, N);
    PROFILE_ACC_END(polyadd);

    // v = t * r + e2 + encode(m)
    PROFILE_ACC_START(pointwise);
    if (RUNS(C_PWM_TR)) k_poly_pointwise_q(t_hat, r_hat, v, N);
    PROFILE_ACC_END(pointwise);
    PROFILE_ACC_START(intt);
    if (RUNS(C_INTT_V)) k_ntt_inv(v, N, psi_inv_rev, n_inv);
    PROFILE_ACC_END(intt);
    PROFILE_ACC_START(polyadd);
    if (RUNS(C_ADD_E2)) k_poly_add_q(v, e2, v, N);
    PROFILE_ACC_END(polyadd);

    PROFILE_ACC_START(message);
    if (RUNS(C_ENCODE)) encode_msg(msg, tmp);
    PROFILE_ACC_END(message);
    PROFILE_ACC_START(polyadd);
    if (RUNS(C_ADD_M)) k_poly_add_q(v, tmp, v, N);
    PROFILE_ACC_END(polyadd);

    PROFILE_ACC_START(compress);
    if (RUNS(C_COMP_U)) k_compress_q(u, cu, N, DU);
    if (RUNS(C_COMP_V)) k_compress_q(v, cv, N, DV);
    PROFILE_ACC_END(compress);
}

// --- Decryption -------------------------------------------------------------
// Returns the number of wrong bits; 0 is the only acceptable answer.
static int decrypt(void) {
    PROFILE_ACC_START(compress);
    if (RUNS(C_DECOMP_U)) k_decompress_q(cu, u, N, DU);
    if (RUNS(C_DECOMP_V)) k_decompress_q(cv, v, N, DV);
    PROFILE_ACC_END(compress);

    PROFILE_ACC_START(ntt);
    if (RUNS(C_NTT_U)) k_ntt_fwd(u, N, psi_rev);
    PROFILE_ACC_END(ntt);

    PROFILE_ACC_START(pointwise);
    if (RUNS(C_PWM_US)) k_poly_pointwise_q(u, s_hat, tmp, N);
    PROFILE_ACC_END(pointwise);

    PROFILE_ACC_START(intt);
    if (RUNS(C_INTT_US)) k_ntt_inv(tmp, N, psi_inv_rev, n_inv);
    PROFILE_ACC_END(intt);

    PROFILE_ACC_START(polyadd);
    if (RUNS(C_SUB_US)) k_poly_sub_q(v, tmp, tmp, N);
    PROFILE_ACC_END(polyadd);

    PROFILE_ACC_START(message);
    if (RUNS(C_DECODE)) decode_msg(tmp, dec);
    PROFILE_ACC_END(message);

    int bad = 0;
    if (!ISOLATED)
        for (int i = 0; i < N; i++)
            if (dec[i] != msg[i]) bad++;
    return bad;
}

static void report(void) {
    PROFILE_PRINTF("pqcrypto n=%d q=%d eta=%d du=%d dv=%d, %d round(s)\n",
           N, K_NTT_Q, ETA, DU, DV, NROUND);
    // The ciphertext is (N*DU + N*DV) bits against N bits of plaintext; that
    // expansion, and the noise budget that forces it, is the whole cost of
    // lattice cryptography.
    PROFILE_PRINTF("ciphertext %d bytes for %d message bits, %d bit error(s)\n",
           (N * DU + N * DV) / 8, N, errors);
    PROFILE_PRINTF("[profile] %-12s %10s %10s %6s %6s\n",
           "kernel", "cycles", "instr", "calls", "share");
    PROFILE_ACC_REPORT_OF(total,     PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(ntt,       PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(intt,      PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(pointwise, PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(polyadd,   PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(sample,    PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(compress,  PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(message,   PROFILE_ACC_CYCLES(total));
}

int main(void) {
    k_ntt_tables(N, psi_rev, psi_inv_rev, &n_inv);

    if (ISOLATED) {
        uint32_t st = 0x5EEDu;
        init_synthetic();
        dump_on();
        PROFILE_ACC_START(total);
        keygen(&st);
        encrypt(&st);
        decrypt();
        PROFILE_ACC_END(total);
        dump_off();
        report();
        printf("pqcrypto: isolated run 0x%x, checksum=0x%08x\n", (unsigned)RUN, (unsigned)checksum_all());
        return 0;
    }

    uint32_t sum = 0;

    for (int round = 0; round < NROUND; round++) {
        uint32_t st = 0x5EEDu + (uint32_t)round;

        for (int i = 0; i < N; i++)          // a fresh message every round
            msg[i] = (int32_t)(k_rand(&st) & 1u);

        PROFILE_ACC_START(total);
        keygen(&st);
        encrypt(&st);
        int bad = decrypt();
        PROFILE_ACC_END(total);

        errors += bad;
        sum += (k_checksum_i32(cu, N) ^ (k_checksum_i32(cv, N) << 1)) *
               (uint32_t)(round + 1);
        sum += (uint32_t)bad * 0x10000u;
    }

    report();

    if (errors) {
        printf("pqcrypto: %d bit error(s) FAIL\n", errors);
        return 1;
    }
    return check_result("pqcrypto", sum, GOLDEN);
}
