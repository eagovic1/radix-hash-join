"""Isti join kao u main.cpp, ali u DuckDB-u, kao industrijska referenca.

Podaci imaju istu raspodjelu kao u common.hpp (ne iste redove, jer se
std::mt19937 ne moze ponoviti u SQL-u):
  R - kljucevi 0..N_R-1, svaki tacno jednom, izmijesani; payload = 7 * key
  S - slucajni kljucevi iz 0..N_R-1; payload = redni broj reda
Svaki red iz S ima tacno jedan par u R, pa join mora vratiti N_S redova.

Mjere se dva upita (podaci su vec u tabelama), svaki sa 1 zagrijavanjem + 10 mjerenja:
  - join upit: cijeli upit sa joinom;
  - isti upit bez joina: skeniranje R i S i ista agregacija, bez joina.
Razlika je procjena cijene samog joina, bez posla koji DuckDB radi oko njega.
Podrazumijevano DuckDB radi sa 1 niti, da bi poredjenje sa sekvencijalnim
verzijama u main.cpp bilo fer.

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

# Isti upit BEZ joina: skenira R i S i radi istu vrstu agregacije nad S
# (count + sum dva payload-a u UBIGINT). Mjeri posao koji DuckDB radi oko joina
# (citanje tabela, cjevovod operatora, agregacija), da bi se procijenila cijena
# samog joina: join upit - upit bez joina.
# sum(payload) nad R, a ne count(*), da bi se R zaista skenirala (count(*) DuckDB
# moze procitati iz metapodataka tabele).
BEZ_JOINA = """
    SELECT (SELECT sum(payload) FROM R), count(*), sum(payload::UBIGINT + payload)
    FROM S
"""


def mjeri(upit, provjeri):
    """1 zagrijavanje + 10 mjerenja upita; vraca vremena u ms i zadnji rezultat."""
    vremena, rez = [], None
    for i in range(ZAGRIJAVANJA + PONAVLJANJA):
        t0 = time.perf_counter()
        rez = con.execute(upit).fetchone()
        t1 = time.perf_counter()
        provjeri(rez)
        if i >= ZAGRIJAVANJA:
            vremena.append((t1 - t0) * 1000)
    return vremena, rez


def provjeri_join(rez):
    rez = (int(rez[0]), int(rez[1]))
    if rez != ocekivano:
        print(f"GRESKA: join vratio {rez}, ocekivano {ocekivano}")
        sys.exit(1)


def provjeri_bez_joina(rez):
    if int(rez[1]) != N_S:
        print(f"GRESKA: upit bez joina vratio {int(rez[1])} redova, ocekivano {N_S}")
        sys.exit(1)


def ispisi(naziv, vremena):
    med = statistics.median_high(vremena)  # kao u main.cpp: kod parnog broja gornji od dva
    print(f"{naziv:<22} medijana {med:7.1f} ms   min {min(vremena):7.1f} ms   max {max(vremena):7.1f} ms")
    return med


print(f"DuckDB {duckdb.__version__}, niti: {NITI}")
print(f"R: {N_R} redova, S: {N_S} redova, ocekivano {ocekivano[0]} redova rezultata")
# Naziv operatora u planu zavisi od verzije ("HASH_JOIN" ili "Hash Join").
plan = con.execute("EXPLAIN " + JOIN).fetchall()[0][1].upper().replace(" ", "_")
print("join operator:", "hash join" if "HASH_JOIN" in plan else "NIJE hash join, vidi EXPLAIN")

v_join, rez = mjeri(JOIN, provjeri_join)
v_bez, _ = mjeri(BEZ_JOINA, provjeri_bez_joina)

print(f"provjera: OK (checksum {int(rez[1])})")
print(f"{ZAGRIJAVANJA} zagrijavanje + {PONAVLJANJA} mjerenja\n")
med_join = ispisi("join upit:", v_join)
med_bez = ispisi("isti upit bez joina:", v_bez)
samo_join = med_join - med_bez
print(f"{'procjena samog joina:':<22} {samo_join:7.1f} ms   (join upit - upit bez joina)\n")
print(f"brzina join upita:     {(N_R + N_S) / med_join / 1e3:.1f} M red/s (R + S, kao u main.cpp)")
if samo_join > 0:
    print(f"brzina samog joina:    {(N_R + N_S) / samo_join / 1e3:.1f} M red/s (procjena)")
print("\nZa poredjenje sa ./join: 'join upit' je cijeli upit, a 'procjena samog joina'")
print("je blize koloni 'ukupno', koja mjeri samo join nad nizovima u memoriji.")
