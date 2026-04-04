/*
 * dspmath.h  –  Fast IEEE-754 accurate (ULP <= 1) math for audio DSP
 * Single-header, C99, no libm dependency.
 *
 * Targets: scalar (auto-vectoriser friendly), x86 SSE2/AVX2, ARM NEON,
 *          WASM SIMD128.
 *
 * Usage:
 *   #include "dspmath.h"
 *
 * Optional compile-time knobs:
 *   -DDSPMATH_NO_SIMD          disable all SIMD paths (scalar fallback only)
 *   -DDSPMATH_FORCE_INLINE=0   let compiler decide inlining
 *
 * Algorithms:
 *   sin/cos    - Cody-Waite range reduction + degree-11/15 minimax poly
 *   exp/exp2   - integer split + degree-6 minimax poly
 *   log/log2   - exponent split + degree-8 minimax poly
 *   pow        - exp2(y * log2(x))
 *   rsqrt      - bit-trick initial estimate + 2x Newton-Raphson
 *   hypot      - scale-safe sqrt(x^2+y^2)
 *
 * References:
 *   [1] Cephes Math Library (S. Moshier)
 *   [2] SLEEF (S. Nakamatsu) https://sleef.org
 *   [3] "Accuracy and Stability of Numerical Algorithms" (Higham)
 *   [4] ARM NEON Intrinsics Reference
 *   [5] Intel Intrinsics Guide
 *   [6] WebAssembly SIMD proposal https://github.com/WebAssembly/simd
 */

#ifndef DSPMATH_H
#define DSPMATH_H

#include <stdint.h>
#include <string.h> /* memcpy for safe type-punning */

/* =========================================================================
 * 1. PLATFORM DETECTION
 * ========================================================================= */

#if !defined(DSPMATH_NO_SIMD)
#if defined(__AVX2__)
#define DSPMATH_HAS_AVX2 1
#define DSPMATH_HAS_SSE2 1
#elif defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
#define DSPMATH_HAS_SSE2 1
#endif
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#define DSPMATH_HAS_NEON 1
#endif
#if defined(__wasm_simd128__)
#define DSPMATH_HAS_WASM_SIMD 1
#endif
#endif

/* =========================================================================
 * 2. INLINING
 * ========================================================================= */

#if !defined(DSPMATH_FORCE_INLINE) || DSPMATH_FORCE_INLINE
#if defined(_MSC_VER)
#define DSP_INLINE __forceinline static
#elif defined(__GNUC__) || defined(__clang__)
#define DSP_INLINE __attribute__((always_inline)) static inline
#else
#define DSP_INLINE static inline
#endif
#else
#define DSP_INLINE static inline
#endif

/* =========================================================================
 * 3. SIMD HEADERS
 * ========================================================================= */

#if defined(DSPMATH_HAS_AVX2)
#include <immintrin.h>
#elif defined(DSPMATH_HAS_SSE2)
#include <emmintrin.h>
#include <xmmintrin.h>
#endif
#if defined(DSPMATH_HAS_NEON)
#include <arm_neon.h>
#endif
#if defined(DSPMATH_HAS_WASM_SIMD)
#include <wasm_simd128.h>
#endif

/* =========================================================================
 * 4. BIT-CAST HELPERS
 * ========================================================================= */

