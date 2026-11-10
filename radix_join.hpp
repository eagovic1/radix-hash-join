// Radix hash join: R ⋈ S po kljucu (R.key = S.key).
//
// Ideja: naivni hash join je spor jer je hash tabela za cijelu R prevelika
// za keš, pa svaki probe ide u RAM. Radix join zato prvo obje tabele podijeli
// na 2^B particija po donjih B bitova kljuca. Redovi sa istim kljucem uvijek
// zavrse u particiji istog broja, pa je dovoljno spojiti R_p sa S_p za svaki p.
// Hash tabela jedne particije je mala i stane u keš.
//
//   1) PARTICIONISANJE: R -> R_0..R_{P-1}, S -> S_0..S_{P-1}
//   2) JOIN PO PARTICIJAMA: za svaki p: build(R_p), probe(S_p)
#pragma once

#include "common.hpp"
#include "cvor_tabela.hpp"
#include "lanac_tabela.hpp"
#include "csr_tabela.hpp"

// Tabela podijeljena na particije: redovi particije p su
// redovi[pocetak[p]] .. redovi[pocetak[p + 1] - 1].
struct Particije {
    std::vector<Red> redovi;
    std::vector<size_t> pocetak;  // P + 1 elemenata
};

// Particija kojoj red pripada: donjih B bitova kljuca.
// (Ovdje su kljucevi vec ravnomjerno rasporedjeni; u praksi bi se uzeli bitovi hasha kljuca.)
inline uint32_t particija(uint32_t key, uint32_t maska) { return key & maska; }

// Podjela tabele na P = 2^B particija u tri koraka (kao counting sort):
inline Particije particionisi(const std::vector<Red>& ulaz, int B) {
    const size_t P = size_t(1) << B;
    const uint32_t maska = (uint32_t)P - 1;

    // 1) histogram: koliko redova ide u svaku particiju
    std::vector<size_t> broj(P, 0);
    for (const Red& r : ulaz)
        broj[particija(r.key, maska)]++;

    // 2) prefiksni zbir: gdje u izlazu pocinje svaka particija
    Particije izlaz;
    izlaz.pocetak.assign(P + 1, 0);
    for (size_t p = 0; p < P; ++p)
        izlaz.pocetak[p + 1] = izlaz.pocetak[p] + broj[p];

    // 3) raspodjela: svaki red upisi na sljedece slobodno mjesto svoje particije
    izlaz.redovi.resize(ulaz.size());
    std::vector<size_t> pozicija(izlaz.pocetak.begin(), izlaz.pocetak.end() - 1);
    for (const Red& r : ulaz)
        izlaz.redovi[pozicija[particija(r.key, maska)]++] = r;

    return izlaz;
}

// Join po particijama: za svaku particiju obican hash join (build + probe),
// sa istom hash tabelom kao naivni join, samo mnogo manjom.
// B = broj radix bitova kojim su R i S particionisani.
inline Rezultat join_particija_cvorovi(const Particije& R, const Particije& S, int B) {
    Rezultat rez;
    const size_t P = R.pocetak.size() - 1;

    for (size_t p = 0; p < P; ++p) {
        // BUILD particije R_p. Svi kljucevi u particiji imaju istih donjih B bitova,
        // pa hash mora te bitove preskociti (pomak = B), inace bi svi kljucevi
        // particije pali u mali broj bucketa (vidi komentar u cvor_tabela.hpp).
        CvorTabela tabela(R.redovi.data() + R.pocetak[p], R.pocetak[p + 1] - R.pocetak[p], B);

        // PROBE particijom S_p: svaki par sa istim kljucem je red rezultata
        for (size_t i = S.pocetak[p]; i < S.pocetak[p + 1]; ++i) {
            const Red& s = S.redovi[i];
            tabela.za_svaki(s.key, [&](const Red& r) {
                rez.broj_redova++;
                rez.checksum += (uint64_t)r.payload + s.payload;
            });
        }
    }
    return rez;
}

inline Rezultat radix_join_cvorovi(const std::vector<Red>& R, const std::vector<Red>& S, int B) {
    return join_particija_cvorovi(particionisi(R, B), particionisi(S, B), B);
}

// Isto kao join_particija_cvorovi, ali sa LanacTabela (lanac_tabela.hpp) umjesto tabele od cvorova.
// Jedna tabela se koristi za sve particije: napravi() samo prepise njene nizove,
// pa nema alokacije ni oslobadjanja po particiji.
inline Rezultat join_particija_lanac(const Particije& R, const Particije& S, int B) {
    Rezultat rez;
    const size_t P = R.pocetak.size() - 1;
    LanacTabela tabela;

    for (size_t p = 0; p < P; ++p) {
        // BUILD particije R_p, sa pomak = B iz istog razloga kao u join_particija_cvorovi
        tabela.napravi(R.redovi.data() + R.pocetak[p], R.pocetak[p + 1] - R.pocetak[p], B);

        // PROBE particijom S_p: svaki par sa istim kljucem je red rezultata
        for (size_t i = S.pocetak[p]; i < S.pocetak[p + 1]; ++i) {
            const Red& s = S.redovi[i];
            tabela.za_svaki(s.key, [&](const Red& r) {
                rez.broj_redova++;
                rez.checksum += (uint64_t)r.payload + s.payload;
            });
        }
    }
    return rez;
}

inline Rezultat radix_join_lanac(const std::vector<Red>& R, const std::vector<Red>& S, int B) {
    return join_particija_lanac(particionisi(R, B), particionisi(S, B), B);
}

// Isto kao join_particija_lanac, ali sa CsrTabela (csr_tabela.hpp): bucket nije
// lanac, nego uzastopan komad niza. Jedna tabela se koristi za sve particije.
inline Rezultat join_particija_csr(const Particije& R, const Particije& S, int B) {
    Rezultat rez;
    const size_t P = R.pocetak.size() - 1;
    CsrTabela tabela;

    for (size_t p = 0; p < P; ++p) {
        // BUILD particije R_p, sa pomak = B iz istog razloga kao u join_particija_cvorovi
        tabela.napravi(R.redovi.data() + R.pocetak[p], R.pocetak[p + 1] - R.pocetak[p], B);

        // PROBE particijom S_p: svaki par sa istim kljucem je red rezultata
        for (size_t i = S.pocetak[p]; i < S.pocetak[p + 1]; ++i) {
            const Red& s = S.redovi[i];
            tabela.za_svaki(s.key, [&](const Red& r) {
                rez.broj_redova++;
                rez.checksum += (uint64_t)r.payload + s.payload;
            });
        }
    }
    return rez;
}

inline Rezultat radix_join_csr(const std::vector<Red>& R, const std::vector<Red>& S, int B) {
    return join_particija_csr(particionisi(R, B), particionisi(S, B), B);
}
