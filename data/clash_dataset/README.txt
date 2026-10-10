CLASH LOCATION DATASET - Motion-responsive lightsabre (Cayla Jacobs)
=====================================================================

605 labelled clashes recorded with the MPU6050 in the lightsabre hilt,
used to train and test the clash-location classifier (Section 6.4).

SENSOR SETTINGS
  MPU6050, 500 Hz (FIFO), accelerometer +/-16 g, gyroscope +/-1000 deg/s,
  digital low-pass filter 184 Hz.

SESSIONS (folder data/clash_dataset)
  Session  Blade orientation  Use        Clashes
  S01      Horizontal         Training   105
  S02      Horizontal         Training   107
  S03      Horizontal         Training   106
  S04      Horizontal         Test        36
  V01      Vertical           Training   108
  V02      Vertical           Training   108
  V03      Vertical           Test        35
  Zones: BOTTOM / MIDDLE / TOP (three equal 307 mm zones of the blade).
  Each block of 36 clashes contains every zone x strength x face
  combination once, in random order.

FILES
  all_clashes_summary.csv   One row per clash (all sessions):
      session, orientation, set (Training/Test), id,
      zone            label: zone that was struck
      strength        LIGHT / MEDIUM / HARD (prompted)
      face            FRONT / BACK / LEFT / RIGHT (prompted blade face)
      peak_g          peak acceleration magnitude (g)
      jerk_g          largest sample-to-sample rise of |a| at the trigger (g)
      pre_gyro_dps    angular speed just before the impact (deg/s)
      fs_hz           measured sample rate
      overflows       FIFO overflow counter (logging diagnostics)
      file            raw data file, relative to data/clash_dataset
      time            time of recording

  <session>/<ZONE>_<session>_<id>.csv
      Raw window of one clash, 256 samples at 500 Hz:
      sample = -64 ... +191 (0 = acceleration peak)
      ax_g, ay_g, az_g      acceleration (g)
      gx_dps, gy_dps, gz_dps angular velocity (deg/s)

  <session>/clash_log.csv
      Per-session log (same content as the summary file).

NOTES
  - One clash in S01-S04 and V02 shows a ~454 Hz rate reading caused by a
    FIFO reset during logging; the samples themselves are sensor-clocked.
  - Final classifier: 74 FFT and time-domain features from a window of
    8 samples before and 16 after the peak, gravity removed, multinomial
    logistic regression (lambda = 0.01). Leave-one-session-out CV 82.2 %,
    independent test (S04 + V03) 93.0 %.
