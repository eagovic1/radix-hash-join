// Hash tabela sa bucketima, za build stranu joina (redovi iz R).
//
// Redovi se ne kopiraju: tabela pamti samo indekse u postojeci niz redova.
// Svaki bucket je lanac redova povezan indeksima:
//
//   prvi[b]      = indeks prvog reda u bucketu b (ili PRAZNO)
//   sljedeci[i]  = indeks sljedeceg reda u istom bucketu kao red i (ili PRAZNO)
//
// Za razliku od std::unordered_map nema alokacije po elementu: cijela tabela su
// dva niza uint32_t (4 B po bucketu + 4 B po redu), pa je mala i kompaktna.
#pragma once

#include "common.hpp"

class HashTabela {
public:
    HashTabela() = default;

    // BUILD: ubaci redove[0..n-1] u tabelu. Niz redova mora postojati dok se tabela koristi.
    HashTabela(const Red* redovi, size_t n) : _redovi(redovi) {
        // broj bucketa: najmanji stepen dvojke >= 2n (najvise pola reda po bucketu,
        // pa su lanci kratki i probe najcesce nadje red u prvom koraku)
        _bitova = 1;
        while ((size_t(1) << _bitova) < 2 * n) _bitova++;

        _prvi.assign(size_t(1) << _bitova, PRAZNO);
        _sljedeci.resize(n);

        // Svaki red stavljamo na pocetak lanca svog bucketa.
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t b = bucket(redovi[i].key);
            _sljedeci[i] = _prvi[b];
            _prvi[b] = i;
        }
    }

    // PROBE: vrati red iz R sa datim kljucem, ili nullptr ako ga nema.
    // (Kod nas su kljucevi u R jedinstveni, pa je dovoljan prvi pogodak.)
    const Red* nadji(uint32_t key) const {
        for (uint32_t i = _prvi[bucket(key)]; i != PRAZNO; i = _sljedeci[i])
            if (_redovi[i].key == key)
                return &_redovi[i];
        return nullptr;
    }

private:
    static constexpr uint32_t PRAZNO = UINT32_MAX;

    // Bucket = donjih _bitova bitova hasha kljuca. Hash je finalizer iz MurmurHash3:
    // dobro izmijesa sve bitove kljuca, pa se i uzastopni kljucevi, i kljucevi
    // jedne radix particije (svi imaju iste donje bitove), ravnomjerno rasporede.
    uint32_t bucket(uint32_t key) const {
        uint32_t h = key;
        h ^= h >> 16; h *= 0x85ebca6bu;
        h ^= h >> 13; h *= 0xc2b2ae35u;
        h ^= h >> 16;
        return h & ((uint32_t(1) << _bitova) - 1);
    }

    const Red* _redovi = nullptr;
    int _bitova = 1;
    std::vector<uint32_t> _prvi;      // po bucketu
    std::vector<uint32_t> _sljedeci;  // po redu
};
