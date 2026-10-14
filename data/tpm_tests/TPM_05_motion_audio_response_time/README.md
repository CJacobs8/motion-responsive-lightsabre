# TPM_05 – How fast do you hear a swing?

**Target:** ≤ 200 ms from detecting a swing to the sound reaching the speaker

With `TIMING_TEST 5`, the firmware logs when the swing is detected and when the
first audible swing sample (volume ≥ 0.025) is written to the I²S buffer. The
audio already waiting in that buffer (up to 8 × 256 frames at 22.05 kHz, about
93 ms) is added on top, because that's how long it still takes to come out of
the speaker.

| | Mean | Min | Max | ≤ 200 ms |
|---|---|---|---|---|
| Detected → speaker | 138.5 ms | 81.4 ms | 180.7 ms | 19 / 19 |
| Movement starts → speaker | 202.1 ms | 81.5 ms | 276.1 ms | – |

Swing 1 was left out because I swung during the ignition. The I²S buffer accounts
for 70–93 ms of the delay, so a smaller buffer is the easiest way to make it snappier.

**Files:** `TPM_05_motion_audio_response_time.csv` · `TPM_05_motion_audio_response_time.xlsx` ·
`TPM_05_serial_log.txt` · `TPM_05_swing_slow_motion_demo.mp4`
