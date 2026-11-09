// Naivni hash join: R ⋈ S po kljucu (R.key = S.key).
//   BUILD: hash tabela od cijele R
//   PROBE: za svaki red iz S potrazi kljuc u tabeli
#pragma once

#include "common.hpp"
#include "hash_tabela.hpp"

// BUILD: ubaci sve redove iz R u hash tabelu.
inline HashTabela build(const std::vector<Red>& R) {
    return HashTabela(R.data(), R.size());
}

// PROBE: za svaki red iz S potrazi kljuc u tabeli; ako postoji, to je red rezultata.
// Rezultat ne cuvamo u memoriji, nego brojimo redove i sabiramo payload-e.
inline Rezultat probe(const HashTabela& tabela, const std::vector<Red>& S) {
    Rezultat rez;
    for (const Red& s : S) {
        if (const Red* r = tabela.nadji(s.key)) {
            rez.broj_redova++;
            rez.checksum += (uint64_t)r->payload + s.payload;
        }
    }
    return rez;
}

inline Rezultat hash_join(const std::vector<Red>& R, const std::vector<Red>& S) {
    return probe(build(R), S);
}
