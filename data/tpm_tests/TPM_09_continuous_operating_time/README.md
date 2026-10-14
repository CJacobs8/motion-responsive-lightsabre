# TPM_09 – How long does the battery last?

**Target:** at least 30 min of continuous use on one charge

| LED brightness | Average battery current | Running time (until the BMS cut off) | Met? |
|---|---|---|---|
| 55 % | 2.61 A | 76 min | ✅ |

The raw voltage and current log of this test unfortunately wasn't kept, so only
the summary values above (recorded during the test) are available.

## Predicted discharge curve

To check that 76 minutes makes sense, `TPM_09_predicted_discharge.csv` and `.png`
show a **calculated** discharge curve. **It is not measured data.** It assumes:

- a 2S pack of Samsung INR18650-35E cells (3.4 Ah)
- a constant 2.61 A (the measured average)
- a typical cell voltage vs. depth-of-discharge profile for this cell type
- about 35 mΩ internal resistance per cell
- BMS cut-off at 6.0 V (3.0 V per cell)

With these assumptions, the predicted time to cut-off is about 76 min, in line
with the measurement and with the simple estimate 3.4 Ah ÷ 2.61 A ≈ 78 min.
