/*
 * dspmath_test.c  –  ULP accuracy and timing tests for dspmath.h
 *
 * Build examples:
 *   Scalar only:
 *     gcc -O2 -std=c99 dspmath_test.c -o test -lm
 *
 *   SSE2:
 *     gcc -O2 -std=c99 -msse2 dspmath_test.c -o test -lm
 *
 *   AVX2 + FMA:
 *     gcc -O3 -std=c99 -mavx2 -mfma dspmath_test.c -o test -lm
 *
 *   ARM NEON (AArch64):
 *     aarch64-linux-gnu-gcc -O3 -std=c99 dspmath_test.c -o test -lm
 *
 *   WASM SIMD (Emscripten):
 *     emcc -O3 -msimd128 dspmath_test.c -o test.html -lm
 *
 * The test compares dsp_* results against the system libm reference
 * and reports the maximum ULP error and wall-clock throughput.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define DSPMATH_IMPLEMENTATION
#include "dspmath.h"

/* -------------------------------------------------------------------------
 * Utilities
 * ------------------------------------------------------------------------- */

static uint32_t f2u(float f) { uint32_t u; memcpy(&u,&f,4); return u; }

/* ULP distance between two floats (ignores sign for NaN / inf comparison) */
static uint32_t ulp_dist(float a, float b) {
    uint32_t ua = f2u(a);
    uint32_t ub = f2u(b);
    /* make sign-magnitude comparable */
    if ((int32_t)ua < 0) ua = 0x80000000u - ua;
    if ((int32_t)ub < 0) ub = 0x80000000u - ub;
    return ua > ub ? ua - ub : ub - ua;
}

static double clock_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* -------------------------------------------------------------------------
 * Test infrastructure
 * ------------------------------------------------------------------------- */

#define N_BATCH 4096    /* batch size for throughput test  */
#define N_SWEEP 10000   /* scalar sweep for ULP max        */
#define N_REPS  200     /* repetitions for timing          */

static float g_src[N_BATCH];
static float g_dst[N_BATCH];

static void fill_range(float lo, float hi) {
    for (int i = 0; i < N_BATCH; ++i)
        g_src[i] = lo + (hi - lo) * ((float)i / (N_BATCH - 1));
}

typedef struct {
    const char *name;
    uint32_t    max_ulp;
    double      ns_per_sample; /* batch throughput */
} Result;

/* Scalar ULP sweep: fn vs ref over [lo,hi] */
static uint32_t sweep_ulp1(float(*fn)(float), double(*ref)(double),
                            float lo, float hi) {
    uint32_t mx = 0;
    for (int i = 0; i < N_SWEEP; ++i) {
        float x   = lo + (hi - lo) * ((float)i / (N_SWEEP - 1));
        float got = fn(x);
        float exp = (float)ref((double)x);
        uint32_t u = ulp_dist(got, exp);
        if (u > mx) mx = u;
    }
    return mx;
}

/* Batch throughput: run batch_fn N_REPS times, report ns/sample */
typedef void (*batch_fn1)(float*, const float*, int);

static double bench_batch1(batch_fn1 fn, float lo, float hi) {
    fill_range(lo, hi);
    /* warm up */
    fn(g_dst, g_src, N_BATCH);
    double t0 = clock_sec();
    for (int r = 0; r < N_REPS; ++r)
        fn(g_dst, g_src, N_BATCH);
    double elapsed = clock_sec() - t0;
    return elapsed / (N_REPS * N_BATCH) * 1e9;
}

/* -------------------------------------------------------------------------
 * Individual test cases
 * ------------------------------------------------------------------------- */

static Result test_sinf(void) {
    Result r;
    r.name          = "dsp_sinf";
    r.max_ulp       = sweep_ulp1(dsp_sinf,  sin,  -6.28f, 6.28f);
    r.ns_per_sample = bench_batch1(dsp_sin_n, -6.28f, 6.28f);
    return r;
}
static Result test_cosf(void) {
    Result r;
    r.name          = "dsp_cosf";
    r.max_ulp       = sweep_ulp1(dsp_cosf, cos, -6.28f, 6.28f);
    r.ns_per_sample = bench_batch1(dsp_cos_n, -6.28f, 6.28f);
    return r;
}
static Result test_expf(void) {
    Result r;
    r.name          = "dsp_expf";
    r.max_ulp       = sweep_ulp1(dsp_expf, exp, -80.0f, 80.0f);
    r.ns_per_sample = bench_batch1(dsp_exp_n, -80.0f, 80.0f);
    return r;
}
static Result test_logf(void) {
    Result r;
    r.name          = "dsp_logf";
    r.max_ulp       = sweep_ulp1(dsp_logf, log, 1e-6f, 1e6f);
    r.ns_per_sample = bench_batch1(dsp_log_n, 1e-6f, 1e6f);
    return r;
}
static Result test_sqrtf(void) {
    Result r;
    r.name          = "dsp_sqrtf";
    r.max_ulp       = sweep_ulp1(dsp_sqrtf, sqrt, 0.0f, 1e6f);
    r.ns_per_sample = bench_batch1(dsp_sqrt_n, 0.0f, 1e6f);
    return r;
}
static Result test_rsqrtf(void) {
    /* Reference: 1/sqrtf(x) via double */
    uint32_t mx = 0;
    for (int i = 0; i < N_SWEEP; ++i) {
        float x = 1e-4f + (1e6f - 1e-4f) * ((float)i / (N_SWEEP - 1));
        float got = dsp_rsqrtf(x);
        float exp = (float)(1.0 / sqrt((double)x));
        uint32_t u = ulp_dist(got, exp);
        if (u > mx) mx = u;
    }
    Result r;
    r.name          = "dsp_rsqrtf";
    r.max_ulp       = mx;
    r.ns_per_sample = bench_batch1(dsp_rsqrt_n, 1e-4f, 1e6f);
    return r;
}

