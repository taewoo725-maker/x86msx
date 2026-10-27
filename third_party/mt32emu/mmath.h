/* Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009 Dean Beeler, Jerome Fisher
 * Copyright (C) 2011-2022 Dean Beeler, Jerome Fisher, Sergey V. Mikayev
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MT32EMU_MMATH_H
#define MT32EMU_MMATH_H



extern "C" { double sin(double); double cos(double); double fmod(double, double); }   // LA32FloatWaveGenerator (실수 방식, 쓰이지 않음)

namespace MT32Emu {

// Mathematical constants
const double DOUBLE_PI = 3.141592653589793;
const double DOUBLE_LN_10 = 2.302585092994046;
const float FLOAT_PI = 3.1415927f;
const float FLOAT_2PI = 6.2831853f;
const float FLOAT_LN_2 = 0.6931472f;
const float FLOAT_LN_10 = 2.3025851f;

// 5.0.180: 본체에는 libm 이 없어 필요한 만큼만 직접 계산 (EXP2F 는 마스터 튜닝 계산에만 쓰임)
static inline float EXP2F(float x) {
	int n = int(x);
	if (float(n) > x) n--;
	double f = double(x) - n, t = 1.0, s = 1.0;
	const double k = 0.6931471805599453 * f;
	for (int i = 1; i < 14; i++) { t *= k / i; s += t; }
	while (n > 0) { s *= 2.0; n--; }
	while (n < 0) { s *= 0.5; n++; }
	return float(s);
}

} // namespace MT32Emu

#endif // #ifndef MT32EMU_MMATH_H
