// nn_dirty.h -- Cio' che una mossa cambia per la rete, scritto dalla make del motore (07/10/2026, scacchiera unica v2).
//
// Una sola numerazione: case del MOTORE (a8 = 0 .. h1 = 63, 64 = nessuna casa) e codici del motore (0..11 = P N B R Q K
// p n b r q k). La make scrive qui il pezzo mosso (NnDirtyPiece), su una pila con uno stato per mossa (NnStack); i
// pedoni toccati (NnDirtyPawns) e le tuple di minaccia che cambiano (NnDirtyThreats) li aggiunge il motore prima di una
// valutazione (search/06_nndirty.inc, nn_dirty_catch_up). La rete (nnue/) le legge cosi' come sono. Le tabelle degli
// indici delle feature lavorano direttamente in questa numerazione (vedi nnue/nnue/features/*.h): nessuna conversione
// a runtime.
//
// Il file e' incluso dal motore (threads.cpp, attraverso nnue_bridge.h) e dalla rete (nnue/types.h): solo tipi semplici,
// nessun tipo ne' macro dell'uno o dell'altra. Lo stesso layout in ogni unita' di compilazione: nessun campo dipende
// da macro.

#ifndef NN_DIRTY_H_INCLUDED
#define NN_DIRTY_H_INCLUDED

#include <cstddef>
#include <cstdint>

constexpr int NN_SQ_NONE = 64;  // "nessuna casa" (uguale a no_sq del motore)

// Tipi di pezzo del motore: codice % 6. Il nero e' il bianco + 6.
enum : int { NN_PAWN = 0, NN_KNIGHT = 1, NN_BISHOP = 2, NN_ROOK = 3, NN_QUEEN = 4, NN_KING = 5, NN_BLACK = 6 };

// Pila delle dirty: uno stato per ogni mossa della ricerca dalla radice (le mosse nulle non aggiungono stati: la
// scacchiera non cambia). 247 = MAX_PLY (246) della rete + la radice; la ricerca non supera max_ply + 16 semimosse.
constexpr int NN_STACK_SIZE = 247;

struct NnDirtyPiece {
    std::uint8_t pc;          // pezzo che muove
    std::uint8_t from, to;    // to = NN_SQ_NONE per le promozioni (il pezzo che arriva e' add_pc)
    std::uint8_t remove_pc;   // pezzo catturato, o torre dell'arrocco che parte
    std::uint8_t remove_sq;   // sua casa (en passant: la casa del pedone, non `to`); NN_SQ_NONE se nessuno
    std::uint8_t add_pc;      // pezzo promosso, o torre dell'arrocco che arriva
    std::uint8_t add_sq;      // sua casa; NN_SQ_NONE se nessuno. Arrocco = to e add_sq entrambi presenti.
    std::uint8_t phase;       // fascia di materiale (HalfKA a esperti) della posizione DOPO la mossa
    std::uint8_t phaseChanged;  // 1 se una cattura ha cambiato fascia: refresh dell'accumulatore, come per il re
};

// Una tupla di minaccia in 32 bit: casa dell'attaccante (0-7), casa dell'attaccato (8-15), pezzo attaccato (16-19),
// pezzo attaccante (20-23), 1 = la minaccia compare / 0 = sparisce (31).
constexpr int NN_THR_PCSQ = 0, NN_THR_TSQ = 8, NN_THR_TPC = 16, NN_THR_PC = 20;
inline std::uint32_t nn_threat(int pc, int tpc, int pcSq, int tSq, bool add) {
    return (std::uint32_t(add) << 31) | (std::uint32_t(pc) << NN_THR_PC) | (std::uint32_t(tpc) << NN_THR_TPC)
         | (std::uint32_t(tSq) << NN_THR_TSQ) | std::uint32_t(pcSq);
}

// Un pezzo partecipa al massimo a 8 minacce in uscita e 16 in entrata, e muovendosi ne scopre al massimo 8: una mossa
// che non e' un arrocco ne cambia al massimo (8 + 16) * 3 + 8 = 80, un arrocco 36. Le 16 voci in piu' accolgono le
// scritture vettoriali da 16 tuple oltre il conteggio (emissione AVX-512).
constexpr int NN_THREATS_MAX = 96;
struct NnDirtyThreats {
    std::uint32_t n;
    std::uint32_t list[NN_THREATS_MAX];
};

