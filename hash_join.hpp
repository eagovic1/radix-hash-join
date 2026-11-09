// Naivni hash join: R ⋈ S po kljucu (R.key = S.key).
//   BUILD: hash tabela od cijele R
//   PROBE: za svaki red iz S potrazi kljuc u tabeli
#pragma once

#include "common.hpp"

// BUILD: ubaci sve redove iz R u hash tabelu.
inline HashTabela build(const std::vector<Red>& R) {
    HashTabela tabela;
    tabela.reserve(R.size());
    for (const Red& r : R)
        tabela[r.key] = r.payload;
    return tabela;
}

// PROBE: za svaki red iz S potrazi kljuc u tabeli; ako postoji, to je red rezultata.
// Rezultat ne cuvamo u memoriji, nego brojimo redove i sabiramo payload-e.
inline Rezultat probe(const HashTabela& tabela, const std::vector<Red>& S) {
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

inline Rezultat hash_join(const std::vector<Red>& R, const std::vector<Red>& S) {
    return probe(build(R), S);
}
