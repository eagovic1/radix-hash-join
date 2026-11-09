// Radix hash join: R ⋈ S po kljucu (R.key = S.key).
// Podaci, provjera i mjerenje su u common.hpp.
//
// Ideja: naivni hash join je spor jer je hash tabela za cijelu R prevelika
// za keš, pa svaki probe ide u RAM. Radix join zato prvo obje tabele podijeli
// na 2^B particija po donjih B bitova kljuca. Redovi sa istim kljucem uvijek
// zavrse u particiji istog broja, pa je dovoljno spojiti R_p sa S_p za svaki p.
// Hash tabela jedne particije je mala i stane u keš.
//
//   1) PARTICIONISANJE: R -> R_0..R_{P-1}, S -> S_0..S_{P-1}
//   2) JOIN PO PARTICIJAMA: za svaki p: build(R_p), probe(S_p)
//
// Samo Linux / WSL:
//   g++ -O2 -std=c++17 radix_join.cpp -o radix_join
//   ./radix_join [N_R] [N_S] [B]

#include "common.hpp"

#include <cstdlib>

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
Particije particionisi(const std::vector<Red>& ulaz, int B) {
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
Rezultat join_particija(const Particije& R, const Particije& S) {
    Rezultat rez;
    const size_t P = R.pocetak.size() - 1;

    for (size_t p = 0; p < P; ++p) {
        // BUILD particije R_p
        HashTabela tabela;
        tabela.reserve(R.pocetak[p + 1] - R.pocetak[p]);
        for (size_t i = R.pocetak[p]; i < R.pocetak[p + 1]; ++i)
            tabela[R.redovi[i].key] = R.redovi[i].payload;

        // PROBE particijom S_p
        for (size_t i = S.pocetak[p]; i < S.pocetak[p + 1]; ++i) {
            const Red& s = S.redovi[i];
            auto it = tabela.find(s.key);
            if (it != tabela.end()) {
                rez.broj_redova++;
                rez.checksum += (uint64_t)it->second + s.payload;
            }
        }
    }
    return rez;
}

int main(int argc, char** argv) {
    const size_t N_R = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1'000'000;
    const size_t N_S = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 10'000'000;
    // Broj radix bitova. 6 je najbrze za podrazumijevane velicine na i7-11800H:
    // particija R ima ~15 600 redova, pa njena hash tabela (~0,6 MB) stane u L2.
    const int B      = argc > 3 ? std::atoi(argv[3]) : 6;

    std::vector<Red> R, S;
    generisi(N_R, N_S, R, S);
    std::printf("RADIX HASH JOIN\nR: %zu redova, S: %zu redova\n", N_R, N_S);
    std::printf("B = %d bitova -> %zu particija, ~%zu redova R po particiji\n",
                B, size_t(1) << B, N_R >> B);

    // provjera ispravnosti prije mjerenja
    const Rezultat ocekivano = ocekivani_rezultat(S);
    Rezultat rez = join_particija(particionisi(R, B), particionisi(S, B));
    if (!provjeri(rez, ocekivano)) return 1;

    // mjerenje: particionisanje (R i S) i join po particijama odvojeno
    std::vector<Mjerenje> m_part, m_join;
    for (int i = 0; i < ZAGRIJAVANJA + PONAVLJANJA; ++i) {
        Particije Rp, Sp;
        Mjerenje a = mjeri([&] { Rp = particionisi(R, B); Sp = particionisi(S, B); });
        Mjerenje j = mjeri([&] { rez = join_particija(Rp, Sp); });

        // Rezultat moramo iskoristiti, inace ga kompajler moze izbaciti.
        if (rez.checksum != ocekivano.checksum) {
            std::printf("GRESKA u pokretanju %d\n", i);
            return 1;
        }
        if (i >= ZAGRIJAVANJA) { m_part.push_back(a); m_join.push_back(j); }
    }

    std::printf("%d zagrijavanje + %d mjerenja\n", ZAGRIJAVANJA, PONAVLJANJA);
    Mjerenje a = ispisi_fazu("PARTICIONISANJE R + S", m_part, N_R + N_S);
    Mjerenje j = ispisi_fazu("JOIN PO PARTICIJAMA (build + probe)", m_join, N_R + N_S);
    ispisi_ukupno({a, j}, N_R + N_S);
    return 0;
}
