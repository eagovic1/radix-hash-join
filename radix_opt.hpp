// Radix lanac, optimizovana verzija (verzija 6 u vtune_main.cpp).
//
// Isti algoritam kao radix lanac pool (particionisi_u + join_particija_lanac),
// sa tri izmjene usmjerene na ono sto je VTune pokazao za verziju 5
// (Store Bound ~21%, skoro sve DTLB Store Overhead; Core Bound ~19%):
//
//  1) Software write-combining baferi pri particionisanju.
//     Verzija 5 svaki red upisuje direktno u njegovu particiju, pa sa B = 8
//     upisi stalno skacu po 256 razlicitih stranica, a L1 DTLB ima ~64 ulaza.
//     Ovdje svaka particija ima bafer od jedne keš linije (64 B = 8 redova),
//     poravnat na 64 B. Svih 2^B bafera (16 KB za B = 8) stane u L1 i u
//     4 stranice. Tek kad se bafer napuni, cijela linija ide u particiju
//     non-temporal (streaming) upisom, koji zaobilazi keš i ne cita liniju
//     prije upisa. Upisa na udaljene stranice je 8x manje.
//
//  2) __restrict i lokalni pokazivaci umjesto vector[...] u petljama.
//     MSVC ne koristi pravila o tipovima za aliasing, pa bez ovoga mora
//     pretpostaviti da upis u niz moze promijeniti pokazivac/velicinu vektora
//     i ponovo ih citati u svakoj iteraciji.
//
//  3) Particionisanje i join su NOINLINE, pa ih VTune prikazuje kao posebne
//     funkcije (inace se sve ugradi u main i faze se ne mogu razdvojiti).
//
// Hash tabela je ista kao LanacTabela (lanac kroz nizove indeksa), ali su
// nizovi zauzeti jednom za sve particije, na velicinu najvece particije.
#pragma once

#include "common.hpp"

#include <new>

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
#include <emmintrin.h>
#define IMA_SSE2 1
#endif

#if defined(_MSC_VER)
#define OPT_RESTRICT __restrict
#define OPT_NOINLINE __declspec(noinline)
#else
#define OPT_RESTRICT __restrict__
#define OPT_NOINLINE __attribute__((noinline))
#endif

constexpr size_t KES_LINIJA = 64;
constexpr size_t REDOVA_U_LINIJI = KES_LINIJA / sizeof(Red);  // 8

// Bafer jedne particije: tacno jedna keš linija.
struct alignas(KES_LINIJA) Linija {
    Red r[REDOVA_U_LINIJI];
};

// Niz redova ciji je pocetak poravnat na 64 B. Tada je i svaka pozicija
// djeljiva sa 8 pocetak keš linije, sto streaming upis cijele linije trazi.
// Kao vector::resize: memorija se zauzme i popuni nulama samo kad niz raste,
// pa ponovljeni pozivi sa istom velicinom nemaju page faultova.
class PoravnatiRedovi {
public:
    PoravnatiRedovi() = default;
    ~PoravnatiRedovi() { oslobodi(); }
    PoravnatiRedovi(const PoravnatiRedovi&) = delete;
    PoravnatiRedovi& operator=(const PoravnatiRedovi&) = delete;

    void resize(size_t n) {
        if (n > _kapacitet) {
            oslobodi();
            _p = static_cast<Red*>(::operator new(n * sizeof(Red), std::align_val_t{KES_LINIJA}));
            std::memset(_p, 0, n * sizeof(Red));  // mapira sve stranice odmah
            _kapacitet = n;
        }
        _n = n;
    }

    Red* data() { return _p; }
    const Red* data() const { return _p; }
    size_t size() const { return _n; }

private:
    void oslobodi() {
        if (_p) ::operator delete(_p, std::align_val_t{KES_LINIJA});
        _p = nullptr;
        _kapacitet = _n = 0;
    }

    Red* _p = nullptr;
    size_t _n = 0;
    size_t _kapacitet = 0;
};

// Kao Particije iz radix_join.hpp, samo sa poravnatim nizom redova.
struct ParticijeOpt {
    PoravnatiRedovi redovi;
    std::vector<size_t> pocetak;  // P + 1 elemenata
};

// Upis jedne pune linije iz bafera u particiju, mimo keša.
// dst mora biti poravnat na 64 B.
inline void upisi_liniju(Red* OPT_RESTRICT dst, const Linija& bafer) {
#ifdef IMA_SSE2
    const __m128i* s = reinterpret_cast<const __m128i*>(bafer.r);
    __m128i* d = reinterpret_cast<__m128i*>(dst);
    _mm_stream_si128(d + 0, _mm_load_si128(s + 0));
    _mm_stream_si128(d + 1, _mm_load_si128(s + 1));
    _mm_stream_si128(d + 2, _mm_load_si128(s + 2));
    _mm_stream_si128(d + 3, _mm_load_si128(s + 3));
#else
    std::memcpy(dst, bafer.r, KES_LINIJA);
#endif
}

