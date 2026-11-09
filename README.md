# radix-hash-join

Poređenje naivnog i radix hash joina. Program izmjeri oba joina na istim podacima, radix join za više vrijednosti broja bitova B, i ispiše tabelu sa vremenima i cache promašajima.

```
./join [N_R] [N_S]      # podrazumijevano N_R = 1 000 000, N_S = 10 000 000
```

Cache promašaji se mjere samo na **Linuxu** (i WSL-u). Na Windowsu i macOS-u se mjeri samo vrijeme, a u kolonama `L1D/red` i `LLC/red` piše `-`.

## Linux

```bash
sudo apt install -y g++            # Ubuntu/Debian; na drugim distribucijama paket g++ / gcc-c++
g++ -O2 -std=c++17 main.cpp -o join
./join
```

Ako u kolonama za cache promašaje piše `-`, brojače blokira `/proc/sys/kernel/perf_event_paranoid` (treba biti ≤ 2):

```bash
sudo sysctl kernel.perf_event_paranoid=2
```

## Windows

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

## macOS (samo vrijeme)

```bash
xcode-select --install             # jednom, instalira clang
clang++ -O2 -std=c++17 main.cpp -o join
./join
```
