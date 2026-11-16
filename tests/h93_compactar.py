#!/usr/bin/env python3
"""H93 (parte 2) - gzip, tar e zip dentro do SaguiOS.

Critério:
- gzip compacta (apagando o original, ou guardando com -k), gzip -l lista,
  gzip -t confere, gunzip devolve igual e não substitui um arquivo que já
  existe sem -f; os apelidos compactar/descompactar funcionam;
- tar cria pacotes (com pastas, e com z o .tar.gz), lista (t, tv) e extrai
  (x, com -C); zip cria pacotes com pastas e unzip lista (-l) e extrai (-d);
  extrair de novo por cima é recusado sem a letra o / -o, e funciona com ela;
- um .tar.gz e um .zip feitos no computador pelo Python (tarfile e
  zipfile, com pastas, nome acentuado em UTF-8, caminho com mais de 100
  bytes, arquivos guardados e compactados) são extraídos no SaguiOS;
- pacotes maliciosos (zip e tar com "../fora.txt" e "/abs.txt") são
  recusados e nada é gravado fora da pasta de destino;
- tudo o que o SaguiOS criou (os .gz, .tar, .tar.gz, .zip e o que ele
  extraiu) é levado para o pendrive e conferido no computador com o gzip,
  o tarfile e o zipfile do Python, byte a byte.
"""
import gzip
import io
import os
import random
import re
import subprocess
import sys
import tarfile
import time
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, imagem_console, criar_pendrive, pendrive_qemu, SAIDA, FalhaTeste  # noqa: E402

sorte = random.Random(930)
TEXTO = "".join(f"linha {i:04d}: o sagui pula de galho em galho e come banana\n" for i in range(2500))
DADOS = bytes(sorte.randrange(256) for _ in range(40000)) + TEXTO.encode() * 3 + bytes(30000)

# A árvore que o Python empacota (nomes como vão no pacote: UTF-8).
# (Mais de 100 bytes no pacote, mas o caminho inteiro no SaguiOS, com a
# pasta de destino, cabe nos 128 do kernel.)
LONGO = "pasta-com-um-nome-bem-comprido-para-passar-dos-cem/" \
        "outra-pasta-mais-comprida/arquivo-no-fundo.txt"
ARVORE = {
    "projeto/leia.txt": b"leia-me do projeto\n",
    "projeto/src/principal.c": TEXTO[:5000].encode(),
    "projeto/dados/vazio.txt": b"",
    "projeto/acentuação.txt": "texto com acentuação: ção\n".encode("utf-8"),
    "projeto/" + LONGO: b"no fundo das pastas\n",
    "projeto/binario.bin": DADOS[:20000],
}


def verificar(cond, msg, detalhe=""):
    if not cond:
        raise FalhaTeste(f"{msg}: {detalhe}" if detalhe else msg)
    print("  ok:", msg)


def tar_do_python():
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz", format=tarfile.PAX_FORMAT) as t:
        pastas = sorted({"/".join(n.split("/")[:i]) for n in ARVORE for i in range(1, n.count("/") + 1)})
        for p in pastas:
            ti = tarfile.TarInfo(p)
            ti.type = tarfile.DIRTYPE
            ti.mode = 0o755
            t.addfile(ti)
        for nome, conteudo in ARVORE.items():
            ti = tarfile.TarInfo(nome)
            ti.size = len(conteudo)
            t.addfile(ti, io.BytesIO(conteudo))
    return buf.getvalue()


def zip_do_python():
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        z.writestr(zipfile.ZipInfo("projeto/"), b"")
        for i, (nome, conteudo) in enumerate(ARVORE.items()):
            z.writestr(nome, conteudo, compress_type=zipfile.ZIP_STORED if i % 2 else zipfile.ZIP_DEFLATED)
    return buf.getvalue()


def malicioso_zip():
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        z.writestr("bom.txt", b"este pode\n")
        z.writestr("../fora.txt", b"nao devia sair da pasta\n")
        z.writestr("/abs.txt", b"absoluto\n")
        z.writestr("a/../../fora2.txt", b"escondido\n")
    return buf.getvalue()


def malicioso_tar():
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w", format=tarfile.USTAR_FORMAT) as t:
        for nome, c in (("bom.txt", b"este pode\n"), ("../fora.txt", b"fora\n"), ("/abs.txt", b"abs\n")):
            ti = tarfile.TarInfo(nome)
            ti.size = len(c)
            t.addfile(ti, io.BytesIO(c))
    return buf.getvalue()


def tem(saida_ls, nome):
    """O ls mostrou este arquivo (uma linha "nome   tamanho bytes")?"""
    return re.search(rf"^{re.escape(nome)}\s+\d+ bytes", saida_ls, re.M) is not None


def ler_pen(pen, caminho):
    r = subprocess.run(["mtype", "-i", pen + "@@1M", f"::{caminho}"], capture_output=True)
    if r.returncode != 0:
        raise FalhaTeste(f"mtools: {caminho} não está no pendrive ({r.stderr.decode(errors='replace').strip()})")
    return r.stdout


