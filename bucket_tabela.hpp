// Hash tabela u kojoj je svaki bucket uzastopan komad niza, za build stranu
// radix joina (redovi jedne particije R).
//
// Build preuredi redove particije po bucketima, istim postupkom kao particionisi()
// (histogram -> prefiksni zbir -> raspodjela), samo po bucketima umjesto po particijama:
//
//   _redovi:   [ redovi bucketa 0 | redovi bucketa 1 | ... ]   jedan niz
//   _pocetak:  _pocetak[b] = gdje u _redovi pocinje bucket b, a _pocetak[b + 1] gdje
//              zavrsava; _pocetak je prefiksni zbir histograma (broja redova po bucketu)
//
// Primjer (4 bucketa, histogram 2, 3, 0, 1):
//   _redovi  = [ 12 20 | 7 3 9 | 15 ]      _pocetak = [ 0, 2, 5, 5, 6 ]
//
// U odnosu na NizTabela (niz_tabela.hpp) ova verzija uklanja slabost:
//   [5] nema lanca: probe zna unaprijed koliko redova ima bucket i gdje su
//       (jedan do drugog), pa ne cita "sljedeci" da bi nasao kraj lanca, i umjesto
//       tri nasumicna pristupa po koraku (_prvi, _redovi, _sljedeci) ima dva:
//       _pocetak[b] i _pocetak[b + 1] (susjedni, ista keš linija), pa redove bucketa
// Sve ostalo je isto kao u NizTabela, da se vidi efekat samo ove promjene:
// isti hash i pomak, isti broj bucketa ([4] ostaje), jedna tabela za sve particije.
// Kljucevi u R i dalje ne moraju biti jedinstveni: probe pregleda cijeli bucket.
//
// Cijena: build ima dva prolaza kroz particiju i kopira redove (uzastopno, u bafer
// koji je vec u kesu), sto vraca dio slabosti [2], ali bez pokazivaca po redu.
#pragma once

#include "common.hpp"

class BucketTabela {
public:
    // BUILD: preuredi redovi[0..n-1] po bucketima. Vektori se ponovo koriste, pa se
    // memorija zauzima samo kad particija naraste preko najvece dosad.
    // pomak: isto kao u NizTabela, u radix joinu pomak = B (vidi niz_tabela.hpp).
    void napravi(const Red* redovi, size_t n, int pomak) {
        _pomak = pomak;

        // broj bucketa: najmanji stepen dvojke >= n (kao u NizTabela)
        size_t broj_bucketa = 1;
        while (broj_bucketa < n) broj_bucketa *= 2;
        _maska = broj_bucketa - 1;

        // 1) histogram: koliko redova ide u svaki bucket (broji se u _pocetak[b + 1])
        _pocetak.assign(broj_bucketa + 1, 0);
        for (size_t i = 0; i < n; ++i)
            _pocetak[bucket(redovi[i].key) + 1]++;

        // 2) prefiksni zbir: _pocetak[b] = gdje pocinje bucket b
        for (size_t b = 0; b < broj_bucketa; ++b)
            _pocetak[b + 1] += _pocetak[b];

        // 3) raspodjela: svaki red upisi na sljedece slobodno mjesto svog bucketa
        _redovi.resize(n);
        _pozicija.assign(_pocetak.begin(), _pocetak.end() - 1);
        for (size_t i = 0; i < n; ++i)
            _redovi[_pozicija[bucket(redovi[i].key)]++] = redovi[i];
    }

    // PROBE: pozovi f(red) za svaki red iz R sa datim kljucem.
    // Pregleda tacno redove bucketa, koji stoje jedan do drugog.
    template <class F>
    void za_svaki(uint32_t key, F f) const {
        if (_pocetak.empty()) return;  // tabela jos nije napravljena
        const size_t b = bucket(key);
        for (uint32_t i = _pocetak[b]; i < _pocetak[b + 1]; ++i)
            if (_redovi[i].key == key)
                f(_redovi[i]);
    }

private:
    // Isti hash kao u NizTabela.
    size_t bucket(uint32_t key) const {
        return (uint64_t)(key >> _pomak) * 2654435761ull & _maska;
    }

    int _pomak = 0;
    size_t _maska = 0;
    std::vector<Red> _redovi;          // redovi particije, poredani po bucketima
    std::vector<uint32_t> _pocetak;    // broj bucketa + 1 elemenata
    std::vector<uint32_t> _pozicija;   // pomocni niz za raspodjelu u buildu
};
