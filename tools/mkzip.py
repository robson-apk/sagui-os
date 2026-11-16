#!/usr/bin/env python3
"""
mkzip.py - junta arquivos num ZIP (com DEFLATE), para o projeto JClic de
exemplo do SaguiOS (H92): o build põe userland/jogos/jclic/exemplo/ em
/jogos/jclic/sagui.jclic.zip, que é como os projetos JClic são distribuídos.

Uso: mkzip.py <saida.zip> <arquivo>...   (cada arquivo entra só com o nome)

A data de cada entrada é fixa, para o ZIP sair igual a cada build.
"""
import os
import sys
import zipfile


def main():
    if len(sys.argv) < 3:
        sys.exit("uso: mkzip.py <saida.zip> <arquivo>...")
    with zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED) as z:
        for caminho in sys.argv[2:]:
            info = zipfile.ZipInfo(os.path.basename(caminho), date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(caminho, "rb") as f:
                z.writestr(info, f.read())


if __name__ == "__main__":
    main()
