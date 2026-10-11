# TPM_10 – Deactivation time

**Target:** deactivation time ≤ 1 s

**Method:** The blade was retracted with a long press (800 ms) of the encoder
button. With `TIMING_TEST 10` in `Lightsaber_Main.ino`, the firmware records
`micros()` when the button is pressed and prints the time to the first LED
frame of the retraction animation on the Serial Monitor (115200 baud).

**Results (10 trials):**

| Mean | Min | Max | Std. dev. | Trials ≤ 1 s |
|---|---|---|---|---|
| 819.8 ms | 809.1 ms | 832.3 ms | 8.0 ms | 10 / 10 |

Of this, 800 ms is the deliberate long press; the retraction starts within
9–32 ms after the hold time.

**Files**

- `TPM_10_deactivation_time.csv` – measured times per trial
- `TPM_10_deactivation_time.xlsx` – data sheet with summary calculations
- `TPM_10_serial_log.txt` – Serial Monitor output of the timing lines
- `TPM_10_retraction_demo.mp4` – video of a retraction
