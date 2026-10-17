# TPM_07 – Live clash test

The real test of the classifier: no recorded data, just the finished lightsabre
running the final firmware (`firmware/Lightsaber_Main`, model in `ClashModel.h`).
For each clash I was given a zone and a strength, hit that part of the blade, and
wrote down where the clash glow appeared.

| Blade held | Clashes | Correct | Accuracy |
|---|---|---|---|
| Horizontal | 29 | 27 | 93.1 % |
| Vertical | 30 | 27 | 90.0 % |
| **Total** | **59** | **54** | **91.5 %** (95 % CI 81.6–96.3 %) |

The 95 % confidence interval is a Wilson score interval for a binomial proportion (each clash is either correct or not), not a bootstrap.

That's comfortably above the 80 % target. On version 2 of the sabre, the same
classifier got 57 of 59 right (96.6 %).

<p align="center"><a href="TPM_07_clash_location_demo.mp4">Watch the demo video</a></p>

**Files:** `live_test_results.csv` (one row per clash: number, orientation, zone hit,
strength, zone shown, correct) · `live_test_log.xlsx` (original log with summary) ·
`TPM_07_clash_location_demo.mp4`

*Notes:* clash 9 is logged as incorrect even though the zone shown is recorded as
middle, so it's counted as incorrect here. Clash 6 wasn't recorded.
