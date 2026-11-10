# Hash join: četiri verzije

Kratak pregled četiri implementacije hash joina `R ⋈ S` po ključu (`R.key = S.key`), sa ključnim razlikama i rezultatima mjerenja. Uputstvo za pokretanje je u [README.md](README.md).

## Zadatak

- **R** (build strana, manja): ključevi `0..N_R−1`, svaki tačno jednom, izmiješani.
- **S** (probe strana, veća): slučajni ključevi iz `0..N_R−1`.
- Red je par `(key, payload)` od 4 + 4 bajta.
- Svaki red iz S ima tačno jedan par u R, pa join vraća `N_S` redova. Ispravnost se provjerava brojem redova i checksum-om (zbir `R.payload + S.payload`), izračunatim bez joina.

Hash join ima dvije faze:
- **build:** od R napravi hash tabelu;
- **probe:** za svaki red iz S potraži ključ u tabeli.

## Četiri verzije ukratko

| | Particionisanje | Raspored bucketa | Broj tabela |
|---|---|---|---|
| **1. naivni** | ne | lista čvorova (`CvorTabela`) | jedna, za cijelu R |
| **2. radix čvorovi** | da, 2^B particija | lista čvorova (`CvorTabela`) | nova za svaku particiju |
| **3. radix lanac** | da, 2^B particija | lanac kroz nizove indeksa (`LanacTabela`) | jedna, ponovo korištena |
| **4. radix CSR** | da, 2^B particija | uzastopan komad jednog niza (`CsrTabela`) | jedna, ponovo korištena |

Naziv svake verzije je način joina + raspored hash tabele. Verzije 2–4 imaju isto particionisanje i razlikuju se samo po tome kako su redovi jednog bucketa smješteni u memoriji. Svaka sljedeća verzija mijenja jednu stvar, pa se u mjerenju vidi efekat svake promjene posebno.

## 1. Naivni hash join

Fajlovi: [`hash_join.hpp`](hash_join.hpp), [`cvor_tabela.hpp`](cvor_tabela.hpp)

Jedna hash tabela za cijelu R, pa probe svakim redom S.

**Problem:** kad tabela ne stane u keš, skoro svaki probe skače na nasumično mjesto u RAM-u (~1,2–2,2 LLC promašaja po redu). Probe tako troši skoro svo vrijeme.

Hash tabela je **namjerno spora referentna verzija**, sa numerisanim slabostima:

| | Slabost |
|---|---|
| [1] | svaki red u posebnom čvoru alociranom sa `new`, pa je obilazak lanca pointer chasing |
| [2] | red se kopira u čvor, plus 8 B pokazivača po redu |
| [3] | `%` (dijeljenje) u hash funkciji umjesto `&` maske |
| [4] | broj bucketa = n, pa su lanci duži nego sa 2n |
| [5] | probe obilazi cijeli lanac s grananjem |
| [6] | u radix joinu se za svaku particiju alocira nova tabela |

## 2. Radix čvorovi

Fajlovi: [`radix_join.hpp`](radix_join.hpp) (`particionisi`, `join_particija_cvorovi`)

**Ideja:** obje tabele se prvo podijele na 2^B particija po donjih B bitova ključa. Redovi sa istim ključem uvijek završe u particiji istog broja, pa je dovoljno spojiti `R_p` sa `S_p`. Tabela jedne particije je mala i stane u keš.

```
1) PARTICIONISANJE (histogram -> prefiksni zbir -> raspodjela)
   R -> R_0 | R_1 | ... | R_{P-1}        S -> S_0 | S_1 | ... | S_{P-1}

2) JOIN PO PARTICIJAMA
   za svaki p:  build(R_p)  ->  probe(S_p)
```

**Pomak u hashu:** svi ključevi jedne particije imaju istih donjih B bitova. Hash ih zato preskače (`key >> B`). Inače bi svi ključevi particije pali u mali broj bucketa i lanci bi postali ogromni.

**Razlika prema naivnom:** isti hash join, ali po malim particijama. Plaća se dodatna faza particionisanja.

## 3. Radix lanac (lanac kroz nizove indeksa)

Fajl: [`lanac_tabela.hpp`](lanac_tabela.hpp) (`join_particija_lanac`)

Isto particionisanje, ali tabela bez čvorova:

```
_prvi[b]     = indeks prvog reda u bucketu b
_sljedeci[i] = indeks sljedećeg reda u istom bucketu
redovi       = ostaju u particiji, ne kopiraju se
```

**Razlika prema verziji 2:** uklanja slabosti [1], [2], [3] i [6]:
- nema `new`/`delete` po redu ni po particiji;
- cijela tabela su dva uzastopna niza `uint32_t`;
- broj bucketa je stepen dvojke, pa se računa `&` umjesto `%`;
- jedna tabela se ponovo koristi za sve particije.

Bucket je i dalje **lanac** kroz niz, pa [4] i [5] ostaju.

