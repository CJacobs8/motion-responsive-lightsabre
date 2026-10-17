# TPM_01 – How fast does it ignite?

**Target:** ≤ 1 s from pressing the button to the blade lighting up

I ignited the blade 10 times with a long press (800 ms) on the push button of the rotary encoder, which is the ignition button on the hilt. With
`TIMING_TEST 1`, the firmware notes `micros()` the moment the button goes down and
prints the time until the first LED of the ignition lights up.

| | All trials | Trials 2–10 |
|---|---|---|
| Mean | 943.3 ms | 871.1 ms |
| Min | 809.0 ms | 809.0 ms |
| Max | 1592.8 ms | 956.6 ms |
| Std. dev. | 234.9 ms | 58.8 ms |
| ≤ 1 s | 9 / 10 | 9 / 9 |

Most of that time is the deliberate 800 ms hold. The one slow trial was the very
first ignition after switching on, which is still a bit of a mystery worth chasing.

**Files:** `TPM_01_activation_time.csv` (per trial) · `TPM_01_activation_time.xlsx`
(summary) · `TPM_01_serial_log.txt` (raw log) · `TPM_01_ignition_video.mp4` (demo)
