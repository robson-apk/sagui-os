#!/usr/bin/env python3
"""H04 - imagem para o pendrive e para o VirtualBox.

Critério: boot nos dois modos do QEMU; .vdi gerado sem erro; SHA-256 gerado.
O .vdi também é testado dando boot nele pelo QEMU (não há VirtualBox na VPS)
e convertido de volta para conferir que o conteúdo é o mesmo do .img.
"""
import hashlib
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, RAIZ, SAIDA  # noqa: E402

B = os.path.join(RAIZ, "build")


def main():
    subprocess.run(["make", "-s", "-C", RAIZ, "vdi"], check=True, stdout=subprocess.DEVNULL)
    img = os.path.join(B, "saguios.img")
    vdi = os.path.join(B, "saguios.vdi")
    verificar(os.path.exists(vdi), ".vdi gerado")
    info = subprocess.run(["qemu-img", "info", vdi], capture_output=True, text=True, check=True).stdout
    verificar("file format: vdi" in info, "qemu-img reconhece o arquivo como VDI")

    for arq in ("saguios.img", "saguios.vdi"):
        linha = open(os.path.join(B, arq + ".sha256")).read().split()
        real = hashlib.sha256(open(os.path.join(B, arq), "rb").read()).hexdigest()
        verificar(linha[0] == real and linha[1] == arq, f"SHA-256 de {arq} confere ({real[:16]}...)")

    volta = os.path.join(SAIDA, "vdi-volta.raw")
    os.makedirs(SAIDA, exist_ok=True)
    subprocess.run(["qemu-img", "convert", "-f", "vdi", "-O", "raw", vdi, volta], check=True)
    dados_img = open(img, "rb").read()
    dados_volta = open(volta, "rb").read()
    verificar(dados_volta[:len(dados_img)] == dados_img, "conteúdo do .vdi é idêntico ao .img")
    verificar(dados_volta[len(dados_img):] == bytes(len(dados_volta) - len(dados_img)), "sobra do .vdi está zerada")

    tam = int(open(os.path.join(B, "tamanho.txt")).read())
    # A meta de 1,44 MB era da versão 1.0; na 2.0 não há teto (o tamanho só é medido).
    verificar(tam > 0, f"tamanho útil {tam} bytes ({100 * tam / 1474560:.0f}% de um disquete de 1,44 MB; sem teto na 2.0)")

    for modo in ("ide", "usb"):
        with Maquina("h04", modo) as m:
            m.esperar_serial("[SAGUI] boot ok", 120)
            verificar(True, f"[{modo}] boot da imagem .img até o kernel")
    with Maquina("h04-vdi", "ide", imagem=vdi, formato="vdi") as m:
        m.esperar_serial("[SAGUI] boot ok", 120)
        verificar(True, "[vdi] boot do arquivo .vdi até o kernel")
        m.captura("H04-boot-vdi")
    return 0


if __name__ == "__main__":
    sys.exit(main())