def arvore_latin1():
    """A árvore como fica no SaguiOS (nomes em Latin-1) e volta pelo tar dele (UTF-8)."""
    return {n: c for n, c in ARVORE.items()}


def conteudo_tar(dados, prefixo=""):
    with tarfile.open(fileobj=io.BytesIO(dados)) as t:
        return {m.name[len(prefixo):]: t.extractfile(m).read() for m in t.getmembers()
                if m.isfile() and m.name.startswith(prefixo)}, {m.name for m in t.getmembers() if m.isdir()}


def main():
    pen = criar_pendrive("h93-pen", rotulo="PACOTES", arquivos={
        "texto.txt": TEXTO, "dados.bin": DADOS, "host.tgz": tar_do_python(), "host.zip": zip_do_python(),
        "mal.zip": malicioso_zip(), "mal.tar": malicioso_tar()})
    extras = ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)
    with Maquina("h93", "ide", imagem=imagem_console(), extras=extras) as m:
        m.esperar_serial("sagui:/$", 120)
        m.comando("montar")
        m.comando("mkdir /tmp/t")
        m.comando("cd /tmp/t")
        m.comando("cp /usb/texto.txt texto.txt")
        m.comando("cp /usb/dados.bin dados.bin")

        # ---- gzip
        m.comando("gzip -k dados.bin", tempo=120)
        out = m.comando("compactar texto.txt", tempo=120)
        out = m.comando("ls")
        verificar(tem(out, "dados.bin.gz") and tem(out, "dados.bin") and tem(out, "texto.txt.gz") and not tem(out, "texto.txt"),
                  "gzip -k guarda o original; compactar (apelido) apaga o original")
        out = m.comando("gzip -l texto.txt.gz")
        verificar("texto.txt" in out and str(len(TEXTO)) in out and "%" in out, "gzip -l mostra tamanhos, taxa e nome", out)
        out = m.comando("gzip -t dados.bin.gz", tempo=120)
        verificar("dados.bin.gz: ok" in out, "gzip -t confere")
        m.comando("cp texto.txt.gz /usb/texto-sagui.txt.gz")
        out = m.comando("descompactar texto.txt.gz", tempo=120)
        out = m.comando("ls")
        verificar(tem(out, "texto.txt") and not tem(out, "texto.txt.gz"), "descompactar (apelido de gunzip) devolve o arquivo")
        out = m.comando("gunzip dados.bin.gz", tempo=120)
        verificar("já existe" in out, "gunzip não substitui um arquivo que já existe")
        m.comando("cp dados.bin.gz /usb/dados-sagui.bin.gz")
        m.comando("mv dados.bin dados-original.bin")
        out = m.comando("gunzip dados.bin.gz", tempo=120)
        verificar("erro" not in out.lower(), "gunzip de um arquivo binário de %d bytes" % len(DADOS))
        m.comando("gzip -9 -c texto.txt > nivel9.gz", tempo=120)
        m.comando("cp nivel9.gz /usb/nivel9.gz")

        # ---- tar e zip feitos no SaguiOS
        m.comando("mkdir arv")
        m.comando("mkdir arv/sub")
        m.comando("echo primeiro arquivo > arv/um.txt")
        m.comando("echo dentro da subpasta > arv/sub/dois.txt")
        m.comando("cp texto.txt arv/sub/texto.txt")
        m.comando("cp dados.bin arv/dados.bin")
        out = m.comando("tar cvf pac.tar arv", tempo=120)
        verificar("arv/sub/dois.txt" in out, "tar cvf mostra os nomes")
        out = m.comando("empacotar tvf pac.tar")
        verificar(re.search(r"d\s+0\s+arv/sub/", out) and re.search(rf"-\s+{len(TEXTO)}\s+arv/sub/texto.txt", out),
                  "tar tvf (apelido empacotar) lista pastas e tamanhos", out)
        m.comando("tar czf pac.tar.gz arv", tempo=180)
        out = m.comando("zip -9 pac.zip arv", tempo=180)
        verificar("compactado: arv/sub/texto.txt" in out and "itens" in out, "zip -9 compacta a pasta inteira")
        out = m.comando("unzip -l pac.zip")
        verificar("arv/sub/dois.txt" in out and "itens" in out, "unzip -l lista")
        out = m.comando("tar xvf pac.tar.gz -C x", tempo=180)
        verificar("arv/sub/texto.txt" in out, "tar x de um .tar.gz (reconhecido sozinho) com -C")
        out = m.comando("deszipar -d z pac.zip", tempo=180)
        verificar("extraído: z/arv/sub/texto.txt" in out, "deszipar (apelido de unzip) com -d")
        out = m.comando("tar xf pac.tar -C x")
        verificar("já existe" in out, "tar x por cima: recusa substituir sem a letra o")
        out = m.comando("unzip -d z pac.zip")
        verificar("já existe" in out, "unzip por cima: recusa substituir sem -o")
        out = m.comando("tar xof pac.tar -C x", tempo=120)
        verificar("já existe" not in out, "tar xof substitui")
        out = m.comando("unzip -o -q -d z pac.zip", tempo=120)
        verificar("já existe" not in out, "unzip -o substitui")

        # ---- pacotes feitos no computador
        out = m.comando("tar xvf /usb/host.tgz -C h", tempo=180)
        verificar("arquivo-no-fundo.txt" in out and "tar:" not in out, "tar extrai o .tar.gz do Python (pax, nome longo)")
        out = m.comando("unzip -d hz /usb/host.zip", tempo=180)
        verificar("hz/projeto/binario.bin" in out and "unzip:" not in out, "unzip extrai o .zip do Python")
        out = m.comando("ls h/projeto")
        verificar("acentua" in out, "nome acentuado vindo em UTF-8 vira Latin-1")

        # ---- maliciosos
        m.comando("mkdir mal")
        out = m.comando("unzip -d mal/z /usb/mal.zip")
        verificar(out.count("recusado") == 3 and "extraído: mal/z/bom.txt" in out, "zip slip: 3 caminhos perigosos recusados", out)
        out = m.comando("tar xvf /usb/mal.tar -C mal/t")
        verificar(out.count("recusado") == 2 and "bom.txt" in out, "tar: ../ e / recusados", out)
        out = m.comando("ls /tmp/t/mal") + m.comando("ls /tmp/t") + m.comando("ls /")
        verificar("fora" not in out and "abs.txt" not in out, "nada foi gravado fora da pasta de destino")

        # ---- leva tudo para o pendrive
        # (Num pacote só: uma gravação grande no pendrive em vez de várias.)
        out = m.comando("tar cf /usb/extraido.tar x z h hz dados.bin dados-original.bin pac.tar pac.tar.gz pac.zip", tempo=180)
        verificar("tar:" not in out, "tar cf grava o pacote com tudo no pendrive", out)
        m.captura("H93-pacotes")
        out = m.comando("desmontar")
        verificar("Pode remover" in out, "desmontar grava o pendrive")
        time.sleep(0.5)

    # ---- conferência no computador
    verificar(gzip.decompress(ler_pen(pen, "/texto-sagui.txt.gz")) == TEXTO.encode(), "Python: o .gz do SaguiOS (texto) abre igual")
    g = ler_pen(pen, "/texto-sagui.txt.gz")
    verificar(g[3] & 8 and g[10:20] == b"texto.txt\0", "Python: com o nome original no cabeçalho")
    verificar(gzip.decompress(ler_pen(pen, "/dados-sagui.bin.gz")) == DADOS, "Python: o .gz do SaguiOS (binário) abre igual")
    verificar(gzip.decompress(ler_pen(pen, "/nivel9.gz")) == TEXTO.encode(), "Python: gzip -9 -c pela saída abre igual")
    esperado = {"arv/um.txt": b"primeiro arquivo\n", "arv/sub/dois.txt": b"dentro da subpasta\n",
                "arv/sub/texto.txt": TEXTO.encode(), "arv/dados.bin": DADOS}
    tudo, _ = conteudo_tar(ler_pen(pen, "/extraido.tar"))
    for nome in ("pac.tar", "pac.tar.gz"):
        arqs, pastas = conteudo_tar(tudo[nome])
        verificar(arqs == esperado and {"arv", "arv/sub"} <= {p.rstrip("/") for p in pastas},
                  f"Python (tarfile): {nome} do SaguiOS tem as pastas e os arquivos certos", f"{sorted(arqs)} {pastas}")
    with zipfile.ZipFile(io.BytesIO(tudo["pac.zip"])) as z:
        verificar(z.testzip() is None, "Python (zipfile): o .zip do SaguiOS passa no teste de CRC")
        arqs = {i.filename: z.read(i) for i in z.infolist() if not i.is_dir()}
        pastas = {i.filename for i in z.infolist() if i.is_dir()}
        verificar(arqs == esperado and {"arv/", "arv/sub/"} <= pastas, "Python (zipfile): conteúdo e pastas certos")
        verificar(any(i.compress_type == zipfile.ZIP_DEFLATED for i in z.infolist()), "  com arquivos compactados (DEFLATE)")
    arqs = tudo
    verificar(arqs["dados.bin"] == DADOS and arqs["dados-original.bin"] == DADOS, "gunzip no SaguiOS devolveu o binário igual")
    for pre in ("x/", "z/"):
        verificar({k[len(pre):]: v for k, v in arqs.items() if k.startswith(pre)} == esperado,
                  f"o que o SaguiOS extraiu em {pre} é igual ao original")
    for pre in ("h/", "hz/"):
        verificar({k[len(pre):]: v for k, v in arqs.items() if k.startswith(pre)} == arvore_latin1(),
                  f"os pacotes do Python extraídos em {pre} voltam iguais (nomes e conteúdo)")


if __name__ == "__main__":
    try:
        main()
        print("PASSOU")
    except FalhaTeste as e:
        print("FALHOU:", e)
        sys.exit(1)
