#include "defs.h"
#include "chess960.h"
#include "attacks.h" // A3: pawn_attacks, per il filtro e.p. fantasma
#include "io.h"
#include "movegen.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <string>

// print bitboard
void print_bitboard(U64 bitboard)
{
    printf("\n");

    for (int rank = 0; rank < 8; rank++)
    {
        for (int file = 0; file < 8; file++)
        {
            int square = rank * 8 + file;
            if (!file)
                printf("  %d ", 8 - rank);
            printf(" %d", get_bit(bitboard, square) ? 1 : 0);
        }
        printf("\n");
    }

    printf("\n     a b c d e f g h\n\n");
    printf("     Bitboard: %llud\n\n", bitboard);
}

// print board
void print_board()
{
    printf("\n");

    for (int rank = 0; rank < 8; rank++)
    {
        for (int file = 0; file < 8; file++)
        {
            int square = rank * 8 + file;
            if (!file)
                printf("  %d ", 8 - rank);

            int piece = -1;

            for (int bb_piece = P; bb_piece <= k; bb_piece++)
            {
                if (get_bit(bitboards[bb_piece], square))
                    piece = bb_piece;
            }

            printf(" %c", (piece == -1) ? '.' : ascii_pieces[piece]);
        }
        printf("\n");
    }

    printf("\n     a b c d e f g h\n\n");
    printf("     Side:     %s\n", !side ? "white" : "black");
    printf("     Enpassant:   %s\n", (enpassant != no_sq) ? square_to_coordinates[enpassant] : "no");
    printf("     Castling:  %c%c%c%c\n\n", (castle & wk) ? 'K' : '-',
        (castle & wq) ? 'Q' : '-',
        (castle & bk) ? 'k' : '-',
        (castle & bq) ? 'q' : '-');
    if (g_chess960)
    {
        // Case lette dalla FEN (vedi chess960.h): re bianco/nero, torri per K Q k q.
        printf("     Chess960:  re %s/%s, torri", g_c960.king_from[0] != no_sq ? square_to_coordinates[g_c960.king_from[0]] : "-",
               g_c960.king_from[1] != no_sq ? square_to_coordinates[g_c960.king_from[1]] : "-");
        for (int i = 0; i < 4; i++)
            printf(" %s", g_c960.rook_from[i] != no_sq ? square_to_coordinates[g_c960.rook_from[i]] : "-");
        printf("\n\n");
    }
    printf("     Hash key:  %llx\n", hash_key);
    printf("     Fifty move: %d\n\n", fifty);
}

// Build a FEN string for the current global board state. Ranks are emitted
// 8->1 (square 0 = a8, matching print_board). The fullmove number is not
// tracked by the engine, so a placeholder "1" is used (irrelevant for the
// position itself / for training).
std::string board_to_fen()
{
    std::string fen;
    for (int rank = 0; rank < 8; rank++)
    {
        int empty = 0;
        for (int file = 0; file < 8; file++)
        {
            int square = rank * 8 + file;
            int piece = -1;
            for (int bb = P; bb <= k; bb++)
                if (get_bit(bitboards[bb], square)) { piece = bb; break; }

            if (piece == -1)
                empty++;
            else
            {
                if (empty) { fen += char('0' + empty); empty = 0; }
                fen += ascii_pieces[piece];
            }
        }
        if (empty) fen += char('0' + empty);
        if (rank < 7) fen += '/';
    }

    fen += ' ';
    fen += (side == white) ? 'w' : 'b';

    fen += ' ';
    std::string cr;
    if (g_chess960)
    {
        // Shredder-FEN: la colonna della torre (KQkq sarebbe ambiguo con due torri dallo stesso lato)
        const char base[4] = {'A', 'A', 'a', 'a'};
        for (int i = 0; i < 4; i++)
            if (castle & c960_bit(i)) cr += char(base[i] + (castle_rook_sq[i] & 7));
    }
    else
    {
        if (castle & wk) cr += 'K';
        if (castle & wq) cr += 'Q';
        if (castle & bk) cr += 'k';
        if (castle & bq) cr += 'q';
    }
    fen += cr.empty() ? "-" : cr.c_str();

    fen += ' ';
    fen += (enpassant != no_sq) ? square_to_coordinates[enpassant] : "-";

    fen += ' ';
    fen += std::to_string(fifty);
    fen += " 1";   // fullmove placeholder
    return fen;
}

