// LanacTabela: hash tabela u kojoj je bucket lanac kroz nizove indeksa, za build
// stranu radix joina (redovi jedne particije R). Koristi je verzija "radix lanac".
//
// Redovi se ne kopiraju: tabela pamti pokazivac na niz redova particije i indekse u njega.
// Svaki bucket je lanac redova povezan indeksima:
//
//   _prvi[b]     = indeks prvog reda u bucketu b (ili PRAZNO)
//   _sljedeci[i] = indeks sljedeceg reda u istom bucketu kao red i (ili PRAZNO)
//
// U odnosu na CvorTabela (cvor_tabela.hpp) ova verzija uklanja slabosti:
//   [1] nema cvorova alociranih sa new: cijela tabela su dva uzastopna niza
//   [2] red se ne kopira i nema pokazivaca po redu: 4 B (indeks) po redu + 4 B po bucketu
//   [3] broj bucketa je stepen dvojke, pa se bucket racuna sa & maskom, ne sa %
//   [6] napravi() ponovo koristi iste vektore, pa radix join drzi jednu tabelu za
//       sve particije, bez alokacije po particiji
// Namjerno ostaju:
//   [4] lanci: broj bucketa je samo >= n (oko jedan red po bucketu)
//   [5] probe obilazi cijeli lanac s grananjem (ne staje na prvom pogotku)
//
// Napomena o hashu: hash je isti kao u CvorTabela, (key >> pomak) * 2654435761,
// samo sa & maska umjesto %. & maska uzima DONJE bitove proizvoda, a oni zavise
// samo od donjih bitova kljuca (mnozenje prenosi bitove samo prema gore).
// Za nase guste kljuceve to je cak savrsena raspodjela, ali kljucevi koji se
// razlikuju samo u gornjim bitovima bi pali u isti bucket. Bolje bi bilo uzeti
// gornje bitove proizvoda (Fibonacci hashing) ili hash koji mijesa sve bitove.
#pragma once

#include "common.hpp"

class LanacTabela {
public:
    // BUILD: ubaci redovi[0..n-1] u tabelu. Niz redova mora postojati dok se tabela koristi.
    // Vektori se ponovo koriste (assign/resize), pa se memorija zauzima samo kad
    // particija naraste preko najvece dosad.
    //
    // pomak: koliko donjih bitova kljuca hash preskace (u radix joinu pomak = B).
    // Svi kljucevi jedne particije imaju istih donjih B bitova, jer su po njima
    // particionisani, pa su oblika p + k * 2^B. Kad hash ne bi preskocio tih B bitova,
    // (kljuc * c) & maska bi zavisio skoro samo od p, pa bi svi kljucevi particije
    // pali u mali broj bucketa i lanci bi bili ogromni. Sa pomak = B hash vidi samo k.
    void napravi(const Red* redovi, size_t n, int pomak) {
        _redovi = redovi;
        _pomak = pomak;

        // broj bucketa: najmanji stepen dvojke >= n
        size_t broj_bucketa = 1;
        while (broj_bucketa < n) broj_bucketa *= 2;
        _maska = broj_bucketa - 1;

        _prvi.assign(broj_bucketa, PRAZNO);
        _sljedeci.resize(n);

        // Svaki red stavljamo na pocetak lanca svog bucketa.
        for (uint32_t i = 0; i < n; ++i) {
            size_t b = bucket(redovi[i].key);
            _sljedeci[i] = _prvi[b];
            _prvi[b] = i;
        }
    }

    // PROBE: pozovi f(red) za svaki red iz R sa datim kljucem.
    // Obilazi cijeli lanac [5], jer kljucevi u R ne moraju biti jedinstveni.
    template <class F>
    void za_svaki(uint32_t key, F f) const {
        if (_prvi.empty()) return;  // tabela jos nije napravljena
        for (uint32_t i = _prvi[bucket(key)]; i != PRAZNO; i = _sljedeci[i])
            if (_redovi[i].key == key)
                f(_redovi[i]);
    }

private:
    static constexpr uint32_t PRAZNO = UINT32_MAX;

    // Isti multiplikativni hash kao u CvorTabela, ali & maska umjesto % [3].
    size_t bucket(uint32_t key) const {
        return (uint64_t)(key >> _pomak) * 2654435761ull & _maska;
    }

    const Red* _redovi = nullptr;
    int _pomak = 0;
    size_t _maska = 0;
    std::vector<uint32_t> _prvi;      // po bucketu
    std::vector<uint32_t> _sljedeci;  // po redu
};
