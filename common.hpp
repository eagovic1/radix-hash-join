// Zajednicki dio za hash_join.hpp, radix_join.hpp i main.cpp:
// tabele, generisanje podataka, provjera rezultata i mjerenje
// (vrijeme na svim sistemima, cache promasaji preko perf_event_open samo na Linuxu).
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

#ifdef __linux__
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

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

inline bool isti(const Rezultat& a, const Rezultat& b) {
    return a.broj_redova == b.broj_redova && a.checksum == b.checksum;
}

// ---------------------------------------------------------------------------
// Brojac hardverskog dogadjaja (npr. cache promasaja) preko perf_event_open.
// Broji samo za ovaj proces, samo nas (user-space) kod, izmedju start() i stop().
// perf_event_open postoji samo na Linuxu; na Windowsu i macOS-u brojaci
// vracaju -1, pa se u tabeli ispisuje "-", a vrijeme se mjeri normalno.
// ---------------------------------------------------------------------------
#ifdef __linux__

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

#else  // Windows, macOS: nema brojaca

struct Brojac {
    void start() {}
    void stop() {}
    long long procitaj() const { return -1; }
};
inline Brojac l1_miss, llc_ref, llc_miss;

#endif

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

// Medijansko mjerenje (po vremenu): srednji element sortiranih mjerenja,
// kod parnog broja gornji od dva. Brojaci su iz tog istog pokretanja.
inline Mjerenje medijana(std::vector<Mjerenje> m) {
    std::sort(m.begin(), m.end(), [](const Mjerenje& a, const Mjerenje& b) { return a.ms < b.ms; });
    return m[m.size() / 2];
}
