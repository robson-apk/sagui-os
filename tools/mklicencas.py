#!/usr/bin/env python3
"""mklicencas.py - junta os textos de licença que vão dentro da imagem (H94).

Uso: mklicencas.py <raiz_do_repositorio> <pasta_de_saida>

A saída é a pasta /sistema/licencas/ do SaguiOS (no build, em
build/rootfs/sistema/licencas). Os textos não ficam duplicados no git: cada
um é copiado, na hora do build, do lugar onde já está no repositório:

  - LICENSE (a GPL 3 do SaguiOS), CONTRIBUIDORES.md e CREDITOS.md;
  - cada pasta de terceiros/: o ORIGEM.md e todos os arquivos de licença
    (LICENSE*, COPYING*, PATENTS*, NOTICE*), juntos num arquivo só por
    componente. Uma pasta de terceiros/ sem arquivo de licença faz o build
    parar: o código de fora só entra com a licença junto (regra da D054);
  - as licenças dos dados (fontes DejaVu, tabelas do Unicode), que estão ao
    lado dos dados;
  - avisos escritos aqui para o que não tem um arquivo de licença no
    repositório (o dicionário da RFC 7932, os certificados raiz da Mozilla,
    o fundo da Mata Atlântica e as tabelas das normas do MP3 e do H.264).

Também grava:
  - indice.txt: uma linha "arquivo|nome" por item, na ordem em que a janela
    "Créditos e licenças" (sobre-o-sistema creditos) mostra a lista;
  - LEIA-ME.txt: o mesmo índice, para quem abre a pasta pelo Arquivos ou
    pelo Terminal.

Os arquivos saem em UTF-8 com extensão .txt; o mksgfs.py os converte para
Latin-1, a codificação do sistema. Antes disso, as poucas letras tipográficas
que não existem no Latin-1 (travessão, aspas curvas, reticências, setas)
viram os equivalentes simples, em vez de "?".
"""
import os
import re
import sys

# Letras fora do Latin-1 que aparecem nos textos e têm um equivalente simples.
TROCAS = {
    "—": "-", "–": "-", "‒": "-", "‐": "-", "‑": "-",
    "“": '"', "”": '"', "„": '"', "‘": "'", "’": "'",
    "…": "...", "→": "->", "←": "<-", "•": "*", " ": " ",
    "≤": "<=", "≥": ">=", "™": "(TM)", " ": " ", " ": " ",
}

PREFIXOS_LICENCA = ("LICENSE", "LICENCE", "COPYING", "PATENTS", "NOTICE", "LICENCA")

# Textos escritos aqui: dados que não têm arquivo de licença no repositório.
BROTLI = """Dicionário do Brotli (RFC 7932)
===============================

Onde está no SaguiOS: userland/navegador/fontes/brotli_tab.h (no Cipó, para
as fontes WOFF2 e as páginas compactadas com Brotli).

O que é: o dicionário estático e as transformações dos apêndices A e B da
RFC 7932, "Brotli Compressed Data Format", de J. Alakuijala e Z. Szabadka
(julho de 2016). Só os números; o descompactador foi escrito no projeto.

Copyright (c) 2016 IETF Trust and the persons identified as the document
authors. All rights reserved.

Os componentes de código das RFCs podem ser usados nos termos da licença BSD
simplificada descrita na seção 4.e das "Legal Provisions Relating to IETF
Documents" do IETF Trust (https://trustee.ietf.org/license-info), e são
fornecidos sem garantia, como diz essa licença:

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of Internet Society, IETF or IETF Trust, nor the names of
   specific contributors, may be used to endorse or promote products derived
   from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
"""

