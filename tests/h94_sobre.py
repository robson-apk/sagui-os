#!/usr/bin/env python3
"""H94 - a janela "Sobre o SaguiOS", os créditos, as licenças e o histórico.

Critério:
  - no computador: cada pasta de terceiros/ tem arquivo de licença, e cada um
    desses arquivos chegou, pelo build, a /sistema/licencas/<pasta>.txt
    dentro do ramdisk (um componente novo sem licença faz este teste falhar);
    o índice da janela lista todos;
  - no SaguiOS: a janela Sobre mostra a versão 2.2.0, a arquitetura (32 bits),
    os créditos e os botões "Créditos e licenças" e "Histórico"; o botão abre a
    janela das licenças, que começa na GPL 3; escolher um componente de
    terceiros mostra o texto dele; o botão "Histórico" mostra o VERSOES.md.
"""
import os
import re
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, janela, RAIZ  # noqa: E402

ICONES = ["Arquivos", "Terminal", "Editor", "Calculadora", "Banana", "Sagui 3D", "Configurações", "Sobre"]
POR_COLUNA = (768 - 40) // 92
LICENCAS = os.path.join(RAIZ, "build", "rootfs", "sistema", "licencas")
PREFIXOS = ("LICENSE", "LICENCE", "COPYING", "PATENTS", "NOTICE", "LICENCA")


def caminhos_do_ramdisk():
    """Os caminhos da tabela do SGFS (formato em tools/mksgfs.py)."""
    with open(os.path.join(RAIZ, "build", "ramdisk.sgfs"), "rb") as f:
        dados = f.read()
    _ass, _v, tam_ent, qtd, tab = struct.unpack_from("<4sHHII", dados, 0)
    caminhos = {}
    for i in range(qtd):
        cam, _tipo, tam, desloc = struct.unpack_from("<56sB3xII", dados, tab + i * tam_ent)
        caminhos[cam.split(b"\0")[0].decode("latin-1")] = dados[desloc:desloc + tam]
    return caminhos


def conferir_no_computador():
    ramdisk = caminhos_do_ramdisk()
    indice = open(os.path.join(LICENCAS, "indice.txt"), encoding="utf-8").read()
    pastas = sorted(p for p in os.listdir(os.path.join(RAIZ, "terceiros"))
                    if os.path.isdir(os.path.join(RAIZ, "terceiros", p)))
    verificar(len(pastas) >= 5, f"{len(pastas)} pastas em terceiros/")
    for pasta in pastas:
        dir_ = os.path.join(RAIZ, "terceiros", pasta)
        licencas = [a for a in os.listdir(dir_) if a.upper().startswith(PREFIXOS)]
        verificar(licencas, f"terceiros/{pasta}/ tem arquivo de licença ({', '.join(sorted(licencas))})")
        verificar(os.path.exists(os.path.join(dir_, "ORIGEM.md")), f"terceiros/{pasta}/ tem ORIGEM.md")
        dentro = ramdisk.get(f"sistema/licencas/{pasta}.txt")
        verificar(dentro is not None, f"/sistema/licencas/{pasta}.txt está no ramdisk")
        texto = dentro.decode("latin-1")
        for lic in licencas:
            # Cada linha com letras do arquivo de licença tem de estar lá (as
            # linhas só em ASCII; as outras passaram pela troca para Latin-1).
            linhas = [l.strip() for l in open(os.path.join(dir_, lic), encoding="utf-8")
                      if l.strip() and all(ord(c) < 128 for c in l)]
            faltam = [l for l in linhas if l not in texto]
            verificar(linhas and not faltam, f"{pasta}: o texto inteiro de {lic} está na imagem ({len(linhas)} linhas)")
        verificar(f"{pasta}.txt|" in indice, f"{pasta} aparece na lista da janela Créditos e licenças")
    gpl = ramdisk.get("sistema/licencas/gpl-3.txt", b"")
    verificar(gpl.strip() == open(os.path.join(RAIZ, "LICENSE"), "rb").read().strip(), "a GPL 3 (LICENSE) está inteira na imagem")
    for arq in ("LEIA-ME.txt", "contribuidores.txt", "dejavu.txt", "unicode.txt", "certificados.txt",
                "brotli-rfc7932.txt", "mata-atlantica.txt"):
        verificar(f"sistema/licencas/{arq}" in ramdisk, f"/sistema/licencas/{arq} está na imagem")
    verificar(b"mozilla.org/MPL/2.0" in ramdisk["sistema/licencas/certificados.txt"], "aviso da MPL 2.0 dos certificados")
    verificar("sistema/ajuda/versoes.md" in ramdisk, "VERSOES.md vai para /sistema/ajuda/versoes.md")
    itens = [l.split("|") for l in indice.splitlines() if l]
    return itens


