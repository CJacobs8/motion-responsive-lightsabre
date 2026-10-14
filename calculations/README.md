# 🧮 Design calculations

Before anything was soldered or printed, I worked through the numbers to make sure
the lightsabre would actually work: that the battery could supply the LEDs, that
the converter wouldn't overheat and that the blade mount wouldn't snap off on the
first hard clash.

**[`Lightsabre_Design_Calculations.pdf`](Lightsabre_Design_Calculations.pdf)** is the
full worksheet (made in SMath Studio). It's summarised in Appendix D of the report
and Section 4.6 (Detailed System Design).

What's inside:

- ⚡ **Electronics** – LED data timing, controller processing capacity and sensor data transfer
- 🔋 **Power** – power demand and the 58 % brightness limit, buck converter duty cycle,
  battery sizing, runtime and charging
- 🔌 **Wiring** – wire sizes and connector and terminal ratings
- 🏗️ **Structure** – grip reactions, shear force, bending moment, stress and deflection of
  the blade, and the strength of the epoxy blade mount
- 🌡️ **Heat** – LED heat in the blade, converter heat and hilt ventilation

The most useful result turned out to be the bond calculation: it predicted that the
blade mount's flange bond would fail at a tip force of about 29 N, and sure enough,
that's the joint that broke during testing.