CERTIFICADOS = """Certificados raiz (lista da Mozilla)
====================================

Onde estão no SaguiOS: /sistema/certificados/ (24 arquivos .der). O HTTPS
usa esses certificados para conferir que um site é quem diz ser.

De onde vieram: a lista de autoridades certificadoras da Mozilla (NSS), na
forma em que o Ubuntu a distribui no pacote ca-certificates, copiada por
tools/mkraizes.py. A lista de quais raízes entraram, com o nome e a validade
de cada uma, está em CREDITOS.md (no item "Créditos (resumo)" desta janela).

Licença: Mozilla Public License, versão 2.0 (MPL 2.0), que permite
redistribuir e é compatível com a GPL 3 do SaguiOS.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at https://mozilla.org/MPL/2.0/.

Os certificados são distribuídos sem mudanças (só convertidos de PEM para
DER, a mesma informação em binário). A forma original está no projeto NSS
da Mozilla (arquivo certdata.txt) e no pacote ca-certificates das
distribuições.
"""

MATA_ATLANTICA = """Fundo "Mata Atlântica"
======================

Onde está no SaguiOS: /arte/mata-atlantica.sgi (um dos fundos da área de
trabalho, escolhido nas Configurações).

Origem: imagem gerada por inteligência artificial (modelo Z-Image Turbo) e
trazida por Robson de Lima Barbosa no pull request #1 do SaguiOS; convertida
para o formato .sgi por tools/mkfundo.py.

Licença: distribuída com o SaguiOS sob a GPL 3, como o resto da arte do
projeto (veja o item "SaguiOS (GPL 3)").
"""

NORMAS = """Tabelas das normas do MP3 e do H.264
====================================

Não são código de terceiros, mas dados que vêm do texto das normas e que todo
decodificador precisa ter, iguais em todos. O código que os usa foi escrito
no SaguiOS, a partir do texto das normas.

- MP3 (userland/tocador/mp3_tabelas.h): os códigos de Huffman, as faixas de
  fator de escala, a pré-ênfase e a janela de síntese dos anexos da
  ISO/IEC 11172-3, e as contagens de fatores de escala da ISO/IEC 13818-3
  (D047).

- H.264 perfil High (userland/video/h264_alto_tab.h): os contextos do CABAC
  para os blocos 8x8 (tabela 9-43), o normAdjust8x8 (equação 8-316) e as
  listas de escala padrão (tabelas 7-3 e 7-4) da Recomendação ITU-T H.264
  (ISO/IEC 14496-10) (D048).

As tabelas do CABAC e do CAVLC do H.264 que vieram com o pull request #6
continuam em terceiros/ffmpeg-h264-tabelas/ e têm o seu próprio item nesta
lista, com a licença LGPL do FFmpeg.
"""


def ler(caminho):
    with open(caminho, encoding="utf-8") as f:
        return f.read()


def limpar(texto):
    """Troca as letras tipográficas e tira os comentários HTML do Markdown."""
    texto = re.sub(r"<!--.*?-->\n?", "", texto, flags=re.S)
    for de, para in TROCAS.items():
        texto = texto.replace(de, para)
    return texto


def licenca_nos_creditos(creditos, pasta):
    """A coluna "Licença" da linha de terceiros/<pasta>/ na tabela do CREDITOS.md."""
    for linha in creditos.splitlines():
        if f"`terceiros/{pasta}/`" in linha:
            colunas = [c.strip() for c in linha.strip().strip("|").split("|")]
            if len(colunas) >= 4:
                return colunas[-1]
    return ""


def nome_do_terceiro(raiz, pasta, creditos):
    """Nome curto para a lista: o título do ORIGEM.md antes dos dois-pontos,
    e a licença (sem os detalhes entre parênteses) se ainda couber na lista
    da janela (32 letras)."""
    nome = pasta
    origem = os.path.join(raiz, "terceiros", pasta, "ORIGEM.md")
    if os.path.exists(origem):
        for linha in ler(origem).splitlines():
            if linha.startswith("# "):
                nome = linha[2:].split(":")[0].strip()
                break
    lic = licenca_nos_creditos(creditos, pasta)
    lic = re.split(r" \(| \+ ", lic)[0].strip()
    rotulo = f"{nome} ({lic})" if lic else nome
    return rotulo if len(rotulo) <= 32 else nome


