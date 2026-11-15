#!/usr/bin/env python3
"""Mede o carregamento de um site no Cipó (uso manual): medir_site.py <url> [rotulo]."""
import os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina  # noqa: E402
REDE = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0,mac=52:54:00:12:34:56"]
url = sys.argv[1]; rot = sys.argv[2] if len(sys.argv) > 2 else "site"
with Maquina("medir-" + rot, "ide", memoria="768M", extras=REDE) as m:
    m.esperar_serial("sagui:/$", 120)
    m.esperar_serial("[REDE] DHCP: endereco", 60)
    n = len(m.serial())
    t0 = time.time()
    m.digitar(f"navegador {url}\n", 0.03)
    fim = time.time() + 240
    while time.time() < fim and f"pronto {url}" not in m.serial()[n:]:
        time.sleep(0.5)
    t = time.time() - t0
    time.sleep(10)
    n2 = len(m.serial())
    t1 = time.time()
    for _ in range(6):
        m.tecla("pgdn"); time.sleep(1.5)
    print(f"6 PgDn em {time.time() - t1:.1f} s")
    time.sleep(8)
    for l in re.findall(r"PERF.*", m.serial()[n2:]): print("  rolando:", l[:120])
    s = m.serial()[n:]
    m.captura(f"medir-{rot}")
    print(f"pronto em {t:.1f} s (relogio do host)")
    for l in re.findall(r"navegador: (?:scripts em|carregada).*|js: coleta.*", s): print(" ", l[:160])
    print("  reparses de css:", len(re.findall(r"cipo: css \d+ regras", s)))
    print("  js abortado:", s.count("js: abortado"))
