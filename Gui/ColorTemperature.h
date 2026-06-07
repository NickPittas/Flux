/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Natron <https://natrongithub.github.io/>,
 * (C) 2018-2023 The Natron developers
 * (C) 2013-2018 INRIA and Alexandre Gauthier-Foichat
 *
 * Natron is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Natron is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Natron.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

#ifndef Gui_ColorTemperature_h
#define Gui_ColorTemperature_h

// Nuke TMI (Temperature, Magenta/Green, Intensity) color adjustment.
//
// These are the exact gain formulas reverse-engineered from Nuke's color picker.
// Applied to a base color in LINEAR space — no clamping, negatives and
// super-whites are valid and expected.
//
// Temperature (T) range [-1, 1]:
//   T < 0 = warm (orange), T > 0 = cool (blue)
//
// Tint (M) range [-1, 1]:
//   M < 0 = green shift, M > 0 = magenta shift
//
// Derived gain formulas (verified against 5 known Nuke data points):
//
//   R_gain = 1 - T/2 + M - 2M²/3 - TM/3
//   G_gain = 1 - 2M/3 + 2TM/3
//   B_gain = 1 + T/2 + M/3 - TM/3
//
// Reference values (base = white 1,1,1):
//   T=0,  M=0:  R=1,     G=1,     B=1
//   T=-1, M=0:  R=1.5,   G=1,     B=0.5
//   T=1,  M=1:  R=0.5,   G=1,     B=1.5
//   T=0,  M=1:  R=4/3,   G=1/3,   B=4/3
//   T=0,  M=-1: R=-2/3,  G=5/3,   B=2/3

namespace Natron {

// Apply Temperature + Tint gains to a base color (linear space, no clamping).
inline void temperatureTintToRgb(double r, double g, double b,
                                 double temp, double tint,
                                 double* outR, double* outG, double* outB)
{
    double T = temp;
    double M = tint;

    double rGain = 1.0 - T * 0.5 + M - 2.0 * M * M / 3.0 - T * M / 3.0;
    double gGain = 1.0 - 2.0 * M / 3.0 + 2.0 * T * M / 3.0;
    double bGain = 1.0 + T * 0.5 + M / 3.0 - T * M / 3.0;

    *outR = r * rGain;
    *outG = g * gGain;
    *outB = b * bGain;
}

// Generate a display color for slider track visualization.
// Produces what neutral grey (0.5, 0.5, 0.5) becomes at given T, M.
// Clamped here only for display purposes (QColor can't represent negatives).
inline void temperatureToRgb(double temp, double tint,
                             double* r, double* g, double* b)
{
    temperatureTintToRgb(0.5, 0.5, 0.5, temp, tint, r, g, b);

    // Clamp only for QColor display
    if (*r < 0.0) *r = 0.0; else if (*r > 1.0) *r = 1.0;
    if (*g < 0.0) *g = 0.0; else if (*g > 1.0) *g = 1.0;
    if (*b < 0.0) *b = 0.0; else if (*b > 1.0) *b = 1.0;
}

} // namespace Natron

#endif // Gui_ColorTemperature_h
