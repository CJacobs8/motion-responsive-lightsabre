// Clash detection, feature extraction (Welch spectrum) and logistic regression
// classifier for the clash location. Must match the MATLAB feature code.

#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>

#ifdef ARDUINO
#include <Arduino.h>
#include <Wire.h>
#endif

#define CC_MPU_ADDRESS        0x68
#define CC_FS_HZ              500       // 1 kHz / (1 + SMPLRT_DIV=1)
#define CC_ACCEL_LSB_PER_G    2048.0f   // +/-16 g
#define CC_GYRO_LSB_PER_DPS   32.8f     // +/-1000 deg/s
#define CC_BYTES_PER_SAMPLE   12        // accel XYZ + gyro XYZ

#define CC_LOG_PRE            64
#define CC_LOG_POST           192
#define CC_LOG_LEN            (CC_LOG_PRE + CC_LOG_POST)

#define CC_PEAK_SEARCH        8         // samples after trigger searched for |a| peak
#define CC_REFRACTORY         150       // samples (300 ms) - one impact = one clash
#define CC_RING               512       // must exceed CC_LOG_LEN + CC_PEAK_SEARCH + FIFO burst
#define CC_MAX_WIN            256
#define CC_WELCH_LEN          16        // Welch frame length (samples)
#define CC_WELCH_HOP          8         // frame hop (50 % overlap)
#define CC_WELCH_NFFT         64        // zero-padded FFT length

#define CC_NUM_SIGNALS        8
#define CC_NUM_BANDS          4
#define CC_FEATS_PER_SIGNAL   (4 + CC_NUM_BANDS + 1)
#define CC_NUM_FEATURES       (CC_NUM_SIGNALS * CC_FEATS_PER_SIGNAL + 2)

#if defined(__has_include)
#if __has_include("ClashModel.h")
#include "ClashModel.h"
#endif
#endif

#ifndef CLASH_MODEL_TRAINED
#define CLASH_MODEL_TRAINED   0
#define CLASH_MODEL_PRE       16
#define CLASH_MODEL_POST      48
#define CLASH_MODEL_CLASSES   3
#endif

#ifndef CLASH_MODEL_GRAVITY_REMOVED
#define CLASH_MODEL_GRAVITY_REMOVED 0
#endif
#define CC_GRAVITY_BASE_START 64    // samples before the peak (inclusive)
#define CC_GRAVITY_BASE_END   14    // samples before the peak (exclusive)

#if CLASH_MODEL_TRAINED
static_assert(CLASH_MODEL_NUM_FEATURES == CC_NUM_FEATURES,
              "ClashModel.h was exported with a different feature set");
static_assert(CLASH_MODEL_FS_HZ == CC_FS_HZ,
              "ClashModel.h was trained at a different sample rate");
#endif

static_assert(CLASH_MODEL_PRE + CLASH_MODEL_POST <= CC_MAX_WIN, "window too long");
static_assert(CLASH_MODEL_PRE <= CC_LOG_PRE && CLASH_MODEL_POST <= CC_LOG_POST,
              "model window must fit inside the logged window");

#define CC_ZONE_BOTTOM 0
#define CC_ZONE_MIDDLE 1
#define CC_ZONE_TOP    2
static const char *const CC_ZONE_NAMES[3] = {"BOTTOM", "MIDDLE", "TOP"};

struct CCSample
{
    float ax, ay, az;   // g
    float gx, gy, gz;   // deg/s
};

static CCSample  ccRing[CC_RING];
static uint32_t  ccTotal = 0;            // samples received since start

static float     ccTriggerG    = 3.0f;   // |a| threshold (g)
static float     ccTriggerJerk = 1.0f;   // sample-to-sample |a| jump (g)
static float     ccPrevMag     = 1.0f;
static uint32_t  ccLastTrigger = 0;
static bool      ccHaveTriggered = false;

static bool      ccTriggerEvent = false; // consumed by ccConsumeTrigger()
static bool      ccPending      = false; // waiting for post-trigger samples
static uint32_t  ccPendingTrig  = 0;
static uint32_t  ccLastGap      = 0;     // sample index of last FIFO overflow
static bool      ccHadGap       = false;

static float     ccLastPeakG    = 0.0f;  // diagnostics
static float     ccLastJerk     = 0.0f;
static uint32_t  ccOverflows    = 0;

static inline const CCSample &ccAt(uint32_t absoluteIndex)
{
    return ccRing[absoluteIndex % CC_RING];
}

static inline float ccAccelMag(const CCSample &s)
{
    return sqrtf(s.ax * s.ax + s.ay * s.ay + s.az * s.az);
}

