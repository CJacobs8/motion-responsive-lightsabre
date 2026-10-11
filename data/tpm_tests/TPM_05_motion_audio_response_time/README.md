# TPM_05 – Motion audio response time

**Target:** response time ≤ 200 ms

**Method:** With `TIMING_TEST 5` in `Lightsaber_Main.ino`, the firmware records
`micros()` when the swing state is set and again when the first audio sample
with an audible swing volume (≥ 0.025) is written to the I2S buffer. The audio
already queued in the I2S buffer ahead of that sample (up to 8 × 256 frames at
22 050 Hz, 93 ms) is added, giving the time until the swing sound reaches the
amplifier.

**Results (19 swings; swing 1 excluded because it was made during the ignition):**

| | Mean | Min | Max | Swings ≤ 200 ms |
|---|---|---|---|---|
| Detect → speaker | 138.5 ms | 81.4 ms | 180.7 ms | 19 / 19 |
| Motion onset → speaker | 202.1 ms | 81.5 ms | 276.1 ms | – |

The I2S buffer held 70–93 ms of audio ahead of the swing sound. The rest is
the time for the swing volume to rise to an audible level after detection.

**Files**

- `TPM_05_motion_audio_response_time.csv` – measured times per swing
- `TPM_05_motion_audio_response_time.xlsx` – data sheet with summary calculations
- `TPM_05_serial_log.txt` – Serial Monitor output of the timing lines
- `TPM_05_swing_slow_motion_demo.mp4` – slow-motion demo of swings with the swing sound
