# TPM_01 – Activation time

**Target:** activation time ≤ 1 s

**Method:** The blade was ignited 10 times with a long press (800 ms) of the
encoder button. With `TIMING_TEST 1` in `Lightsaber_Main.ino`, the firmware
records `micros()` when the button is pressed and prints the time to the first
ignition LED frame on the Serial Monitor (115200 baud). A video of an ignition
is included as a visual check.

**Results (10 trials):**

| | All trials | Trials 2–10 |
|---|---|---|
| Mean | 943.3 ms | 871.1 ms |
| Min | 809.0 ms | 809.0 ms |
| Max | 1592.8 ms | 956.6 ms |
| Std. dev. | 234.9 ms | 58.8 ms |
| Trials ≤ 1 s | 9 / 10 | 9 / 9 |

Trial 1 was the first ignition after power-up.

**Files**

- `TPM_01_activation_time.csv` – measured times per trial
- `TPM_01_activation_time.xlsx` – data sheet with summary calculations
- `TPM_01_serial_log.txt` – Serial Monitor output of the timing lines
- `TPM_01_ignition_video.mp4` – video of an ignition