## 4. Radix CSR (bucket kao uzastopan komad niza)

Fajl: [`csr_tabela.hpp`](csr_tabela.hpp) (`join_particija_csr`)

Build preuredi redove particije **po bucketima**, istim postupkom kao particionisanje, pa redovi istog bucketa stoje jedan do drugog:

```
histogram (redova po bucketu):   2    3    0    1
_pocetak (prefiksni zbir):     0    2    5    5    6
_redovi:                       [ 12 20 | 7 3 9 | 15 ]
                                 b0      b1      b3
```

Bucket `b` je `_redovi[_pocetak[b]] .. _redovi[_pocetak[b+1] − 1]`.

Naziv CSR (*compressed sparse row*) dolazi od istog rasporeda kod rijetkih matrica: jedan niz vrijednosti i niz početaka redova.

**Razlika prema verziji 3:** uklanja slabost [5]. Nema lanca, probe unaprijed zna gdje bucket počinje i završava, a redovi bucketa su jedan do drugog. Sve ostalo je isto kao u verziji 3.

**Cijena:** build mora premjestiti svaki red na njegovo mjesto u bucketu, umjesto da upiše samo indeks.

## Pregled slabosti po verzijama

| Slabost | naivni | radix čvorovi | radix lanac | radix CSR |
|---|---|---|---|---|
| [1] čvorovi sa `new` | ima | ima | – | – |
| [2] kopija reda + pokazivač | ima | ima | – | djelimično (kopija, bez pokazivača) |
| [3] `%` umjesto `&` | ima | ima | – | – |
| [4] broj bucketa = n | ima | ima | ima | ima |
| [5] obilazak lanca | ima | ima | ima | – |
| [6] nova tabela po particiji | (jedna tabela) | ima | – | – |

## Rezultati

i7-11800H (L1d 48 KB i L2 1,25 MB po jezgru, L3 24 MB), WSL2, jedna nit, medijana 10 mjerenja. Prikazana je najbolja vrijednost B za svaku radix verziju. Faza 1 je kod naivnog build, a kod radix verzija particionisanje R i S. Faza 2 je kod naivnog probe, a kod radix verzija join po particijama.

**R = 1 M, S = 10 M**

| verzija | faza 1 | faza 2 | ukupno | ubrzanje | LLC promašaji/red |
|---|---|---|---|---|---|
| naivni | 22,6 ms | 141,7 ms | 164,3 ms | 1,00× | 1,17 |
| radix čvorovi (B=6) | 42,0 ms | 51,1 ms | 93,1 ms | 1,76× | 0,19 |
| radix lanac (B=6) | 42,1 ms | 18,4 ms | 60,6 ms | 2,71× | 0,19 |
| radix CSR (B=6) | 42,6 ms | 25,1 ms | 67,7 ms | 2,43× | 0,20 |

**R = 4 M, S = 16 M**

| verzija | faza 1 | faza 2 | ukupno | ubrzanje | LLC promašaji/red |
|---|---|---|---|---|---|
| naivni | 184,0 ms | 351,4 ms | 535,3 ms | 1,00× | 2,20 |
| radix čvorovi (B=8) | 95,6 ms | 146,8 ms | 242,4 ms | 2,21× | 0,29 |
| radix lanac (B=6) | 73,6 ms | 41,0 ms | 114,6 ms | 4,67× | 0,21 |
| radix CSR (B=6) | 73,0 ms | 60,3 ms | 133,3 ms | 4,02× | 0,22 |

Mašina je između pokretanja dosta varirala, pa omjere treba gledati unutar iste tabele.

## Ključni zaključci

1. **Particionisanje rješava keš problem.** LLC promašaji padaju sa 1,2–2,2 na ~0,2 po redu, jer tabela particije stane u keš. Radix je 1,8–2,2× brži od naivnog i sa istom, sporom tabelom.
2. **Struktura hash tabele je sljedeće usko grlo.** Nizovi umjesto čvorova (verzija 3) ubrzavaju join po particijama 2,8–3,6×. Najbolja verzija ukupno je **radix lanac**: 2,7× na 1 M i 4,7× na 4 M prema naivnom.
3. **Popravka slabosti nije automatski ubrzanje.** Radix CSR ima 15–20% brži probe, ali 4–5× skuplji build, pa ukupno nije brži od radix lanac. Na našim podacima su lanci savršeni (tačno jedan red po probe-u), pa uklanjanje lanca malo donosi. Isplatilo bi se uz duže lance, npr. sa duplikatima ključeva u R.
4. **Broj particija je kompromis.** Više particija daje manje tabele, ali skuplje particionisanje, jer se istovremeno piše na 2^B mjesta. Najbolje je B = 6–8.
5. **Slabost [4] se na ovim podacima ne vidi.** Gusti ključevi daju savršenu raspodjelu po bucketima. Efekat bi se vidio tek sa slučajnim ključevima.
