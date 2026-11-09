// Poredjenje naivnog i radix hash joina na istim podacima.
// Radix join se mjeri za vise vrijednosti B (broj radix bitova, 2^B particija).
//
// Prevodjenje i pokretanje: vidi README.md.
// Cache promasaji se mjere samo na Linuxu (i WSL-u).
//   ./join [N_R] [N_S]

#include "hash_join.hpp"
#include "radix_join.hpp"

#include <cstdlib>

const int RADIX_BITOVI[] = {2, 4, 6, 8, 10, 12, 14, 16};

// Medijane dvije faze jedne verzije joina.
struct Faze {
    Mjerenje faza1, faza2;
    bool ok;
};

// Naivni: faza 1 = build, faza 2 = probe.
Faze mjeri_naivni(const std::vector<Red>& R, const std::vector<Red>& S, const Rezultat& ocekivano) {
    std::vector<Mjerenje> m1, m2;
    for (int i = 0; i < ZAGRIJAVANJA + PONAVLJANJA; ++i) {
        HashTabela tabela;
        Rezultat rez;
        Mjerenje a = mjeri([&] { tabela = build(R); });
        Mjerenje b = mjeri([&] { rez = probe(tabela, S); });

        // Rezultat moramo iskoristiti, inace ga kompajler smatra nepotrebnim
        // i izbaci cijeli probe (izmjerilo bi se 0 ms).
        if (!isti(rez, ocekivano)) return {a, b, false};
        if (i >= ZAGRIJAVANJA) { m1.push_back(a); m2.push_back(b); }
    }
    return {medijana(m1), medijana(m2), true};
}

// Radix: faza 1 = particionisanje R i S, faza 2 = join po particijama.
Faze mjeri_radix(const std::vector<Red>& R, const std::vector<Red>& S, int B, const Rezultat& ocekivano) {
    std::vector<Mjerenje> m1, m2;
    for (int i = 0; i < ZAGRIJAVANJA + PONAVLJANJA; ++i) {
        Particije Rp, Sp;
        Rezultat rez;
        Mjerenje a = mjeri([&] { Rp = particionisi(R, B); Sp = particionisi(S, B); });
        Mjerenje b = mjeri([&] { rez = join_particija(Rp, Sp); });

        if (!isti(rez, ocekivano)) return {a, b, false};
        if (i >= ZAGRIJAVANJA) { m1.push_back(a); m2.push_back(b); }
    }
    return {medijana(m1), medijana(m2), true};
}

// Jedan red tabele. Brojaci se sabiraju za obje faze i dijele brojem redova (R + S).
void ispisi_red(const char* naziv, const Faze& f, double naivno_ms, size_t redova) {
    const double ukupno = f.faza1.ms + f.faza2.ms;
    std::printf("%-12s %9.1f %9.1f %9.1f %8.2fx %9.1f", naziv,
                f.faza1.ms, f.faza2.ms, ukupno, naivno_ms / ukupno, redova / ukupno / 1e3);

    auto po_redu = [&](long long a, long long b) {
        if (a < 0 || b < 0) std::printf(" %9s", "-");  // brojac nije dostupan
        else                std::printf(" %9.2f", (double)(a + b) / redova);
    };
    po_redu(f.faza1.l1_miss, f.faza2.l1_miss);
    po_redu(f.faza1.llc_miss, f.faza2.llc_miss);
    std::printf("\n");
    std::fflush(stdout);
}

int main(int argc, char** argv) {
    const size_t N_R = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1'000'000;
    const size_t N_S = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 10'000'000;

    std::vector<Red> R, S;
    generisi(N_R, N_S, R, S);
    const Rezultat ocekivano = ocekivani_rezultat(S);

    std::printf("R: %zu redova, S: %zu redova, ocekivano %llu redova rezultata\n",
                N_R, N_S, (unsigned long long)ocekivano.broj_redova);
    std::printf("%d zagrijavanje + %d mjerenja, vremena su medijane u ms\n\n",
                ZAGRIJAVANJA, PONAVLJANJA);
    std::printf("faza 1: naivni = build,  radix = particionisanje R i S\n");
    std::printf("faza 2: naivni = probe,  radix = join po particijama (build + probe)\n");
    std::printf("L1D/red, LLC/red: cache promasaji obje faze po redu (R + S)\n\n");
    std::printf("%-12s %9s %9s %9s %9s %9s %9s %9s\n",
                "verzija", "faza 1", "faza 2", "ukupno", "ubrzanje", "M red/s", "L1D/red", "LLC/red");

    const size_t redova = N_R + N_S;

    Faze naivni = mjeri_naivni(R, S, ocekivano);
    if (!naivni.ok) { std::printf("GRESKA: naivni join vratio pogresan rezultat\n"); return 1; }
    const double naivno_ms = naivni.faza1.ms + naivni.faza2.ms;
    ispisi_red("naivni", naivni, naivno_ms, redova);

    for (int B : RADIX_BITOVI) {
        Faze radix = mjeri_radix(R, S, B, ocekivano);
        if (!radix.ok) { std::printf("GRESKA: radix join (B = %d) vratio pogresan rezultat\n", B); return 1; }
        char naziv[32];
        std::snprintf(naziv, sizeof(naziv), "radix B=%d", B);
        ispisi_red(naziv, radix, naivno_ms, redova);
    }
    return 0;
}