// Serialize a move to UCI long-algebraic notation ("e2e4", "e7e8q").
std::string move_to_uci(int move)
{
    std::string s;
    s += square_to_coordinates[get_move_source(move)];
    s += square_to_coordinates[get_move_target(move)];
    int promo = get_move_promoted(move);
    if (promo) s += char(tolower(ascii_pieces[promo]));
    return s;
}

// reset board variables
void reset_board()
{
    memset(bitboards, 0ULL, sizeof(bitboards));
    memset(occupancies, 0ULL, sizeof(occupancies));
    side = 0;
    enpassant = no_sq;
    castle = 0;
    repetition_index = 0;
    fifty = 0;
    memset(repetition_table, 0ULL, sizeof(repetition_table));
}

// Ply di partita della posizione data (dal numero di mossa della FEN), per la gestione del tempo (TmFenPly).
int g_fen_ply = 0;

// parse FEN string - IMPROVED to parse halfmove clock
void parse_fen(const char* fen)
{
    reset_board();
    g_fen_ply = 0;

    for (int rank = 0; rank < 8; rank++)
    {
        for (int file = 0; file < 8; file++)
        {
            int square = rank * 8 + file;

            if ((*fen >= 'a' && *fen <= 'z') || (*fen >= 'A' && *fen <= 'Z'))
            {
                int piece = mapCharToPiece(*fen);
                set_bit(bitboards[piece], square);
                fen++;
            }

            if (*fen >= '0' && *fen <= '9')
            {
                int offset = *fen - '0';
                int piece = -1;

                for (int bb_piece = P; bb_piece <= k; bb_piece++)
                {
                    if (get_bit(bitboards[bb_piece], square))
                        piece = bb_piece;
                }

                if (piece == -1)
                    file--;

                file += offset;
                fen++;
            }

            if (*fen == '/')
                fen++;
        }
    }

    // Skip space and parse side to move
    fen++;
    (*fen == 'w') ? (side = white) : (side = black);
    fen += 2;

    // Parse castling rights
    // Chess960 (chess960.h): X-FEN o Shredder-FEN, case di re e torri lette dalla scacchiera e portate nel
    // motore. Negli scacchi normali si torna alle case di sempre (h1/a1/h8/a8, maschera fissa).
    if (g_chess960)
    {
        const char* field = fen;
        while (*fen != ' ' && *fen) fen++;
        c960_parse_castling(field, fen);
        castle = g_c960.rights;
        c960_apply_to_engine();
    }
    else
    {
        c960_reset_standard();
        while (*fen != ' ')
        {
            switch (*fen)
            {
            case 'K': castle |= wk; break;
            case 'Q': castle |= wq; break;
            case 'k': castle |= bk; break;
            case 'q': castle |= bq; break;
            case '-': break;
            }
            fen++;
        }
    }

    // Skip space and parse en passant square
    fen++;

    if (*fen != '-')
    {
        int file = fen[0] - 'a';
        int rank = 8 - (fen[1] - '0');
        // AUDIT D (26/09/2026): una casa en passant incoerente col lato al tratto (e3 col Bianco al tratto)
        // faceva "catturare en passant" un pedone amico e mandava in crash il motore. Si ignora, come SF.
        const bool ep_ok = (side == white && fen[1] == '6') || (side == black && fen[1] == '3');
        enpassant = ep_ok ? rank * 8 + file : no_sq;
        fen += 2;
    }
    else
    {
        enpassant = no_sq;
        fen++;
    }

    // Skip space and parse halfmove clock (fifty-move counter)
    if (*fen == ' ')
    {
        fen++;
        // Parse halfmove clock
        if (*fen >= '0' && *fen <= '9')
        {
            fifty = atoi(fen);
            // Skip past the number
            while (*fen >= '0' && *fen <= '9') fen++;
        }
    }

    // Numero di mossa: la gestione del tempo lo usa come ply di partita (TmFenPly, 05/10/2026, audit del nucleo):
    // dai libri .epd (UHO "... 0 9") la partita non parte da ply 0. Stessa regola di SF: 2*(fullmove-1) + nero.
    if (*fen == ' ')
    {
        fen++;
        int fullmove = 0;
        while (*fen >= '0' && *fen <= '9') fullmove = fullmove * 10 + (*fen++ - '0');
        if (fullmove > 1)
            g_fen_ply = 2 * (fullmove - 1);
    }
    g_fen_ply += side == black;

    // Build occupancy bitboards
    for (int piece = P; piece <= K; piece++)
        occupancies[white] |= bitboards[piece];

    for (int piece = p; piece <= k; piece++)
        occupancies[black] |= bitboards[piece];

    occupancies[both] |= occupancies[white];
    occupancies[both] |= occupancies[black];

    // A3 FIX 2026-07-25: la FEN e' un confine di fiducia (arriva dalla GUI, dai
    // libri, dagli script di datagen) e non era validata affatto. Una FEN
    // illegale non e' solo "sbagliata", corrompe memoria:
    //   - >32 pezzi -> nn_build_piece_list scrive oltre pieces[33]/squares[33]
    //     e lo stesso loop non limitato in debug_eval_position e' raggiungibile dal comando "eval";
    //   - re mancante -> get_ls1b_index(0) = -1 usato come indice di casa
    //     (es. nella ricerca in search/*.inc e in misc.cpp).
    // Qui si rifiuta e si torna alla posizione iniziale: la ricorsione termina
    // subito perche' start_position e' valida per costruzione.
    if (count_bits(bitboards[K]) != 1 || count_bits(bitboards[k]) != 1 ||
        count_bits(occupancies[both]) > 32)
    {
        printf("info string FEN rifiutata (re mancante/duplicato o piu' di 32 pezzi): uso la posizione iniziale\n");
        fflush(stdout);
        parse_fen(start_position);
        return;
    }

    // E.p. fantasma: se nessun pedone del lato al tratto puo' realmente catturare
    // sulla casa dichiarata, quella casa cambia la chiave Zobrist senza cambiare
    // la posizione -> la stessa posizione prende due chiavi e la TT non si
    // riconosce (misurato: 22566 vs 21406 nodi a TT calda). SF filtra allo stesso
    // modo in position.cpp. pawn_attacks[side^1][ep] = da dove un pedone del lato
    // al tratto arriverebbe a catturare in ep.
    if (enpassant != no_sq &&
        !(pawn_attacks[side ^ 1][enpassant] & bitboards[side == white ? P : p]))
        enpassant = no_sq;

    // AUDIT D (27/09/2026): diritti d'arrocco incoerenti con la scacchiera. Il motore conosce solo l'arrocco
    // classico (re in e1/e8, torri negli angoli); una FEN Chess960 o sbagliata con "KQkq" e il re altrove
    // faceva generare arrocchi dalle case sbagliate. Si tiene solo il diritto che la posizione rende
    // possibile (le lettere Shredder A-H erano gia' ignorate dallo switch sopra). Posizioni normali: invariate.
    // Col 960 acceso la verifica equivalente la fa c960_parse_castling.
    const int castle_in = castle;
    if (!g_chess960)
    {
        if (!get_bit(bitboards[K], e1)) castle &= ~(wk | wq);
        if (!get_bit(bitboards[R], h1)) castle &= ~wk;
        if (!get_bit(bitboards[R], a1)) castle &= ~wq;
        if (!get_bit(bitboards[k], e8)) castle &= ~(bk | bq);
        if (!get_bit(bitboards[r], h8)) castle &= ~bk;
        if (!get_bit(bitboards[r], a8)) castle &= ~bq;
    }
    if (castle != castle_in)
    {
        printf("info string diritti d'arrocco incoerenti con la scacchiera (per il Chess960: UCI_Chess960): ignorati\n");
        fflush(stdout);
    }

    // Generate hash key
    hash_key = generate_hash_key();
}