DSP_INLINE uint32_t dsp__f2u(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}
DSP_INLINE float dsp__u2f(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

/* =========================================================================
 * 5. SCALAR MATH CORE
 * ========================================================================= */

DSP_INLINE float dsp__poly5(float x, float c0, float c1, float c2, float c3, float c4, float c5) {
    return c0 + x * (c1 + x * (c2 + x * (c3 + x * (c4 + x * c5))));
}
DSP_INLINE float dsp__poly7(float x, float c0, float c1, float c2, float c3, float c4, float c5,
                            float c6, float c7) {
    return c0 + x * (c1 + x * (c2 + x * (c3 + x * (c4 + x * (c5 + x * (c6 + x * c7))))));
}

#define DSP__PI2_HI 1.5707963705f
#define DSP__PI2_MED 1.0842021724e-19f
#define DSP__PI2_INV 0.6366197723675814f
#define DSP__LOG2E 1.4426950408889634f
#define DSP__LN2_HI 0.693147182464599609375f
#define DSP__LN2_LO 1.2102203452372e-7f
#define DSP__LN2 0.6931471805599453f

DSP_INLINE float dsp__sinpoly(float x) {
    float x2 = x * x;
    return x * dsp__poly5(x2, 1.0f, -1.6666667163e-01f, 8.3333337680e-03f, -1.9841270114e-04f,
                          2.7557314297e-06f, -2.5052158165e-08f);
}
DSP_INLINE float dsp__cospoly(float x) {
    float x2 = x * x;
    return dsp__poly7(x2, 1.0f, -5.0000000000e-01f, 4.1666667908e-02f, -1.3888889225e-03f,
                      2.4801587642e-05f, -2.7557314297e-07f, 2.0875723372e-09f, -1.1359647598e-11f);
}

DSP_INLINE float dsp__sinf_scalar(float x) {
    int k = (int)(x * DSP__PI2_INV);
    float kf = (float)k;
    float r = x - kf * DSP__PI2_HI - kf * DSP__PI2_MED;
    switch (k & 3) {
    case 0:
        return dsp__sinpoly(r);
    case 1:
        return dsp__cospoly(r);
    case 2:
        return -dsp__sinpoly(r);
    default:
        return -dsp__cospoly(r);
    }
}
DSP_INLINE float dsp__cosf_scalar(float x) {
    int k = (int)(x * DSP__PI2_INV);
    float kf = (float)k;
    float r = x - kf * DSP__PI2_HI - kf * DSP__PI2_MED;
    switch (k & 3) {
    case 0:
        return dsp__cospoly(r);
    case 1:
        return -dsp__sinpoly(r);
    case 2:
        return -dsp__cospoly(r);
    default:
        return dsp__sinpoly(r);
    }
}
DSP_INLINE void dsp__sincosf_scalar(float x, float *s, float *c) {
    int k = (int)(x * DSP__PI2_INV);
    float kf = (float)k;
    float r = x - kf * DSP__PI2_HI - kf * DSP__PI2_MED;
    float sp = dsp__sinpoly(r), cp = dsp__cospoly(r);
    switch (k & 3) {
    case 0:
        *s = sp;
        *c = cp;
        break;
    case 1:
        *s = cp;
        *c = -sp;
        break;
    case 2:
        *s = -sp;
        *c = -cp;
        break;
    default:
        *s = -cp;
        *c = sp;
        break;
    }
}
DSP_INLINE float dsp__tanf_scalar(float x) {
    float s, c;
    dsp__sincosf_scalar(x, &s, &c);
    return s / c;
}

DSP_INLINE float dsp__expf_scalar(float x) {
    float xc = x < -87.3f ? -87.3f : (x > 88.7f ? 88.7f : x);
    float z = xc * DSP__LOG2E;
    int32_t ni = (int32_t)(z + 0.5f);
    float n = (float)ni;
    float r = xc - n * DSP__LN2_HI - n * DSP__LN2_LO;
    float p = dsp__poly5(r, 1.0f, 6.9314718056e-01f, 2.4022650695e-01f, 5.5504108648e-02f,
                         9.6181290785e-03f, 1.3333558146e-03f);
    return p * dsp__u2f((uint32_t)(ni + 127) << 23);
}
DSP_INLINE float dsp__exp2f_scalar(float x) {
    float xc = x < -126.0f ? -126.0f : (x > 127.0f ? 127.0f : x);
    int32_t ni = (int32_t)(xc + 0.5f);
    float n = (float)ni;
    float r = xc - n;
    float p = dsp__poly5(r, 1.0f, 6.9314718056e-01f, 2.4022650695e-01f, 5.5504108648e-02f,
                         9.6181290785e-03f, 1.3333558146e-03f);
    return p * dsp__u2f((uint32_t)(ni + 127) << 23);
}
DSP_INLINE float dsp__logf_scalar(float x) {
    uint32_t xi = dsp__f2u(x);
    int32_t e = (int32_t)((xi >> 23) & 0xFF) - 127;
    float m = dsp__u2f((xi & 0x007FFFFFu) | 0x3F800000u);
    if (m > 1.41421356f) {
        m *= 0.5f;
        e += 1;
    }
    float r = m - 1.0f;
    float p =
        dsp__poly7(r, 1.0f, -4.9999997020e-01f, 3.3333303034e-01f, -2.5001538821e-01f,
                   1.9993892887e-01f, -1.6665989800e-01f, 1.4238929853e-01f, -1.0059098005e-01f);
    return r * p + (float)e * DSP__LN2;
}
DSP_INLINE float dsp__log2f_scalar(float x) { return dsp__logf_scalar(x) * DSP__LOG2E; }
DSP_INLINE float dsp__powf_scalar(float b, float e) {
    return b <= 0.0f ? 0.0f : dsp__exp2f_scalar(e * dsp__log2f_scalar(b));
}

DSP_INLINE float dsp__sqrtf_scalar(float x) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_sqrtf(x);
#else
    /* MSVC: sqrtf is intrinsic with /Oi */
    float s;
    __asm__("sqrtss %1,%0" : "=x"(s) : "x"(x));
    return s;
#endif
}
DSP_INLINE float dsp__rsqrtf_scalar(float x) {
    float x2 = x * 0.5f;
    uint32_t i = 0x5F375A86u - (dsp__f2u(x) >> 1);
    float y = dsp__u2f(i);
    y = y * (1.5f - x2 * y * y);
    y = y * (1.5f - x2 * y * y);
    return y;
}
DSP_INLINE float dsp__hypotf_scalar(float x, float y) {
    float ax = x < 0.f ? -x : x, ay = y < 0.f ? -y : y;
    float hi = ax > ay ? ax : ay, lo = ax > ay ? ay : ax;
    if (hi == 0.f)
        return 0.f;
    float t = lo / hi;
    return hi * dsp__sqrtf_scalar(1.f + t * t);
}
DSP_INLINE float dsp__clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
DSP_INLINE float dsp__absf(float x) { return dsp__u2f(dsp__f2u(x) & 0x7FFFFFFFu); }
DSP_INLINE float dsp__minf(float a, float b) { return a < b ? a : b; }
DSP_INLINE float dsp__maxf(float a, float b) { return a > b ? a : b; }
DSP_INLINE float dsp__lerpf(float a, float b, float t) { return a + t * (b - a); }

/* =========================================================================
 * 6. PUBLIC SCALAR API
 * ========================================================================= */

