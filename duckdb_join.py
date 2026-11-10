"""Isti join kao u main.cpp, ali u DuckDB-u, kao industrijska referenca.

Podaci imaju istu raspodjelu kao u common.hpp (ne iste redove, jer se
std::mt19937 ne moze ponoviti u SQL-u):
  R - kljucevi 0..N_R-1, svaki tacno jednom, izmijesani; payload = 7 * key
  S - slucajni kljucevi iz 0..N_R-1; payload = redni broj reda
Svaki red iz S ima tacno jedan par u R, pa join mora vratiti N_S redova.

Mjeri se samo join upit (podaci su vec u tabelama): 1 zagrijavanje + 10 mjerenja,
ispisuje se medijana, min i max. Podrazumijevano DuckDB radi sa 1 niti, da bi
poredjenje sa sekvencijalnim verzijama u main.cpp bilo fer.

Pokretanje:  python duckdb_join.py [N_R] [N_S] [niti]
"""

import statistics
import sys
import time

import duckdb

ZAGRIJAVANJA = 1
PONAVLJANJA = 10

N_R = int(sys.argv[1]) if len(sys.argv) > 1 else 1_000_000
N_S = int(sys.argv[2]) if len(sys.argv) > 2 else 10_000_000
NITI = int(sys.argv[3]) if len(sys.argv) > 3 else 1

con = duckdb.connect()  # baza u memoriji
con.execute(f"SET threads = {NITI}")
con.execute("SELECT setseed(0.42)")  # ponovljivi slucajni brojevi

# --- generisanje podataka (ne ulazi u mjerenje) ---
con.execute(f"""
    CREATE TABLE R AS
    SELECT range::UINTEGER AS key, (range * 7)::UINTEGER AS payload
    FROM range({N_R})
    ORDER BY random()                                   -- izmijesani kljucevi
""")
con.execute(f"""
    CREATE TABLE S AS
    SELECT floor(random() * {N_R})::UINTEGER AS key, range::UINTEGER AS payload
    FROM range({N_S})
""")

# --- ocekivani rezultat, bez joina (kao ocekivani_rezultat u common.hpp) ---
# Svaki red iz S ima par u R, a payload tog para je 7 * key (kao uint32).
ocekivano = con.execute("""
    SELECT count(*), sum((key::UBIGINT * 7) % 4294967296 + payload)
    FROM S
""").fetchone()
ocekivano = (int(ocekivano[0]), int(ocekivano[1]))

# --- join upit ---
JOIN = """
    SELECT count(*), sum(R.payload::UBIGINT + S.payload)
    FROM R JOIN S ON R.key = S.key
"""

print(f"DuckDB {duckdb.__version__}, niti: {NITI}")
print(f"R: {N_R} redova, S: {N_S} redova, ocekivano {ocekivano[0]} redova rezultata")
# Naziv operatora u planu zavisi od verzije ("HASH_JOIN" ili "Hash Join").
plan = con.execute("EXPLAIN " + JOIN).fetchall()[0][1].upper().replace(" ", "_")
print("join operator:", "hash join" if "HASH_JOIN" in plan else "NIJE hash join, vidi EXPLAIN")

vremena = []
for i in range(ZAGRIJAVANJA + PONAVLJANJA):
    t0 = time.perf_counter()
    rez = con.execute(JOIN).fetchone()
    t1 = time.perf_counter()

    rez = (int(rez[0]), int(rez[1]))
    if rez != ocekivano:
        print(f"GRESKA: join vratio {rez}, ocekivano {ocekivano}")
        sys.exit(1)
    if i >= ZAGRIJAVANJA:
        vremena.append((t1 - t0) * 1000)

med = statistics.median_high(vremena)  # kao u main.cpp: kod parnog broja gornji od dva
print(f"provjera: OK (checksum {rez[1]})")
print(f"{ZAGRIJAVANJA} zagrijavanje + {PONAVLJANJA} mjerenja")
print(f"vrijeme:  medijana {med:.1f} ms, min {min(vremena):.1f} ms, max {max(vremena):.1f} ms")
print(f"brzina:   {(N_R + N_S) / med / 1e3:.1f} M red/s (R + S, kao u main.cpp)")
