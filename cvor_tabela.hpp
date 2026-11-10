// CvorTabela: naivna lancana hash tabela sa cvorovima, za build stranu joina (redovi iz R).
// Koriste je verzije "naivni" i "radix cvorovi".
//
// Ovo je NAMJERNO spora referentna verzija, koja ostavlja prostor za optimizaciju.
// Namjerne slabosti (optimizovati ih jednu po jednu):
//
//   [1] svaki red je u posebnom cvoru alociranom sa new -> cvorovi su rasuti po
//       memoriji, pa je obilazak lanca pointer chasing (svaki korak novi skok)
//   [2] red se kopira u cvor (+ 8 B pokazivaca po redu)
//   [3] % u hash funkciji (dijeljenje) umjesto & maske
//   [4] broj bucketa = n -> duzi lanci nego sa 2n
//   [5] probe obilazi cijeli lanac s grananjem (ne staje na prvom pogotku)
//   [6] u radix joinu se za svaku particiju alocira nova tabela (new/delete po cvoru)
//
// Struktura: _bucketi[b] pokazuje na prvi cvor lanca bucketa b (ili nullptr),
// a svaki cvor pokazuje na sljedeci cvor u istom lancu.
#pragma once

#include "common.hpp"

class CvorTabela {
public:
    CvorTabela() = default;

    // BUILD: ubaci redove[0..n-1] u tabelu. Svaki red se kopira u novi cvor [1][2].
    //
    // pomak: koliko donjih bitova kljuca hash preskace. Naivni join koristi 0.
    // U radix joinu svi kljucevi jedne particije imaju istih donjih B bitova
    // (po njima su particionisani). Kad bi hash koristio i te bitove, kljucevi
    // particije bi bili oblika p + k * 2^B, pa bi (kljuc * c) % broj_bucketa
    // pao u mali broj bucketa: lanci bi bili ogromni i join bi se zaglavio.
    // Zato radix join pravi tabelu sa pomak = B, pa hash vidi samo k.
    CvorTabela(const Red* redovi, size_t n, int pomak = 0)
        : _bucketi(n > 0 ? n : 1, nullptr), _pomak(pomak) {  // broj bucketa = n [4]
        for (size_t i = 0; i < n; ++i) {
            Cvor*& prvi = _bucketi[bucket(redovi[i].key)];
            prvi = new Cvor{redovi[i], prvi};  // novi cvor na pocetak lanca
        }
    }

    ~CvorTabela() { oslobodi(); }

    // Kopiranje je zabranjeno (cvorovi bi se oslobodili dvaput), premjestanje je dozvoljeno.
    CvorTabela(const CvorTabela&) = delete;
    CvorTabela& operator=(const CvorTabela&) = delete;

    CvorTabela(CvorTabela&& o) noexcept
        : _bucketi(std::move(o._bucketi)), _pomak(o._pomak) {
        o._bucketi.clear();  // premjestena tabela je prazna
    }

    CvorTabela& operator=(CvorTabela&& o) noexcept {
        if (this != &o) {
            oslobodi();  // prvo oslobodi svoje postojece cvorove
            _bucketi = std::move(o._bucketi);
            _pomak = o._pomak;
            o._bucketi.clear();
        }
        return *this;
    }

    // PROBE: pozovi f(red) za svaki red iz R sa datim kljucem.
    // Obilazi cijeli lanac [5], jer kljucevi u R (kao u pravoj bazi) ne moraju
    // biti jedinstveni.
    template <class F>
    void za_svaki(uint32_t key, F f) const {
        if (_bucketi.empty()) return;  // prazna / premjestena tabela
        for (const Cvor* c = _bucketi[bucket(key)]; c != nullptr; c = c->sljedeci)
            if (c->red.key == key)
                f(c->red);
    }

private:
    struct Cvor {
        Red red;          // kopija reda [2]
        Cvor* sljedeci;   // sljedeci cvor u lancu
    };

    // Multiplikativni hash, pa % broj bucketa [3].
    size_t bucket(uint32_t key) const {
        return (uint64_t)(key >> _pomak) * 2654435761ull % _bucketi.size();
    }

    void oslobodi() {
        for (Cvor* c : _bucketi)
            while (c != nullptr) {
                Cvor* sljedeci = c->sljedeci;
                delete c;
                c = sljedeci;
            }
        _bucketi.clear();
    }

    std::vector<Cvor*> _bucketi;
    int _pomak = 0;
};
