# radix-hash-join

## Naivni hash join (`hash_join.cpp`)

Jednostavan hash join dvije tabele `R ⋈ S` po ključu, sa `std::unordered_map`:

- **build:** svi redovi manje tabele R idu u hash tabelu;
- **probe:** za svaki red veće tabele S traži se ključ u hash tabeli.

R ima ključeve `0..N_R-1` (svaki jednom, izmiješani), a S ima slučajne ključeve iz istog opsega, pa join uvijek vraća tačno `N_S` redova.

Mjerenje (po uzoru na Lab1):

- prije mjerenja join se provjeri sa rezultatom izračunatim bez hash tabele;
- 1 zagrijavanje + 10 mjerenja, ispisuju se medijana, min i max;
- build i probe se mjere odvojeno, bez generisanja podataka;
- cache promašaji se broje preko Linux `perf_event_open` posebno za svaku fazu (iz medijanskog pokretanja) i ispisuju ukupno i po redu ulazne tabele.

Zbog `perf_event_open` program radi samo na Linuxu / WSL-u.

### Pokretanje u WSL-u

Iz PowerShella otvori WSL u folderu projekta:

```powershell
cd C:\dev\radix-hash-join
wsl
```

Zatim u WSL-u (prvi put instaliraj kompajler):

```bash
sudo apt update && sudo apt install -y g++

g++ -O2 -std=c++17 hash_join.cpp -o hash_join
./hash_join              # podrazumijevano: N_R = 1 000 000, N_S = 10 000 000
./hash_join 10000 10000000   # mala R tabela, stane u keš
```

### Primjer izlaza

```
R: 1000000 redova, S: 10000000 redova
provjera: OK (10000000 redova, checksum 84999207147809)
1 zagrijavanje + 10 mjerenja

BUILD (1000000 redova)
  vrijeme:  medijana 27.3 ms, min 26.1 ms, max 45.8 ms
  brzina:   36.7 M redova/s
  L1D promasaji:       2606911  (2.61 po redu)
  LLC pristupi:        2371729  (2.37 po redu)
  LLC promasaji:        517195  (0.52 po redu)

PROBE (10000000 redova)
  vrijeme:  medijana 219.8 ms, min 212.3 ms, max 249.9 ms
  brzina:   45.5 M redova/s
  L1D promasaji:      55744049  (5.57 po redu)
  LLC pristupi:       33861177  (3.39 po redu)
  LLC promasaji:      18041129  (1.80 po redu)

UKUPNO (build + probe medijana): 247.1 ms, 44.5 M redova/s
```

Probe faza (medijana):

| N_R | N_S | vrijeme | M redova/s | L1D prom./red | LLC prom./red |
|---|---|---|---|---|---|
| 10 000 | 10 000 000 | 23,6 ms | 423,6 | 2,81 | 0,04 |
| 1 000 000 | 10 000 000 | 219,8 ms | 45,5 | 5,57 | 1,80 |

Isti broj probe operacija, ali kad hash tabela ne stane u keš, probe je ~9× sporiji, a gotovo svaki red ide u RAM (1,8 LLC promašaja po redu umjesto 0,04). Ovo je problem koji radix hash join rješava particionisanjem.

- **L1D promašaji:** čitanja koja nisu našla podatak u L1 data kešu.
- **LLC pristupi / promašaji:** pristupi zadnjem nivou keša (L3) i oni koji su morali ići u RAM.

Ako piše `nije dostupno`, hardverski brojači nisu dostupni u tom okruženju (npr. stariji WSL ili VM bez PMU-a) ili ih blokira `/proc/sys/kernel/perf_event_paranoid` (treba biti ≤ 2). Vrijeme se ispisuje u svakom slučaju.
