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
#include <atomic>
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
// Hash join u dvije faze
// ---------------------------------------------------------------------------

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

Brojac l1_miss(PERF_TYPE_HW_CACHE, L1D_READ_MISS);                  // L1 data keš
Brojac llc_ref(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_REFERENCES);  // zadnji nivo (LLC)
Brojac llc_miss(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES);

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
// pokretanja (ukupno i po redu ulazne tabele te faze).
// Medijana = srednji element sortiranih mjerenja (kod parnog broja gornji od dva).
Mjerenje ispisi_fazu(const char* ime, std::vector<Mjerenje> m, size_t redova) {
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

int main(int argc, char** argv) {
    const size_t N_R = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1'000'000;
    const size_t N_S = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 10'000'000;

    // --- generisanje podataka (ne ulazi u mjerenje) ---
    std::mt19937 rng(42);

    std::vector<Red> R(N_R);
    for (size_t i = 0; i < N_R; ++i) R[i] = {(uint32_t)i, (uint32_t)(i * 7)};  // payload = 7 * key
    std::shuffle(R.begin(), R.end(), rng);

    std::uniform_int_distribution<uint32_t> slucajan_kljuc(0, (uint32_t)N_R - 1);
    std::vector<Red> S(N_S);
    for (size_t i = 0; i < N_S; ++i) S[i] = {slucajan_kljuc(rng), (uint32_t)i};

    std::printf("R: %zu redova, S: %zu redova\n", N_R, N_S);

    // --- provjera ispravnosti prije mjerenja ---
    // Ocekivani rezultat racunamo bez hash tabele: svaki red iz S ima par u R,
    // a payload tog para je 7 * key.
    Rezultat ocekivano;
    for (const Red& s : S) {
        ocekivano.broj_redova++;
        ocekivano.checksum += (uint64_t)(uint32_t)(s.key * 7) + s.payload;
    }
    Rezultat rez = probe(build(R), S);
    if (rez.broj_redova != ocekivano.broj_redova || rez.checksum != ocekivano.checksum) {
        std::printf("GRESKA: join vratio %llu redova (checksum %llu), ocekivano %llu (checksum %llu)\n",
                    (unsigned long long)rez.broj_redova, (unsigned long long)rez.checksum,
                    (unsigned long long)ocekivano.broj_redova, (unsigned long long)ocekivano.checksum);
        return 1;
    }
    std::printf("provjera: OK (%llu redova, checksum %llu)\n",
                (unsigned long long)rez.broj_redova, (unsigned long long)rez.checksum);

    // --- mjerenje: build i probe odvojeno ---
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
    std::printf("\nUKUPNO (build + probe medijana): %.1f ms, %.1f M redova/s\n",
                b.ms + p.ms, (N_R + N_S) / (b.ms + p.ms) / 1e3);
    return 0;
}
