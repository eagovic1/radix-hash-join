# radix-hash-join

## Naivni hash join (`hash_join.cpp`)

Jednostavan hash join dvije tabele `R ⋈ S` po ključu, sa `std::unordered_map`:

- **build:** svi redovi manje tabele R idu u hash tabelu;
- **probe:** za svaki red veće tabele S traži se ključ u hash tabeli.

R ima ključeve `0..N_R-1` (svaki jednom, izmiješani), a S ima slučajne ključeve iz istog opsega, pa join uvijek vraća tačno `N_S` redova. Program mjeri vrijeme samo za join (bez generisanja podataka) i broji cache promašaje preko Linux `perf_event_open`. Zato radi samo na Linuxu / WSL-u.

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
rezultat: 10000000 redova (ocekivano 10000000), checksum 84999207147809
vrijeme: 223.1 ms
L1D promasaji:   59359817
LLC pristupi:    36049787
LLC promasaji:   14936834
```

| N_R | N_S | vrijeme | L1D promašaji | LLC promašaji |
|---|---|---|---|---|
| 10 000 | 10 000 000 | 24,8 ms | 28,2 M | 0,35 M |
| 1 000 000 | 10 000 000 | 223,1 ms | 59,4 M | 14,9 M |

Isti broj probe operacija, ali kad hash tabela ne stane u keš, join je ~9× sporiji, a LLC promašaja je ~40× više. Ovo je problem koji radix hash join rješava particionisanjem.

- **L1D promašaji:** čitanja koja nisu našla podatak u L1 data kešu.
- **LLC pristupi / promašaji:** pristupi zadnjem nivou keša (L3) i oni koji su morali ići u RAM.

Ako piše `nije dostupno`, hardverski brojači nisu dostupni u tom okruženju (npr. stariji WSL ili VM bez PMU-a) ili ih blokira `/proc/sys/kernel/perf_event_paranoid` (treba biti ≤ 2). Vrijeme se ispisuje u svakom slučaju.