def main():
    itens = conferir_no_computador()
    alvo = next(i for i, (arq, _n) in enumerate(itens) if arq == "font-rs.txt")
    with Maquina("h94", "ide") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        i = ICONES.index("Sobre")
        m.clique(20 + (i // POR_COLUNA) * 110 + 48, 40 + (i % POR_COLUNA) * 92 + 32, duplo=True)
        m.esperar_serial("sobre: versao", 15)
        s = m.serial()
        v = re.findall(r"sobre: versao (.*?), imagem \d+ bytes, \d+ processos, (\d+) bits", s)[-1]
        verificar(v[0] == "2.2.0", f"Sobre mostra a versão {v[0]}")
        verificar(v[1] == "32", f"Sobre mostra a arquitetura: {v[1]} bits")
        for linha in ("SaguiOS, um projeto de Eli Rigobeli.",
                      "Escrito por inteligência artificial (Claude, da",
                      "Anthropic), sob a direção do Eli.",
                      "Desde a versão 2, com a colaboração da comunidade."):
            verificar(f"sobre: creditos: {linha}" in s, f"créditos: {linha}")
        verificar("sobre: botoes: Créditos e licenças | Histórico | Fechar" in s, "botões Créditos e licenças, Histórico e Fechar")
        time.sleep(1)
        m.captura("H94-1-sobre")
        sobre = janela(m.serial(), "Sobre o SaguiOS")

        # Créditos e licenças: começa na GPL 3.
        m.clique(sobre["x"] + 20 + 85, sobre["y"] + 376 + 14)
        m.esperar_serial("licencas: pronto", 15)
        m.esperar_serial("licencas: mostrando SaguiOS (GPL 3)", 10)
        s = m.serial()
        qtd = int(re.findall(r"licencas: pronto, (\d+) itens", s)[-1])
        verificar(qtd == len(itens), f"a lista tem os {qtd} itens do índice")
        verificar(re.search(r"licencas: mostrando SaguiOS \(GPL 3\) \(\d+ bytes\): +GNU GENERAL PUBLIC LICENSE", s),
                  "a janela abre no texto da GPL 3")
        time.sleep(0.5)
        lic = janela(m.serial(), "Créditos e licenças")
        m.clique(lic["x"] + 10 + 100, lic["y"] + 10 + 2 + alvo * 20 + 10)
        m.esperar_serial("licencas: mostrando font-rs", 10)
        verificar(re.search(r"licencas: mostrando font-rs \(Apache License 2\.0\) \(\d+ bytes\): # font-rs: rasterizador", m.serial()),
                  "escolher o font-rs mostra o ORIGEM.md e a licença dele")
        time.sleep(1)
        m.captura("H94-2-licencas")
        m.tecla("esc")
        m.esperar_serial("(sobre-o-sistema) terminou", 10)
        time.sleep(0.5)

        # Histórico: o VERSOES.md.
        m.clique(sobre["x"] + 200 + 55, sobre["y"] + 376 + 14)
        m.esperar_serial("historico: pronto", 15)
        verificar(re.search(r"historico: pronto \(\d+ bytes\): # Histórico de versões do SaguiOS", m.serial()),
                  "o botão Histórico mostra o VERSOES.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