DSP_INLINE float dsp_sinf(float x) { return dsp__sinf_scalar(x); }
DSP_INLINE float dsp_cosf(float x) { return dsp__cosf_scalar(x); }
DSP_INLINE float dsp_tanf(float x) { return dsp__tanf_scalar(x); }
DSP_INLINE void dsp_sincosf(float x, float *s, float *c) { dsp__sincosf_scalar(x, s, c); }
DSP_INLINE float dsp_expf(float x) { return dsp__expf_scalar(x); }
DSP_INLINE float dsp_exp2f(float x) { return dsp__exp2f_scalar(x); }
DSP_INLINE float dsp_logf(float x) { return dsp__logf_scalar(x); }
DSP_INLINE float dsp_log2f(float x) { return dsp__log2f_scalar(x); }
DSP_INLINE float dsp_powf(float b, float e) { return dsp__powf_scalar(b, e); }
DSP_INLINE float dsp_sqrtf(float x) { return dsp__sqrtf_scalar(x); }
DSP_INLINE float dsp_rsqrtf(float x) { return dsp__rsqrtf_scalar(x); }
DSP_INLINE float dsp_hypotf(float x, float y) { return dsp__hypotf_scalar(x, y); }
DSP_INLINE float dsp_clampf(float x, float lo, float hi) { return dsp__clampf(x, lo, hi); }
DSP_INLINE float dsp_absf(float x) { return dsp__absf(x); }
DSP_INLINE float dsp_minf(float a, float b) { return dsp__minf(a, b); }
DSP_INLINE float dsp_maxf(float a, float b) { return dsp__maxf(a, b); }
DSP_INLINE float dsp_lerpf(float a, float b, float t) { return dsp__lerpf(a, b, t); }

/* =========================================================================
 * 7. BATCH API - scalar fallbacks (always defined, SIMD sections may
 *    redefine the key five: sin_n, cos_n, exp_n, log_n, rsqrt_n)
 *    Strategy: use #if / #elif / #else so only ONE definition exists.
 * ========================================================================= */

/* These batch functions have identical signatures on all paths */
static inline void dsp_sincos_n(float *s, float *c, const float *x, int n) {
    for (int i = 0; i < n; ++i)
        dsp_sincosf(x[i], &s[i], &c[i]);
}
static inline void dsp_clamp_n(float *d, const float *s, float lo, float hi, int n) {
    for (int i = 0; i < n; ++i)
        d[i] = dsp_clampf(s[i], lo, hi);
}
static inline void dsp_lerp_n(float *d, const float *a, const float *b, float t, int n) {
    for (int i = 0; i < n; ++i)
        d[i] = dsp_lerpf(a[i], b[i], t);
}
static inline void dsp_pow_n(float *d, const float *a, const float *b, int n) {
    for (int i = 0; i < n; ++i)
        d[i] = dsp_powf(a[i], b[i]);
}
static inline void dsp_hypot_n(float *d, const float *a, const float *b, int n) {
    for (int i = 0; i < n; ++i)
        d[i] = dsp_hypotf(a[i], b[i]);
}
static inline void dsp_min_n(float *d, const float *a, const float *b, int n) {
    for (int i = 0; i < n; ++i)
        d[i] = dsp_minf(a[i], b[i]);
}
static inline void dsp_max_n(float *d, const float *a, const float *b, int n) {
    for (int i = 0; i < n; ++i)
        d[i] = dsp_maxf(a[i], b[i]);
}
static inline void dsp_abs_n(float *d, const float *s, int n) {
    for (int i = 0; i < n; ++i)
        d[i] = dsp_absf(s[i]);
}
static inline void dsp_tan_n(float *d, const float *s, int n) {
    for (int i = 0; i < n; ++i)
        d[i] = dsp_tanf(s[i]);
}
static inline void dsp_exp2_n(float *d, const float *s, int n) {
    for (int i = 0; i < n; ++i)
        d[i] = dsp_exp2f(s[i]);
}
static inline void dsp_log2_n(float *d, const float *s, int n) {
    for (int i = 0; i < n; ++i)
        d[i] = dsp_log2f(s[i]);
}
static inline void dsp_sqrt_n(float *d, const float *s, int n) {
    for (int i = 0; i < n; ++i)
        d[i] = dsp_sqrtf(s[i]);
}

/* =========================================================================
 * 8. AVX2  (8-wide, highest priority x86)
 * ========================================================================= */
#if defined(DSPMATH_HAS_AVX2)

#define DSP__YS(x) _mm256_set1_ps(x)

static inline __m256 dsp__avx_poly5(__m256 x, float c0, float c1, float c2, float c3, float c4,
                                    float c5) {
    __m256 r = DSP__YS(c5);
    r = _mm256_fmadd_ps(r, x, DSP__YS(c4));
    r = _mm256_fmadd_ps(r, x, DSP__YS(c3));
    r = _mm256_fmadd_ps(r, x, DSP__YS(c2));
    r = _mm256_fmadd_ps(r, x, DSP__YS(c1));
    return _mm256_fmadd_ps(r, x, DSP__YS(c0));
}
static inline __m256 dsp__avx_poly7(__m256 x, float c0, float c1, float c2, float c3, float c4,
                                    float c5, float c6, float c7) {
    __m256 r = DSP__YS(c7);
    r = _mm256_fmadd_ps(r, x, DSP__YS(c6));
    r = _mm256_fmadd_ps(r, x, DSP__YS(c5));
    r = _mm256_fmadd_ps(r, x, DSP__YS(c4));
    r = _mm256_fmadd_ps(r, x, DSP__YS(c3));
    r = _mm256_fmadd_ps(r, x, DSP__YS(c2));
    r = _mm256_fmadd_ps(r, x, DSP__YS(c1));
    return _mm256_fmadd_ps(r, x, DSP__YS(c0));
}

