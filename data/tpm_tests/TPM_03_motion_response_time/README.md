# TPM_03 – Motion detection response time

**Target:** response time ≤ 100 ms

**Method:** The MPU6050 samples at 500 Hz and the firmware reads it every 3 ms.
With `TIMING_TEST 3` in `Lightsaber_Main.ino`, the firmware records `micros()`
when the raw (unfiltered, offset-corrected) gyro rate first exceeds the swing
threshold (1.8 °/s) and prints the time until the swing state is set from the
filtered rate. The saber was held still between 20 separate swings.
Timing uncertainty is about ±3 ms (gyro read interval).

**Results (20 swings):**

| Mean | Median | Min | Max | Std. dev. | Swings ≤ 100 ms |
|---|---|---|---|---|---|
| 60.0 ms | 60.5 ms | 0.2 ms | 116.3 ms | 31.1 ms | 18 / 20 |

Swing 17 (0.2 ms): the onset and the swing state were detected in the same gyro read.

**Files**

- `TPM_03_motion_response_time.csv` – measured time per swing
- `TPM_03_motion_response_time.xlsx` – data sheet with summary calculations
- `TPM_03_serial_log.txt` – Serial Monitor output of the timing lines
- `TPM_03_swing_demo.mp4` – short demo of the swing response
