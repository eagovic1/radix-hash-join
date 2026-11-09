// Zajednicki dio za hash_join.cpp i radix_join.cpp:
// tabele, generisanje podataka, provjera rezultata i mjerenje
// (vrijeme + cache promasaji preko perf_event_open, samo Linux / WSL).
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <unordered_map>
#include <vector>

#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

const int ZAGRIJAVANJA = 1;   // pokretanja koja se ne mjere
const int PONAVLJANJA = 10;   // mjerena pokretanja

struct Red {
    uint32_t key;
    uint32_t payload;
};

using HashTabela = std::unordered_map<uint32_t, uint32_t>;  // kljuc -> payload iz R

struct Rezultat {
    uint64_t broj_redova = 0;
    uint64_t checksum = 0;  // zbir R.payload + S.payload za sve parove
};

// ---------------------------------------------------------------------------
// Podaci
// ---------------------------------------------------------------------------
//   R - manja tabela (build), kljucevi 0..N_R-1, svaki tacno jednom, izmijesani
//   S - veca tabela (probe), svaki kljuc je slucajan broj iz 0..N_R-1
// Svaki red iz S ima tacno jedan par u R, pa join mora vratiti N_S redova.
inline void generisi(size_t N_R, size_t N_S, std::vector<Red>& R, std::vector<Red>& S) {
    std::mt19937 rng(42);

    R.resize(N_R);
    for (size_t i = 0; i < N_R; ++i) R[i] = {(uint32_t)i, (uint32_t)(i * 7)};  // payload = 7 * key
    std::shuffle(R.begin(), R.end(), rng);

    std::uniform_int_distribution<uint32_t> slucajan_kljuc(0, (uint32_t)N_R - 1);
    S.resize(N_S);
    for (size_t i = 0; i < N_S; ++i) S[i] = {slucajan_kljuc(rng), (uint32_t)i};
}

// Ocekivani rezultat, izracunat bez hash tabele: svaki red iz S ima par u R,
// a payload tog para je 7 * key.
inline Rezultat ocekivani_rezultat(const std::vector<Red>& S) {
    Rezultat rez;
    for (const Red& s : S) {
        rez.broj_redova++;
        rez.checksum += (uint64_t)(uint32_t)(s.key * 7) + s.payload;
    }
    return rez;
}

inline bool provjeri(const Rezultat& rez, const Rezultat& ocekivano) {
    if (rez.broj_redova == ocekivano.broj_redova && rez.checksum == ocekivano.checksum) {
        std::printf("provjera: OK (%llu redova, checksum %llu)\n",
                    (unsigned long long)rez.broj_redova, (unsigned long long)rez.checksum);
        return true;
    }
    std::printf("GRESKA: join vratio %llu redova (checksum %llu), ocekivano %llu (checksum %llu)\n",
                (unsigned long long)rez.broj_redova, (unsigned long long)rez.checksum,
                (unsigned long long)ocekivano.broj_redova, (unsigned long long)ocekivano.checksum);
    return false;
}

// ---------------------------------------------------------------------------
// Brojac hardverskog dogadjaja (npr. cache promasaja) preko perf_event_open.
// Broji samo za ovaj proces, samo nas (user-space) kod, izmedju start() i stop().
// ---------------------------------------------------------------------------
struct Brojac {
    int fd;

    Brojac(uint32_t type, uint64_t config) {
        perf_event_attr attr;
        std::memset(&attr, 0, sizeof(attr));
        attr.size = sizeof(attr);
        attr.type = type;
        attr.config = config;
        attr.disabled = 1;        // ukljucujemo ga rucno sa start()
        attr.exclude_kernel = 1;  // ne brojimo kernel
        attr.exclude_hv = 1;
        fd = (int)syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0);  // -1 = greska
    }
    void start() { ioctl(fd, PERF_EVENT_IOC_RESET, 0); ioctl(fd, PERF_EVENT_IOC_ENABLE, 0); }
    void stop()  { ioctl(fd, PERF_EVENT_IOC_DISABLE, 0); }

    long long procitaj() const {  // -1 = brojac nije dostupan
        long long v;
        if (fd < 0 || read(fd, &v, sizeof(v)) != sizeof(v)) return -1;
        return v;
    }
};

const uint64_t L1D_READ_MISS = PERF_COUNT_HW_CACHE_L1D
                             | (PERF_COUNT_HW_CACHE_OP_READ << 8)
                             | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);

inline Brojac l1_miss(PERF_TYPE_HW_CACHE, L1D_READ_MISS);                  // L1 data keš
inline Brojac llc_ref(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_REFERENCES);  // zadnji nivo (LLC)
inline Brojac llc_miss(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES);

// Rezultat jednog mjerenja jedne faze.
struct Mjerenje {
    double ms;
    long long l1_miss, llc_ref, llc_miss;
};

// Izvrsi f() i izmjeri vrijeme i cache dogadjaje samo za taj poziv.
template <class F>
Mjerenje mjeri(F f) {
    l1_miss.start(); llc_ref.start(); llc_miss.start();
    std::atomic_signal_fence(std::memory_order_seq_cst);  // kompajler ne smije
    auto t0 = std::chrono::steady_clock::now();           // pomjeriti kod preko
    f();                                                   // mjerenja
    auto t1 = std::chrono::steady_clock::now();
    std::atomic_signal_fence(std::memory_order_seq_cst);
    l1_miss.stop(); llc_ref.stop(); llc_miss.stop();

    return {std::chrono::duration<double, std::milli>(t1 - t0).count(),
            l1_miss.procitaj(), llc_ref.procitaj(), llc_miss.procitaj()};
}

// Ispis jedne faze: medijana/min/max vremena, brzina, i brojaci iz medijanskog
// pokretanja (ukupno i po redu).
// Medijana = srednji element sortiranih mjerenja (kod parnog broja gornji od dva).
inline Mjerenje ispisi_fazu(const char* ime, std::vector<Mjerenje> m, size_t redova) {
    std::sort(m.begin(), m.end(), [](const Mjerenje& a, const Mjerenje& b) { return a.ms < b.ms; });
    const Mjerenje& med = m[m.size() / 2];

    std::printf("\n%s (%zu redova)\n", ime, redova);
    std::printf("  vrijeme:  medijana %.1f ms, min %.1f ms, max %.1f ms\n",
                med.ms, m.front().ms, m.back().ms);
    std::printf("  brzina:   %.1f M redova/s\n", redova / med.ms / 1e3);

    auto brojac = [&](const char* naziv, long long v) {
        if (v < 0) std::printf("  %-15s nije dostupno\n", naziv);
        else       std::printf("  %-15s %12lld  (%.2f po redu)\n", naziv, v, (double)v / redova);
    };
    brojac("L1D promasaji:", med.l1_miss);
    brojac("LLC pristupi:", med.llc_ref);
    brojac("LLC promasaji:", med.llc_miss);
    return med;
}

// Zbir medijana svih faza.
inline void ispisi_ukupno(const std::vector<Mjerenje>& faze, size_t redova) {
    Mjerenje u{0, 0, 0, 0};
    for (const Mjerenje& f : faze) {
        u.ms += f.ms; u.l1_miss += f.l1_miss; u.llc_ref += f.llc_ref; u.llc_miss += f.llc_miss;
    }
    std::printf("\nUKUPNO (zbir medijana faza, %zu redova)\n", redova);
    std::printf("  vrijeme:  %.1f ms, %.1f M redova/s\n", u.ms, redova / u.ms / 1e3);
    std::printf("  L1D promasaji: %lld, LLC promasaji: %lld\n", u.l1_miss, u.llc_miss);
}
