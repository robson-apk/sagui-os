#!/usr/bin/env python3
"""H97 - instalar o SaguiOS num disco rígido e guardar arquivos nele.

Critérios:
  - o driver ATA acha o disco de boot e um disco vazio (hd0 e hd1);
  - "instalar hd1 --sim" copia o sistema, cria a FAT32 dos arquivos e
    termina com "instalar: pronto";
  - ligando só pelo disco instalado: o sistema sobe, o kernel reconhece a
    FAT32 do SaguiOS e a monta em /usb sozinho;
  - um arquivo gravado em /usb continua lá depois de reiniciar (a memória
    é apagada no reinício; só o disco guarda);
  - a tabela de partições do disco instalado: 0xDA (sistema) e 0x0C (FAT32,
    alinhada em 1 MB), conferida aqui fora, no arquivo do disco.
"""
import os
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, SAIDA  # noqa: E402


def esperar(m, texto, tempo=60, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return
        time.sleep(0.3)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}; fim: {m.serial()[-300:]!r}")


def main():
    hd = os.path.join(SAIDA, "h97-hd.img")
    with open(hd, "wb") as f:
        f.truncate(256 * 1024 * 1024)

    # 1. Instalar, com o disco do sistema em hd0 e o vazio em hd1.
    with Maquina("h97a", "ide", memoria="768M", extras=["-drive", f"format=raw,file={hd},if=ide,index=1"]) as m:
        m.esperar_serial("sagui:/$", 120)
        s = m.serial()
        verificar("[DISCO] hd0:" in s and "[DISCO] hd1:" in s, "o driver ATA acha os dois discos")
        # A janela, pelo menu Iniciar (último item da grade 5 x 4).
        n = len(m.serial())
        m.clique(40, 12)
        time.sleep(0.8)
        m.clique(44 + 4 * 108 + 54, 134 + 3 * 92 + 30)
        esperar(m, "instalar: janela aberta (1 discos)", 20, n)
        verificar(True, "o menu Iniciar abre o instalador, que oferece o hd1")
        time.sleep(1)
        m.captura("H97-instalador")
        m.tecla("esc")
        time.sleep(1)
        n = len(m.serial())
        m.digitar("instalar hd1 --sim\n", 0.03)
        esperar(m, "instalar: pronto em hd1", 180, n)
        verificar(True, "o instalador terminou")
        esperar(m, "sagui:/$", 20, n)

    mbr = open(hd, "rb").read(512)
    verificar(mbr[510:512] == b"\x55\xaa", "o disco instalado tem a assinatura de boot")
    tipos = [mbr[446 + k * 16 + 4] for k in range(4)]
    ini_fat = [struct.unpack_from("<I", mbr, 446 + k * 16 + 8)[0] for k in range(4) if tipos[k] == 0x0C]
    verificar(0xDA in tipos and ini_fat and ini_fat[0] % 2048 == 0, f"partições: {[hex(t) for t in tipos]}, FAT32 em {ini_fat}")
    # A FAT32 formatada pelo instalador, conferida pelo fsck.vfat do Linux.
    fat = os.path.join(SAIDA, "h97-fat.img")
    with open(hd, "rb") as f, open(fat, "wb") as g:
        f.seek(ini_fat[0] * 512)
        g.write(f.read())
    r = subprocess.run(["fsck.vfat", "-n", fat], capture_output=True, text=True)
    os.remove(fat)
    verificar(r.returncode == 0, f"fsck.vfat: a FAT32 do instalador está íntegra ({r.stdout.strip().splitlines()[-1]})")

    # 2. Ligar só pelo disco instalado; gravar um arquivo; reiniciar; ler.
    with Maquina("h97b", "ide", imagem=hd, memoria="768M", reiniciar=True) as m:
        m.esperar_serial("sagui:/$", 120)
        s = m.serial()
        verificar("tem configuracoes do SaguiOS: montado sozinho" in s, "a partição dos arquivos monta sozinha no boot")
        # Já instalado: o instalador some do menu (a busca não acha).
        n = len(m.serial())
        m.clique(40, 12)
        time.sleep(0.8)
        m.digitar("instalar", 0.08)
        esperar(m, '[GUI] menu: busca "instalar" (0 resultados)', 10, n)
        verificar(True, "ligado pelo disco instalado, o instalador não aparece no menu")
        m.tecla("esc")
        time.sleep(0.8)
        n = len(m.serial())
        m.digitar("echo guardado no disco > /usb/nota.txt\n", 0.03)
        time.sleep(1.5)
        m.digitar("reiniciar\n", 0.03)
        fim = time.time() + 180
        while time.time() < fim and m.serial()[n:].count("sagui:/$") < 2:
            time.sleep(0.3)
        n2 = len(m.serial())
        verificar("[SAGUI] reiniciando" in m.serial()[n:], "o sistema reiniciou")
        m.digitar("cat /usb/nota.txt\n", 0.03)
        time.sleep(2)
        m.captura("H97-depois-de-reiniciar")
        texto = m.tela_texto() if hasattr(m, "tela_texto") else ""
        verificar("guardado no disco" in m.serial()[n2:] or "guardado no disco" in texto,
                  "o arquivo continua no disco depois de reiniciar")
    return 0


if __name__ == "__main__":
    main()