def gravar(saida, arquivo, texto):
    with open(os.path.join(saida, arquivo), "w", encoding="utf-8") as f:
        f.write(limpar(texto).rstrip("\n") + "\n")


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    raiz, saida = sys.argv[1], sys.argv[2]
    os.makedirs(saida, exist_ok=True)
    creditos = ler(os.path.join(raiz, "CREDITOS.md"))
    itens = []   # (arquivo, nome na lista)

    gravar(saida, "gpl-3.txt", ler(os.path.join(raiz, "LICENSE")))
    itens.append(("gpl-3.txt", "SaguiOS (GPL 3)"))
    gravar(saida, "contribuidores.txt", ler(os.path.join(raiz, "CONTRIBUIDORES.md")))
    itens.append(("contribuidores.txt", "Quem fez o SaguiOS"))
    gravar(saida, "creditos.txt", creditos)
    itens.append(("creditos.txt", "Créditos (resumo)"))

    # Código de terceiros: todas as pastas, sem lista fixa, para que uma pasta
    # nova apareça sozinha na janela.
    pasta_terc = os.path.join(raiz, "terceiros")
    for pasta in sorted(os.listdir(pasta_terc)):
        dir_ = os.path.join(pasta_terc, pasta)
        if not os.path.isdir(dir_):
            continue
        licencas = sorted(a for a in os.listdir(dir_) if a.upper().startswith(PREFIXOS_LICENCA))
        if not licencas:
            sys.exit(f"mklicencas: terceiros/{pasta}/ não tem arquivo de licença (LICENSE, COPYING...)")
        partes = []
        if os.path.exists(os.path.join(dir_, "ORIGEM.md")):
            partes.append(ler(os.path.join(dir_, "ORIGEM.md")))
        for lic in licencas:
            partes.append(f"==== {lic} (terceiros/{pasta}/{lic}) ====\n\n" + ler(os.path.join(dir_, lic)))
        arquivo = f"{pasta}.txt"
        gravar(saida, arquivo, "\n\n".join(p.rstrip("\n") for p in partes))
        itens.append((arquivo, nome_do_terceiro(raiz, pasta, creditos)))

    # Dados com licença própria, que ficam ao lado dos dados.
    gravar(saida, "dejavu.txt", "Fontes DejaVu (/fontes/)\n========================\n\n" +
           ler(os.path.join(raiz, "rootfs", "fontes", "LICENCA-DejaVu.txt")))
    itens.append(("dejavu.txt", "Fontes DejaVu (Bitstream Vera)"))
    gravar(saida, "unicode.txt", "Tabelas do Unicode 16.0.0 (quebra de linha do Cipó)\n"
           "===================================================\n\n" +
           ler(os.path.join(raiz, "userland", "navegador", "motor", "LICENCA-Unicode.txt")))
    itens.append(("unicode.txt", "Dados do Unicode (Unicode v3)"))

    # Avisos escritos aqui.
    for arquivo, nome, texto in (("brotli-rfc7932.txt", "Brotli, RFC 7932 (BSD)", BROTLI),
                                 ("certificados.txt", "Certificados raiz (MPL 2.0)", CERTIFICADOS + "\n\nTexto completo da MPL 2.0\n=========================\n\n" +
                                  ler(os.path.join(raiz, "docs", "licencas", "MPL-2.0.txt"))),
                                 ("mata-atlantica.txt", "Fundo Mata Atlântica", MATA_ATLANTICA),
                                 ("normas.txt", "Tabelas do MP3 e do H.264", NORMAS)):
        gravar(saida, arquivo, texto)
        itens.append((arquivo, nome))

    with open(os.path.join(saida, "indice.txt"), "w", encoding="utf-8") as f:
        for arquivo, nome in itens:
            f.write(f"{arquivo}|{nome}\n")
    leia = ["Licenças e créditos do SaguiOS", "==============================", "",
            "Esta pasta é gerada no build (tools/mklicencas.py) a partir dos arquivos",
            "do repositório. A janela \"Sobre o SaguiOS\", no botão \"Créditos e",
            "licenças\", mostra os mesmos textos.", ""]
    largura = max(len(a) for a, _ in itens)
    leia += [f"  {a.ljust(largura)}  {n}" for a, n in itens]
    gravar(saida, "LEIA-ME.txt", "\n".join(leia))
    print(f"mklicencas: {len(itens)} textos em {saida}")


if __name__ == "__main__":
    main()
