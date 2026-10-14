# TPM_08 – How fast does the right zone light up?

**Target:** ≤ 200 ms from the impact to the clash glow appearing in the classified zone

This was measured in the same 20 clashes as TPM_06. The time includes everything:
waiting for the acceleration peak, collecting the 48 samples after it, running the
classifier and drawing the next LED frame.

| Mean | Min | Max | Std. dev. | ≤ 200 ms |
|---|---|---|---|---|
| 134.1 ms | 116.2 ms | 158.3 ms | 11.7 ms | 20 / 20 |

The classifier itself only takes **5.3 ms** on the Pico 2. Most of the time is
spent waiting for enough of the blade vibration to arrive.

**Files:** `TPM_08_clash_response_time.csv` (time, zone, probabilities and classification time
per clash) · `TPM_08_clash_response_time.xlsx` · `TPM_06_TPM_08_serial_log.txt` ·
video: [`TPM_06_clash_slow_motion_demo.mp4`](../TPM_06_clash_detection_response_time/TPM_06_clash_slow_motion_demo.mp4)
