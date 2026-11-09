// Naivni hash join: R ⋈ S po kljucu (R.key = S.key).
// Podaci, provjera i mjerenje su u common.hpp.
//
// Samo Linux / WSL:
//   g++ -O2 -std=c++17 hash_join.cpp -o hash_join
//   ./hash_join [N_R] [N_S]

#include "common.hpp"

#include <cstdlib>

// BUILD: ubaci sve redove iz R u hash tabelu.
HashTabela build(const std::vector<Red>& R) {
    HashTabela tabela;
    tabela.reserve(R.size());
    for (const Red& r : R)
        tabela[r.key] = r.payload;
    return tabela;
}

// PROBE: za svaki red iz S potrazi kljuc u tabeli; ako postoji, to je red rezultata.
// Rezultat ne cuvamo u memoriji, nego brojimo redove i sabiramo payload-e.
Rezultat probe(const HashTabela& tabela, const std::vector<Red>& S) {
    Rezultat rez;
    for (const Red& s : S) {
        auto it = tabela.find(s.key);
        if (it != tabela.end()) {
            rez.broj_redova++;
            rez.checksum += (uint64_t)it->second + s.payload;
        }
    }
    return rez;
}

int main(int argc, char** argv) {
    const size_t N_R = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1'000'000;
    const size_t N_S = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 10'000'000;

    std::vector<Red> R, S;
    generisi(N_R, N_S, R, S);
    std::printf("NAIVNI HASH JOIN\nR: %zu redova, S: %zu redova\n", N_R, N_S);

    // provjera ispravnosti prije mjerenja
    const Rezultat ocekivano = ocekivani_rezultat(S);
    Rezultat rez = probe(build(R), S);
    if (!provjeri(rez, ocekivano)) return 1;

    // mjerenje: build i probe odvojeno
    std::vector<Mjerenje> m_build, m_probe;
    for (int i = 0; i < ZAGRIJAVANJA + PONAVLJANJA; ++i) {
        HashTabela tabela;
        Mjerenje b = mjeri([&] { tabela = build(R); });
        Mjerenje p = mjeri([&] { rez = probe(tabela, S); });

        // Rezultat moramo iskoristiti, inace ga kompajler smatra nepotrebnim
        // i izbaci cijeli probe (izmjerilo bi se 0 ms).
        if (rez.checksum != ocekivano.checksum) {
            std::printf("GRESKA u pokretanju %d\n", i);
            return 1;
        }
        if (i >= ZAGRIJAVANJA) { m_build.push_back(b); m_probe.push_back(p); }
    }

    std::printf("%d zagrijavanje + %d mjerenja\n", ZAGRIJAVANJA, PONAVLJANJA);
    Mjerenje b = ispisi_fazu("BUILD", m_build, N_R);
    Mjerenje p = ispisi_fazu("PROBE", m_probe, N_S);
    ispisi_ukupno({b, p}, N_R + N_S);
    return 0;
}