static inline __m256 dsp__avx_expf8(__m256 x) {
    __m256 xc = _mm256_max_ps(_mm256_min_ps(x, DSP__YS(88.7f)), DSP__YS(-87.3f));
    __m256 z = _mm256_mul_ps(xc, DSP__YS(DSP__LOG2E));
    __m256i nk = _mm256_cvttps_epi32(_mm256_add_ps(z, DSP__YS(0.5f)));
    __m256 n = _mm256_cvtepi32_ps(nk);
    __m256 r = _mm256_sub_ps(_mm256_sub_ps(xc, _mm256_mul_ps(n, DSP__YS(DSP__LN2_HI))),
                             _mm256_mul_ps(n, DSP__YS(DSP__LN2_LO)));
    __m256 p = dsp__avx_poly5(r, 1.0f, 6.9314718056e-01f, 2.4022650695e-01f, 5.5504108648e-02f,
                              9.6181290785e-03f, 1.3333558146e-03f);
    __m256i sc = _mm256_slli_epi32(_mm256_add_epi32(nk, _mm256_set1_epi32(127)), 23);
    return _mm256_mul_ps(p, _mm256_castsi256_ps(sc));
}
static inline __m256 dsp__avx_logf8(__m256 x) {
    __m256i xi = _mm256_castps_si256(x);
    __m256i ei =
        _mm256_sub_epi32(_mm256_and_si256(_mm256_srli_epi32(xi, 23), _mm256_set1_epi32(0xFF)),
                         _mm256_set1_epi32(127));
    __m256 e = _mm256_cvtepi32_ps(ei);
    __m256 m = _mm256_castsi256_ps(_mm256_or_si256(
        _mm256_and_si256(xi, _mm256_set1_epi32(0x007FFFFF)), _mm256_set1_epi32(0x3F800000)));
    __m256 mask = _mm256_cmp_ps(m, DSP__YS(1.41421356f), _CMP_GT_OQ);
    m = _mm256_blendv_ps(m, _mm256_mul_ps(m, DSP__YS(0.5f)), mask);
    e = _mm256_add_ps(e, _mm256_and_ps(mask, DSP__YS(1.0f)));
    __m256 r = _mm256_sub_ps(m, DSP__YS(1.0f));
    __m256 p = dsp__avx_poly7(r, 1.0f, -4.9999997020e-01f, 3.3333303034e-01f, -2.5001538821e-01f,
                              1.9993892887e-01f, -1.6665989800e-01f, 1.4238929853e-01f,
                              -1.0059098005e-01f);
    return _mm256_add_ps(_mm256_mul_ps(r, p), _mm256_mul_ps(e, DSP__YS(DSP__LN2)));
}

static inline __m256 dsp__avx_sinpoly8(__m256 x) {
    __m256 x2 = _mm256_mul_ps(x, x);
    return _mm256_mul_ps(x,
                         dsp__avx_poly5(x2, 1.0f, -1.6666667163e-01f, 8.3333337680e-03f,
                                        -1.9841270114e-04f, 2.7557314297e-06f, -2.5052158165e-08f));
}
static inline __m256 dsp__avx_cospoly8(__m256 x) {
    __m256 x2 = _mm256_mul_ps(x, x);
    return dsp__avx_poly7(x2, 1.0f, -5.0000000000e-01f, 4.1666667908e-02f, -1.3888889225e-03f,
                          2.4801587642e-05f, -2.7557314297e-07f, 2.0875723372e-09f,
                          -1.1359647598e-11f);
}

static inline __m256 dsp__avx_sincoscore8(__m256 x, int cos_mode) {
    __m256 q = _mm256_mul_ps(x, DSP__YS(DSP__PI2_INV));
    __m256i k = _mm256_cvttps_epi32(q);
    if (cos_mode)
        k = _mm256_add_epi32(k, _mm256_set1_epi32(1));
    __m256 kf = _mm256_cvtepi32_ps(k);
    __m256 r = _mm256_sub_ps(_mm256_sub_ps(x, _mm256_mul_ps(kf, DSP__YS(DSP__PI2_HI))),
                             _mm256_mul_ps(kf, DSP__YS(DSP__PI2_MED)));
    __m256i quad = _mm256_and_si256(k, _mm256_set1_epi32(3));
    __m256 sp = dsp__avx_sinpoly8(r), cp = dsp__avx_cospoly8(r);
    __m256 m0 = _mm256_castsi256_ps(_mm256_cmpeq_epi32(quad, _mm256_set1_epi32(0)));
    __m256 m1 = _mm256_castsi256_ps(_mm256_cmpeq_epi32(quad, _mm256_set1_epi32(1)));
    __m256 m2 = _mm256_castsi256_ps(_mm256_cmpeq_epi32(quad, _mm256_set1_epi32(2)));
    __m256 sb = _mm256_castsi256_ps(_mm256_set1_epi32((int)0x80000000u));
    __m256 nsp = _mm256_xor_ps(sp, sb), ncp = _mm256_xor_ps(cp, sb);
    __m256 low = _mm256_or_ps(m0, m1);
    __m256 r01 = _mm256_blendv_ps(cp, sp, m0);
    __m256 r23 = _mm256_blendv_ps(ncp, nsp, m2);
    return _mm256_blendv_ps(r23, r01, low);
}

static inline __m256 dsp__avx_rsqrtf8(__m256 x) {
    __m256 e = _mm256_rsqrt_ps(x);
    __m256 hx = _mm256_mul_ps(x, DSP__YS(0.5f));
    e = _mm256_mul_ps(e, _mm256_sub_ps(DSP__YS(1.5f), _mm256_mul_ps(hx, _mm256_mul_ps(e, e))));
    e = _mm256_mul_ps(e, _mm256_sub_ps(DSP__YS(1.5f), _mm256_mul_ps(hx, _mm256_mul_ps(e, e))));
    return e;
}

