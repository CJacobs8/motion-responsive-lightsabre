# TPM_06 – Clash detection response time

**Target:** response time ≤ 100 ms

**Method:** The clash trigger (|a| ≥ 3 g and a jump of ≥ 1 g between samples)
is checked on every 500 Hz MPU6050 sample. With `TIMING_TEST 6` in
`Lightsaber_Main.ino`, the firmware logs for each clash:

- impact → detected: age of the trigger sample when the firmware handles it
  (number of newer samples × 2 ms),
- detected → sound written: until the first clash-sound sample is written to the I2S buffer,
- buffer: audio already queued ahead of it (up to 93 ms),
- impact → speaker: the sum of the three.

**Results (20 clashes, blade on):**

| | Mean | Min | Max | Clashes ≤ 100 ms |
|---|---|---|---|---|
| Impact → clash sound at speaker | 92.2 ms | 73.9 ms | 113.2 ms | 18 / 20 |
| Impact → detected | 9.5 ms | 0 ms | 28 ms | – |

The clash sound is written within 0.3 ms of detection; the I2S buffer
(70–93 ms) is the largest part of the delay. The impact → detected time
varies with how long the main loop takes before it reads the sensor FIFO.

**Files**

- `TPM_06_clash_detection_response_time.csv` – measured times per clash
- `TPM_06_clash_detection_response_time.xlsx` – data sheet with summary calculations
- `TPM_06_TPM_08_serial_log.txt` – Serial Monitor output (shared with TPM_08)
- `TPM_06_clash_slow_motion_demo.mp4` – slow-motion demo of a clash with the clash sound and glow
