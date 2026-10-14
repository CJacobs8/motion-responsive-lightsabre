# TPM_06 – How fast do you hear a clash?

**Target:** ≤ 100 ms from the impact to the clash sound reaching the speaker

A clash is triggered when |a| ≥ 3 g and jumps by at least 1 g between two samples,
checked on every 500 Hz sample. With `TIMING_TEST 6`, the firmware breaks each
clash into three parts:

1. **impact → detected** – how old the trigger sample is when the firmware gets to it
2. **detected → sound written** – until the first clash sample goes into the I²S buffer
3. **buffer** – the audio already queued ahead of it (up to 93 ms)

| | Mean | Min | Max | ≤ 100 ms |
|---|---|---|---|---|
| Impact → clash sound at speaker | 92.2 ms | 73.9 ms | 113.2 ms | 18 / 20 |
| Impact → detected | 9.5 ms | 0 ms | 28 ms | – |

The sound is written within 0.3 ms of detection, so almost all of the delay is the
I²S buffer. The rest varies with how long the main loop takes before it reads the
sensor FIFO.

**Files:** `TPM_06_clash_detection_response_time.csv` · `TPM_06_clash_detection_response_time.xlsx` ·
`TPM_06_TPM_08_serial_log.txt` (shared with TPM_08) · `TPM_06_clash_slow_motion_demo.mp4`