static inline void ccSetTrigger(float thresholdG, float jerkG)
{
    ccTriggerG = thresholdG;
    ccTriggerJerk = jerkG;
}

static uint32_t ccFindAnchor(uint32_t trigger)
{
    uint32_t best = trigger;
    float bestMag = -1.0f;
    for (uint32_t i = trigger; i < trigger + CC_PEAK_SEARCH; i++)
    {
        float m = ccAccelMag(ccAt(i));
        if (m > bestMag) { bestMag = m; best = i; }
    }
    ccLastPeakG = bestMag;
    return best;
}

static void ccPushSample(const CCSample &s)
{
    ccRing[ccTotal % CC_RING] = s;

    float mag  = ccAccelMag(s);
    float jerk = fabsf(mag - ccPrevMag);
    ccPrevMag  = mag;

    bool outOfRefractory =
        !ccHaveTriggered || (ccTotal - ccLastTrigger) >= CC_REFRACTORY;

    if (outOfRefractory && mag >= ccTriggerG && jerk >= ccTriggerJerk)
    {
        ccHaveTriggered = true;
        ccLastTrigger   = ccTotal;
        ccTriggerEvent  = true;     // audio can react immediately
        ccLastJerk      = jerk;

        if (!ccPending)
        {
            ccPending     = true;
            ccPendingTrig = ccTotal;
        }
    }

    ccTotal++;
}

static inline bool ccConsumeTrigger()
{
    bool e = ccTriggerEvent;
    ccTriggerEvent = false;
    return e;
}

static bool ccExtractWindow(uint32_t anchor, int pre, int post, CCSample *out)
{
    if (anchor < (uint32_t)pre) return false;
    uint32_t start = anchor - pre;
    uint32_t end   = anchor + post;               // exclusive
    if (ccTotal < end) return false;              // not collected yet
    if (ccTotal - start > CC_RING) return false;  // already overwritten
    if (ccHadGap && ccLastGap > start && ccLastGap <= end) return false;
    for (uint32_t i = start; i < end; i++) out[i - start] = ccAt(i);
    return true;
}

static float ccCos[CC_MAX_WIN];
static float ccSin[CC_MAX_WIN];
static int   ccTableN = 0;

static void ccBuildTables(int n)
{
    if (ccTableN == n) return;
    for (int m = 0; m < n; m++)
    {
        double a = 2.0 * M_PI * (double)m / (double)n;
        ccCos[m] = (float)cos(a);
        ccSin[m] = (float)sin(a);
    }
    ccTableN = n;
}

static void ccSignalFeatures(const float *x, int n, float fs, float *f)
{

    float sum = 0, sumSq = 0, maxAbs = 0;
    for (int i = 0; i < n; i++)
    {
        sum   += x[i];
        sumSq += x[i] * x[i];
        float a = fabsf(x[i]);
        if (a > maxAbs) maxAbs = a;
    }
    float mean = sum / n;
    float var = 0;
    static float d[CC_MAX_WIN];
    for (int i = 0; i < n; i++) { d[i] = x[i] - mean; var += d[i] * d[i]; }

    f[0] = mean;
    f[1] = sqrtf(var / (n - 1));
    f[2] = maxAbs;
    f[3] = sqrtf(sumSq / n);

    ccBuildTables(CC_WELCH_NFFT);
    static float ham[CC_WELCH_LEN];
    static bool hamReady = false;
    if (!hamReady)
    {
        for (int m = 0; m < CC_WELCH_LEN; m++)
            ham[m] = (float)(0.54 - 0.46 * cos(2.0 * M_PI * m / (CC_WELCH_LEN - 1)));
        hamReady = true;
    }

    const int K = CC_WELCH_NFFT / 2;
    static float P[CC_WELCH_NFFT / 2];
    for (int k = 0; k < K; k++) P[k] = 0.0f;
    int frames = 0;
    float fr[CC_WELCH_LEN];

    for (int s0 = 0; s0 + CC_WELCH_LEN <= n; s0 += CC_WELCH_HOP)
    {
        float fm = 0;
        for (int m = 0; m < CC_WELCH_LEN; m++) fm += x[s0 + m];
        fm /= CC_WELCH_LEN;
        for (int m = 0; m < CC_WELCH_LEN; m++) fr[m] = (x[s0 + m] - fm) * ham[m];

        for (int k = 1; k <= K; k++)
        {
            float re = 0, im = 0;
            int idx = 0;
            for (int m = 0; m < CC_WELCH_LEN; m++)
            {
                re += fr[m] * ccCos[idx];
                im -= fr[m] * ccSin[idx];
                idx += k;
                if (idx >= CC_WELCH_NFFT) idx -= CC_WELCH_NFFT;
            }
            P[k - 1] += re * re + im * im;
        }
        frames++;
    }

    float band[CC_NUM_BANDS] = {0};
    float magSum = 0, weighted = 0;
    for (int k = 1; k <= K; k++)
    {
        float pk = frames > 0 ? P[k - 1] / frames : 0.0f;
        float mag = sqrtf(pk);
        int b = ((k - 1) * CC_NUM_BANDS) / K;
        band[b]  += pk;
        magSum   += mag;
        weighted += mag * ((float)k * fs / (float)CC_WELCH_NFFT);
    }

    for (int b = 0; b < CC_NUM_BANDS; b++)
        f[4 + b] = log10f(band[b] / ((float)CC_WELCH_LEN * (float)CC_WELCH_LEN) + 1e-6f);

    f[4 + CC_NUM_BANDS] = magSum > 0 ? weighted / magSum : 0.0f;
}

