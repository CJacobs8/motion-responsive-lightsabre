# Firmware

There are two Arduino sketches for the Raspberry Pi Pico 2. Both are built with the
Arduino IDE and the Raspberry Pi Pico/RP2040/RP2350 board package. Every function has a
short `/** ... */` comment above it describing what it does.

## `Lightsaber_Main/` – the lightsabre

| File | What it does |
|---|---|
| `Lightsaber_Main.ino` | The complete lightsabre: LED animations (ignition, idle, swing, clash glow, retraction), audio streaming and mixing (hum, SmoothSwing-style swing, clash, power sounds), motion and spin detection, the OLED menu and the rotary encoder. `setup()` initialises everything and `loop()` is the main control loop. |
| `ClashClassifier.h` | Clash detection and location. Reads the MPU6050 FIFO at 500 Hz into a ring buffer, triggers on a sudden acceleration rise, extracts the window around the peak, calculates the 74 features and runs the logistic regression model. Shared with `ClashLogger` so both use identical maths. |
| `ClashModel.h` | The trained classifier, exported by `matlab/ClashPipeline.m`: window lengths, feature means and standard deviations, weights and offsets. Replace this file to update the model. |
| `LiveTest.h` | The live clash test (TPM_07). Enabled with `#define LIVE_TEST 1`; it prompts a zone and strength for each clash over the Serial Monitor and logs whether the glow appeared in the right zone. |

**Useful switches at the top of `Lightsaber_Main.ino`**

- `LIVE_TEST` – `1` runs the live clash test, `0` is the normal lightsabre.
- `TIMING_TEST` – `0` is the normal lightsabre; `1`, `3`, `5`, `6` or `10` log the response
  time of TPM_01, 03, 05, 06/08 or 10 to the Serial Monitor (see `data/tpm_tests`).
- `NUM_LEDS`, `LED_BRIGHTNESS` and the pin definitions, if your wiring is different.

The sound files are read from the microSD card: `hum.wav`, `ignite.wav`, `poweroff.wav`,
`clash1.wav`, `swingL4.wav` and `swingH4.wav` (16-bit mono WAV, 22.05 kHz).

## `ClashLogger/` – recording training data

| File | What it does |
|---|---|
| `ClashLogger.ino` | Records labelled clashes. It lights the zone to strike on the blade, detects each clash with the same trigger as the lightsabre and sends a 256-sample window (64 before and 192 after the peak) with its label over serial to `matlab/CaptureClashes.m`. Prompts come in shuffled blocks of 36 so every zone, strength and blade face is covered equally. |

## Updating the classifier

1. Record new sessions with `ClashLogger` and `matlab/CaptureClashes.m`.
2. Run `matlab/ClashPipeline.m`, which writes a new `ClashModel.h`.
3. Copy it into `Lightsaber_Main/` and upload the sketch again.
