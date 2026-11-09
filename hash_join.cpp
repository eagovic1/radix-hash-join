// Naivni hash join: R ⋈ S po kljucu (R.key = S.key).
//
//   R - manja tabela (build), kljucevi 0..N_R-1, svaki tacno jednom, izmijesani
//   S - veca tabela (probe), svaki kljuc je slucajan broj iz 0..N_R-1
//
// Svaki red iz S ima tacno jedan par u R, pa join mora vratiti N_S redova.
//
// Samo Linux / WSL (cache promasaji se citaju preko perf_event_open):
//   g++ -O2 -std=c++17 hash_join.cpp -o hash_join
//   ./hash_join [N_R] [N_S]

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <unordered_map>
#include <vector>

#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

struct Red {
    uint32_t key;
    uint32_t payload;
};

// ---------------------------------------------------------------------------
// Hash join
// ---------------------------------------------------------------------------
// Build: ubaci sve redove iz R u hash tabelu (kljuc -> payload).
// Probe: za svaki red iz S potrazi kljuc u tabeli; ako postoji, to je rezultat.
// Rezultat ne cuvamo u memoriji, nego brojimo pogotke i sabiramo payload-e
// (checksum), da se vidi da je join tacan.
struct Rezultat {
    uint64_t broj_redova = 0;
    uint64_t checksum = 0;
};

Rezultat hash_join(const std::vector<Red>& R, const std::vector<Red>& S) {
    // 1) BUILD faza
    std::unordered_map<uint32_t, uint32_t> tabela;
    tabela.reserve(R.size());
    for (const Red& r : R)
        tabela[r.key] = r.payload;

    // 2) PROBE faza
    Rezultat rez;
    for (const Red& s : S) {
        auto it = tabela.find(s.key);
        if (it != tabela.end()) {
            rez.broj_redova++;
            rez.checksum += it->second + s.payload;
        }
    }
    return rez;
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

    void ispisi(const char* ime) const {
        long long v;
        if (fd < 0 || read(fd, &v, sizeof(v)) != sizeof(v))
            std::printf("%-16s nije dostupno\n", ime);
        else
            std::printf("%-16s %lld\n", ime, v);
    }
};

int main(int argc, char** argv) {
    const size_t N_R = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1'000'000;
    const size_t N_S = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 10'000'000;

    // --- generisanje podataka (ne ulazi u mjerenje) ---
    std::mt19937 rng(42);

    std::vector<Red> R(N_R);
    for (size_t i = 0; i < N_R; ++i) R[i] = {(uint32_t)i, (uint32_t)(i * 7)};
    std::shuffle(R.begin(), R.end(), rng);

    std::uniform_int_distribution<uint32_t> slucajan_kljuc(0, (uint32_t)N_R - 1);
    std::vector<Red> S(N_S);
    for (size_t i = 0; i < N_S; ++i) S[i] = {slucajan_kljuc(rng), (uint32_t)i};

    // --- mjerenje: vrijeme i cache promasaji samo za hash join ---
    const uint64_t L1D_READ_MISS = PERF_COUNT_HW_CACHE_L1D
                                 | (PERF_COUNT_HW_CACHE_OP_READ << 8)
                                 | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
    Brojac l1_miss(PERF_TYPE_HW_CACHE, L1D_READ_MISS);              // L1 data keš
    Brojac llc_ref(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_REFERENCES); // zadnji nivo (LLC)
    Brojac llc_miss(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES);

    l1_miss.start(); llc_ref.start(); llc_miss.start();
    auto t0 = std::chrono::steady_clock::now();

    Rezultat rez = hash_join(R, S);

    auto t1 = std::chrono::steady_clock::now();
    l1_miss.stop(); llc_ref.stop(); llc_miss.stop();

    // --- ispis ---
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::printf("R: %zu redova, S: %zu redova\n", N_R, N_S);
    std::printf("rezultat: %llu redova (ocekivano %zu), checksum %llu\n",
                (unsigned long long)rez.broj_redova, N_S,
                (unsigned long long)rez.checksum);
    std::printf("vrijeme: %.1f ms\n", ms);
    l1_miss.ispisi("L1D promasaji:");
    llc_ref.ispisi("LLC pristupi:");
    llc_miss.ispisi("LLC promasaji:");
    return 0;
}