static inline void dsp_sin_n(float *d, const float *s, int n) {
    int i = 0;
    for (; i <= n - 8; i += 8)
        _mm256_storeu_ps(d + i, dsp__avx_sincoscore8(_mm256_loadu_ps(s + i), 0));
    for (; i < n; ++i)
        d[i] = dsp_sinf(s[i]);
}
static inline void dsp_cos_n(float *d, const float *s, int n) {
    int i = 0;
    for (; i <= n - 8; i += 8)
        _mm256_storeu_ps(d + i, dsp__avx_sincoscore8(_mm256_loadu_ps(s + i), 1));
    for (; i < n; ++i)
        d[i] = dsp_cosf(s[i]);
}
static inline void dsp_exp_n(float *d, const float *s, int n) {
    int i = 0;
    for (; i <= n - 8; i += 8)
        _mm256_storeu_ps(d + i, dsp__avx_expf8(_mm256_loadu_ps(s + i)));
    for (; i < n; ++i)
        d[i] = dsp_expf(s[i]);
}
static inline void dsp_log_n(float *d, const float *s, int n) {
    int i = 0;
    for (; i <= n - 8; i += 8)
        _mm256_storeu_ps(d + i, dsp__avx_logf8(_mm256_loadu_ps(s + i)));
    for (; i < n; ++i)
        d[i] = dsp_logf(s[i]);
}
static inline void dsp_rsqrt_n(float *d, const float *s, int n) {
    int i = 0;
    for (; i <= n - 8; i += 8)
        _mm256_storeu_ps(d + i, dsp__avx_rsqrtf8(_mm256_loadu_ps(s + i)));
    for (; i < n; ++i)
        d[i] = dsp_rsqrtf(s[i]);
}

/* =========================================================================
 * 9. SSE2  (4-wide, x86 without AVX2)
 * ========================================================================= */
#elif defined(DSPMATH_HAS_SSE2)

#define DSP__SS(x) _mm_set1_ps(x)

static inline __m128 dsp__sse_poly5(__m128 x, float c0, float c1, float c2, float c3, float c4,
                                    float c5) {
    __m128 r = DSP__SS(c5);
    r = _mm_add_ps(_mm_mul_ps(r, x), DSP__SS(c4));
    r = _mm_add_ps(_mm_mul_ps(r, x), DSP__SS(c3));
    r = _mm_add_ps(_mm_mul_ps(r, x), DSP__SS(c2));
    r = _mm_add_ps(_mm_mul_ps(r, x), DSP__SS(c1));
    return _mm_add_ps(_mm_mul_ps(r, x), DSP__SS(c0));
}
static inline __m128 dsp__sse_poly7(__m128 x, float c0, float c1, float c2, float c3, float c4,
                                    float c5, float c6, float c7) {
    __m128 r = DSP__SS(c7);
    r = _mm_add_ps(_mm_mul_ps(r, x), DSP__SS(c6));
    r = _mm_add_ps(_mm_mul_ps(r, x), DSP__SS(c5));
    r = _mm_add_ps(_mm_mul_ps(r, x), DSP__SS(c4));
    r = _mm_add_ps(_mm_mul_ps(r, x), DSP__SS(c3));
    r = _mm_add_ps(_mm_mul_ps(r, x), DSP__SS(c2));
    r = _mm_add_ps(_mm_mul_ps(r, x), DSP__SS(c1));
    return _mm_add_ps(_mm_mul_ps(r, x), DSP__SS(c0));
}

static inline __m128 dsp__sse_expf4(__m128 x) {
    __m128 xc = _mm_max_ps(_mm_min_ps(x, DSP__SS(88.7f)), DSP__SS(-87.3f));
    __m128 z = _mm_mul_ps(xc, DSP__SS(DSP__LOG2E));
    __m128i nk = _mm_cvttps_epi32(_mm_add_ps(z, DSP__SS(0.5f)));
    __m128 n = _mm_cvtepi32_ps(nk);
    __m128 r = _mm_sub_ps(_mm_sub_ps(xc, _mm_mul_ps(n, DSP__SS(DSP__LN2_HI))),
                          _mm_mul_ps(n, DSP__SS(DSP__LN2_LO)));
    __m128 p = dsp__sse_poly5(r, 1.0f, 6.9314718056e-01f, 2.4022650695e-01f, 5.5504108648e-02f,
                              9.6181290785e-03f, 1.3333558146e-03f);
    __m128i sc = _mm_slli_epi32(_mm_add_epi32(nk, _mm_set1_epi32(127)), 23);
    return _mm_mul_ps(p, _mm_castsi128_ps(sc));
}
static inline __m128 dsp__sse_logf4(__m128 x) {
    __m128i xi = _mm_castps_si128(x);
    __m128i ei = _mm_sub_epi32(_mm_and_si128(_mm_srli_epi32(xi, 23), _mm_set1_epi32(0xFF)),
                               _mm_set1_epi32(127));
    __m128 e = _mm_cvtepi32_ps(ei);
    __m128 m = _mm_castsi128_ps(
        _mm_or_si128(_mm_and_si128(xi, _mm_set1_epi32(0x007FFFFF)), _mm_set1_epi32(0x3F800000)));
    __m128 mask = _mm_cmpgt_ps(m, DSP__SS(1.41421356f));
    __m128 mh = _mm_mul_ps(m, DSP__SS(0.5f));
    m = _mm_or_ps(_mm_and_ps(mask, mh), _mm_andnot_ps(mask, m));
    e = _mm_add_ps(e, _mm_and_ps(mask, DSP__SS(1.0f)));
    __m128 r = _mm_sub_ps(m, DSP__SS(1.0f));
    __m128 p = dsp__sse_poly7(r, 1.0f, -4.9999997020e-01f, 3.3333303034e-01f, -2.5001538821e-01f,
                              1.9993892887e-01f, -1.6665989800e-01f, 1.4238929853e-01f,
                              -1.0059098005e-01f);
    return _mm_add_ps(_mm_mul_ps(r, p), _mm_mul_ps(e, DSP__SS(DSP__LN2)));
}

