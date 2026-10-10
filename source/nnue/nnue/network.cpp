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

#include "network.h"

#include <cstdlib>
#include <cstring>  // memcpy/memset della tabella degli stati base (GRAFT_PASSEDREL_COSTO2 §8)
#include <fstream>
#include <iostream>
#include <optional>
#include <type_traits>
#include <vector>

#define INCBIN_SILENCE_BITCODE_WARNING
#include "../incbin/incbin.h"

#include "../../profile.h"
#include "../../nneurons.h"
#include "../evaluate.h"
#include "../misc.h"
#include "../nn_board.h"
#include "../types.h"
#include "nnue_architecture.h"
#include "nnue_common.h"
#include "nnue_misc.h"
#include "nnz_helper.h"

// Macro to embed the default efficiently updatable neural network (NNUE) file
// data in the engine binary (using incbin.h, by Dale Weiler).
// This macro invocation will declare the following three variables
//     const unsigned char        gEmbeddedNNUEData[];  // a pointer to the embedded data
//     const unsigned char *const gEmbeddedNNUEEnd;     // a marker to the end
//     const unsigned int         gEmbeddedNNUESize;    // the size of the embedded file
// Note that this does not work in Microsoft Visual Studio.
#if !defined(UNIVERSAL_BINARY) && !defined(_MSC_VER) && !defined(NNUE_EMBEDDING_OFF)
INCBIN(EmbeddedNNUE, EvalFileDefaultName);
#elif defined(TRIUMV_EMBED_RESOURCE)
// Triumviratus/Windows: incbin non funziona con _MSC_VER (anche clang-cl) ->
// la rete sta in una risorsa RCDATA dell'exe; i puntatori sono risolti a
// runtime dal bridge (nnue_bridge.cpp, FindResource/LockResource).
extern const unsigned char* gEmbeddedNNUEData;
extern unsigned int         gEmbeddedNNUESize;
#elif defined(UNIVERSAL_BINARY_MACOS_X86_SLICE)
// Determined at runtime, see universal/nnue_embed.cpp
extern const unsigned char* const gEmbeddedNNUEData;
extern const unsigned int         gEmbeddedNNUESize;
#elif defined(UNIVERSAL_BINARY)
extern const unsigned char gEmbeddedNNUEData[];
extern const unsigned int  gEmbeddedNNUESize;
#else
const unsigned char gEmbeddedNNUEData[1] = {0x0};
const unsigned int  gEmbeddedNNUESize    = 1;
#endif

