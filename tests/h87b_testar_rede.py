#!/usr/bin/env python3
"""H87b: o botão "Testar conexão" da aba Rede das Configurações.

Com a rede do QEMU (user, com DHCP e DNS do host): o teste resolve
example.com pelo DNS e abre uma conexão TCP na porta 80. Sem internet na
máquina dos testes, o DNS ou o TCP falham, e o teste confere que a falha
aparece escrita (o caminho com sucesso NÃO é testado nesse caso, e o teste
diz isso).
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar  # noqa: E402

REDE = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0,mac=52:54:00:12:34:56"]


def main():
    with Maquina("h87b", "ide", extras=REDE) as m:
        m.esperar_serial("sagui:/$", 120)
        m.esperar_serial("[REDE] DHCP: endereco", 60)
        m.digitar("configuracoes rede\n", 0.04)
        m.esperar_serial('"Configurações"', 30)
        time.sleep(1.5)
        j = re.findall(r'criada por \d+: "Configurações" (\d+)x(\d+) em \((\d+),(\d+)\)', m.serial())[-1]
        x, y = int(j[2]) + 2, int(j[3]) + 24
        antes = len(m.serial())
        m.clique(x + 190 + 80, y + 284 + 14)   # Testar conexão (X0 = 190; modo automático)
        fim = time.time() + 40
        while "configuracoes: teste de rede:" not in m.serial()[antes:] and time.time() < fim:
            time.sleep(0.3)
        r = re.search(r"configuracoes: teste de rede: (.*)", m.serial()[antes:])
        verificar(r is not None, "o botão Testar conexão roda o teste")
        time.sleep(0.5)
        m.captura("H87-testar-rede")
        res = r.group(1)
        if "tcp ok" in res:
            verificar(re.search(r"dns \d+ ms, tcp ok \d+ ms", res), f"DNS e conexão TCP funcionam ({res.strip()})")
        else:
            print(f"  AVISO: sem internet aqui ({res.strip()}): só a mensagem de falha foi conferida")
            verificar("falhou" in res or "sem rede" in res, "a falha aparece no resultado")


if __name__ == "__main__":
    main()
