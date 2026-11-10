# radix-hash-join

Poređenje naivnog i radix hash joina dvije tabele `R ⋈ S` po ključu. Program izmjeri oba joina na istim podacima, radix join za više vrijednosti broja bitova B, i ispiše tabelu sa vremenima i cache promašajima. Isti join se može pokrenuti i u [DuckDB](https://duckdb.org)-u kao industrijskoj referenci.

## Sadržaj

- Pokretanje: [Linux](#linux) · [Windows](#windows) · [macOS](#macos-samo-vrijeme)
- [Poređenje sa DuckDB-om](#poređenje-sa-duckdb-om)

## Fajlovi

| Fajl | Sadržaj |
|---|---|
| [`main.cpp`](main.cpp) | mjeri naivni join i radix join za B = 2, 4, …, 16 i ispisuje tabelu |
| [`hash_join.hpp`](hash_join.hpp) | naivni hash join: build (hash tabela od cijele R) + probe (svaki red S) |
| [`radix_join.hpp`](radix_join.hpp) | radix hash join: particionisanje R i S, pa hash join po particijama |
| [`hash_tabela.hpp`](hash_tabela.hpp) | lančana hash tabela sa čvorovima, sa spiskom namjernih slabosti za optimizaciju |
| [`common.hpp`](common.hpp) | generisanje podataka, provjera rezultata, mjerenje vremena i cache promašaja |
| [`duckdb_join.py`](duckdb_join.py) | isti join u DuckDB-u, za poređenje |

## Pokretanje

```
./join [N_R] [N_S]      # podrazumijevano N_R = 1 000 000, N_S = 10 000 000
```

Cache promašaji se mjere samo na **Linuxu** (i WSL-u). Na Windowsu i macOS-u se mjeri samo vrijeme, a u kolonama `L1D/red` i `LLC/red` piše `-`.

### Linux

```bash
sudo apt install -y g++            # Ubuntu/Debian; na drugim distribucijama paket g++ / gcc-c++
g++ -O2 -std=c++17 main.cpp -o join
./join
```

Ako u kolonama za cache promašaje piše `-`, brojače blokira `/proc/sys/kernel/perf_event_paranoid` (treba biti ≤ 2):

```bash
sudo sysctl kernel.perf_event_paranoid=2
```

### Windows

**Preko WSL-a (preporučeno, mjeri i cache promašaje).** Iz PowerShella:

```powershell
cd C:\dev\radix-hash-join
wsl
```

pa u WSL-u isto kao za Linux:

```bash
sudo apt install -y g++
g++ -O2 -std=c++17 main.cpp -o join
./join
```

**Bez WSL-a (samo vrijeme).** U „x64 Native Tools Command Prompt for VS“ (Visual Studio sa C++ podrškom):

```bat
cl /O2 /std:c++17 /EHsc main.cpp /Fe:join.exe
join.exe
```

### macOS (samo vrijeme)

```bash
xcode-select --install             # jednom, instalira clang
clang++ -O2 -std=c++17 main.cpp -o join
./join
```

## Poređenje sa DuckDB-om

`duckdb_join.py` izvršava isti join u [DuckDB](https://duckdb.org)-u kao industrijsku referencu: podaci iste raspodjele, ista provjera rezultata, 1 zagrijavanje + 10 mjerenja, i podrazumijevano **1 nit** (fer poređenje sa sekvencijalnim verzijama).

```
python duckdb_join.py [N_R] [N_S] [niti]   # podrazumijevano 1 000 000, 10 000 000, 1
```

Potreban je Python 3 i paket `duckdb`.

**Linux / WSL, macOS:**

```bash
python3 -m venv .venv              # na Ubuntu/WSL prvo: sudo apt install -y python3-venv
. .venv/bin/activate
pip install duckdb
python duckdb_join.py
```

**Windows (PowerShell):**

```powershell
py -m venv .venv
.venv\Scripts\Activate.ps1
pip install duckdb
python duckdb_join.py
```
