# Hash join: pet verzija

Kratak pregled pet implementacija hash joina `R ⋈ S` po ključu (`R.key = S.key`), sa ključnim razlikama, rezultatima mjerenja i [poređenjem sa DuckDB-om](#poređenje-sa-duckdb-om). Uputstvo za pokretanje je u [README.md](README.md).

## Zadatak

- **R** (build strana, manja): ključevi `0..N_R−1`, svaki tačno jednom, izmiješani.
- **S** (probe strana, veća): slučajni ključevi iz `0..N_R−1`.
- Red je par `(key, payload)` od 4 + 4 bajta.
- Svaki red iz S ima tačno jedan par u R, pa join vraća `N_S` redova. Ispravnost se provjerava brojem redova i checksum-om (zbir `R.payload + S.payload`), izračunatim bez joina.

Hash join ima dvije faze:
- **build:** od R napravi hash tabelu;
- **probe:** za svaki red iz S potraži ključ u tabeli.

## Pet verzija ukratko

| | Particionisanje | Raspored bucketa | Broj tabela |
|---|---|---|---|
| **1. naivni** | ne | lista čvorova (`CvorTabela`) | jedna, za cijelu R |
| **2. radix čvorovi** | da, 2^B particija | lista čvorova (`CvorTabela`) | nova za svaku particiju |
| **3. radix lanac** | da, 2^B particija | lanac kroz nizove indeksa (`LanacTabela`) | jedna, ponovo korištena |
| **4. radix CSR** | da, 2^B particija | uzastopan komad jednog niza (`CsrTabela`) | jedna, ponovo korištena |
| **5. radix lanac pool** | da, u bafere zauzete unaprijed | lanac kroz nizove indeksa (`LanacTabela`) | jedna, ponovo korištena |

Naziv svake verzije je način joina + raspored hash tabele. Verzije 2–4 imaju isto particionisanje i razlikuju se samo po tome kako su redovi jednog bucketa smješteni u memoriji. Verzija 5 je verzija 3 kojoj se mijenja samo memorija za particije. Svaka sljedeća verzija mijenja jednu stvar, pa se u mjerenju vidi efekat svake promjene posebno.

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

## 5. Radix lanac pool (memorija za particije zauzeta unaprijed)

Fajlovi: [`radix_join.hpp`](radix_join.hpp) (`particionisi_u`), [`main.cpp`](main.cpp) (`mjeri_radix_pool`)

Nakon verzije 3 join po particijama traje ~30% vremena, a **particionisanje ~70%**. Polovina particionisanja pri tome nije sam posao, nego traženje memorije. Svako pokretanje traži ~8 B po redu nove memorije za particionisane R i S (~88 MB za 1 M / 10 M). Kernel pri prvom upisu mapira i nulira svaku stranicu od 4 KB (page fault).

Verzija 5 zato zauzme bafere za particije **jednom, prije mjerenja**, a svako pokretanje samo prepiše već mapiranu memoriju. Tako rade i baze: imaju memorijski pool i ne traže memoriju od operativnog sistema za svaki upit.

```
verzije 2–4:  svako pokretanje   particionisi(R, B)        -> nova memorija, page faultovi
verzija 5:    jednom, unaprijed  Rp.redovi.resize(N_R) ...
              svako pokretanje   particionisi_u(R, B, Rp)  -> prepisuje postojeću memoriju
```

**Razlika prema verziji 3:** hash tabela i join po particijama su isti; mijenja se samo gdje particionisanje piše. `particionisi()` je postala omotač oko `particionisi_u()`, pa je logika particionisanja zajednička za sve radix verzije.

**Šta ova verzija mjeri, a šta ne.** Verzija 5 mjeri isti algoritam kao verzija 3, ali bez cijene dobijanja nove memorije od operativnog sistema. Program pokreće join 11 puta (1 zagrijavanje + 10 mjerenja): verzije 2–4 plaćaju page faultove u svakom pokretanju, a verzija 5 jednom, izvan mjerenja.

- **Kad je to stvarno ubrzanje:** kad isti proces izvršava mnogo joinova, kao server baze podataka koji radi satima. Baza ne vraća memoriju sistemu nakon upita, nego je čuva u memorijskom poolu, pa sljedeći upit dobija već mapiranu memoriju. Nije potrebno unaprijed znati koliko će biti joinova; dovoljno je da se memorija ne vraća.
- **Kad nije:** za jedan, izolovan join. Memoriju za particije program mora jednom dobiti od sistema, pa tada verzija 5 traje isto kao verzija 3.
- **Šta pokazuje razlika 3 → 5:** koliko košta traženje nove memorije, ~polovinu particionisanja. Za jedan join bi tu pomogle druge tehnike, npr. velike stranice (2 MB umjesto 4 KB, pa ~500× manje page faultova) ili particionisanje bez pune kopije S.

Verzije 2–4 plaćaju page faultove u svakom pokretanju i zbog jednog detalja glibc-a: blok od desetina MB `malloc` dobija posebnim mapiranjem i vraća ga sistemu odmah nakon `free`, pa ga sljedeće pokretanje traži ispočetka.

## Pregled slabosti po verzijama

| Slabost | naivni | radix čvorovi | radix lanac | radix CSR | radix lanac pool |
|---|---|---|---|---|---|
| [1] čvorovi sa `new` | ima | ima | – | – | – |
| [2] kopija reda + pokazivač | ima | ima | – | djelimično (kopija, bez pokazivača) | – |
| [3] `%` umjesto `&` | ima | ima | – | – | – |
| [4] broj bucketa = n | ima | ima | ima | ima | ima |
| [5] obilazak lanca | ima | ima | ima | – | ima |
| [6] nova tabela po particiji | (jedna tabela) | ima | – | – | – |
| nova memorija za particije u svakom pokretanju | (nema particija) | ima | ima | ima | – |

## Rezultati

i7-11800H (8 jezgara; L1d 48 KB i L2 1,25 MB po jezgru, L3 24 MB), WSL2, jedna nit, medijana 10 mjerenja. Sve brojke u ovoj i sljedećoj sekciji (uključujući DuckDB) su iz jednog uzastopnog pokretanja. Prikazana je najbolja vrijednost B za svaku radix verziju. Faza 1 je kod naivnog build, a kod radix verzija particionisanje R i S. Faza 2 je kod naivnog probe, a kod radix verzija join po particijama.

**R = 1 M, S = 10 M**

| verzija | faza 1 | faza 2 | ukupno | ubrzanje | LLC promašaji/red |
|---|---|---|---|---|---|
| naivni | 23,6 ms | 142,3 ms | 165,9 ms | 1,00× | 1,18 |
| radix čvorovi (B=6) | 43,4 ms | 51,4 ms | 94,7 ms | 1,75× | 0,21 |
| radix lanac (B=6) | 42,1 ms | 17,6 ms | 59,6 ms | 2,78× | 0,20 |
| radix CSR (B=6) | 43,5 ms | 24,8 ms | 68,3 ms | 2,43× | 0,22 |
| radix lanac pool (B=6) | 21,1 ms | 17,5 ms | 38,7 ms | 4,29× | 0,18 |

**R = 4 M, S = 16 M**

| verzija | faza 1 | faza 2 | ukupno | ubrzanje | LLC promašaji/red |
|---|---|---|---|---|---|
| naivni | 178,9 ms | 345,9 ms | 524,9 ms | 1,00× | 2,20 |
| radix čvorovi (B=8) | 92,9 ms | 143,9 ms | 236,7 ms | 2,22× | 0,29 |
| radix lanac (B=6) | 72,5 ms | 39,7 ms | 112,2 ms | 4,68× | 0,20 |
| radix CSR (B=6) | 77,2 ms | 61,8 ms | 139,0 ms | 3,78× | 0,24 |
| radix lanac pool (B=6) | 41,2 ms | 41,0 ms | 82,2 ms | 6,39× | 0,20 |

Mašina je između pokretanja programa dosta varirala (naivni 1 M: od ~160 do ~380 ms), pa apsolutna vremena iz različitih pokretanja nisu uporediva. Omjere treba gledati unutar iste tabele.

## Poređenje sa DuckDB-om

Kao industrijska referenca isti join je izvršen u [DuckDB](https://duckdb.org)-u (verzija 2.0.0), skriptom [`duckdb_join.py`](duckdb_join.py). Podaci imaju istu raspodjelu (ne iste redove, jer se `std::mt19937` ne može ponoviti u SQL-u), a rezultat se provjerava istim checksum-om. DuckDB izvršava upit u svom C++ motoru; Python je samo omotač i dodaje ~0,2 ms po upitu.

DuckDB-ovo vrijeme uključuje i posao oko joina (čitanje tabela, cjevovod operatora, agregaciju). Zato skript mjeri i **isti upit bez joina**, a razlika je **procjena samog joina**, koja je najbliža koloni „ukupno“ iz naših tabela.

**Jedna nit** (fer poređenje sa našim sekvencijalnim verzijama):

| | 1 M / 10 M | 4 M / 16 M |
|---|---|---|
| DuckDB: join upit | 80,8 ms | 893,6 ms |
| DuckDB: isti upit bez joina | 31,4 ms | 50,6 ms |
| **DuckDB: procjena samog joina** | **49,4 ms** | **843,1 ms** |
| naš naivni | 165,9 ms | 524,9 ms |
| **naš radix lanac (B=6)** | **59,6 ms** | **112,2 ms** |
| naš radix lanac pool (B=6) | 38,7 ms | 82,2 ms |

**Osam niti** (DuckDB-ov uobičajeni režim; cilj za paralelnu verziju):

| | 1 M / 10 M | 4 M / 16 M |
|---|---|---|
| DuckDB: join upit | 23,1 ms | 211,5 ms |
| DuckDB: procjena samog joina | 17,7 ms | 201,4 ms |

**Šta pokazuje:**

1. **Na 1 M DuckDB je malo brži.** Njegov join (~49 ms) je ~1,2× brži od našeg najboljeg za jedan join (radix lanac, 60 ms). Njegova hash tabela tu još stane u keš, a probe je vektorizovan (blokovi od ~2000 redova), pa dobro skriva čekanje na memoriju.
2. **Na 4 M DuckDB naglo uspori.** 4× više redova R, a join je ~17× sporiji (49 → 843 ms), i sporiji je čak i od našeg naivnog. Hash tabela za cijelu R više ne stane u keš, a probe strana se ne particioniše. Naš radix lanac je tu **~7,5× brži**. Upravo ovaj problem radix join rješava.
3. **Posao oko joina je mali** (31–51 ms sa jednom niti), pa razlika na 4 M nije posljedica toga što DuckDB radi više posla, nego samog joina.
4. **Sa 8 niti** DuckDB na 4 M (201 ms) i dalje je sporiji od našeg radix lanca sa **jednom** niti (112 ms). Na 1 M je sa 8 niti ~3,4× brži od nas (18 prema 60 ms). To je realan cilj za paralelnu verziju.

**Napomene za tumačenje:**
- DuckDB je opšta baza: radi sa bilo kojim tipovima, NULL vrijednostima i upitima. Naš kod je specijalizovan za ovaj jedan slučaj (`uint32_t` ključevi, rezultat se samo broji i sabira). Pošteno je reći da je *specijalizovan radix join za ovaj slučaj brži od opšte baze kad tabela ne stane u keš*, a ne da je brži od DuckDB-a općenito.
- Prema DuckDB-ovom dizajnu (rad *Saving Private Hash Join*, VLDB 2025), njegovo radix particionisanje služi prvenstveno za joinove veće od RAM-a i za paralelni build, a ne da particije stanu u keš. Kad sve stane u memoriju, join se ponaša kao join bez particionisanja, sa jednom tabelom za cijelu build stranu. To objašnjava pad performansi na 4 M.

## Ključni zaključci

1. **Particionisanje rješava keš problem.** LLC promašaji padaju sa 1,2–2,2 na ~0,2 po redu, jer tabela particije stane u keš. Radix čvorovi su 1,8–2,2× brži od naivnog, i to sa istom, sporom tabelom.
2. **Struktura hash tabele je sljedeće usko grlo.** Nizovi umjesto čvorova (verzija 3) ubrzavaju join po particijama 2,9× na 1 M (51 → 18 ms) i 4,5× na 4 M (177 → 40 ms), uz B = 6.
3. **Popravka slabosti nije automatski ubrzanje.** Radix CSR ima 15–20% brži probe, ali 4–5× skuplji build, pa ukupno nije brži od radix lanac. Na našim podacima su lanci savršeni (tačno jedan red po probe-u), pa uklanjanje lanca malo donosi. Isplatilo bi se uz duže lance, npr. sa duplikatima ključeva u R.
4. **Kad se jedna faza ubrza, druga postaje usko grlo.** Nakon verzije 3 particionisanje je ~70% vremena, a pola od toga su page faultovi zbog nove memorije. Kad je memorija već zauzeta (verzija 5), particionisanje je ~2× brže, a ukupno 4,3× na 1 M i 6,4× na 4 M prema naivnom. To važi za proces koji izvršava mnogo joinova i ponovo koristi memoriju, kao baza sa memorijskim poolom. Za jedan, izolovan join verzija 5 traje kao verzija 3, pa je tada najbrža **radix lanac** (2,8× na 1 M i 4,7× na 4 M).
5. **Broj particija je kompromis.** Više particija daje manje tabele, ali skuplje particionisanje, jer se istovremeno piše na 2^B mjesta. Najbolje je B = 6–8.
6. **Slabost [4] se na ovim podacima ne vidi.** Gusti ključevi daju savršenu raspodjelu po bucketima. Efekat bi se vidio tek sa slučajnim ključevima.
7. **Industrijska baza potvrđuje problem.** DuckDB sa jednom niti je na 1 M malo brži od našeg najboljeg joina, ali na 4 M, kad mu hash tabela ne stane u keš, uspori ~17× i postaje ~7,5× sporiji od radix lanca. I sa 8 niti je na 4 M sporiji od našeg radix lanca sa jednom niti.
