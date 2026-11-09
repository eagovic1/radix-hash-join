# radix-hash-join

Poređenje naivnog i radix hash joina dvije tabele `R ⋈ S` po ključu, sa mjerenjem vremena i cache promašaja.

| Fajl | Sadržaj |
|---|---|
| `hash_join.hpp` | naivni hash join: build (hash tabela od cijele R) + probe (svaki red S) |
| `radix_join.hpp` | radix hash join: particionisanje R i S, pa hash join po particijama |
| `common.hpp` | generisanje podataka, provjera rezultata, mjerenje vremena i cache promašaja |
| `main.cpp` | mjeri naivni join i radix join za B = 2, 4, …, 16 i ispisuje tabelu |

**Podaci:** R ima ključeve `0..N_R-1` (svaki jednom, izmiješani), a S ima slučajne ključeve iz istog opsega, pa join uvijek vraća tačno `N_S` redova. Podrazumijevano je `N_R = 1 000 000` i `N_S = 10 000 000`. Oba joina koriste istu hash tabelu (`std::unordered_map`), pa se razlikuju samo po particionisanju.

**Mjerenje** (po uzoru na Lab1):

- rezultat svakog pokretanja se provjeri sa rezultatom izračunatim bez hash tabele (ovo ujedno sprečava kompajler da izbaci join);
- 1 zagrijavanje + 10 mjerenja, u tabeli su medijane;
- svaka verzija ima dvije faze koje se mjere odvojeno, bez generisanja podataka:
  - naivni: faza 1 = build, faza 2 = probe;
  - radix: faza 1 = particionisanje R i S, faza 2 = join po particijama;
- cache promašaji se broje preko Linux `perf_event_open` i dijele brojem redova (R + S).

Zbog `perf_event_open` program radi samo na Linuxu / WSL-u.

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

g++ -O2 -std=c++17 main.cpp -o join
./join                  # [N_R] [N_S], podrazumijevano 1 000 000 i 10 000 000
```

Vrijednosti B koje se testiraju su u nizu `RADIX_BITOVI` na vrhu `main.cpp`.

## Rezultati (i7-11800H, WSL2)

Keševi: L1d 48 KB i L2 1,25 MB po jezgru, L3 24 MB zajednički.

```
verzija         faza 1    faza 2    ukupno  ubrzanje   M red/s   L1D/red   LLC/red
naivni            27.4     196.4     223.9     1.00x      49.1      5.51      1.48
radix B=2         42.4      84.6     127.1     1.76x      86.6      3.94      0.30
radix B=4         41.3      63.1     104.5     2.14x     105.3      3.32      0.23
radix B=6         41.5      45.8      87.4     2.56x     125.9      3.12      0.19
radix B=8         53.6      45.6      99.2     2.26x     110.9      2.77      0.27
radix B=10        58.9      46.0     104.9     2.13x     104.8      1.15      0.26
radix B=12        90.1      46.2     136.3     1.64x      80.7      3.13      0.27
radix B=14       122.4      45.4     167.7     1.33x      65.6      4.68      0.28
radix B=16       165.9      35.2     201.1     1.11x      54.7      5.05      0.32
```

Vremena variraju par posto između pokretanja.

- **Naivni join** gubi skoro sve vrijeme u probe fazi: ~1,5 LLC promašaja po redu, tj. skoro svaki probe ide u RAM.
- **Radix join** je najbrži za **B = 6** (64 particije, ~15 600 redova R po particiji): hash tabela particije (~0,6 MB) stane u L2, join faza pada sa 196 na 46 ms, a LLC promašaja je ~7× manje. Ukupno ~2,5× brže od naivnog.
- **Veći B** ne ubrzava join fazu (ostaje ~46 ms, L2 je već dovoljno brz), ali particionisanje postaje sve skuplje: u jednom prolazu se istovremeno piše na `2^B` mjesta u memoriji, pa raste broj promašaja u kešu i TLB-u. Zato je najbolje B na sredini. Za mnogo particija pravi radix join particioniše u više prolaza.
- **B = 10** ima najmanje L1D promašaja: particija R ima ~1000 redova, pa njena hash tabela (~40 KB) stane i u L1 (48 KB). Vrijeme se ipak ne popravlja, jer je particionisanje već skuplje.

### Šta znače brojači

- **L1D/red:** promašaji L1 data keša pri čitanju, po redu (R + S).
- **LLC/red:** promašaji zadnjeg nivoa keša (L3), tj. pristupi koji su morali ići u RAM, po redu.

Ako piše `-`, hardverski brojači nisu dostupni u tom okruženju (npr. stariji WSL ili VM bez PMU-a) ili ih blokira `/proc/sys/kernel/perf_event_paranoid` (treba biti ≤ 2). Vremena se ispisuju u svakom slučaju.
