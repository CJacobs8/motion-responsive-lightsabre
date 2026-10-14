# TPM_03 – How fast does it notice a swing?

**Target:** ≤ 100 ms from the start of a movement to the swing being detected

The MPU6050 samples at 500 Hz and the firmware reads it every 3 ms. With
`TIMING_TEST 3`, it notes the moment the raw gyro rate first crosses the swing
threshold and prints how long it takes before the (filtered) swing state is set.
I held the sabre still between each of 20 swings.

| Mean | Median | Min | Max | Std. dev. | ≤ 100 ms |
|---|---|---|---|---|---|
| 60.0 ms | 60.5 ms | 0.2 ms | 116.3 ms | 31.1 ms | 18 / 20 |

The 0.2 ms swing (#17) isn't a typo: the movement started and was detected in the
same sensor read. Timing uncertainty is about ±3 ms.

**Files:** `TPM_03_motion_response_time.csv` · `TPM_03_motion_response_time.xlsx` ·
`TPM_03_serial_log.txt` · `TPM_03_swing_demo.mp4`
