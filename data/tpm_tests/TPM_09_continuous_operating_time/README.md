# TPM_09 – Continuous Operating Time

**Target:** ≥ 30 min continuous operation from a fully charged battery.

## Measured result

| Quantity | Value |
|---|---|
| LED brightness setting | 55 % |
| Average battery current | 2.61 A |
| Operating time (to BMS cut-off) | 76 min |
| Target met | Yes |

The raw voltage/current log of this test was not retained; the summary values
above were recorded during the test.

## Predicted discharge curve (theoretical – not measured data)

`TPM_09_predicted_discharge.csv` and `TPM_09_predicted_discharge.png` show a
**calculated** discharge curve for comparison with the measured operating time.
It is not a recording of the test.

Assumptions:

- 2S pack of Samsung INR18650-35E cells, 3.4 Ah rated capacity
- Constant battery current of 2.61 A (the measured average)
- Typical per-cell voltage vs depth-of-discharge profile for this cell type
  (approximate, read from typical Li-ion discharge characteristics)
- Internal resistance of about 35 mΩ per cell
- BMS cut-off at 6.0 V (3.0 V per cell)

With these assumptions the predicted time to cut-off is about 76 min, consistent
with the measurement and with Equation (7) (3.4 Ah / 2.61 A ≈ 78 min).
