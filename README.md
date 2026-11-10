# radix-hash-join

Poređenje četiri verzije hash joina dvije tabele `R ⋈ S` po ključu. Program izmjeri sve četiri na istim podacima, radix verzije za više vrijednosti broja bitova B, i ispiše tabelu sa vremenima i cache promašajima. Isti join se može pokrenuti i u [DuckDB](https://duckdb.org)-u kao industrijskoj referenci.

## Sadržaj

- [Četiri verzije](#četiri-verzije) · detaljnije u [DOKUMENTACIJA.md](DOKUMENTACIJA.md)
- Pokretanje: [Linux](#linux) · [Windows](#windows) · [macOS](#macos-samo-vrijeme)
- [Poređenje sa DuckDB-om](#poređenje-sa-duckdb-om)

## Četiri verzije

| Verzija | Šta radi | Hash tabela |
|---|---|---|
| **naivni** | jedna hash tabela za cijelu R (build), pa probe svakim redom S | lančana tabela od čvorova ([`hash_tabela.hpp`](hash_tabela.hpp)) |
| **radix** | R i S se podijele na 2^B particija po donjih B bitova ključa, pa hash join po particijama | ista tabela od čvorova, nova za svaku particiju |
| **radix niz** | isto particionisanje kao radix | tabela od nizova indeksa ([`niz_tabela.hpp`](niz_tabela.hpp)), jedna za sve particije |
| **radix bucket** | isto particionisanje kao radix | svaki bucket je uzastopan komad niza, bez lanca ([`bucket_tabela.hpp`](bucket_tabela.hpp)), jedna za sve particije |

Tabela od čvorova je namjerno spora referentna verzija: na vrhu [`hash_tabela.hpp`](hash_tabela.hpp) su numerisane njene slabosti [1]–[6]. Tabela od nizova uklanja [1], [2], [3] i [6]. Tabela sa bucketima kao komadima niza uklanja i [5] (obilazak lanca), a [4] ostaje.

Na našim podacima (gusti ključevi, savršeni lanci od jednog reda) radix bucket ima ~15–20% brži probe, ali 4–5× skuplji build (redovi se premještaju po bucketima), pa ukupno nije brži od radix niz.

## Fajlovi

| Fajl | Sadržaj |
|---|---|
| [`main.cpp`](main.cpp) | mjeri sve četiri verzije (radix za B = 2, 4, 6, 8) i ispisuje tabelu |
| [`hash_join.hpp`](hash_join.hpp) | naivni hash join: build (hash tabela od cijele R) + probe (svaki red S) |
| [`radix_join.hpp`](radix_join.hpp) | radix hash join (sa čvorovima, nizovima i bucketima kao komadima niza): particionisanje R i S, pa hash join po particijama |
| [`hash_tabela.hpp`](hash_tabela.hpp) | lančana hash tabela sa čvorovima, sa spiskom namjernih slabosti za optimizaciju |
| [`niz_tabela.hpp`](niz_tabela.hpp) | hash tabela od nizova indeksa, bez čvorova i bez alokacije po particiji |
| [`bucket_tabela.hpp`](bucket_tabela.hpp) | hash tabela u kojoj je svaki bucket uzastopan komad niza (histogram → prefiksni zbir → raspodjela) |
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

**Windows (PowerShell).** Ako Python nije instaliran (`py` nije prepoznat), instaliraj ga pa ponovo otvori PowerShell. Jednostavnije je pokrenuti skript u WSL-u, kao za Linux.

```powershell
winget install -e --id Python.Python.3.13      # samo ako Python nije instaliran
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned   # samo ako Activate.ps1 javi gresku
py -m venv .venv
.venv\Scripts\Activate.ps1
pip install duckdb
python duckdb_join.py
```
