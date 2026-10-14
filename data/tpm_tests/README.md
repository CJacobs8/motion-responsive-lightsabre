# ⏱️ Performance tests

Every response time here was measured by the firmware itself. Each test has a
`TIMING_TEST` switch in `Lightsaber_Main.ino` that logs `micros()` timestamps to the
Serial Monitor, so there's no guessing from video. The demo videos are just there so
you can see what's being measured.

| | Test | Target | Result | |
|---|---|---|---|---|
| 01 | [Activation time](TPM_01_activation_time/) | ≤ 1 s | 0.94 s (9/10) | 🟡 |
| 02 | Blade illumination | ≥ 60 % | stable 5 V rail at every setting | 🟢 |
| 03 | [Motion detection](TPM_03_motion_response_time/) | ≤ 100 ms | 60 ms (18/20) | 🟡 |
| 04 | [Motion classification](../clash_dataset/) | ≥ 80 % | 85.4 % CV, 97.2 % test | 🟢 |
| 05 | [Swing → sound](TPM_05_motion_audio_response_time/) | ≤ 200 ms | 139 ms | 🟢 |
| 06 | [Clash detection](TPM_06_clash_detection_response_time/) | ≤ 100 ms | 92 ms (18/20) | 🟡 |
| 07 | [Clash location (live)](../live_test/) | ≥ 80 % | 91.5 % | 🟢 |
| 08 | [Clash → glow](TPM_08_clash_response_time/) | ≤ 200 ms | 134 ms | 🟢 |
| 09 | [Battery life](TPM_09_continuous_operating_time/) | ≥ 30 min | 76 min | 🟢 |
| 10 | [Deactivation time](TPM_10_deactivation_time/) | ≤ 1 s | 0.82 s | 🟢 |

🟢 met · 🟡 partially met (most but not all trials within the target)

Each folder has the raw serial log, a CSV, a spreadsheet with the summary and, where
it made sense, a short video.
