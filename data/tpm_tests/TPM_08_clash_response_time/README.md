# TPM_08 – Clash response time (zone glow)

**Target:** response time ≤ 200 ms

**Method:** Measured in the same run as TPM_06 (`TIMING_TEST 6`, 20 clashes).
The firmware logs the time from the impact (first 500 Hz sample over the clash
trigger) to the first LED frame that shows the clash glow in the classified
zone. This includes the classification window (peak within 8 samples of the
trigger, then 48 samples after the peak), the classification itself and the
next LED frame.

**Results (20 clashes):**

| Mean | Min | Max | Std. dev. | Clashes ≤ 200 ms |
|---|---|---|---|---|
| 134.1 ms | 116.2 ms | 158.3 ms | 11.7 ms | 20 / 20 |

The classification took 5.3 ms per clash on the Pico 2.

**Files**

- `TPM_08_clash_response_time.csv` – time, zone, probabilities, peak and classification time per clash
- `TPM_08_clash_response_time.xlsx` – data sheet with summary calculations
- `TPM_06_TPM_08_serial_log.txt` – Serial Monitor output (shared with TPM_06)
- Video: see `../TPM_06_clash_detection_response_time/TPM_06_clash_slow_motion_demo.mp4`