// Podjela na P = 2^B particija kao particionisi_u (histogram, prefiksni zbir,
// raspodjela), ali raspodjela ide preko write-combining bafera.
//
// Pozicija j u izlazu uvijek ide u slot j % 8 bafera svoje particije. Kad se
// popuni slot 7, bafer odgovara tacno liniji [j - 7, j] izlaza. Ako ta linija
// cijela pripada particiji, upise se streaming upisom. Prva linija particije
// moze pocinjati u prethodnoj particiji; tada se obicnim upisima prepisu samo
// redovi ove particije. Na kraju se obicnim upisima prepisu nepune linije.
OPT_NOINLINE inline void particionisi_opt(const std::vector<Red>& ulaz, int B, ParticijeOpt& izlaz) {
    const size_t P = size_t(1) << B;
    const uint32_t maska = (uint32_t)P - 1;
    const size_t n = ulaz.size();
    const Red* OPT_RESTRICT in = ulaz.data();

    // 1) histogram
    std::vector<size_t> broj_v(P, 0);
    size_t* OPT_RESTRICT broj = broj_v.data();
    for (size_t i = 0; i < n; ++i)
        broj[in[i].key & maska]++;

    // 2) prefiksni zbir
    izlaz.pocetak.assign(P + 1, 0);
    size_t* OPT_RESTRICT poc = izlaz.pocetak.data();
    for (size_t p = 0; p < P; ++p)
        poc[p + 1] = poc[p] + broj[p];

    // 3) raspodjela preko bafera
    izlaz.redovi.resize(n);
    Red* OPT_RESTRICT out = izlaz.redovi.data();
    std::vector<size_t> pozicija_v(poc, poc + P);
    size_t* OPT_RESTRICT poz = pozicija_v.data();
    std::vector<Linija> baferi_v(P);
    Linija* OPT_RESTRICT baf = baferi_v.data();

    for (size_t i = 0; i < n; ++i) {
        const Red r = in[i];
        const uint32_t p = r.key & maska;
        const size_t j = poz[p]++;
        baf[p].r[j % REDOVA_U_LINIJI] = r;

        if (j % REDOVA_U_LINIJI == REDOVA_U_LINIJI - 1) {  // bafer pun
            const size_t linija = j + 1 - REDOVA_U_LINIJI;
            if (linija >= poc[p]) {
                upisi_liniju(out + linija, baf[p]);
            } else {  // prva linija particije, pocinje u prethodnoj particiji
                for (size_t k = poc[p]; k <= j; ++k)
                    out[k] = baf[p].r[k % REDOVA_U_LINIJI];
            }
        }
    }

    // ostaci: nepune zadnje linije svake particije
    for (size_t p = 0; p < P; ++p) {
        const size_t kraj = poz[p];
        const size_t od = std::max(kraj - kraj % REDOVA_U_LINIJI, poc[p]);
        for (size_t k = od; k < kraj; ++k)
            out[k] = baf[p].r[k % REDOVA_U_LINIJI];
    }

#ifdef IMA_SSE2
    _mm_sfence();  // streaming upisi moraju biti zavrseni prije citanja particija
#endif
}

// Join po particijama kao join_particija_lanac: za svaku particiju build lanca
// od R_p, pa probe sa S_p. Nizovi lanca su zauzeti jednom, za najvecu particiju.
OPT_NOINLINE inline Rezultat join_particija_opt(const ParticijeOpt& R, const ParticijeOpt& S, int B) {
    constexpr uint32_t PRAZNO = UINT32_MAX;
    const size_t P = R.pocetak.size() - 1;
    const size_t* OPT_RESTRICT rp = R.pocetak.data();
    const size_t* OPT_RESTRICT sp = S.pocetak.data();

    size_t najveca = 1;
    for (size_t p = 0; p < P; ++p)
        najveca = std::max(najveca, rp[p + 1] - rp[p]);
    size_t najvise_bucketa = 1;
    while (najvise_bucketa < najveca) najvise_bucketa *= 2;

    std::vector<uint32_t> prvi_v(najvise_bucketa), sljedeci_v(najveca);
    uint32_t* OPT_RESTRICT prvi = prvi_v.data();
    uint32_t* OPT_RESTRICT sljedeci = sljedeci_v.data();

    const Red* OPT_RESTRICT Rr = R.redovi.data();
    const Red* OPT_RESTRICT Sr = S.redovi.data();
    uint64_t broj_redova = 0, checksum = 0;

    for (size_t p = 0; p < P; ++p) {
        const Red* OPT_RESTRICT r = Rr + rp[p];
        const size_t n = rp[p + 1] - rp[p];

        size_t broj_bucketa = 1;
        while (broj_bucketa < n) broj_bucketa *= 2;
        const size_t maska = broj_bucketa - 1;

        // BUILD: isti hash kao LanacTabela, sa pomak = B
        std::fill(prvi, prvi + broj_bucketa, PRAZNO);
        for (uint32_t i = 0; i < n; ++i) {
            const size_t b = (uint64_t)(r[i].key >> B) * 2654435761ull & maska;
            sljedeci[i] = prvi[b];
            prvi[b] = i;
        }

        // PROBE
        for (size_t i = sp[p]; i < sp[p + 1]; ++i) {
            const Red s = Sr[i];
            const size_t b = (uint64_t)(s.key >> B) * 2654435761ull & maska;
            for (uint32_t k = prvi[b]; k != PRAZNO; k = sljedeci[k])
                if (r[k].key == s.key) {
                    broj_redova++;
                    checksum += (uint64_t)r[k].payload + s.payload;
                }
        }
    }

    Rezultat rez;
    rez.broj_redova = broj_redova;
    rez.checksum = checksum;
    return rez;
}
