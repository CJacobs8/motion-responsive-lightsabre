# Motion-responsive lightsabre

Final-year project (Mechatronics Engineering, Stellenbosch University) – Cayla Jacobs.

A lightsabre prototype built around a Raspberry Pi Pico 2 with an MPU6050
motion sensor, addressable LED blade, I²S audio and an OLED menu. Swings drive
SmoothSwing audio and blade effects, and a clash classifier identifies where on
the blade (bottom, middle or top) a clash occurred so that the clash effect is
shown in that zone.

## Repository structure

| Folder | Contents |
|---|---|
| `firmware/Lightsaber_Main/` | Main lightsabre firmware (Arduino, Pico 2). `ClashModel.h` is the trained classifier exported from MATLAB. |
| `firmware/ClashLogger/` | Data-logging firmware used to record labelled clashes. |
| `matlab/` | Data capture (`CaptureClashes.m`) and the training/validation/export pipeline (`ClashPipeline.m`) with helper functions. |
| `data/clash_dataset/` | 605 labelled clashes (training sessions S01–S03, V01–V02; test sessions S04, V03). See its README. |
| `data/live_test/` | Live test of the classifier on the lightsabre (59 clashes). |
| `data/tpm_tests/` | Technical performance measure (TPM) test data. |
| `data/blade_strength/` | Blade joint strength test data. |
| `calculations/` | SMath Studio worksheet with the complete design calculations (Appendix D). |
| `results/` | Figures and test predictions produced by `ClashPipeline.m`. |

## Clash-location classifier

- MPU6050 at 500 Hz (±16 g, ±1000 °/s), clash detected when |a| ≥ 3 g and rises by ≥ 1 g between samples.
- Classification window: 8 samples before and 48 after the acceleration peak (112 ms); gravity removed using the pre-impact acceleration.
- 74 features: time-domain statistics and Welch-spectrum band energies (16-sample Hamming frames, 50 % overlap, zero-padded to 64 points) of 8 signals, plus 2 rotation/translation features.
- Multinomial logistic regression with L2 penalty (λ = 0.01).

| Evaluation | Clashes | Accuracy |
|---|---|---|
| Leave-one-session-out cross-validation | 534 | 85.4 % |
| Independent test sessions (S04 + V03) | 71 | 97.2 % |
| Live test on the lightsabre | 59 | 91.5 % |

## Reproducing the results

1. Open MATLAB in the `matlab/` folder and run `ClashPipeline.m`. It reads
   `data/clash_dataset`, writes the figures to `results/` and exports `ClashModel.h`.
2. Copy `results/ClashModel.h` to `firmware/Lightsaber_Main/` and upload
   `Lightsaber_Main.ino` to the Pico 2 (Arduino IDE, Raspberry Pi Pico/RP2040/RP2350 core).

To record new clashes, upload `firmware/ClashLogger/ClashLogger.ino`, set
`PORT` and `SESSION` in `matlab/CaptureClashes.m` and run it.

## Hardware and libraries

Raspberry Pi Pico 2, MPU6050, MAX98357A I²S amplifier, microSD module,
WS2812B LED strip, SSD1306 OLED and rotary encoder.
Arduino libraries: FastLED, Adafruit GFX, Adafruit SSD1306 (SD, Wire and I2S
are part of the Pico core). The sound files (hum, swing, clash, ignite and
power-off WAV, 16-bit mono 22.05 kHz) are stored on the microSD card and are
not included in this repository.