namespace Triumviratus::Eval::NNUE {

// ⭐ EvalBucketOverride (17/08/2026) — E' UNA SONDA, non una patch.
// I bucket di output sono 8 e si scelgono con `(pezzi - 1) / 4`. Quindi OGNI cattura
// che attraversa un confine cambia TESTA DI OUTPUT: la stessa posizione, a un ply di
// distanza, viene valutata da pesi diversi. E succede esattamente nei nodi dove si sta
// decidendo se catturare, cioe' dove un artefatto sistematico fa piu' danno.
// 🔑 La domanda si risponde con un NUMERO, non con un SPRT: si forza la stessa
// posizione sotto due bucket adiacenti e si misura il salto. Se e' di decine di
// centipawn e' un difetto vero nella eval che stiamo per spedire; se e' di due o tre,
// l'idea e' morta ed e' costato dieci minuti scoprirlo.
// -1 = comportamento normale (default, byte-identico). 0..7 = forza quel bucket.
// ⚠️ Diagnostica: in partita va lasciata a -1.
int g_eval_bucket_override = -1;

// Un solo punto di verita' per la scelta del bucket: era duplicata fra `evaluate` e
// `mens_trace`, e due copie della stessa formula sono due posti dove sbagliare.
static inline int nnue_output_bucket(int piece_count) {
    if (g_eval_bucket_override >= 0 && g_eval_bucket_override < LayerStacks)
        return g_eval_bucket_override;
    return (piece_count - 1) / 4;
}

// Triumviratus: true se un net embeddato e' effettivamente disponibile a runtime
// (incbin: size > 1; resource/universal: puntatore risolto). Il bridge lo usa per
// decidere se il nome-default puo' essere instradato su <internal>.
bool embedded_net_available() {
#if defined(TRIUMV_EMBED_RESOURCE) || defined(UNIVERSAL_BINARY_MACOS_X86_SLICE)
    return gEmbeddedNNUEData != nullptr && gEmbeddedNNUESize > 1;
#else
    return gEmbeddedNNUESize > 1;
#endif
}

namespace Detail {

// Read evaluation function parameters
template<typename T>
bool read_parameters(std::istream& stream, T& reference) {

    u32 header;
    header = read_little_endian<u32>(stream);
    if (!stream || header != T::get_hash_value())
        return false;
    return reference.read_parameters(stream);
}

// Write evaluation function parameters
template<typename T>
bool write_parameters(std::ostream& stream, const T& reference) {

    write_little_endian<u32>(stream, T::get_hash_value());
    return reference.write_parameters(stream);
}

}  // namespace Detail

void Network::load(const std::string& rootDirectory, std::string evalfilePath) {
#if defined(DEFAULT_NNUE_DIRECTORY)
    std::vector<std::string> dirs = {"<internal>", "", rootDirectory,
                                     stringify(DEFAULT_NNUE_DIRECTORY)};
#else
    std::vector<std::string> dirs = {"<internal>", "", rootDirectory};
#endif

    if (evalfilePath.empty())
        evalfilePath = evalFile.defaultName;

    for (const auto& directory : dirs)
    {
        if (std::string(evalFile.current) != evalfilePath)
        {
            if (directory != "<internal>")
                load_user_net(directory, evalfilePath);
            else if (evalfilePath == std::string(evalFile.defaultName))
                load_internal();
        }
    }
}


bool Network::save(const std::optional<std::string>& filename) const {
    std::string actualFilename;
    std::string msg;

    if (filename.has_value())
        actualFilename = filename.value();
    else
    {
        if (std::string(evalFile.current) != std::string(evalFile.defaultName))
        {
            msg = "Failed to export a net. "
                  "A non-embedded net can only be saved if the filename is specified";

            sync_cout << msg << sync_endl;
            return false;
        }

        actualFilename = evalFile.defaultName;
    }

    std::ofstream stream(actualFilename, std::ios_base::binary);
    bool          saved = save(stream, evalFile.current, evalFile.netDescription);

    msg = saved ? "Network saved successfully to " + actualFilename : "Failed to export a net";

    sync_cout << msg << sync_endl;
    return saved;
}


NetworkOutput Network::evaluate(const NnBoard&     pos,
                                AccumulatorStack&  accumulatorStack,
                                AccumulatorCaches& cache) const {

    constexpr u64 alignment = CacheLineSize;

    alignas(alignment) TransformedFeatureType transformedFeatures[FeatureTransformer::BufferSize];

    ASSERT_ALIGNED(transformedFeatures, alignment);

    NNZInfo<L1> nnzInfo;

    const int bucket = nnue_output_bucket(pos.count());
    Value     psqt_v, pos_v;
    {
        PROF_GUARD(prof_ft);
        const auto psqt = featureTransformer.transform(pos, accumulatorStack, cache,
                                                       transformedFeatures, bucket, nnzInfo);
        psqt_v = static_cast<Value>(psqt / OutputScale);
    }
#ifdef TRIUMV_NEURONS
    {   // studio dei neuroni del primo strato (nneurons.h): uscite per fascia di materiale, con un campione
        static constexpr int kVal[5] = {100, 300, 300, 500, 900};
        const int us = pos.side_to_move(), them = us ^ 1;
        int md = 0;
        for (int t = NN_PAWN; t <= NN_QUEEN; t++)
            md += kVal[t] * (popcount(pos.bb(t + NN_BLACK * us)) - popcount(pos.bb(t + NN_BLACK * them)));
        NNeurons::record(reinterpret_cast<const std::uint8_t*>(transformedFeatures), int(L1), pos.count(), md, us,
                         int(pos.king(Color(us))), int(pos.king(Color(them))), pos.bbs());
    }
#endif
    {
        const auto positional = network[bucket].propagate(transformedFeatures, nnzInfo);
        pos_v = static_cast<Value>(positional / OutputScale);
    }
    return {psqt_v, pos_v};
}


Network::MensLayerTrace Network::mens_trace(const NnBoard&     pos,
                                            AccumulatorStack&  accumulatorStack,
                                            AccumulatorCaches& cache) const {
    constexpr u64 alignment = CacheLineSize;
    alignas(alignment) TransformedFeatureType transformedFeatures[FeatureTransformer::BufferSize];
    NNZInfo<L1> nnzInfo;
    const int  bucket = nnue_output_bucket(pos.count());
    const auto psqt   = featureTransformer.transform(pos, accumulatorStack, cache,
                                                     transformedFeatures, bucket, nnzInfo);
    MensLayerTrace t;
    t.bucket = bucket;
    const auto positional = network[bucket].propagate_trace(transformedFeatures, nnzInfo, t.l2, t.l3);
    t.psqt       = static_cast<int>(psqt / OutputScale);
    t.positional = static_cast<int>(positional / OutputScale);
    return t;
}


void Network::verify(std::string                                  evalfilePath,
                     const std::function<void(std::string_view)>& f) const {
    if (evalfilePath.empty())
        evalfilePath = evalFile.defaultName;

    if (std::string(evalFile.current) != evalfilePath)
    {
        if (f)
        {
            std::string msg1 =
              "Network evaluation parameters compatible with the engine must be available.";
            std::string msg2 = "The network file " + evalfilePath + " was not loaded successfully.";
            std::string msg3 = "The UCI option EvalFile might need to specify the full path, "
                               "including the directory name, to the network file.";
            // NB: qui c'era il testo ereditato da Stockfish che rimandava a
            // tests.stockfishchess.org/api/nn/<nome>. E' FALSO per noi: le nostre reti non
            // stanno sul server di test di SF, e chi ci finiva trovava un 404. (27/07/2026)
            std::string msg4 = "Triumviratus networks are published with the engine release: "
                               "https://github.com/Tors3/Triumviratus/releases  (the network is "
                               "also embedded in the binary, so a bare executable normally works "
                               "on its own; this error means the file that WAS found is not "
                               "compatible with this build).";
            std::string msg5 = "The engine will be terminated now.";

            std::string msg = "ERROR: " + msg1 + '\n' + "ERROR: " + msg2 + '\n' + "ERROR: " + msg3
                            + '\n' + "ERROR: " + msg4 + '\n' + "ERROR: " + msg5 + '\n';

            f(msg);
        }

        exit(EXIT_FAILURE);
    }

    if (f)
    {
        usize size = sizeof(featureTransformer) + sizeof(NetworkArchitecture) * LayerStacks;
        f("NNUE evaluation using " + evalfilePath + " (" + std::to_string(size / (1024 * 1024))
          + "MiB, (" + std::to_string(featureTransformer.InputDimensions) + ", "
          + std::to_string(network[0].TransformedFeatureDimensions) + ", "
          + std::to_string(network[0].FC_0_OUTPUTS) + ", " + std::to_string(network[0].FC_1_OUTPUTS)
          + ", 1))");
    }
}


NnueEvalTrace Network::trace_evaluate(const NnBoard&     pos,
                                      AccumulatorStack&  accumulatorStack,
                                      AccumulatorCaches& cache) const {

    constexpr u64 alignment = CacheLineSize;

    alignas(alignment) TransformedFeatureType transformedFeatures[FeatureTransformer::BufferSize];

    ASSERT_ALIGNED(transformedFeatures, alignment);

    NnueEvalTrace t{};
    t.correctBucket = (pos.count() - 1) / 4;
    for (IndexType bucket = 0; bucket < LayerStacks; ++bucket)
    {
        NNZInfo<L1> nnzInfo;
        const auto  materialist = featureTransformer.transform(pos, accumulatorStack, cache,
                                                               transformedFeatures, bucket, nnzInfo);
        const auto  positional  = network[bucket].propagate(transformedFeatures, nnzInfo);

        t.psqt[bucket]       = static_cast<Value>(materialist / OutputScale);
        t.positional[bucket] = static_cast<Value>(positional / OutputScale);
    }

    return t;
}


void Network::load_user_net(const std::string& dir, const std::string& evalfilePath) {
    std::ifstream stream(dir + evalfilePath, std::ios::binary);
    auto          description = load(stream);

    if (description.has_value())
    {
        evalFile.current        = evalfilePath;
        evalFile.netDescription = description.value();
    }
}


void Network::load_internal() {
    // C++ way to prepare a buffer for a memory stream
    class MemoryBuffer: public std::basic_streambuf<char> {
       public:
        MemoryBuffer(char* p, usize n) {
            setg(p, p, p + n);
            setp(p, p + n);
        }
    };

#if defined(UNIVERSAL_BINARY_MACOS_X86_SLICE) || defined(TRIUMV_EMBED_RESOURCE)
    if (gEmbeddedNNUEData == nullptr)  // failed embedded load / risorsa assente
        return;
#endif

    MemoryBuffer buffer(const_cast<char*>(reinterpret_cast<const char*>(gEmbeddedNNUEData)),
                        usize(gEmbeddedNNUESize));

    std::istream stream(&buffer);
    auto         description = load(stream);

    if (description.has_value())
    {
        evalFile.current        = evalFile.defaultName;
        evalFile.netDescription = description.value();
    }
}


void Network::initialize() { initialized = true; }


bool Network::save(std::ostream&      stream,
                   const std::string& name,
                   const std::string& netDescription) const {
    if (name.empty() || name == "None")
        return false;

    return write_parameters(stream, netDescription);
}


std::optional<std::string> Network::load(std::istream& stream) {
    initialize();
    std::string description;

    return read_parameters(stream, description) ? std::make_optional(description) : std::nullopt;
}


usize Network::get_content_hash() const {
    if (!initialized)
        return 0;

    usize h = 0;
    hash_combine(h, featureTransformer);
    for (auto&& layerstack : network)
        hash_combine(h, layerstack);
    hash_combine(h, evalFile);
    return h;
}

// Read network header
bool Network::read_header(std::istream& stream, u32* hashValue, std::string* desc) const {
    u32 version, size;

    version    = read_little_endian<u32>(stream);
    *hashValue = read_little_endian<u32>(stream);
    size       = read_little_endian<u32>(stream);
    if (!stream || version != Version)
        return false;
    desc->resize(size);
    stream.read(&(*desc)[0], size);
    return !stream.fail();
}


// Write network header
bool Network::write_header(std::ostream& stream, u32 hashValue, const std::string& desc) const {
    write_little_endian<u32>(stream, Version);
    write_little_endian<u32>(stream, hashValue);
    write_little_endian<u32>(stream, u32(desc.size()));
    stream.write(&desc[0], desc.size());
    return !stream.fail();
}


static_assert(FeatureTransformer::get_hash_value_graft(0) == FeatureTransformer::get_hash_value(),
              "maschera 0 = formato senza blocchi da innesto");

bool Network::read_parameters(std::istream& stream, std::string& netDescription) {
    u32 hashValue;
    if (!read_header(stream, &hashValue, &netDescription))
        return false;
    // v3: accetta anche il formato v2 (3 blocchi) -> il segmento PassedPawns
    // viene zero-fillato in FeatureTransformer::read_parameters (eval identica).
    const bool v2Compat = (hashValue == Network::hash_v2);
    // Blocchi da innesto (09/10/2026): il formato dice quali ha la rete; gli altri restano a zero e spenti.
    unsigned graftMask = 0;
    bool     prelBased = false;  // PassedRel nel formato a base ("PRB1", GRAFT_PASSEDREL_COSTO2 §8)
    for (unsigned m = 1; m <= PawnGraftSet::ALL; m++)
    {
        if (!PawnGraftSet::valid_mask(m))  // KingFiles con KingFilesQ, Space con Space24 (10/10/2026)
            continue;
        if (hashValue == Network::hash_graft(m))
            graftMask = m, prelBased = false;
        if ((m & 1u) && hashValue == Network::hash_graft(m, true))
            graftMask = m, prelBased = true;
    }
    if (hashValue != Network::hash && !v2Compat && !graftMask)
    {
        // Diagnostica: senza questi numeri un rifiuto di rete e' muto e si finisce a
        // indovinare quale pezzo non combacia (ordine dei blocchi, hash di una feature,
        // dimensioni della testa). Costa una riga e vale ogni volta. (27/07/2026)
        std::cerr << "ERROR: network hash mismatch. file=0x" << std::hex << hashValue
                  << " engine=0x" << Network::hash << " (v2-compat=0x" << Network::hash_v2 << ")"
                  << "\n       FT=0x" << FeatureTransformer::get_hash_value()
                  << "  arch=0x" << NetworkArchitecture::get_hash_value() << std::dec << std::endl;
        return false;
    }
    {
        // Sezione FT: header di sezione + parametri (dual-format; specchia
        // Detail::read_parameters, che non puo' passare il flag).
        u32 header = read_little_endian<u32>(stream);
        const u32 expected = v2Compat ? FeatureTransformer::get_hash_value_v2()
                                      : FeatureTransformer::get_hash_value_graft(graftMask, prelBased);
        if (!stream || header != expected)
        {
            std::cerr << "ERROR: feature-transformer hash mismatch. file=0x" << std::hex << header
                      << " engine=0x" << expected << std::dec << std::endl;
            return false;
        }
        if (!featureTransformer.read_parameters(stream, v2Compat, graftMask, prelBased))
            return false;
    }
    for (usize i = 0; i < LayerStacks; ++i)
    {
        if (!Detail::read_parameters(stream, network[i]))
            return false;
    }
    const bool ok = stream && stream.peek() == std::ios::traits_type::eof();
#ifdef TRIUMV_NO_GRAFTS
    if (graftMask)
    {
        std::cerr << "ERROR: rete con blocchi da innesto (maschera " << graftMask
                  << "): questa build e' compilata senza (TRIUMV_NO_GRAFTS)" << std::endl;
        return false;
    }
#endif
    if (ok)
    {
#ifndef TRIUMV_NO_GRAFTS
        nn_graft_mask = graftMask;
#endif
        // Formato a base: tabella degli stati base (8 = nessuna base con gli altri formati, che non la hanno).
        Features::PrelBased = featureTransformer.prelBasedLoaded;
        std::memcpy(Features::PrelBase, featureTransformer.prelBaseLoaded, sizeof(Features::PrelBase));
#ifdef TRIUMV_PREL_NOFILTER
        // Solo verifica (§8.5): la rete a base senza il filtro somma anche le righe base, che sono zero: stesso bench.
        // Non salvare reti con questa build (uscirebbero nel formato PRV2 con i pesi piegati).
        Features::PrelBased = false;
        // R1 (_wip graft_passedrel3): il percorso caldo filtra con "stato != PrelBase[g]" senza leggere PrelBased
        // (invariante: senza formato a base la tabella e' tutta a 8, passed_rel.h).
        std::memset(Features::PrelBase, 8, sizeof(Features::PrelBase));
#endif
    }
#if defined(TRIUMV_GRAFT_RANDOM) && defined(TRIUMV_NO_GRAFTS)
    #error "TRIUMV_GRAFT_RANDOM vuole i blocchi da innesto (senza TRIUMV_NO_GRAFTS)"
#endif
#ifdef TRIUMV_GRAFT_RANDOM
    // Verifica dei blocchi da innesto (09/10/2026): pesi casuali piccoli e blocchi TRIUMV_GRAFT_RANDOM (maschera, es.
    // 15 = tutti) accesi con qualsiasi rete, per confrontare con nnperft l'aggiornamento incrementale con il refresh
    // completo. Solo build di diagnosi.
    if (ok && !graftMask)
    {
        std::uint32_t x = 0x9E3779B9u;
        auto*         w = featureTransformer.threatWeights.data()
                  + usize(FeatureTransformer::ThreatPlusPawnDimensions) * FeatureTransformer::OutputDimensions;
        for (usize i = 0; i < usize(FeatureTransformer::GraftInputDimensions) * FeatureTransformer::OutputDimensions; ++i)
        {
            x ^= x << 13, x ^= x >> 17, x ^= x << 5;
            w[i] = ThreatWeightType(int(x % 15) - 7);
        }
        auto* p = featureTransformer.threatPsqtWeights.data()
                  + usize(FeatureTransformer::ThreatPlusPawnDimensions) * PSQTBuckets;
        for (usize i = 0; i < usize(FeatureTransformer::GraftInputDimensions) * PSQTBuckets; ++i)
        {
            x ^= x << 13, x ^= x >> 17, x ^= x << 5;
            p[i] = PSQTWeightType(int(x % 201) - 100);
        }
        static_assert(PawnGraftSet::valid_mask(unsigned(TRIUMV_GRAFT_RANDOM)), "TRIUMV_GRAFT_RANDOM: maschera non valida");
        nn_graft_mask = unsigned(TRIUMV_GRAFT_RANDOM);
    #ifdef TRIUMV_GRAFT_RANDOM_BASED
        // Formato a base con pesi casuali (GRAFT_PASSEDREL_COSTO2 §8.5): stato base casuale per gruppo (8 = nessuno) e
        // riga dello stato base a zero, come dopo la conversione. Il refresh di confronto di nnperft (eval_full) passa
        // dal riferimento non filtrato: incrementale filtrato e refresh devono coincidere.
        if (nn_graft_mask & 1u)
        {
            for (int g = 0; g < 96; g++)
            {
                x ^= x << 13, x ^= x >> 17, x ^= x << 5;
                const int st = int(x % 9);
                Features::PrelBase[g] = std::uint8_t(st);
                if (st == 8)
                    continue;
                const usize row = usize(FeatureTransformer::ThreatPlusPawnDimensions) + (g >= 48 ? 384 : 0)
                                + usize(st) * 48 + usize(g % 48);
                std::memset(featureTransformer.threatWeights.data() + row * FeatureTransformer::OutputDimensions, 0,
                            FeatureTransformer::OutputDimensions * sizeof(ThreatWeightType));
                std::memset(featureTransformer.threatPsqtWeights.data() + row * PSQTBuckets, 0,
                            PSQTBuckets * sizeof(PSQTWeightType));
            }
            Features::PrelBased = true;
        }
    #endif
    }
#endif
#if defined(TRIUMV_GRAFT_RANDOM) && defined(TRIUMV_GRAFT_RANDOM_V1OFF)
    // PassedState con la v1 a zero (percorso V1Off) e pesi casuali nel blocco: solo nnperft (incrementale contro
    // refresh), la valutazione non e' quella della rete.
    if (ok && !graftMask && (nn_graft_mask & PawnGraftSet::PASSED_STATE))
        for (int g = 0; g < 96; g++)
        {
            const usize v = FeatureTransformer::pst_v1_row(g);
            std::memset(featureTransformer.threatWeights.data() + v * FeatureTransformer::OutputDimensions, 0,
                        FeatureTransformer::OutputDimensions * sizeof(ThreatWeightType));
            std::memset(featureTransformer.threatPsqtWeights.data() + v * PSQTBuckets, 0,
                        PSQTBuckets * sizeof(PSQTWeightType));
        }
#endif
    // PassedState (10/10/2026): la v1 si spegne solo se la rete la porta tutta a zero (righe nulle: esatto).
    if (ok)
        Features::V1Off = (nn_graft_mask & PawnGraftSet::PASSED_STATE) && featureTransformer.pst_v1_zero();
#ifdef TRIUMV_PREL_DELTA
    // R3 (_wip graft_passedrel3): righe delta di PassedRel dai pesi appena letti (anche quelli casuali di
    // TRIUMV_GRAFT_RANDOM, qui sopra), a ogni rete letta.
    if (ok)
        featureTransformer.build_prel_delta((nn_graft_mask & PawnGraftSet::LISTS) == PawnGraftSet::PASSED_REL);
#endif
    if (ok)
        ++nn_net_epoch;  // _wip graft_space_locked: invalida la cache "pe" dei blocchi pedoni di ogni thread
    return ok;
}


bool Network::save_pst(const std::string& filename) const {
#ifdef TRIUMV_NO_GRAFTS
    return false;
#else
    if (nn_graft_mask)  // solo da una rete senza blocchi: le righe del blocco partono da zero
        return false;
    auto copy = std::make_unique<Network>(*this);
    if (!copy->featureTransformer.pst_fold_v1(false))
        return false;
    nn_graft_mask = PawnGraftSet::PASSED_STATE;
    const bool ok = copy->save(std::optional<std::string>(filename));
    nn_graft_mask = 0;
    return ok;
#endif
}


bool Network::write_parameters(std::ostream& stream, const std::string& netDescription) const {
    // Con i blocchi da innesto accesi la rete esce nel formato con quei blocchi (hash e sezione FT propri).
    const bool prelBased = Features::PrelBased && (nn_graft_mask & 1u);  // formato a base (GRAFT_PASSEDREL_COSTO2 §8)
    if (!write_header(stream, nn_graft_mask ? Network::hash_graft(nn_graft_mask, prelBased) : Network::hash,
                      netDescription))
        return false;
    if (nn_graft_mask)
    {
        write_little_endian<u32>(stream, FeatureTransformer::get_hash_value_graft(nn_graft_mask, prelBased));
        if (!featureTransformer.write_parameters(stream))
            return false;
    }
    else if (!Detail::write_parameters(stream, featureTransformer))
        return false;
    for (usize i = 0; i < LayerStacks; ++i)
    {
        if (!Detail::write_parameters(stream, network[i]))
            return false;
    }
    return bool(stream);
}

}  // namespace Triumviratus::Eval::NNUE
