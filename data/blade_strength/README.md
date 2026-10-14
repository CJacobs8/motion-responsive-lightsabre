# 💥 Blade strength

There was no separate "smash it until it breaks" test. The blade's strength was put
to the test simply by recording the clash dataset: hundreds of light, medium and hard
hits on a broomstick.

| | Version 1 (Heavy Grade blade) | Version 2 (Standard Grade blade) |
|---|---|---|
| Clashes | 60 | 605 |
| Mean peak acceleration | > 10 g | 8.9 g |
| Highest peak acceleration | – | 18.9 g |
| Joint failure? | Yes, after 60 clashes | No (not tested to failure) |

The version 1 joint failed exactly where the calculations said it would: the epoxy
flange bond at the blade mount (predicted limit about 29 N tip force). The lighter
blade in version 2 puts less inertial load on that joint, and it got through the
whole dataset without a problem.

The clash recordings themselves are in [`../clash_dataset`](../clash_dataset/), with
the peak acceleration of every clash in `all_clashes_summary.csv`.
