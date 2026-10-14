# TPM_10 – How fast does it switch off?

**Target:** ≤ 1 s from pressing the button to the blade starting to retract

Same idea as TPM_01, just in reverse: a long press (800 ms) retracts the blade, and
with `TIMING_TEST 10` the firmware times the press to the first frame of the
retraction animation, 10 times.

| Mean | Min | Max | Std. dev. | ≤ 1 s |
|---|---|---|---|---|
| 819.8 ms | 809.1 ms | 832.3 ms | 8.0 ms | 10 / 10 |

800 ms of that is the deliberate hold, so the retraction actually kicks off within
9–32 ms of the hold finishing.

**Files:** `TPM_10_deactivation_time.csv` · `TPM_10_deactivation_time.xlsx` ·
`TPM_10_serial_log.txt` · `TPM_10_retraction_demo.mp4`
