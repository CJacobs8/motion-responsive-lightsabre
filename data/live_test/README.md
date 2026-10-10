# Live test of the clash-location classifier

Manual live test on the assembled lightsabre running the final firmware
(`firmware/Lightsaber_Main`, model in `ClashModel.h`).

For each clash, the prompted zone was struck with the given strength and the
zone in which the clash glow appeared was recorded.

| Orientation | Clashes | Correct | Accuracy |
|---|---|---|---|
| Horizontal | 29 | 27 | 93.1 % |
| Vertical | 30 | 27 | 90.0 % |
| **Total** | **59** | **54** | **91.5 %** (95 % CI 81.6–96.3 %) |

Files
- `live_test_results.csv` – one row per clash: clash number, orientation,
  zone struck, strength, zone shown on the blade, correct (Y/N).
- `live_test_log.xlsx` – original log with the summary sheet.

Note: clash 9 is logged as incorrect although the zone shown is recorded as Middle; it is counted as incorrect above. Clash 6 was not recorded.