// I pedoni che una mossa toglie (al massimo 2: pedone per pedone, en passant) e aggiunge (al massimo 1; una promozione
// nessuno), con i bitboard dei pedoni PRIMA della mossa, per i blocchi PawnPair e PassedPawns.
struct NnDirtyPawns {
    std::uint64_t before[2];      // pedoni bianchi, neri prima della mossa
    std::uint8_t  removedSq[2], removedC[2];
    std::uint8_t  addedSq, addedC;  // addedSq = NN_SQ_NONE se nessuno
    std::uint8_t  nRemoved;
    std::uint8_t  any;            // 0 = la mossa non tocca pedoni: niente da fare a valle
};

// Blocchi da innesto opzionali (09/10/2026; nnue/nnue/features/pawn_grafts.h). Resta PassedRel (passati con le
// relazioni: dipende da pedoni, re e occupazione davanti ai passati); KingFiles, Space, LockedPawns, Space24 e
// KingFilesQ tolti il 10/10/2026. Una rete lo accende con il suo formato: nn_graft_mask (bit 0 PassedRel), scritto da
// Network::read_parameters. Con una rete senza blocchi tutto il lavoro si salta: albero e velocita' come prima.
#ifndef TRIUMV_NO_GRAFTS
inline unsigned nn_graft_mask = 0;
#else
// -DTRIUMV_NO_GRAFTS (10/10/2026): blocchi da innesto tolti alla compilazione. La maschera e' una costante 0, quindi
// ogni ramo dei blocchi (recupero, accumulatore, refresh) sparisce e il percorso con una rete senza blocchi e' quello
// di prima dei graft. Una rete con blocchi viene rifiutata al caricamento. Per il rilascio se nessun blocco entra, e
// per le misure di confronto pulite (xperf 10/10: il codice di PassedState spento costava +1,4% di cicli nei finali).
constexpr unsigned nn_graft_mask = 0;
#endif
// Generazione dei pesi della rete (_wip graft_space_locked): Network::read_parameters la incrementa a ogni rete letta.
// La cache "pe" dei blocchi pedoni (update_accumulator_refresh_cache) e' thread_local e la chiave e' fatta di soli
// pedoni e orientazione: su un thread che sopravvive a un cambio di rete (il thread UCI: eval, nnueverify) una entry
// scritta con i pesi di prima darebbe un hit sbagliato, quindi la entry porta anche questa generazione (introdotta
// quando anche Space e LockedPawns, ora tolti, stavano nella cache).
inline unsigned nn_net_epoch = 0;

// Le voci dei blocchi (blocco << 12 | dati, ordinate) che una mossa toglie e aggiunge, uguali per le due prospettive:
// le calcola UNA volta nn_dirty_catch_up confrontando le voci prima e dopo la mossa. Fuori da NnState (in
// NnStack::graft): con i blocchi spenti NnState resta quello di prima (il blocco dentro NnState costava +0,9% di cicli
// anche spento, xperf 09/10/2026).
// Nelle liste della pila c'e' solo PassedRel: al piu' 16 voci (8 passati per colore). NN_GRAFT_REF_MAX = posti dei
// buffer dei controlli -DTRIUMV_VERIFY_GRAFT (riferimento e righe v1 di R1).
// Blocchi nelle liste della pila: bit 0 PassedRel e bit 6 PassedState (PassedPawns v3, 10/10/2026; i due si escludono,
// PawnGrafts::valid_mask). KingFiles, Space, LockedPawns, Space24 e KingFilesQ tolti il 10/10/2026 (non davano Elo).
constexpr unsigned NN_GRAFT_LIST_MASK = 65u;
constexpr unsigned NN_GRAFT_PST       = 64u;  // PassedState: voci colore << 13 | casa << 7 | stato (passed_state.h)
constexpr int      NN_GRAFT_MAX       = 16;
constexpr int      NN_GRAFT_REF_MAX   = 32;
// _wip graftfix (10/10/2026, P2): una sola lista, le tolte in e[0, graftRem) e le aggiunte in e[graftRem, graftRem +
// graftAdd); i due conteggi stanno in NnState (padding), cosi' una diff vuota non tocca questa struttura e una diff
// tipica (2-6 voci) sta in una linea di cache. Al piu' 16 tolte e 16 aggiunte.
struct NnDirtyGraft {
    std::uint16_t e[2 * NN_GRAFT_MAX];
};