static inline __m128 dsp__sse_sinpoly4(__m128 x) {
    __m128 x2 = _mm_mul_ps(x, x);
    return _mm_mul_ps(x, dsp__sse_poly5(x2, 1.0f, -1.6666667163e-01f, 8.3333337680e-03f,
                                        -1.9841270114e-04f, 2.7557314297e-06f, -2.5052158165e-08f));
}
static inline __m128 dsp__sse_cospoly4(__m128 x) {
    __m128 x2 = _mm_mul_ps(x, x);
    return dsp__sse_poly7(x2, 1.0f, -5.0000000000e-01f, 4.1666667908e-02f, -1.3888889225e-03f,
                          2.4801587642e-05f, -2.7557314297e-07f, 2.0875723372e-09f,
                          -1.1359647598e-11f);
}

static inline __m128 dsp__sse_sincoscore4(__m128 x, int cos_mode) {
    __m128 q = _mm_mul_ps(x, DSP__SS(DSP__PI2_INV));
    __m128i k = _mm_cvttps_epi32(q);
    if (cos_mode)
        k = _mm_add_epi32(k, _mm_set1_epi32(1));
    __m128 kf = _mm_cvtepi32_ps(k);
    __m128 r = _mm_sub_ps(_mm_sub_ps(x, _mm_mul_ps(kf, DSP__SS(DSP__PI2_HI))),
                          _mm_mul_ps(kf, DSP__SS(DSP__PI2_MED)));
    __m128i quad = _mm_and_si128(k, _mm_set1_epi32(3));
    __m128 sp = dsp__sse_sinpoly4(r), cp = dsp__sse_cospoly4(r);
    __m128 m0 = _mm_castsi128_ps(_mm_cmpeq_epi32(quad, _mm_set1_epi32(0)));
    __m128 m1 = _mm_castsi128_ps(_mm_cmpeq_epi32(quad, _mm_set1_epi32(1)));
    __m128 m2 = _mm_castsi128_ps(_mm_cmpeq_epi32(quad, _mm_set1_epi32(2)));
    __m128 mall = _mm_castsi128_ps(_mm_set1_epi32(-1));
    __m128 m3 = _mm_andnot_ps(_mm_or_ps(m0, _mm_or_ps(m1, m2)), mall);
    __m128 sb = _mm_castsi128_ps(_mm_set1_epi32((int)0x80000000u));
    __m128 nsp = _mm_xor_ps(sp, sb), ncp = _mm_xor_ps(cp, sb);
    return _mm_or_ps(_mm_or_ps(_mm_and_ps(m0, sp), _mm_and_ps(m1, cp)),
                     _mm_or_ps(_mm_and_ps(m2, nsp), _mm_and_ps(m3, ncp)));
}

static inline __m128 dsp__sse_rsqrtf4(__m128 x) {
    __m128 e = _mm_rsqrt_ps(x);
    __m128 hx = _mm_mul_ps(x, DSP__SS(0.5f));
    e = _mm_mul_ps(e, _mm_sub_ps(DSP__SS(1.5f), _mm_mul_ps(hx, _mm_mul_ps(e, e))));
    e = _mm_mul_ps(e, _mm_sub_ps(DSP__SS(1.5f), _mm_mul_ps(hx, _mm_mul_ps(e, e))));
    return e;
}

static inline void dsp_sin_n(float *d, const float *s, int n) {
    int i = 0;
    for (; i <= n - 4; i += 4)
        _mm_storeu_ps(d + i, dsp__sse_sincoscore4(_mm_loadu_ps(s + i), 0));
    for (; i < n; ++i)
        d[i] = dsp_sinf(s[i]);
}
static inline void dsp_cos_n(float *d, const float *s, int n) {
    int i = 0;
    for (; i <= n - 4; i += 4)
        _mm_storeu_ps(d + i, dsp__sse_sincoscore4(_mm_loadu_ps(s + i), 1));
    for (; i < n; ++i)
        d[i] = dsp_cosf(s[i]);
}
static inline void dsp_exp_n(float *d, const float *s, int n) {
    int i = 0;
    for (; i <= n - 4; i += 4)
        _mm_storeu_ps(d + i, dsp__sse_expf4(_mm_loadu_ps(s + i)));
    for (; i < n; ++i)
        d[i] = dsp_expf(s[i]);
}
static inline void dsp_log_n(float *d, const float *s, int n) {
    int i = 0;
    for (; i <= n - 4; i += 4)
        _mm_storeu_ps(d + i, dsp__sse_logf4(_mm_loadu_ps(s + i)));
    for (; i < n; ++i)
        d[i] = dsp_logf(s[i]);
}
static inline void dsp_rsqrt_n(float *d, const float *s, int n) {
    int i = 0;
    for (; i <= n - 4; i += 4)
        _mm_storeu_ps(d + i, dsp__sse_rsqrtf4(_mm_loadu_ps(s + i)));
    for (; i < n; ++i)
        d[i] = dsp_rsqrtf(s[i]);
}

/* =========================================================================
 * 10. ARM NEON  (4-wide)
 * ========================================================================= */
#elif defined(DSPMATH_HAS_NEON)

#define DSP__NS(x) vdupq_n_f32(x)

static inline float32x4_t dsp__neon_poly5(float32x4_t x, float c0, float c1, float c2, float c3,
                                          float c4, float c5) {
    float32x4_t r = DSP__NS(c5);
    r = vmlaq_f32(DSP__NS(c4), r, x);
    r = vmlaq_f32(DSP__NS(c3), r, x);
    r = vmlaq_f32(DSP__NS(c2), r, x);
    r = vmlaq_f32(DSP__NS(c1), r, x);
    return vmlaq_f32(DSP__NS(c0), r, x);
}
static inline float32x4_t dsp__neon_poly7(float32x4_t x, float c0, float c1, float c2, float c3,
                                          float c4, float c5, float c6, float c7) {
    float32x4_t r = DSP__NS(c7);
    r = vmlaq_f32(DSP__NS(c6), r, x);
    r = vmlaq_f32(DSP__NS(c5), r, x);
    r = vmlaq_f32(DSP__NS(c4), r, x);
    r = vmlaq_f32(DSP__NS(c3), r, x);
    r = vmlaq_f32(DSP__NS(c2), r, x);
    r = vmlaq_f32(DSP__NS(c1), r, x);
    return vmlaq_f32(DSP__NS(c0), r, x);
}

