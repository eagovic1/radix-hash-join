# radix-hash-join

Poređenje pet verzija hash joina dvije tabele `R ⋈ S` po ključu. Program izmjeri svih pet na istim podacima, radix verzije za više vrijednosti broja bitova B, i ispiše tabelu sa vremenima i cache promašajima. Isti join se može pokrenuti i u [DuckDB](https://duckdb.org)-u kao industrijskoj referenci.

## Sadržaj

- [Pet verzija](#pet-verzija) · detaljnije u [DOKUMENTACIJA.md](DOKUMENTACIJA.md)
- Pokretanje: [Linux](#linux) · [Windows](#windows) · [macOS](#macos-samo-vrijeme)
- [Poređenje sa DuckDB-om](#poređenje-sa-duckdb-om)

## Pet verzija

Naziv svake verzije je način joina + raspored hash tabele. Radix čvorovi, lanac i CSR imaju isto particionisanje i razlikuju se samo po tome kako su redovi jednog bucketa smješteni u memoriji. Radix lanac pool je radix lanac kojem se mijenja samo memorija za particije.

| Verzija | Šta radi | Hash tabela (raspored bucketa) |
|---|---|---|
| **naivni** | jedna hash tabela za cijelu R (build), pa probe svakim redom S | lista čvorova alociranih sa `new` ([`cvor_tabela.hpp`](cvor_tabela.hpp)) |
| **radix čvorovi** | R i S se podijele na 2^B particija po donjih B bitova ključa, pa hash join po particijama | ista tabela od čvorova, nova za svaku particiju |
| **radix lanac** | isto particionisanje | lanac kroz nizove indeksa ([`lanac_tabela.hpp`](lanac_tabela.hpp)), jedna tabela za sve particije |
| **radix CSR** | isto particionisanje | uzastopan komad jednog niza, bez lanca ([`csr_tabela.hpp`](csr_tabela.hpp)), jedna tabela za sve particije |
| **radix lanac pool** | isto particionisanje, ali u bafere zauzete jednom, unaprijed (kao memorijski pool u bazi) | ista kao radix lanac |

Tabela od čvorova je namjerno spora referentna verzija: na vrhu [`cvor_tabela.hpp`](cvor_tabela.hpp) su numerisane njene slabosti [1]–[6]. Radix lanac uklanja [1], [2], [3] i [6], a radix CSR uklanja i [5] (obilazak lanca). [4] ostaje u svim verzijama.

Na našim podacima (gusti ključevi, savršeni lanci od jednog reda) radix CSR ima ~15–20% brži probe, ali 4–5× skuplji build (redovi se premještaju po bucketima), pa ukupno nije brži od radix lanac.

Radix lanac pool ne traži novu memoriju od operativnog sistema u svakom pokretanju, pa ne plaća page faultove, i particionisanje je ~2× brže. To je ista implementacija algoritma kao radix lanac, samo sa memorijom zauzetom izvan mjerenja. Ubrzanje je stvarno kad isti proces izvršava mnogo joinova i ponovo koristi memoriju (kao baza sa memorijskim poolom). Za jedan, izolovan join pool verzija traje isto kao radix lanac. Detaljnije u [DOKUMENTACIJA.md](DOKUMENTACIJA.md#5-radix-lanac-pool-memorija-za-particije-zauzeta-unaprijed).

## Fajlovi

| Fajl | Sadržaj |
|---|---|
| [`main.cpp`](main.cpp) | mjeri svih pet verzija (radix za B = 2, 4, 6, 8) i ispisuje tabelu |
| [`hash_join.hpp`](hash_join.hpp) | naivni hash join: build (hash tabela od cijele R) + probe (svaki red S) |
| [`radix_join.hpp`](radix_join.hpp) | particionisanje R i S (u novu ili postojeću memoriju), i join po particijama za čvorove, lanac i CSR |
| [`cvor_tabela.hpp`](cvor_tabela.hpp) | `CvorTabela`: lančana hash tabela sa čvorovima, sa spiskom namjernih slabosti za optimizaciju |
| [`lanac_tabela.hpp`](lanac_tabela.hpp) | `LanacTabela`: bucket je lanac kroz nizove indeksa, bez čvorova i bez alokacije po particiji |
| [`csr_tabela.hpp`](csr_tabela.hpp) | `CsrTabela`: bucket je uzastopan komad niza (histogram → prefiksni zbir → raspodjela) |
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

Skript mjeri dva upita: **join upit** i **isti upit bez joina** (samo skeniranje R i S i agregacija). Njihova razlika, **procjena samog joina**, je ono što je najbliže koloni `ukupno` iz `./join`. Za fer poređenje pokreni oba programa jedan za drugim, sa istim veličinama, npr. `./join 4000000 16000000 && python duckdb_join.py 4000000 16000000`.

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