struct NnState {
    NnDirtyPiece  dp;            // 9 byte
    std::uint8_t  computed[2];    // accumulatore di questo stato calcolato, per prospettiva (bianco, nero)
    std::uint8_t  idx;            // posizione nella pila (blocchi da innesto, nn_graft_of); nel padding, stessa taglia
    // Voci dei blocchi da innesto tolte e aggiunte dalla mossa (0, 0 = nessuna; sempre 0 con una rete senza blocchi).
    // Le azzera la make (nn_make_dirty), le scrive nn_dirty_catch_up solo se la diff non e' vuota. Nel padding prima
    // di `pawns`: NnState resta di 440 byte (il +0,9% del vecchio codice veniva dalla crescita di NnState).
    std::uint8_t  graftRem, graftAdd;
    NnDirtyPawns  pawns;
    std::uint64_t key;           // -DTRIUMV_VERIFY_NNSYNC: chiave dei pezzi dopo la mossa (controllo di sincronia)
    NnDirtyThreats threats;
};
static_assert(offsetof(NnState, pawns) == 16, "NnState: graftRem/graftAdd devono stare nel padding");

struct NnStack {
    int     size;   // stati presenti; lo stato 0 e' la radice
    int     ready;  // gli stati [ready, size) non hanno ancora minacce e pedoni (nn_dirty_catch_up)
    NnState st[NN_STACK_SIZE];
    NnDirtyGraft graft[NN_STACK_SIZE];  // graft[i] = voci dei blocchi da innesto cambiate dalla mossa dello stato i
    // Le voci di ogni stato, scritte solo quando una mossa le cambia: graftSrc[i] = lo stato (<= i) la cui lista
    // graftList vale anche per i (255 = da calcolare). Cosi' il recupero non ricalcola mai da capo le voci della
    // posizione di partenza (09/10/2026: quel ricalcolo a ogni valutazione costava +5% / +26% di tempo).
    // _wip graftfix: per ogni lista anche la span dei passati (case davanti ai passati, passati compresi).
    std::uint64_t graftSpan[NN_STACK_SIZE];
    // _wip graft_passedrel2 (10/10/2026, docs/audit_8.0/GRAFT_PASSEDREL_COSTO2.md, Q1): per ogni lista anche l'insieme
    // dei passati (bianchi e neri insieme: le case sono distinte). Con la sola PassedRel il recupero decide "niente da
    // fare" confrontando bitboard, senza leggere la lista. Scritto da nn_prel_step e alla radice.
    std::uint64_t graftPass[NN_STACK_SIZE];
    // R4 (10/10/2026, docs/audit_8.0/GRAFT_PASSEDREL_COSTO3.md §4.1): passati bianchi e neri dopo la mossa dello stato i,
    // per PassedPawns v1 con OGNI rete. Li scrive il recupero (nn_dirty_catch_up: due passers() per evento di pedone,
    // copia altrimenti); l'accumulatore prende prima/dopo da [i - 1] e [i] invece di rifare otto passers() per
    // aggiornamento. v1PassW[0] = ~0 = radice da calcolare (AccumulatorStack::reset). Colori separati: un pedone che
    // cattura un passato sulla stessa casa lascia la casa passata ma cambia il colore.
    std::uint64_t v1PassW[NN_STACK_SIZE];
    std::uint64_t v1PassB[NN_STACK_SIZE];
    // (graftKf, firma di KingFiles, e graftCnt, voci per blocco: tolti il 10/10/2026 con KingFiles.)
    std::uint8_t  graftSrc[NN_STACK_SIZE];
    std::uint8_t  graftN[NN_STACK_SIZE];
    std::uint16_t graftList[NN_STACK_SIZE][NN_GRAFT_MAX];
};

// L'accumulatore riceve gli stati per riferimento: i dati dei blocchi da innesto di uno stato si trovano dalla sua
// posizione nella pila (NnState::idx, scritto dalla make, nn_make_dirty; la radice da reset), con aritmetica sugli
// indirizzi: niente thread_local (con MinGW passa da una chiamata, emutls).
inline const NnStack& nn_stack_of(const NnState& s) {
    const NnState* base = &s - s.idx;
    return *reinterpret_cast<const NnStack*>(reinterpret_cast<const char*>(base) - offsetof(NnStack, st));
}
inline const NnDirtyGraft& nn_graft_of(const NnState& s) { return nn_stack_of(s).graft[s.idx]; }

#endif  // NN_DIRTY_H_INCLUDED