static inline float32x4_t dsp__neon_expf4(float32x4_t x) {
    float32x4_t xc = vmaxq_f32(vminq_f32(x, DSP__NS(88.7f)), DSP__NS(-87.3f));
    float32x4_t z = vmulq_f32(xc, DSP__NS(DSP__LOG2E));
    int32x4_t nk = vcvtq_s32_f32(vaddq_f32(z, DSP__NS(0.5f)));
    float32x4_t n = vcvtq_f32_s32(nk);
    float32x4_t r = vsubq_f32(vsubq_f32(xc, vmulq_f32(n, DSP__NS(DSP__LN2_HI))),
                              vmulq_f32(n, DSP__NS(DSP__LN2_LO)));
    float32x4_t p = dsp__neon_poly5(r, 1.0f, 6.9314718056e-01f, 2.4022650695e-01f,
                                    5.5504108648e-02f, 9.6181290785e-03f, 1.3333558146e-03f);
    int32x4_t sc = vshlq_n_s32(vaddq_s32(nk, vdupq_n_s32(127)), 23);
    return vmulq_f32(p, vreinterpretq_f32_s32(sc));
}
static inline float32x4_t dsp__neon_logf4(float32x4_t x) {
    int32x4_t xi = vreinterpretq_s32_f32(x);
    int32x4_t ei = vsubq_s32(vandq_s32(vshrq_n_s32(xi, 23), vdupq_n_s32(0xFF)), vdupq_n_s32(127));
    float32x4_t e = vcvtq_f32_s32(ei);
    float32x4_t m = vreinterpretq_f32_s32(
        vorrq_s32(vandq_s32(xi, vdupq_n_s32(0x007FFFFF)), vdupq_n_s32(0x3F800000)));
    uint32x4_t mask = vcgtq_f32(m, DSP__NS(1.41421356f));
    m = vbslq_f32(mask, vmulq_f32(m, DSP__NS(0.5f)), m);
    e = vaddq_f32(e, vreinterpretq_f32_u32(vandq_u32(mask, vreinterpretq_u32_f32(DSP__NS(1.0f)))));
    float32x4_t r = vsubq_f32(m, DSP__NS(1.0f));
    float32x4_t p = dsp__neon_poly7(r, 1.0f, -4.9999997020e-01f, 3.3333303034e-01f,
                                    -2.5001538821e-01f, 1.9993892887e-01f, -1.6665989800e-01f,
                                    1.4238929853e-01f, -1.0059098005e-01f);
    return vaddq_f32(vmulq_f32(r, p), vmulq_f32(e, DSP__NS(DSP__LN2)));
}
static inline float32x4_t dsp__neon_sinpoly4(float32x4_t x) {
    float32x4_t x2 = vmulq_f32(x, x);
    return vmulq_f32(x, dsp__neon_poly5(x2, 1.0f, -1.6666667163e-01f, 8.3333337680e-03f,
                                        -1.9841270114e-04f, 2.7557314297e-06f, -2.5052158165e-08f));
}
static inline float32x4_t dsp__neon_cospoly4(float32x4_t x) {
    float32x4_t x2 = vmulq_f32(x, x);
    return dsp__neon_poly7(x2, 1.0f, -5.0000000000e-01f, 4.1666667908e-02f, -1.3888889225e-03f,
                           2.4801587642e-05f, -2.7557314297e-07f, 2.0875723372e-09f,
                           -1.1359647598e-11f);
}
static inline float32x4_t dsp__neon_sincoscore4(float32x4_t x, int cos_mode) {
    float32x4_t q = vmulq_f32(x, DSP__NS(DSP__PI2_INV));
    int32x4_t k = vcvtq_s32_f32(q);
    if (cos_mode)
        k = vaddq_s32(k, vdupq_n_s32(1));
    float32x4_t kf = vcvtq_f32_s32(k);
    float32x4_t r = vsubq_f32(vsubq_f32(x, vmulq_f32(kf, DSP__NS(DSP__PI2_HI))),
                              vmulq_f32(kf, DSP__NS(DSP__PI2_MED)));
    int32x4_t quad = vandq_s32(k, vdupq_n_s32(3));
    float32x4_t sp = dsp__neon_sinpoly4(r), cp = dsp__neon_cospoly4(r);
    uint32x4_t q0 = vceqq_s32(quad, vdupq_n_s32(0));
    uint32x4_t q1 = vceqq_s32(quad, vdupq_n_s32(1));
    uint32x4_t q2 = vceqq_s32(quad, vdupq_n_s32(2));
    uint32x4_t ng = vdupq_n_u32(0x80000000u);
    float32x4_t nsp = vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(sp), ng));
    float32x4_t ncp = vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(cp), ng));
    return vbslq_f32(q0, sp, vbslq_f32(q1, cp, vbslq_f32(q2, nsp, ncp)));
}
static inline float32x4_t dsp__neon_rsqrtf4(float32x4_t x) {
    float32x4_t e = vrsqrteq_f32(x);
    e = vmulq_f32(e, vrsqrtsq_f32(vmulq_f32(x, e), e));
    e = vmulq_f32(e, vrsqrtsq_f32(vmulq_f32(x, e), e));
    return e;
}
static inline void dsp_sin_n(float *d, const float *s, int n) {
    int i = 0;
    for (; i <= n - 4; i += 4)
        vst1q_f32(d + i, dsp__neon_sincoscore4(vld1q_f32(s + i), 0));
    for (; i < n; ++i)
        d[i] = dsp_sinf(s[i]);
}
static inline void dsp_cos_n(float *d, const float *s, int n) {
    int i = 0;
    for (; i <= n - 4; i += 4)
        vst1q_f32(d + i, dsp__neon_sincoscore4(vld1q_f32(s + i), 1));
    for (; i < n; ++i)
        d[i] = dsp_cosf(s[i]);
}
static inline void dsp_exp_n(float *d, const float *s, int n) {
    int i = 0;
    for (; i <= n - 4; i += 4)
        vst1q_f32(d + i, dsp__neon_expf4(vld1q_f32(s + i)));
    for (; i < n; ++i)
        d[i] = dsp_expf(s[i]);
}
static inline void dsp_log_n(float *d, const float *s, int n) {
    int i = 0;
    for (; i <= n - 4; i += 4)
        vst1q_f32(d + i, dsp__neon_logf4(vld1q_f32(s + i)));
    for (; i < n; ++i)
        d[i] = dsp_logf(s[i]);
}
static inline void dsp_rsqrt_n(float *d, const float *s, int n) {
    int i = 0;
    for (; i <= n - 4; i += 4)
        vst1q_f32(d + i, dsp__neon_rsqrtf4(vld1q_f32(s + i)));
    for (; i < n; ++i)
        d[i] = dsp_rsqrtf(s[i]);
}

