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

#include "internals.h"

#include "Tables.h"
#include "mmath.h"

namespace MT32Emu {

// UNUSED: const int MIDDLEC = 60;

const Tables &Tables::getInstance() {
	static const Tables instance;
	return instance;
}

#include "Tables_data.inc"

Tables::Tables() {
	// 5.0.180: 표는 PC에서 원본 계산(Tables_data.inc)으로 미리 만들어 둠 — 본체에는 실수 계산 함수(log·exp·sin)가 없음
	for (int i = 0; i < 101; i++) {
		levelToAmpSubtraction[i] = T_level[i];
		masterVolToAmpSubtraction[i] = T_mvol[i];
		pulseWidth100To255[i] = T_pw[i];
	}
	for (int i = 0; i < 256; i++) envLogarithmicTime[i] = T_env[i];
	for (int i = 0; i < 512; i++) { exp9[i] = T_exp9[i]; logsin9[i] = T_logsin9[i]; }
	// found from sample analysis
	static const Bit8u resAmpDecayFactorTable[] = {31, 16, 12, 8, 5, 3, 2, 1};
	resAmpDecayFactors = resAmpDecayFactorTable;
}

} // namespace MT32Emu
