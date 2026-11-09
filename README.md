# radix-hash-join

Naivni i radix hash join dvije tabele `R ⋈ S` po ključu, sa mjerenjem vremena i cache promašaja.

| Fajl | Sadržaj |
|---|---|
| `hash_join.cpp` | naivni hash join: build (hash tabela od cijele R) + probe (svaki red S) |
| `radix_join.cpp` | radix hash join: particionisanje R i S, pa hash join po particijama |
| `common.hpp` | zajednički dio: generisanje podataka, provjera rezultata, mjerenje |

**Podaci:** R ima ključeve `0..N_R-1` (svaki jednom, izmiješani), a S ima slučajne ključeve iz istog opsega, pa join uvijek vraća tačno `N_S` redova. Podrazumijevano je `N_R = 1 000 000` i `N_S = 10 000 000`. Oba joina koriste istu hash tabelu (`std::unordered_map`), pa se razlikuju samo po particionisanju.

**Mjerenje** (po uzoru na Lab1):

- prije mjerenja join se provjeri sa rezultatom izračunatim bez hash tabele;
- 1 zagrijavanje + 10 mjerenja, ispisuju se medijana, min i max;
- faze se mjere odvojeno (naivni: build / probe; radix: particionisanje / join po particijama), bez generisanja podataka;
- cache promašaji se broje preko Linux `perf_event_open` posebno za svaku fazu (iz medijanskog pokretanja) i ispisuju ukupno i po redu.

Zbog `perf_event_open` programi rade samo na Linuxu / WSL-u.

## Radix hash join ukratko

Naivni join je spor jer hash tabela za cijelu R (desetine MB) ne stane u keš, pa skoro svaki probe ide u RAM. Radix join zato:

1. **particionisanje:** obje tabele podijeli na `P = 2^B` particija po donjih `B` bitova ključa (histogram → prefiksni zbir → raspodjela, kao counting sort). Redovi sa istim ključem uvijek završe u particiji istog broja;
2. **join po particijama:** za svaku particiju `p` napravi hash tabelu od `R_p` i probaj je redovima iz `S_p`. Tabela jedne particije je mala i stane u keš.

## Pokretanje u WSL-u

Iz PowerShella otvori WSL u folderu projekta:

```powershell
cd C:\dev\radix-hash-join
wsl
```

Zatim u WSL-u (prvi put instaliraj kompajler):

```bash
sudo apt update && sudo apt install -y g++

g++ -O2 -std=c++17 hash_join.cpp -o hash_join
g++ -O2 -std=c++17 radix_join.cpp -o radix_join

./hash_join                    # [N_R] [N_S]
./radix_join                   # [N_R] [N_S] [B], podrazumijevano B = 6
./radix_join 1000000 10000000 10
```

## Rezultati (i7-11800H, WSL2)

Keševi: L1d 48 KB i L2 1,25 MB po jezgru, L3 24 MB zajednički.

| | vrijeme | M redova/s | L1D promašaji | LLC promašaji |
|---|---|---|---|---|
| naivni: build | 22,0 ms | 45,5 | 2,6 M | 0,34 M |
| naivni: probe | 183,8 ms | 54,4 | 56,1 M | 15,2 M |
| **naivni ukupno** | **205,8 ms** | 53,5 | 58,7 M | 15,5 M |
| radix: particionisanje | 41,8 ms | 263,2 | 4,4 M | 1,7 M |
| radix: join po particijama | 45,8 ms | 240,4 | 30,1 M | 0,37 M |
| **radix ukupno (B = 6)** | **87,6 ms** | 125,6 | 34,5 M | 2,1 M |

Radix join je ~2,3× brži i ima ~7× manje LLC promašaja. Particionisanje košta 42 ms, ali zato join po particijama skoro uopšte ne ide u RAM (0,03 LLC promašaja po redu, a naivni probe 1,5).

### Izbor broja bitova B

Ukupno vrijeme radix joina (R = 1 M, S = 10 M):

| B | particija | redova R po particiji | particionisanje | join | ukupno |
|---|---|---|---|---|---|
| 0 | 1 | 1 000 000 | 50,9 ms | 226,1 ms | 277,0 ms |
| 4 | 16 | 62 500 | 42,9 ms | 66,4 ms | 109,3 ms |
| **6** | **64** | **15 625** | **43,9 ms** | **47,3 ms** | **91,2 ms** |
| 8 | 256 | 3 906 | 54,7 ms | 46,8 ms | 101,6 ms |
| 10 | 1 024 | 976 | 59,0 ms | 45,3 ms | 104,3 ms |
| 12 | 4 096 | 244 | 88,1 ms | 47,8 ms | 135,9 ms |
| 16 | 65 536 | 15 | 174,7 ms | 34,3 ms | 208,9 ms |

- `B = 0` je jedna particija, odnosno naivni join (uz kopiranje tabela).
- Sa većim B particije su manje, pa join brzo pada dok hash tabela particije ne stane u L2 (~B = 6), a dalje se gotovo ne mijenja.
- Particionisanje u jednom prolazu postaje sve skuplje sa brojem particija, jer se istovremeno piše na previše mjesta u memoriji (keš i TLB). Zato je najbolje B na sredini. Za veći broj particija pravi radix join particioniše u više prolaza.

### Šta znače brojači

- **L1D promašaji:** čitanja koja nisu našla podatak u L1 data kešu.
- **LLC pristupi / promašaji:** pristupi zadnjem nivou keša (L3) i oni koji su morali ići u RAM.

Ako piše `nije dostupno`, hardverski brojači nisu dostupni u tom okruženju (npr. stariji WSL ili VM bez PMU-a) ili ih blokira `/proc/sys/kernel/perf_event_paranoid` (treba biti ≤ 2). Vrijeme se ispisuje u svakom slučaju.