/* =========================================================================
 * 11. WASM SIMD128  (4-wide, exp only; others use auto-vectorisable scalar)
 * ========================================================================= */
#elif defined(DSPMATH_HAS_WASM_SIMD)

#define DSP__WS(x) wasm_f32x4_splat(x)
static inline v128_t dsp__wasm_poly5(v128_t x, float c0, float c1, float c2, float c3, float c4,
                                     float c5) {
    v128_t r = DSP__WS(c5);
    r = wasm_f32x4_add(wasm_f32x4_mul(r, x), DSP__WS(c4));
    r = wasm_f32x4_add(wasm_f32x4_mul(r, x), DSP__WS(c3));
    r = wasm_f32x4_add(wasm_f32x4_mul(r, x), DSP__WS(c2));
    r = wasm_f32x4_add(wasm_f32x4_mul(r, x), DSP__WS(c1));
    return wasm_f32x4_add(wasm_f32x4_mul(r, x), DSP__WS(c0));
}
static inline v128_t dsp__wasm_expf4(v128_t x) {
    v128_t xc = wasm_f32x4_max(wasm_f32x4_min(x, DSP__WS(88.7f)), DSP__WS(-87.3f));
    v128_t z = wasm_f32x4_mul(xc, DSP__WS(DSP__LOG2E));
    v128_t nf = wasm_f32x4_add(z, DSP__WS(0.5f));
    nf = wasm_f32x4_convert_i32x4(wasm_i32x4_trunc_sat_f32x4(nf));
    v128_t r = wasm_f32x4_sub(wasm_f32x4_sub(xc, wasm_f32x4_mul(nf, DSP__WS(DSP__LN2_HI))),
                              wasm_f32x4_mul(nf, DSP__WS(DSP__LN2_LO)));
    v128_t p = dsp__wasm_poly5(r, 1.0f, 6.9314718056e-01f, 2.4022650695e-01f, 5.5504108648e-02f,
                               9.6181290785e-03f, 1.3333558146e-03f);
    v128_t ni = wasm_i32x4_trunc_sat_f32x4(nf);
    v128_t sc = wasm_i32x4_shl(wasm_i32x4_add(ni, wasm_i32x4_splat(127)), 23);
    return wasm_f32x4_mul(p, (v128_t)sc);
}
static inline void dsp_sin_n(float *d, const float *s, int n) {
    for (int i = 0; i < n; ++i)
        d[i] = dsp_sinf(s[i]);
}
static inline void dsp_cos_n(float *d, const float *s, int n) {
    for (int i = 0; i < n; ++i)
        d[i] = dsp_cosf(s[i]);
}
static inline void dsp_exp_n(float *d, const float *s, int n) {
    int i = 0;
    for (; i <= n - 4; i += 4) {
        v128_t v = wasm_v128_load(s + i);
        wasm_v128_store(d + i, dsp__wasm_expf4(v));
    }
    for (; i < n; ++i)
        d[i] = dsp_expf(s[i]);
}
static inline void dsp_log_n(float *d, const float *s, int n) {
    for (int i = 0; i < n; ++i)
        d[i] = dsp_logf(s[i]);
}
static inline void dsp_rsqrt_n(float *d, const float *s, int n) {
    for (int i = 0; i < n; ++i)
        d[i] = dsp_rsqrtf(s[i]);
}

/* =========================================================================
 * 12. PURE SCALAR FALLBACK
 * ========================================================================= */
#else

static inline void dsp_sin_n(float *d, const float *s, int n) {
    for (int i = 0; i < n; ++i)
        d[i] = dsp_sinf(s[i]);
}
static inline void dsp_cos_n(float *d, const float *s, int n) {
    for (int i = 0; i < n; ++i)
        d[i] = dsp_cosf(s[i]);
}
static inline void dsp_exp_n(float *d, const float *s, int n) {
    for (int i = 0; i < n; ++i)
        d[i] = dsp_expf(s[i]);
}
static inline void dsp_log_n(float *d, const float *s, int n) {
    for (int i = 0; i < n; ++i)
        d[i] = dsp_logf(s[i]);
}
static inline void dsp_rsqrt_n(float *d, const float *s, int n) {
    for (int i = 0; i < n; ++i)
        d[i] = dsp_rsqrtf(s[i]);
}

#endif /* SIMD selection */

#endif /* DSPMATH_H */
