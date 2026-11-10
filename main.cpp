// Poredjenje pet verzija hash joina na istim podacima: naivni i cetiri radix verzije.
// Tri radix verzije se razlikuju samo po rasporedu hash tabele: cvorovi ("radix cvorovi"),
// lanac kroz nizove indeksa ("radix lanac") i bucketi kao uzastopni komadi niza
// ("radix CSR"). Peta, "radix lanac pool", je radix lanac sa baferima za particije
// zauzetim jednom, unaprijed, umjesto u svakom pokretanju.
// Radix verzije se mjere za vise vrijednosti B (broj radix bitova, 2^B particija).
//
// Prevodjenje i pokretanje: vidi README.md.
// Cache promasaji se mjere samo na Linuxu (i WSL-u).
//   ./join [N_R] [N_S]

#include "hash_join.hpp"
#include "radix_join.hpp"

#include <cstdlib>

const int RADIX_BITOVI[] = {2, 4, 6, 8};

// Medijane dvije faze jedne verzije joina.
struct Faze {
    Mjerenje faza1, faza2;
    bool ok;
};

// Naivni: faza 1 = build, faza 2 = probe.
Faze mjeri_naivni(const std::vector<Red>& R, const std::vector<Red>& S, const Rezultat& ocekivano) {
    std::vector<Mjerenje> m1, m2;
    for (int i = 0; i < ZAGRIJAVANJA + PONAVLJANJA; ++i) {
        CvorTabela tabela;
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
// join_po_particijama je join_particija_cvorovi, join_particija_lanac ili
// join_particija_csr, pa sve radix verzije koriste isti kod za mjerenje.
template <class JoinPoParticijama>
Faze mjeri_radix(const std::vector<Red>& R, const std::vector<Red>& S, int B, const Rezultat& ocekivano,
                 JoinPoParticijama join_po_particijama) {
    std::vector<Mjerenje> m1, m2;
    for (int i = 0; i < ZAGRIJAVANJA + PONAVLJANJA; ++i) {
        Particije Rp, Sp;
        Rezultat rez;
        Mjerenje a = mjeri([&] { Rp = particionisi(R, B); Sp = particionisi(S, B); });
        Mjerenje b = mjeri([&] { rez = join_po_particijama(Rp, Sp, B); });

        if (!isti(rez, ocekivano)) return {a, b, false};
        if (i >= ZAGRIJAVANJA) { m1.push_back(a); m2.push_back(b); }
    }
    return {medijana(m1), medijana(m2), true};
}

// Radix lanac pool: isto kao radix lanac, ali se baferi za particionisane R i S
// zauzmu jednom, prije mjerenja (kao memorijski pool u bazi), pa svako pokretanje
// samo prepise vec mapiranu memoriju. Ostale radix verzije u svakom pokretanju
// traze novu memoriju od operativnog sistema i placaju page faultove.
Faze mjeri_radix_pool(const std::vector<Red>& R, const std::vector<Red>& S, int B, const Rezultat& ocekivano) {
    // Zauzimanje bafera je izvan mjerenja. resize upise nule, pa su sve stranice
    // mapirane prije prvog mjerenja.
    Particije Rp, Sp;
    Rp.redovi.resize(R.size());
    Sp.redovi.resize(S.size());

    std::vector<Mjerenje> m1, m2;
    for (int i = 0; i < ZAGRIJAVANJA + PONAVLJANJA; ++i) {
        Rezultat rez;
        Mjerenje a = mjeri([&] { particionisi_u(R, B, Rp); particionisi_u(S, B, Sp); });
        Mjerenje b = mjeri([&] { rez = join_particija_lanac(Rp, Sp, B); });

        if (!isti(rez, ocekivano)) return {a, b, false};
        if (i >= ZAGRIJAVANJA) { m1.push_back(a); m2.push_back(b); }
    }
    return {medijana(m1), medijana(m2), true};
}

// Jedan red tabele. Brojaci se sabiraju za obje faze i dijele brojem redova (R + S).
void ispisi_red(const char* naziv, const Faze& f, double naivno_ms, size_t redova) {
    const double ukupno = f.faza1.ms + f.faza2.ms;
    std::printf("%-20s %9.1f %9.1f %9.1f %8.2fx %9.1f", naziv,
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
    std::printf("naivni:            jedna hash tabela od cvorova za cijelu R (cvor_tabela.hpp)\n");
    std::printf("radix cvorovi:     bucket je lista cvorova, nova tabela za svaku particiju (cvor_tabela.hpp)\n");
    std::printf("radix lanac:       bucket je lanac kroz nizove indeksa, jedna tabela (lanac_tabela.hpp)\n");
    std::printf("radix CSR:         bucket je uzastopan komad niza, bez lanca (csr_tabela.hpp)\n");
    std::printf("radix lanac pool:  radix lanac, baferi za particije zauzeti jednom, unaprijed\n");
    std::printf("faza 1: naivni = build,  radix verzije = particionisanje R i S\n");
    std::printf("faza 2: naivni = probe,  radix verzije = join po particijama (build + probe)\n");
    std::printf("L1D/red, LLC/red: cache promasaji obje faze po redu (R + S)\n\n");
    std::printf("%-20s %9s %9s %9s %9s %9s %9s %9s\n",
                "verzija", "faza 1", "faza 2", "ukupno", "ubrzanje", "M red/s", "L1D/red", "LLC/red");

    const size_t redova = N_R + N_S;

    Faze naivni = mjeri_naivni(R, S, ocekivano);
    if (!naivni.ok) { std::printf("GRESKA: naivni join vratio pogresan rezultat\n"); return 1; }
    const double naivno_ms = naivni.faza1.ms + naivni.faza2.ms;
    ispisi_red("naivni", naivni, naivno_ms, redova);

    for (int B : RADIX_BITOVI) {
        Faze radix = mjeri_radix(R, S, B, ocekivano, join_particija_cvorovi);
        if (!radix.ok) { std::printf("GRESKA: radix cvorovi join (B = %d) vratio pogresan rezultat\n", B); return 1; }
        char naziv[32];
        std::snprintf(naziv, sizeof(naziv), "radix cvorovi B=%d", B);
        ispisi_red(naziv, radix, naivno_ms, redova);
    }

    for (int B : RADIX_BITOVI) {
        Faze radix = mjeri_radix(R, S, B, ocekivano, join_particija_lanac);
        if (!radix.ok) { std::printf("GRESKA: radix lanac join (B = %d) vratio pogresan rezultat\n", B); return 1; }
        char naziv[32];
        std::snprintf(naziv, sizeof(naziv), "radix lanac B=%d", B);
        ispisi_red(naziv, radix, naivno_ms, redova);
    }

    for (int B : RADIX_BITOVI) {
        Faze radix = mjeri_radix(R, S, B, ocekivano, join_particija_csr);
        if (!radix.ok) { std::printf("GRESKA: radix CSR join (B = %d) vratio pogresan rezultat\n", B); return 1; }
        char naziv[32];
        std::snprintf(naziv, sizeof(naziv), "radix CSR B=%d", B);
        ispisi_red(naziv, radix, naivno_ms, redova);
    }

    for (int B : RADIX_BITOVI) {
        Faze radix = mjeri_radix_pool(R, S, B, ocekivano);
        if (!radix.ok) { std::printf("GRESKA: radix lanac pool join (B = %d) vratio pogresan rezultat\n", B); return 1; }
        char naziv[32];
        std::snprintf(naziv, sizeof(naziv), "radix lanac pool B=%d", B);
        ispisi_red(naziv, radix, naivno_ms, redova);
    }
    return 0;
}
