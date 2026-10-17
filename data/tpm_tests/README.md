# Performance tests

Every response time here was measured by the firmware itself. Each test has a
`TIMING_TEST` switch in `Lightsaber_Main.ino` that logs `micros()` timestamps to the
Serial Monitor, so there's no guessing from video. The demo videos are just there so
you can see what's being measured.

| | Test | Target | Result | Status |
|---|---|---|---|---|
| 01 | [Activation time](TPM_01_activation_time/) | ≤ 1 s | 0.94 s (9/10) | Partially met |
| 02 | Blade illumination* | ≥ 60 % | stable 5 V rail at every setting | Met |
| 03 | [Motion detection](TPM_03_motion_response_time/) | ≤ 100 ms | 60 ms (18/20) | Partially met |
| 04 | [Motion classification](../clash_dataset/)† | ≥ 80 % | 85.4 % CV, 97.2 % test | Met |
| 05 | [Swing → sound](TPM_05_motion_audio_response_time/) | ≤ 200 ms | 139 ms | Met |
| 06 | [Clash detection](TPM_06_clash_detection_response_time/) | ≤ 100 ms | 92 ms (18/20) | Partially met |
| 07 | [Clash location (live)](../live_test/) | ≥ 80 % | 91.5 % | Met |
| 08 | [Clash → glow](TPM_08_clash_response_time/) | ≤ 200 ms | 134 ms | Met |
| 09 | [Battery life](TPM_09_continuous_operating_time/) | ≥ 30 min | ≈ 300 min | Met |
| 10 | [Deactivation time](TPM_10_deactivation_time/) | ≤ 1 s | 0.82 s | Met |

"Partially met" means most, but not all, trials were within the target.

\* TPM_02 has no folder because it was not a logged test: the brightness was stepped from 25 % to 60 % in the menu while the 5 V rail was watched on an oscilloscope, and the rail stayed stable at about 5.03 V. The oscilloscope capture is Figure G1 in Appendix G of the report.

† TPM_04 links to the clash dataset because that is where the result comes from: the classifier was cross-validated on the five training sessions (534 clashes) and tested once on the independent sessions S04 and V03 (71 clashes). The figures and per-clash predictions are in [`results`](../../results/).

Each folder has the raw serial log, a CSV, a spreadsheet with the summary and, where
it made sense, a short video.