static void ccComputeFeatures(const CCSample *win, int n, float *feat)
{
    static float sig[CC_NUM_SIGNALS][CC_MAX_WIN];
    for (int i = 0; i < n; i++)
    {
        const CCSample &s = win[i];
        sig[0][i] = s.ax; sig[1][i] = s.ay; sig[2][i] = s.az;
        sig[3][i] = s.gx; sig[4][i] = s.gy; sig[5][i] = s.gz;
        sig[6][i] = sqrtf(s.ax * s.ax + s.ay * s.ay + s.az * s.az);
        sig[7][i] = sqrtf(s.gx * s.gx + s.gy * s.gy + s.gz * s.gz);
    }

    for (int c = 0; c < CC_NUM_SIGNALS; c++)
        ccSignalFeatures(sig[c], n, (float)CC_FS_HZ, &feat[c * CC_FEATS_PER_SIGNAL]);

    int ia = 0, ig = 0;
    for (int i = 1; i < n; i++)
    {
        if (sig[6][i] > sig[6][ia]) ia = i;
        if (sig[7][i] > sig[7][ig]) ig = i;
    }
    int base = CC_NUM_SIGNALS * CC_FEATS_PER_SIGNAL;
    feat[base + 0] = log10f((sig[7][ig] + 1e-3f) / (sig[6][ia] + 1e-3f));
    feat[base + 1] = (float)(ig - ia) * 1000.0f / (float)CC_FS_HZ;
}

static int ccClassifyFeatures(const float *feat, float *prob)
{
#if CLASH_MODEL_TRAINED
    float score[CLASH_MODEL_CLASSES];
    float maxScore = -1e30f;
    for (int c = 0; c < CLASH_MODEL_CLASSES; c++)
    {
        float s = clashB[c];
        for (int j = 0; j < CC_NUM_FEATURES; j++)
            s += clashW[c][j] * ((feat[j] - clashMu[j]) / clashSigma[j]);
        score[c] = s;
        if (s > maxScore) maxScore = s;
    }
    float total = 0;
    for (int c = 0; c < CLASH_MODEL_CLASSES; c++)
    {
        prob[c] = expf(score[c] - maxScore);
        total += prob[c];
    }
    int best = 0;
    for (int c = 0; c < CLASH_MODEL_CLASSES; c++)
    {
        prob[c] /= total;
        if (prob[c] > prob[best]) best = c;
    }
    return best;
#else
    (void)feat;
    for (int c = 0; c < 3; c++) prob[c] = 1.0f / 3.0f;
    return CC_ZONE_MIDDLE;
#endif
}

static int ccPollClassification(float *prob, float *featOut = nullptr)
{
    if (!ccPending) return -1;

    if (ccTotal < ccPendingTrig + CC_PEAK_SEARCH + CLASH_MODEL_POST) return -1;

    ccPending = false;
    uint32_t anchor = ccFindAnchor(ccPendingTrig);

    static CCSample win[CC_MAX_WIN];
    static float feat[CC_NUM_FEATURES];
    const int n = CLASH_MODEL_PRE + CLASH_MODEL_POST;

    if (!ccExtractWindow(anchor, CLASH_MODEL_PRE, CLASH_MODEL_POST, win))
        return -1;   // gap / overwritten: skip this clash

#if CLASH_MODEL_GRAVITY_REMOVED
    {
        if (anchor < CC_GRAVITY_BASE_START) return -1;
        uint32_t b0 = anchor - CC_GRAVITY_BASE_START;
        uint32_t b1 = anchor - CC_GRAVITY_BASE_END;
        if (ccTotal - b0 > CC_RING) return -1;
        if (ccHadGap && ccLastGap > b0 && ccLastGap <= b1) return -1;
        float gx = 0, gy = 0, gz = 0;
        for (uint32_t i = b0; i < b1; i++)
        {
            const CCSample &s = ccAt(i);
            gx += s.ax; gy += s.ay; gz += s.az;
        }
        const float nb = (float)(b1 - b0);
        gx /= nb; gy /= nb; gz /= nb;
        for (int i = 0; i < n; i++)
        {
            win[i].ax -= gx; win[i].ay -= gy; win[i].az -= gz;
        }
    }
#endif

    ccComputeFeatures(win, n, feat);
    if (featOut) memcpy(featOut, feat, sizeof(feat));
    return ccClassifyFeatures(feat, prob);
}