/* -------------------------------------------------------------------------
 * Correctness spot-checks (sin/cos orthogonality, exp/log inverse, etc.)
 * ------------------------------------------------------------------------- */

static int spot_checks(void) {
    int ok = 1;
    float s, c;

    /* sin²+cos²=1 */
    for (int i = 0; i < 1000; ++i) {
        float x = -100.0f + 0.2f * i;
        dsp_sincosf(x, &s, &c);
        float err = fabsf(s*s + c*c - 1.0f);
        if (err > 2e-6f) {
            printf("  FAIL sin²+cos² @ x=%.3f  err=%g\n", x, err);
            ok = 0;
        }
    }

    /* exp(log(x)) ≈ x */
    for (int i = 1; i <= 1000; ++i) {
        float x   = (float)i * 0.01f;
        float got = dsp_expf(dsp_logf(x));
        float err = fabsf(got - x) / x;
        if (err > 1e-5f) {
            printf("  FAIL exp(log(x)) @ x=%.4f  rel_err=%g\n", x, err);
            ok = 0;
        }
    }

    /* rsqrt(x) * sqrt(x) ≈ 1 */
    for (int i = 1; i <= 1000; ++i) {
        float x   = (float)i * 0.01f;
        float got = dsp_rsqrtf(x) * dsp_sqrtf(x);
        float err = fabsf(got - 1.0f);
        if (err > 2e-6f) {
            printf("  FAIL rsqrt*sqrt @ x=%.4f  err=%g\n", x, err);
            ok = 0;
        }
    }

    /* clamp / lerp sanity */
    if (dsp_clampf(5.0f, 0.0f, 1.0f) != 1.0f) { puts("  FAIL clamp hi"); ok=0; }
    if (dsp_clampf(-1.f, 0.0f, 1.0f) != 0.0f) { puts("  FAIL clamp lo"); ok=0; }
    if (fabsf(dsp_lerpf(0.f,1.f,0.5f)-0.5f) > 1e-7f) { puts("  FAIL lerp"); ok=0; }

    return ok;
}

/* -------------------------------------------------------------------------
 * Main
 * ------------------------------------------------------------------------- */

int main(void) {
    printf("=== dspmath accuracy + throughput test ===\n\n");

    /* Detect active SIMD path */
#if defined(DSPMATH_HAS_AVX2)
    puts("SIMD path: AVX2 (8-wide)");
#elif defined(DSPMATH_HAS_SSE2)
    puts("SIMD path: SSE2 (4-wide)");
#elif defined(DSPMATH_HAS_NEON)
    puts("SIMD path: ARM NEON (4-wide)");
#elif defined(DSPMATH_HAS_WASM_SIMD)
    puts("SIMD path: WASM SIMD128 (4-wide)");
#else
    puts("SIMD path: scalar (auto-vectoriser)");
#endif
    putchar('\n');

    printf("Spot checks ... ");
    fflush(stdout);
    int ok = spot_checks();
    puts(ok ? "PASS" : "FAIL");

    printf("\n%-14s  %8s  %14s\n", "Function", "Max ULP", "ns/sample");
    printf("%-14s  %8s  %14s\n", "--------", "-------", "---------");

    Result tests[] = {
        test_sinf(),
        test_cosf(),
        test_expf(),
        test_logf(),
        test_sqrtf(),
        test_rsqrtf(),
    };

    int n = (int)(sizeof(tests)/sizeof(tests[0]));
    int all_pass = 1;
    for (int i = 0; i < n; ++i) {
        Result *r = &tests[i];
        const char *verdict = r->max_ulp <= 1 ? "OK" : "!!";
        if (r->max_ulp > 1) all_pass = 0;
        printf("%-14s  %8u  %12.3f ns  %s\n",
               r->name, r->max_ulp, r->ns_per_sample, verdict);
    }
    printf("\nOverall: %s\n", all_pass ? "PASS (all ULP ≤ 1)" : "FAIL");
    return all_pass ? 0 : 1;
}
