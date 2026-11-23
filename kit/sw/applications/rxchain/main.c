// Lab 3 version of the Lab 0 rxchain: same chain, same arithmetic, plus
// config.h to pick the size, what runs, and which kernels go to your
// accelerator.
//
//   int16 samples -> NCO (CORDIC) -> mixer -> FIR x2 -> decimate
//                 -> magnitude -> detector

#include <stdint.h>
#include <stdio.h>
#include "kernels.h"
#include "profile.h"

#define C_NCO    (1u << 0)
#define C_MIX_I  (1u << 1)
#define C_MIX_Q  (1u << 2)
#define C_FIR_I  (1u << 3)
#define C_FIR_Q  (1u << 4)
#define C_DEC_I  (1u << 5)
#define C_DEC_Q  (1u << 6)
#define C_MAG    (1u << 7)
#define C_DETECT (1u << 8)

#define R_NCO       C_NCO
#define R_MIXER     (C_MIX_I | C_MIX_Q)
#define R_FIR       (C_FIR_I | C_FIR_Q)
#define R_DECIMATE  (C_DEC_I | C_DEC_Q)
#define R_MAGNITUDE C_MAG
#define R_DETECT    C_DETECT

#include "run.h"

#if SIZE == LARGE
#define NSAMP  4096
#define TAPS   64
#define DEC    8
#define GOLDEN 0xfc172fa7u
#else
#define NSAMP  256
#define TAPS   24
#define DEC    4
#define GOLDEN 0x3d02c350u
#endif

#define NBLOCK 1
#define NOUT   (((NSAMP) + (DEC) - 1) / (DEC))
#define QSHIFT 15                 // FIR coefficients are Q15
#define MIXSH  14                 // NCO output is Q1.14
#define PHASE_STEP 3217           // phase increment per sample, Q2.13 (~0.39 rad)
#define AMPL       6000           // amplitude of the wanted tone
#define THRESHOLD  1200           // detector threshold on the magnitude

static int16_t x[NSAMP];          // input samples
static int16_t phase[NSAMP];      // NCO phase, Q2.13, wrapped to [-pi, pi]
static int16_t nco_c[NSAMP], nco_s[NSAMP];
static int16_t mix_i[NSAMP], mix_q[NSAMP];
static int16_t flt_i[NSAMP], flt_q[NSAMP];
static int16_t dec_i[NOUT],  dec_q[NOUT];
static int16_t mag[NOUT];
static int16_t h[TAPS];
static int16_t car_c[NSAMP], car_s[NSAMP];   // carrier used to build the input
static uint32_t detected;

PROFILE_ACC_DECL(total);
PROFILE_ACC_DECL(nco);
PROFILE_ACC_DECL(mixer);
PROFILE_ACC_DECL(fir);
PROFILE_ACC_DECL(decimate);
PROFILE_ACC_DECL(magnitude);
PROFILE_ACC_DECL(detect);

// The received signal: the wanted tone at the carrier frequency, an
// interferer near Nyquist, and a little noise.
static void init_input(int block) {
    uint32_t s = 0xA5A5u + (uint32_t)block;
    for (int n = 0; n < NSAMP; n++) {
        int32_t wanted = ((int32_t)car_c[n] * AMPL) >> K_CORDIC_QF;
        int32_t interf = (((n * 4) % 8) - 4) * 1400;
        int32_t noise  = (int32_t)(k_rand(&s) % 401) - 200;
        x[n] = (int16_t)(wanted + interf + noise);
    }
}

// Free-running phase accumulator, wrapped into the CORDIC's input range.
static void init_phase(void) {
    int32_t p = 0;
    for (int n = 0; n < NSAMP; n++) {
        phase[n] = (int16_t)p;
        p += PHASE_STEP;
        if (p >  K_CORDIC_PI_EXT) p -= 2 * K_CORDIC_PI_EXT;
        if (p < -K_CORDIC_PI_EXT) p += 2 * K_CORDIC_PI_EXT;
    }
}

// Triangular-windowed moving average, unit DC gain.
static void init_coeffs(void) {
    int32_t w[TAPS];
    int32_t sum = 0;
    for (int t = 0; t < TAPS; t++) {
        int32_t d = t - (TAPS / 2);
        if (d < 0) d = -d;
        w[t] = (TAPS / 2) - d + 1;
        sum += w[t];
    }
    for (int t = 0; t < TAPS; t++)
        h[t] = (int16_t)((w[t] << QSHIFT) / sum);
}