#ifdef ARDUINO

static void ccWrite(TwoWire &w, uint8_t reg, uint8_t val)
{
    w.beginTransmission(CC_MPU_ADDRESS);
    w.write(reg);
    w.write(val);
    w.endTransmission();
}

static uint8_t ccRead8(TwoWire &w, uint8_t reg)
{
    w.beginTransmission(CC_MPU_ADDRESS);
    w.write(reg);
    w.endTransmission(false);
    w.requestFrom((uint8_t)CC_MPU_ADDRESS, (uint8_t)1);
    return w.available() ? w.read() : 0xFF;
}

static void ccFifoReset(TwoWire &w)
{
    ccWrite(w, 0x6A, 0x04);   // USER_CTRL: FIFO_RESET
    delayMicroseconds(50);
    ccWrite(w, 0x6A, 0x40);   // USER_CTRL: FIFO_EN
}

static bool ccMpuBegin(TwoWire &w)
{
    ccWrite(w, 0x6B, 0x80);   // PWR_MGMT_1: device reset
    delay(100);
    ccWrite(w, 0x6B, 0x01);   // wake, PLL with X gyro clock
    delay(10);
    ccWrite(w, 0x1A, 0x01);   // CONFIG: DLPF 184/188 Hz (gyro rate 1 kHz)
    ccWrite(w, 0x19, 0x01);   // SMPLRT_DIV: 1 kHz / 2 = 500 Hz
    ccWrite(w, 0x1B, 0x10);   // GYRO_CONFIG: +/-1000 dps
    ccWrite(w, 0x1C, 0x18);   // ACCEL_CONFIG: +/-16 g
    ccWrite(w, 0x23, 0x78);   // FIFO_EN: accel + gyro XYZ
    ccFifoReset(w);
    delay(5);
    uint8_t who = ccRead8(w, 0x75);
    return who != 0x00 && who != 0xFF;
}

static int ccMpuReadFifo(TwoWire &w)
{
    if (ccRead8(w, 0x3A) & 0x10)
    {
        ccFifoReset(w);
        ccOverflows++;
        ccHadGap  = true;
        ccLastGap = ccTotal;
        ccPending = false;
        return 0;
    }

    w.beginTransmission(CC_MPU_ADDRESS);
    w.write(0x72);
    w.endTransmission(false);
    w.requestFrom((uint8_t)CC_MPU_ADDRESS, (uint8_t)2);
    if (w.available() < 2) return 0;
    uint16_t count = ((uint16_t)w.read() << 8) | w.read();

    int samples = count / CC_BYTES_PER_SAMPLE;
    int done = 0;

    while (done < samples)
    {
        int chunk = samples - done;
        if (chunk > 20) chunk = 20;          // 240 bytes < Wire buffer
        int bytes = chunk * CC_BYTES_PER_SAMPLE;

        w.beginTransmission(CC_MPU_ADDRESS);
        w.write(0x74);                        // FIFO_R_W
        w.endTransmission(false);
        w.requestFrom((uint8_t)CC_MPU_ADDRESS, (uint8_t)bytes);
        if (w.available() < bytes) break;

        for (int i = 0; i < chunk; i++)
        {
            int16_t r[6];
            for (int j = 0; j < 6; j++)
                r[j] = (int16_t)(((uint16_t)w.read() << 8) | w.read());

            CCSample s;
            s.ax = r[0] / CC_ACCEL_LSB_PER_G;
            s.ay = r[1] / CC_ACCEL_LSB_PER_G;
            s.az = r[2] / CC_ACCEL_LSB_PER_G;
            s.gx = r[3] / CC_GYRO_LSB_PER_DPS;
            s.gy = r[4] / CC_GYRO_LSB_PER_DPS;
            s.gz = r[5] / CC_GYRO_LSB_PER_DPS;
            ccPushSample(s);
        }
        done += chunk;
    }
    return done;
}

static inline const CCSample &ccLatest()
{
    return ccAt(ccTotal ? ccTotal - 1 : 0);
}

#endif // ARDUINO
