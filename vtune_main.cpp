// Jedna verzija hash joina po programu, za profiliranje u Intel VTune-u.
//
// Isti izvorni fajl se prevodi sest puta (vidi build.bat), svaki put sa drugom
// vrijednoscu VARIJANTA, pa nastane sest .exe fajlova:
//   1 v1_naivni.exe           - jedna hash tabela od cvorova za cijelu R
//   2 v2_radix_cvorovi.exe    - radix, bucket je lista cvorova
//   3 v3_radix_lanac.exe      - radix, lanac kroz nizove indeksa
//   4 v4_radix_csr.exe        - radix, bucket je uzastopan komad niza
//   5 v5_radix_lanac_pool.exe - radix lanac, baferi za particije zauzeti unaprijed
//   6 v6_radix_lanac_opt.exe  - kao 5, plus write-combining baferi pri particionisanju
//                               i __restrict (vidi radix_opt.hpp)
//
// Svaki program:
//   1) generise podatke (ne profilira se),
//   2) ponavlja SAMO svoj join dok ne prode bar `sekunde` sekundi
//      (VTune treba bar ~1 s izvrsavanja da bi metrike bile pouzdane, MUX > 0.8),
//   3) provjerava rezultat svakog ponavljanja.
//
// Izolacija algoritma: ako je preveden sa KORISTI_ITT (build.bat to radi kad
// nade VTune), VTune prikuplja podatke samo izmedju __itt_resume i __itt_pause,
// tj. samo za join, bez generisanja podataka.
//
// Pokretanje:  v1_naivni.exe [N_R] [N_S] [B] [sekunde]
//   podrazumijevano N_R = 1 000 000, N_S = 10 000 000, B = 8, sekunde = 3
//   (B se ignorise kod naivnog)

#include "hash_join.hpp"
#include "radix_join.hpp"
#include "radix_opt.hpp"

#include <chrono>
#include <cstdlib>

#ifdef KORISTI_ITT
#include <ittnotify.h>
#define PROFIL_PAUZA()   __itt_pause()
#define PROFIL_NASTAVI() __itt_resume()
#else
#define PROFIL_PAUZA()
#define PROFIL_NASTAVI()
#endif

#ifndef VARIJANTA
#error "Prevedi sa /DVARIJANTA=1..6 (vidi build.bat)"
#endif

#if VARIJANTA == 1
static const char* NAZIV = "naivni";
#elif VARIJANTA == 2
static const char* NAZIV = "radix cvorovi";
#elif VARIJANTA == 3
static const char* NAZIV = "radix lanac";
#elif VARIJANTA == 4
static const char* NAZIV = "radix CSR";
#elif VARIJANTA == 5
static const char* NAZIV = "radix lanac pool";
#elif VARIJANTA == 6
static const char* NAZIV = "radix lanac opt";
#else
#error "VARIJANTA mora biti 1..6"
#endif

int main(int argc, char** argv) {
    PROFIL_PAUZA();  // generisanje podataka se ne profilira

    const size_t N_R     = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1'000'000;
    const size_t N_S     = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 10'000'000;
    const int    B       = argc > 3 ? std::atoi(argv[3]) : 8;
    const double sekunde = argc > 4 ? std::atof(argv[4]) : 3.0;

    std::vector<Red> R, S;
    generisi(N_R, N_S, R, S);
    const Rezultat ocekivano = ocekivani_rezultat(S);

#if VARIJANTA == 1
    (void)B;  // naivni join nema particije
    std::printf("%s, R = %zu, S = %zu, cilj %.1f s\n", NAZIV, N_R, N_S, sekunde);
#else
    std::printf("%s, R = %zu, S = %zu, B = %d, cilj %.1f s\n", NAZIV, N_R, N_S, B, sekunde);
#endif

#if VARIJANTA == 5
    // Pool: baferi zauzeti jednom, izvan profilisanog dijela (resize upise nule,
    // pa su sve stranice vec mapirane).
    Particije Rp, Sp;
    Rp.redovi.resize(R.size());
    Sp.redovi.resize(S.size());
#elif VARIJANTA == 6
    // Isto kao 5: poravnati baferi za particije zauzeti i mapirani unaprijed.
    ParticijeOpt Rp, Sp;
    Rp.redovi.resize(R.size());
    Sp.redovi.resize(S.size());
#endif

    using sat = std::chrono::steady_clock;
    int ponavljanja = 0;
    bool ok = true;
    double ukupno_s = 0;

    PROFIL_NASTAVI();  // od ovdje VTune mjeri: samo join
    const auto t0 = sat::now();
    do {
        Rezultat rez;
#if VARIJANTA == 1
        rez = hash_join(R, S);
#elif VARIJANTA == 2
        rez = radix_join_cvorovi(R, S, B);
#elif VARIJANTA == 3
        rez = radix_join_lanac(R, S, B);
#elif VARIJANTA == 4
        rez = radix_join_csr(R, S, B);
#elif VARIJANTA == 5
        particionisi_u(R, B, Rp);
        particionisi_u(S, B, Sp);
        rez = join_particija_lanac(Rp, Sp, B);
#elif VARIJANTA == 6
        particionisi_opt(R, B, Rp);
        particionisi_opt(S, B, Sp);
        rez = join_particija_opt(Rp, Sp, B);
#endif
        // Rezultat se mora iskoristiti, inace ga kompajler moze izbaciti.
        if (!isti(rez, ocekivano)) { ok = false; break; }
        ++ponavljanja;
        ukupno_s = std::chrono::duration<double>(sat::now() - t0).count();
    } while (ukupno_s < sekunde || ponavljanja < 3);
    PROFIL_PAUZA();

    if (!ok) {
        std::printf("GRESKA: pogresan rezultat u ponavljanju %d\n", ponavljanja + 1);
        return 1;
    }
    std::printf("OK: %d ponavljanja, ukupno %.2f s, prosjek %.1f ms po joinu, %.1f M redova/s\n",
                ponavljanja, ukupno_s, ukupno_s * 1e3 / ponavljanja,
                (double)(N_R + N_S) * ponavljanja / ukupno_s / 1e6);
    return 0;
}