// Inputs of an isolated run: every intermediate signal pseudo-random.
static void init_synthetic(void) {
    fill_rand(x, sizeof(x), 1);
    fill_rand(nco_c, sizeof(nco_c), 2);
    fill_rand(nco_s, sizeof(nco_s), 3);
    fill_rand(mix_i, sizeof(mix_i), 4);
    fill_rand(mix_q, sizeof(mix_q), 5);
    fill_rand(flt_i, sizeof(flt_i), 6);
    fill_rand(flt_q, sizeof(flt_q), 7);
    fill_rand(dec_i, sizeof(dec_i), 8);
    fill_rand(dec_q, sizeof(dec_q), 9);
}

static uint32_t checksum_all(void) {
    uint32_t c = CHECKSUM_INIT;
    c = checksum_bytes(c, nco_c, sizeof(nco_c));
    c = checksum_bytes(c, nco_s, sizeof(nco_s));
    c = checksum_bytes(c, mix_i, sizeof(mix_i));
    c = checksum_bytes(c, mix_q, sizeof(mix_q));
    c = checksum_bytes(c, flt_i, sizeof(flt_i));
    c = checksum_bytes(c, flt_q, sizeof(flt_q));
    c = checksum_bytes(c, dec_i, sizeof(dec_i));
    c = checksum_bytes(c, dec_q, sizeof(dec_q));
    c = checksum_bytes(c, mag, sizeof(mag));
    return checksum_bytes(c, &detected, sizeof(detected));
}

// One block through the whole chain. Returns the detector's output.
static uint32_t receive(void) {
    PROFILE_ACC_START(total);

    PROFILE_ACC_START(nco);
    if (RUNS(C_NCO)) k_cordic_sincos(phase, NSAMP, nco_c, nco_s);
    PROFILE_ACC_END(nco);

    PROFILE_ACC_START(mixer);
    if (RUNS(C_MIX_I)) k_mix_i16(x, nco_c, mix_i, NSAMP, MIXSH);
    if (RUNS(C_MIX_Q)) k_mix_i16(x, nco_s, mix_q, NSAMP, MIXSH);
    PROFILE_ACC_END(mixer);

    PROFILE_ACC_START(fir);
    if (RUNS(C_FIR_I)) k_fir_i16(mix_i, NSAMP, h, TAPS, flt_i, QSHIFT);
    if (RUNS(C_FIR_Q)) k_fir_i16(mix_q, NSAMP, h, TAPS, flt_q, QSHIFT);
    PROFILE_ACC_END(fir);

    PROFILE_ACC_START(decimate);
    int nout = NOUT;
    if (RUNS(C_DEC_I)) nout = k_decimate_i16(flt_i, NSAMP, dec_i, DEC);
    if (RUNS(C_DEC_Q)) k_decimate_i16(flt_q, NSAMP, dec_q, DEC);
    PROFILE_ACC_END(decimate);

    PROFILE_ACC_START(magnitude);
    if (RUNS(C_MAG)) k_mag_i16(dec_i, dec_q, mag, nout);
    PROFILE_ACC_END(magnitude);

    PROFILE_ACC_START(detect);
    uint32_t hits = 0, acc = 0;
    if (RUNS(C_DETECT))
        for (int i = 0; i < nout; i++)
            if (mag[i] > THRESHOLD) { hits++; acc += (uint32_t)mag[i]; }
    PROFILE_ACC_END(detect);

    PROFILE_ACC_END(total);
    detected = (hits << 24) ^ acc;
    return detected;
}

static void report(void) {
    PROFILE_PRINTF("rxchain n=%d taps=%d dec=%d -> %d out\n", NSAMP, TAPS, DEC, NOUT);
    PROFILE_PRINTF("[profile] %-12s %10s %10s %6s %6s\n",
           "kernel", "cycles", "instr", "calls", "share");
    PROFILE_ACC_REPORT_OF(total,     PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(nco,       PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(mixer,     PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(fir,       PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(decimate,  PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(magnitude, PROFILE_ACC_CYCLES(total));
    PROFILE_ACC_REPORT_OF(detect,    PROFILE_ACC_CYCLES(total));
}

int main(void) {
    init_phase();
    init_coeffs();

    if (ISOLATED) {
        init_synthetic();
        dump_on();
        receive();
        dump_off();
        report();
        printf("rxchain: isolated run 0x%x, checksum=0x%08x\n", (unsigned)RUN, (unsigned)checksum_all());
        return 0;
    }

    k_cordic_sincos(phase, NSAMP, car_c, car_s);  // carrier for the test signal

    uint32_t sum = 0;
    for (int b = 0; b < NBLOCK; b++) {
        init_input(b);
        sum += receive() * (uint32_t)(b + 1);
    }

    report();
    return check_result("rxchain", sum, GOLDEN);
}
