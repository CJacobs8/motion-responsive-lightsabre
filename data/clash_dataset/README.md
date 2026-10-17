# Clash dataset

This is the heart of the clash classifier: **605 labelled clashes**, every one of them
a real swing onto a broomstick clamped to a table, recorded with the MPU6050 inside
the lightsabre hilt. The classifier learns from these which part of the blade was hit
(Section 6.4 of the report).

## How it was recorded

The blade was marked into three equal 307 mm zones (**bottom**, **middle** and **top**).
For every clash, the logger prompted a zone, a strength (light, medium or hard) and a
blade face (front, back, left or right). Each block of 36 clashes contains every
combination exactly once in a random order, so no zone ended up with more hard hits
than another.

**Sensor settings:** MPU6050 at 500 Hz (FIFO), accelerometer ±16 g, gyroscope
±1000 °/s, digital low-pass filter at 184 Hz.

## Sessions

I first recorded everything with the blade horizontal. The classifier then struggled
when the blade was held upright, so I added two more training sessions and a test
session with the blade held vertically.

| Session | Blade | Used for | Clashes |
|---|---|---|---|
| S01 | Horizontal | Training | 105 |
| S02 | Horizontal | Training | 107 |
| S03 | Horizontal | Training | 106 |
| S04 | Horizontal | **Test** | 36 |
| V01 | Vertical | Training | 108 |
| V02 | Vertical | Training | 108 |
| V03 | Vertical | **Test** | 35 |
| | | **Total** | **605** |

The test sessions S04 and V03 were never used for training or tuning, only for the
final check.

## Files

**`all_clashes_summary.csv`** – one row per clash, across all sessions:

| Column | Meaning |
|---|---|
| `session`, `orientation`, `set`, `id` | which session and clash this is |
| `zone` | the zone that was struck (the label) |
| `strength` | LIGHT / MEDIUM / HARD (prompted) |
| `face` | FRONT / BACK / LEFT / RIGHT (prompted) |
| `peak_g` | peak acceleration magnitude (g) |
| `jerk_g` | largest sample-to-sample rise of \|a\| at the trigger (g) |
| `pre_gyro_dps` | angular speed just before the impact (°/s) |
| `fs_hz` | measured sample rate |
| `overflows` | FIFO overflow counter (logging diagnostics) |
| `file` | path to the raw clash file |
| `time` | time of recording |

**`<session>/<ZONE>_<session>_<id>.csv`** – the raw window of one clash, 256 samples at
500 Hz, from sample −64 to +191 (sample 0 is the acceleration peak). Columns:
`ax_g`, `ay_g`, `az_g` (acceleration, g) and `gx_dps`, `gy_dps`, `gz_dps` (angular
velocity, °/s).

**`<session>/clash_log.csv`** – the per-session log (same content as the summary file).

## The final classifier

74 time- and frequency-domain features from a window of 8 samples before and 48 after
the peak (112 ms), with gravity removed, fed into a multinomial logistic regression
(λ = 0.01).

| Evaluation | Accuracy |
|---|---|
| Leave-one-session-out cross-validation | 85.4 % |
| Independent test sessions (S04 + V03) | 97.2 % |

To retrain it, run `matlab/ClashPipeline.m`.

*Note:* one clash in each of S01–S04 and V02 shows a sample rate of about 454 Hz. This
comes from a FIFO reset during logging; the samples themselves are still clocked by the
sensor at 500 Hz.
