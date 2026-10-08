/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

//Definition of input features HalfKAv2_hm of NNUE evaluation function

// 07/10/2026 (scacchiera unica v2): make_index, append_changed_indices e requires_refresh sono nell'header, inline,
// nella numerazione del motore. Il percorso vettoriale ICL (write_indices, solo per TRIUMV_PSQ_PHASES == 1) e' stato
// tolto: leggeva la mailbox in forma rete della finny table, che ora tiene i dodici bitboard per pezzo.
// Il file resta per non toccare le liste di build e per il controllo qui sotto.

#include "half_ka_v2_hm.h"

namespace Triumviratus::Eval::NNUE::Features {

// make_index usa OrientTBL con la casa del re nella NOSTRA numerazione, mentre la tabella e' scritta per quella della
// rete (traversa riflessa): e' lecito solo se OrientTBL dipende dalla sola colonna.
constexpr bool orient_by_file_only() {
    for (int s = 0; s < SQUARE_NB; ++s)
        if (HalfKAv2_hm::OrientTBL[s] != HalfKAv2_hm::OrientTBL[s ^ 56])
            return false;
    return true;
}
static_assert(orient_by_file_only(), "HalfKAv2_hm::OrientTBL deve dipendere solo dalla colonna");

}  // namespace Triumviratus::Eval::NNUE::Features
