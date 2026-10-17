# MATLAB scripts

These scripts record the clash dataset and train, validate, test and export the
clash-location classifier. The model is a plain-MATLAB L2-regularised multinomial
logistic regression (no toolbox needed for training); `confusionchart`, used only for
the figures, needs the Statistics and Machine Learning Toolbox.

## Scripts you run

| File | What it does |
|---|---|
| `ClashPipeline.m` | **The main script.** Loads `data/clash_dataset`, checks that strike strength alone cannot predict the zone, sweeps the window lengths P and Q and the penalty λ with leave-one-session-out cross-validation, trains the final model on the five training sessions, tests it once on S04 and V03, saves the figures to `results/` and exports `firmware/Lightsaber_Main/ClashModel.h`. |
| `CaptureClashes.m` | Records clashes sent by `firmware/ClashLogger` over USB and saves them to `data/clash_dataset/<SESSION>/`. Set `PORT` and `SESSION` at the top first. |

## Functions used by the pipeline

| File | What it does |
|---|---|
| `loadClashDataset.m` | Reads the clash CSV files and aligns every clash on its acceleration peak. |
| `buildFeatureMatrix.m` | Calculates the feature matrix for all clashes for a given window (P samples before, Q after the peak). |
| `clashFeatures.m` | Calculates the 74 features of one clash window. Uses exactly the same maths as `ClashClassifier.h` in the firmware. |
| `clashFeatureNames.m` | Names of the 74 features (8 signals × 9 features, plus 2 rotation-versus-translation features). |
| `welchSpectrum.m` | Averaged spectrum of a short signal: 16-sample frames with 50 % overlap, Hamming window, zero-padded to 64 points. |
| `trainSoftmax.m` | Trains the multinomial logistic regression with an L2 penalty λ (gradient descent on standardised features). |
| `predictSoftmax.m` | Predicts the zone and its probabilities with a trained model. |
| `crossValidateSoftmax.m` | Out-of-fold predictions for a given grouping (leave-one-session-out in the pipeline). |
| `stratifiedFolds.m` | Creates class-balanced folds (used for the strength-only sanity check). |
| `clashMetrics.m` | Accuracy, per-zone precision, recall and F1, macro F1 and the confusion matrix. |
| `exportClashModel.m` | Writes the trained model to `ClashModel.h` for the firmware. |

## Outputs (`results/`)

Confusion matrices for cross-validation and the test sessions, the mean clash response
and spectrum per zone, the window-length sweep, feature importance, prediction confidence
and `test_predictions.csv` (one row per test clash).
