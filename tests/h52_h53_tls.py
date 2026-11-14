#!/usr/bin/env python3
"""H52 e H53 - certificados X.509 e cliente TLS 1.3 (userland/tls).

Critério: a biblioteca compila com as opções do SaguiOS sem depender de nada
além da userland/cripto; valida cadeias de certificados (e recusa, com a
mensagem certa, cada tipo de cadeia ruim); e conversa em TLS 1.3 com o
servidor do openssl em todos os conjuntos de cifras e tipos de chave, e com
servidores de verdade na internet.

Este teste não usa o QEMU: a biblioteca é ligada a um programa de teste
(tests/tls/teste_tls.c) que roda aqui mesmo, em 32 bits, sobre soquetes do
Linux. O comando openssl entra só como ferramenta: fabrica as autoridades e
os certificados de teste e faz o papel de servidor (openssl s_server).
"""
import glob
import hashlib
import os
import random
import re
import shutil
import socket
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True
from qemu import FalhaTeste, verificar, RAIZ, SAIDA  # noqa: E402

FONTES_TLS = os.path.join(RAIZ, "userland", "tls")
FONTES_CRIPTO = os.path.join(RAIZ, "userland", "cripto")
PASTA = os.path.join(SAIDA, "h52-tls")
PKI = os.path.join(PASTA, "pki")
PROGRAMA = os.path.join(PASTA, "teste_tls")

# As mesmas opções com que os programas do SaguiOS são compilados (ver
# h50_cripto.py).
OPCOES = ["-m32", "-march=i686", "-std=gnu11", "-O2", "-ffreestanding", "-fno-builtin", "-nostdlib",
          "-fno-pic", "-fno-pie", "-fno-stack-protector", "-mgeneral-regs-only",
          "-Wall", "-Wextra", "-Werror"]

# Maior quadro de pilha aceito para uma função da userland/tls. A mais gulosa
# é a x509_validar_cadeia (dez certificados lidos, uns 2,3 KB); somada à
# conferência de assinatura da cripto (uns 6 KB) fica longe dos 16 KB.
PILHA_MAX = 4096

CONJUNTOS = ["TLS_CHACHA20_POLY1305_SHA256", "TLS_AES_128_GCM_SHA256", "TLS_AES_256_GCM_SHA384"]

sorte = random.Random(52)


def executar(args, entrada=None, ambiente=None):
    r = subprocess.run(args, input=entrada, capture_output=True, env=ambiente)
    if r.returncode != 0:
        raise FalhaTeste(f"{' '.join(args[:4])}... falhou: {r.stderr.decode(errors='replace')[-600:]}")
    return r.stdout


def openssl(*args, entrada=None):
    return executar(["openssl", *args], entrada)


# ------------------------------------------------------------ compilação

def compilar():
    shutil.rmtree(PASTA, ignore_errors=True)
    os.makedirs(PKI)
    objetos_tls, objetos_cripto = [], []
    for fonte in sorted(glob.glob(os.path.join(FONTES_TLS, "*.c"))):
        obj = os.path.join(PASTA, "tls_" + os.path.basename(fonte)[:-2] + ".o")
        executar(["gcc", *OPCOES, "-fstack-usage", "-c", "-o", obj, fonte])
        objetos_tls.append(obj)
    for fonte in sorted(glob.glob(os.path.join(FONTES_CRIPTO, "*.c"))):
        obj = os.path.join(PASTA, "cripto_" + os.path.basename(fonte)[:-2] + ".o")
        executar(["gcc", *OPCOES, "-c", "-o", obj, fonte])
        objetos_cripto.append(obj)
    verificar(len(objetos_tls) >= 5,
              f"userland/tls compila com as opções do SaguiOS, sem avisos ({len(objetos_tls)} arquivos)")

    # Tudo que a userland/tls pede tem de estar nela mesma ou na cripto: nem
    # libc (memcpy, strlen), nem libgcc (__udivdi3), nem chamada ao sistema.
    def simbolos(objetos):
        definidos, pedidos = set(), set()
        for linha in executar(["nm", *objetos]).decode().split("\n"):
            partes = linha.split()
            if len(partes) >= 2 and partes[-2] == "U":
                pedidos.add(partes[-1])
            elif len(partes) == 3:
                definidos.add(partes[2])
        return definidos, pedidos

    def_tls, ped_tls = simbolos(objetos_tls)
    def_cripto, _ = simbolos(objetos_cripto)
    faltam = sorted(ped_tls - def_tls - def_cripto)
    verificar(not faltam, "userland/tls só depende da userland/cripto (nada de libc, libgcc ou sistema)"
              + (f": faltam {faltam}" if faltam else ""))

    maior, dono = 0, ""
    for su in glob.glob(os.path.join(PASTA, "tls_*.su")):
        for linha in open(su):
            partes = linha.rstrip("\n").split("\t")
            if len(partes) >= 2 and int(partes[1]) > maior:
                maior, dono = int(partes[1]), partes[0].split(":")[-1]
    verificar(0 < maior <= PILHA_MAX, f"nenhuma função usa mais de {PILHA_MAX} bytes de pilha (a maior: {dono}, {maior})")

    tamanhos = executar(["size", "-t", *objetos_tls]).decode().strip().split("\n")[-1].split()
    print(f"      (userland/tls: {tamanhos[0]} bytes de código, {tamanhos[1]} de dados, {tamanhos[2]} de bss)")

    executar(["gcc", "-m32", "-std=gnu11", "-no-pie", "-O1", "-Wall", "-Wextra", "-Werror", "-I", FONTES_TLS, "-o", PROGRAMA,
              os.path.join(RAIZ, "tests", "tls", "teste_tls.c"), *objetos_tls, *objetos_cripto])
    verificar(os.path.exists(PROGRAMA), "programa de teste ligado à biblioteca (32 bits)")


# ------------------------------------------------------------ autoridades de teste

EXTENSOES = """
[raiz]
basicConstraints=critical,CA:TRUE
keyUsage=critical,keyCertSign,cRLSign
subjectKeyIdentifier=hash
[inter]
basicConstraints=critical,CA:TRUE,pathlen:0
keyUsage=critical,keyCertSign,cRLSign
subjectKeyIdentifier=hash
authorityKeyIdentifier=keyid
[folha]
basicConstraints=CA:FALSE
keyUsage=critical,digitalSignature
extendedKeyUsage=serverAuth
subjectAltName=DNS:localhost,DNS:*.teste.sagui,DNS:exemplo.sagui,IP:127.0.0.1
authorityKeyIdentifier=keyid
[curingas_ruins]
basicConstraints=CA:FALSE
subjectAltName=DNS:*.sagui,DNS:w*.teste.sagui,DNS:*a.teste.sagui,DNS:a.*.sagui,DNS:*.*.sagui
[so_cn]
basicConstraints=CA:FALSE
[critica]
basicConstraints=CA:FALSE
subjectAltName=DNS:localhost
1.2.3.4.5.6=critical,ASN1:UTF8String:coisa que ninguem conhece
[so_cliente]
basicConstraints=CA:FALSE
extendedKeyUsage=clientAuth
subjectAltName=DNS:localhost
[so_cifrar]
basicConstraints=CA:FALSE
keyUsage=critical,keyEncipherment
subjectAltName=DNS:localhost
"""

CHAVES = {
    "rsa": ["-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048"],
    "rsa3072": ["-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:3072"],
    "p256": ["-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256"],
    "p384": ["-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-384"],
    "ed25519": ["-algorithm", "ED25519"],
}

serie = [1000]


def arq(nome, ext):
    return os.path.join(PKI, f"{nome}.{ext}")


def emitir(nome, cn, chave, emissor=None, secao="folha", resumo="sha256", extras=()):
    """Cria a chave e o certificado "nome". Sem emissor, é autoassinado."""
    openssl("genpkey", *CHAVES[chave], "-out", arq(nome, "key"))
    sujeito = f"/O=Teste SaguiOS/CN={cn}"
    serie[0] += 1
    comum = ["-days", "3650", "-set_serial", str(serie[0]), "-extfile", arq("extensoes", "cnf"),
             "-extensions", secao, *([] if chave == "ed25519" and emissor is None else ["-" + resumo]), *extras]
    openssl("req", "-new", "-key", arq(nome, "key"), "-subj", sujeito, "-out", arq(nome, "csr"))
    if emissor is None:
        openssl("x509", "-req", "-in", arq(nome, "csr"), "-signkey", arq(nome, "key"), "-out", arq(nome, "pem"), *comum)
    else:
        openssl("x509", "-req", "-in", arq(nome, "csr"), "-CA", arq(emissor, "pem"), "-CAkey", arq(emissor, "key"),
                "-out", arq(nome, "pem"), *comum)
    openssl("x509", "-in", arq(nome, "pem"), "-outform", "DER", "-out", arq(nome, "der"))


def fabricar_certificados():
    open(arq("extensoes", "cnf"), "w").write(EXTENSOES)

    # A família principal: raiz RSA -> intermediária RSA (sem netas) -> folhas.
    emitir("raiz", "Raiz de Teste Sagui", "rsa", secao="raiz")
    emitir("inter", "Intermediaria de Teste Sagui", "rsa", "raiz", "inter")
    emitir("folha-rsa", "servidor rsa", "rsa", "inter")
    emitir("folha-p256", "servidor p256", "p256", "inter", resumo="sha384")
    emitir("folha-p384", "servidor p384", "p384", "inter", resumo="sha512")
    emitir("folha-ed25519", "servidor ed25519", "ed25519", "inter")
    emitir("folha-pss", "servidor pss", "rsa3072", "inter",
           extras=["-sigopt", "rsa_padding_mode:pss", "-sigopt", "rsa_pss_saltlen:-1"])
    emitir("folha-pss384", "servidor pss384", "rsa", "inter", resumo="sha384",
           extras=["-sigopt", "rsa_padding_mode:pss", "-sigopt", "rsa_pss_saltlen:-1"])
    # Uma família só de curvas elípticas (assinaturas ECDSA nos certificados).
    emitir("raiz-ec", "Raiz Eliptica Sagui", "p384", secao="raiz", resumo="sha384")
    emitir("inter-ec", "Intermediaria Eliptica Sagui", "p256", "raiz-ec", "inter", resumo="sha384")
    emitir("folha-ec", "servidor ec", "p256", "inter-ec")
    # Uma autoridade em que ninguém confia.
    emitir("raiz-estranha", "Raiz Estranha", "rsa", secao="raiz")
    emitir("inter-estranha", "Intermediaria Estranha", "rsa", "raiz-estranha", "inter")
    emitir("folha-estranha", "servidor estranho", "p256", "inter-estranha")
    # Certificados com defeito.
    emitir("neta", "servidor neto", "p256", "folha-rsa")                  # emitido por quem não é autoridade
    emitir("inter-funda", "Intermediaria Funda", "p256", "inter", "inter")  # a "inter" não pode ter filhas
    emitir("folha-funda", "servidor fundo", "p256", "inter-funda")
    emitir("autoassinado", "servidor sozinho", "p256", secao="folha")
    emitir("curingas-ruins", "curingas", "p256", "inter", "curingas_ruins")
    emitir("so-cn", "so-cn.sagui", "p256", "inter", "so_cn")
    emitir("critica", "servidor critico", "p256", "inter", "critica")
    emitir("so-cliente", "servidor cliente", "p256", "inter", "so_cliente")
    emitir("so-cifrar", "servidor cifrador", "rsa", "inter", "so_cifrar")

    # A folha RSA com um bit trocado na assinatura (os últimos bytes do DER).
    der = bytearray(open(arq("folha-rsa", "der"), "rb").read())
    der[-20] ^= 0x04
    open(arq("adulterada", "der"), "wb").write(der)
    openssl("x509", "-inform", "DER", "-in", arq("adulterada", "der"), "-out", arq("adulterada", "pem"))
    shutil.copy(arq("folha-rsa", "key"), arq("adulterada", "key"))
    # E com um bit trocado no meio da parte assinada (dentro do nome do dono).
    der = bytearray(open(arq("folha-rsa", "der"), "rb").read())
    pos = der.index(b"servidor rsa")
    der[pos] ^= 0x01
    open(arq("adulterada-tbs", "der"), "wb").write(der)
    verificar(len(glob.glob(os.path.join(PKI, "*.der"))) >= 24,
              "autoridades e certificados de teste fabricados com o openssl (RSA, P-256, P-384, Ed25519, PSS)")


# ------------------------------------------------------------ programa de teste

def rodar(*args, ambiente=None):
    amb = dict(os.environ, **(ambiente or {}))
    r = subprocess.run([PROGRAMA, *args], capture_output=True, env=amb, timeout=120)
    if r.returncode != 0:
        raise FalhaTeste(f"o programa de teste terminou com código {r.returncode} ({' '.join(args[:3])}): "
                         f"{r.stderr.decode(errors='replace')[-300:]}")
    return r.stdout.decode(errors="replace").rstrip("\n")


def validar(cadeia, raizes=("raiz",), nome="localhost", data="-"):
    return rodar("validar", nome, data, str(len(raizes)), *[arq(r, "der") for r in raizes],
                 *[arq(c, "der") for c in cadeia])


def esperar_erro(resposta, trecho, mensagem):
    verificar(resposta.startswith("erro: ") and trecho in resposta, f"{mensagem} -> \"{resposta[6:]}\"")


class Cliente:
    """O programa de teste no modo "conectar", comandado linha a linha."""

    def __init__(self, porta, nome="localhost", raizes=("raiz",), data="-", host="127.0.0.1", ambiente=None,
                 arquivos_raizes=None):
        caminhos = arquivos_raizes if arquivos_raizes is not None else [arq(r, "der") for r in raizes]
        self.proc = subprocess.Popen([PROGRAMA, "conectar", host, str(porta), nome, data, *caminhos],
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                     env=dict(os.environ, **(ambiente or {})))
        self.inicio = self.linha()

    def linha(self):
        resultado = []
        t = threading.Thread(target=lambda: resultado.append(self.proc.stdout.readline()), daemon=True)
        t.start()
        t.join(60)
        if not resultado:
            self.proc.kill()
            raise FalhaTeste("o programa de teste não respondeu em 60 s")
        return resultado[0].decode(errors="replace").rstrip("\n")

    def comando(self, texto):
        self.proc.stdin.write(texto.encode() + b"\n")
        self.proc.stdin.flush()
        return self.linha()

    def escrever(self, dados):
        return self.comando("e " + dados.hex())

    def ler(self, n):
        r = self.comando(f"l {n}")
        if not r.startswith("dados "):
            raise FalhaTeste(f"esperava {n} bytes do servidor e veio: {r[:300]}")
        return bytes.fromhex(r[6:])

    def fechar(self):
        try:
            self.proc.stdin.write(b"q\n")
            self.proc.stdin.close()
        except OSError:
            pass
        try:
            self.proc.wait(20)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            raise FalhaTeste("o programa de teste não terminou")
        if self.proc.returncode != 0:
            raise FalhaTeste(f"o programa de teste terminou com código {self.proc.returncode}")


def porta_livre():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    porta = s.getsockname()[1]
    s.close()
    return porta


class Servidor:
    """openssl s_server com um certificado de teste."""
    conta = 0

    def __init__(self, folha, cadeia=("inter",), extras=("-rev",), versao="-tls1_3", conjunto=None, pasta=None):
        Servidor.conta += 1
        self.porta = porta_livre()
        self.saida = os.path.join(PASTA, f"servidor-{Servidor.conta}.txt")
        args = ["openssl", "s_server", "-accept", f"127.0.0.1:{self.porta}", "-cert", arq(folha, "pem"),
                "-key", arq(folha, "key"), versao, *extras]
        if cadeia:
            junto = os.path.join(PASTA, f"cadeia-{Servidor.conta}.pem")
            with open(junto, "w") as f:
                for c in cadeia:
                    f.write(open(arq(c, "pem")).read())
            args += ["-cert_chain", junto]
        if conjunto:
            args += ["-ciphersuites", conjunto]
        self.proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=open(self.saida, "wb"),
                                     stderr=subprocess.STDOUT, cwd=pasta or PASTA)
        fim = time.time() + 15
        while b"ACCEPT" not in self.conteudo():
            if self.proc.poll() is not None or time.time() > fim:
                raise FalhaTeste(f"o openssl s_server não subiu: {self.conteudo().decode(errors='replace')[-400:]}")
            time.sleep(0.02)

    def conteudo(self):
        return open(self.saida, "rb").read()

    def digitar(self, texto):
        self.proc.stdin.write(texto)
        self.proc.stdin.flush()

    def esperar_saida(self, trecho, segundos=15):
        fim = time.time() + segundos
        while trecho not in self.conteudo():
            if time.time() > fim:
                return False
            time.sleep(0.02)
        return True

    def parar(self):
        self.proc.kill()
        self.proc.wait()

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.parar()


def conversa_invertida(servidor, **kw):
    """Com o s_server em modo -rev: manda uma linha e espera ela de volta ao
    contrário. Devolve a primeira linha do programa ("conectado ..." ou "erro: ...")."""
    c = Cliente(servidor.porta, **kw)
    if c.inicio.startswith("conectado"):
        frase = b"ola, sagui %d" % sorte.randrange(10 ** 6)
        r = c.escrever(frase + b"\n")
        if r != f"ok {len(frase) + 1}":
            raise FalhaTeste(f"a escrita falhou: {r}")
        veio = c.ler(len(frase) + 1)
        if veio != frase[::-1] + b"\n":
            raise FalhaTeste(f"mandei {frase!r} e o servidor devolveu {veio!r}")
    c.fechar()
    return c.inicio


# ------------------------------------------------------------ H52: certificados

def testar_leitura():
    r = rodar("analisar", *[arq(n, "der") for n in
                            ("raiz", "inter", "folha-rsa", "folha-p256", "folha-p384", "folha-ed25519", "folha-pss",
                             "folha-pss384", "raiz-ec", "folha-ec", "so-cn", "critica")]).split("\n")
    esperado = [
        ("rsa rsa-pkcs1 resumo=1 ca=1 caminho=-1 critica=0 propria=0", "cn=Raiz de Teste Sagui"),
        ("rsa rsa-pkcs1 resumo=1 ca=1 caminho=0 critica=0", "cn=Intermediaria de Teste Sagui"),
        ("rsa rsa-pkcs1 resumo=1 ca=0 caminho=-1", "cn=servidor rsa"),
        ("p256 rsa-pkcs1 resumo=2 ca=0", "cn=servidor p256"),
        ("p384 rsa-pkcs1 resumo=3 ca=0", "cn=servidor p384"),
        ("ed25519 rsa-pkcs1 resumo=1", "cn=servidor ed25519"),
        ("rsa rsa-pss resumo=1", "cn=servidor pss"),
        ("rsa rsa-pss resumo=2", "cn=servidor pss384"),
        ("p384 ecdsa resumo=2 ca=1 caminho=-1 critica=0 propria=0", "cn=Raiz Eliptica Sagui"),
        ("p256 ecdsa resumo=1 ca=0", "cn=servidor ec"),
        ("p256 rsa-pkcs1 resumo=1 ca=0", "cn=so-cn.sagui"),
        ("critica=1", "cn=servidor critico"),
    ]
    for linha, (meio, fim) in zip(r, esperado):
        if not (linha.startswith("ok ") and meio in linha and linha.endswith(fim)):
            raise FalhaTeste(f"leitura de certificado: esperava '{meio} ... {fim}', veio '{linha}'")
    verificar(len(r) == len(esperado), f"leitura de certificados: chave, assinatura, autoridade, limite de caminho, nome ({len(r)} casos)")

    # As datas: confere com o que o openssl diz do mesmo certificado.
    texto = openssl("x509", "-in", arq("folha-rsa", "pem"), "-noout", "-dates", "-dateopt", "iso_8601").decode()
    datas = [re.sub(r"[^0-9]", "", d) for d in re.findall(r"=(.*)", texto)]
    linha = rodar("analisar", arq("folha-rsa", "der"))
    verificar(f" {datas[0]} {datas[1]} " in linha, f"validade lida igual à do openssl ({datas[0]} a {datas[1]})")

    # DER rígido: lixo, corte e tamanhos mentirosos são recusados sem travar.
    der = open(arq("folha-rsa", "der"), "rb").read()
    ruins = {
        "vazio": b"",
        "cortado": der[:-1],
        "byte a mais": der + b"\x00",
        "tamanho indefinido": b"\x30\x80" + der[4:],
        "tamanho com zero à esquerda": b"\x30\x83\x00" + der[2:4] + der[4:],
        "tamanho maior que o arquivo": der[:2] + bytes([der[2] + 1]) + der[3:],
    }
    for nome, dados in ruins.items():
        caminho = os.path.join(PASTA, "ruim.der")
        open(caminho, "wb").write(dados)
        if rodar("analisar", caminho) != "falha":
            raise FalhaTeste(f"certificado malformado aceito: {nome}")
    verificar(True, f"DER malformado é recusado ({', '.join(ruins)})")

    # Mil certificados estragados ao acaso: nenhum pode derrubar o leitor, e
    # nenhum pode passar na validação (qualquer mudança quebra a assinatura
    # ou a estrutura).
    caminhos = []
    for i in range(1000):
        d = bytearray(der)
        for _ in range(sorte.choice((1, 1, 2, 5))):
            d[sorte.randrange(len(d))] = sorte.randrange(256)
        if bytes(d) == der:
            d[100] ^= 1
        if sorte.random() < 0.1:
            d = d[:sorte.randrange(len(d))]
        caminho = os.path.join(PASTA, f"sorte-{i}.der")
        open(caminho, "wb").write(d)
        caminhos.append(caminho)
    rodar("analisar", *caminhos)
    aceitos = 0
    for caminho in caminhos:
        r = rodar("validar", "localhost", "-", "1", arq("raiz", "der"), caminho, arq("inter", "der"))
        if r == "ok":
            # Só pode passar se a mudança caiu fora do que importa... e não há
            # tal lugar: o DER inteiro é assinatura ou parte assinada.
            aceitos += 1
        os.remove(caminho)
    verificar(aceitos == 0, "1000 certificados estragados ao acaso: o leitor não trava e a validação recusa todos")

    # Os certificados de verdade: todas as raízes do sistema têm de ser lidas.
    reais = sorted(glob.glob("/etc/ssl/certs/*.pem"))
    if reais:
        pasta = os.path.join(PASTA, "raizes-do-sistema")
        os.makedirs(pasta, exist_ok=True)
        caminhos = []
        for i, pem in enumerate(reais):
            caminho = os.path.join(pasta, f"{i}.der")
            if subprocess.run(["openssl", "x509", "-in", pem, "-outform", "DER", "-out", caminho],
                              capture_output=True).returncode == 0:
                caminhos.append(caminho)
        linhas = rodar("analisar", *caminhos).split("\n")
        lidas = sum(1 for linha in linhas if linha.startswith("ok "))
        proprias = sum(1 for linha in linhas if " propria=0 " in linha)
        verificar(lidas == len(caminhos) and proprias >= len(caminhos) * 8 // 10,
                  f"as {len(caminhos)} raízes de /etc/ssl/certs são lidas; em {proprias} a assinatura da própria raiz confere "
                  "(as outras usam SHA-1 ou P-521, que não temos)")


def testar_nomes():
    def nomes(cert, casos):
        r = rodar("nome", arq(cert, "der"), *[n for n, _ in casos])
        for (nome, esperado), veio in zip(casos, r):
            if str(esperado) != veio:
                raise FalhaTeste(f"nome '{nome}' no certificado {cert}: esperava {esperado}, veio {veio}")
        return len(casos)

    n = nomes("folha-rsa", [
        ("localhost", 1), ("LOCALHOST", 1), ("localhost.", 1), ("exemplo.sagui", 1), ("Exemplo.Sagui", 1),
        ("a.teste.sagui", 1), ("WWW.Teste.SAGUI", 1), ("a.teste.sagui.", 1),
        ("teste.sagui", 0), ("a.b.teste.sagui", 0), (".teste.sagui", 0), ("ateste.sagui", 0),
        ("a.teste.sagui.br", 0), ("localhost.sagui", 0), ("xlocalhost", 0), ("localhos", 0), ("sagui", 0),
        ("*.teste.sagui", 0), ("servidor rsa", 0),
        ("127.0.0.1", 1), ("127.0.0.2", 0), ("127.0.0", 0), ("127.0.0.1.1", 0),
    ])
    verificar(True, f"nome do servidor contra o subjectAltName: exato, sem maiúsculas, curinga de um rótulo, endereço IP ({n} casos)")
    n = nomes("curingas-ruins", [
        ("a.sagui", 0), ("www.teste.sagui", 0), ("w.teste.sagui", 0), ("ba.teste.sagui", 0), ("a.teste.sagui", 0),
        ("a.b.sagui", 0), ("a.x.sagui", 0), ("curingas", 0),
    ])
    verificar(True, f"curingas proibidos não casam: '*.sagui', 'w*.', '*a.', 'a.*.', '*.*.' ({n} casos)")
    n = nomes("so-cn", [("so-cn.sagui", 1), ("SO-CN.sagui", 1), ("outro.sagui", 0), ("127.0.0.1", 0)])
    verificar(True, f"sem subjectAltName vale o nome comum; com ele, o nome comum é ignorado ({n} casos)")


def testar_cadeias():
    casos_bons = [
        (["folha-rsa", "inter"], ("raiz",), "raiz fora da cadeia (o caso normal)"),
        (["folha-rsa", "inter", "raiz"], ("raiz",), "raiz também na cadeia"),
        (["folha-rsa", "raiz", "inter"], ("raiz",), "cadeia fora de ordem"),
        (["folha-rsa", "inter-estranha", "inter", "folha-p256"], ("raiz",), "certificados sobrando na cadeia"),
        (["folha-rsa"], ("inter",), "intermediária como âncora de confiança"),
        (["folha-rsa"], ("folha-rsa",), "o próprio certificado entre os de confiança"),
        (["autoassinado"], ("autoassinado",), "autoassinado em que se confia"),
        (["folha-p256", "inter"], ("raiz-ec", "raiz-estranha", "raiz"), "várias raízes"),
        (["folha-p384", "inter"], ("raiz",), "folha P-384, assinatura RSA com SHA-512"),
        (["folha-ed25519", "inter"], ("raiz",), "folha Ed25519"),
        (["folha-pss", "inter"], ("raiz",), "assinatura RSA-PSS com SHA-256"),
        (["folha-pss384", "inter"], ("raiz",), "assinatura RSA-PSS com SHA-384"),
        (["folha-ec", "inter-ec"], ("raiz-ec",), "cadeia toda ECDSA (P-256 e P-384)"),
    ]
    for cadeia, raizes, descricao in casos_bons:
        r = validar(cadeia, raizes)
        if r != "ok":
            raise FalhaTeste(f"cadeia boa recusada ({descricao}): {r}")
    verificar(True, f"cadeias boas aceitas ({len(casos_bons)} casos: " + "; ".join(d for _, _, d in casos_bons[:5]) + "...)")

    esperar_erro(validar(["folha-rsa", "inter"], nome="outro.sagui"), "nome não confere", "nome errado")
    esperar_erro(validar(["folha-rsa", "inter"], data="20990101000000"), "vencido", "certificado vencido")
    esperar_erro(validar(["folha-rsa", "inter"], data="20000101000000"), "ainda não é válido", "certificado ainda não válido")
    esperar_erro(validar(["folha-estranha", "inter-estranha"]), "autoridade desconhecida", "raiz desconhecida")
    esperar_erro(validar(["folha-estranha", "inter-estranha", "raiz-estranha"]), "autoridade desconhecida",
                 "raiz desconhecida mandada na cadeia")
    esperar_erro(validar(["autoassinado"]), "autoassinado", "certificado autoassinado")
    esperar_erro(validar(["folha-rsa"]), "cadeia quebrada", "falta a intermediária")
    esperar_erro(validar(["folha-rsa", "inter-estranha"]), "cadeia quebrada", "intermediária de outra família")
    esperar_erro(validar(["neta", "folha-rsa", "inter"]), "não é uma autoridade", "emitido por quem não é autoridade")
    esperar_erro(validar(["adulterada", "inter"]), "assinatura inválida", "assinatura adulterada")
    esperar_erro(validar(["adulterada-tbs", "inter"]), "assinatura inválida", "conteúdo adulterado")
    esperar_erro(validar(["folha-funda", "inter-funda", "inter"]), "limite de caminho", "intermediária abaixo de pathLen 0")
    esperar_erro(validar(["critica", "inter"]), "extensão crítica", "extensão crítica desconhecida")
    esperar_erro(validar(["so-cliente", "inter"]), "não serve para identificar um servidor", "uso estendido só de cliente")
    esperar_erro(validar(["so-cifrar", "inter"]), "não serve para identificar um servidor", "uso de chave sem assinar")
    esperar_erro(validar(["folha-rsa", "inter"], raizes=()), "autoridade desconhecida", "nenhuma raiz de confiança")
    esperar_erro(validar([]), "não mandou certificado", "cadeia vazia")


# ------------------------------------------------------------ H53: TLS com o openssl s_server

def testar_apertos():
    feitos = []
    for folha in ("folha-rsa", "folha-p256", "folha-p384"):
        for conjunto in CONJUNTOS:
            with Servidor(folha, conjunto=conjunto) as s:
                r = conversa_invertida(s)
                if r != "conectado " + conjunto:
                    raise FalhaTeste(f"aperto de mão com {folha} e {conjunto}: {r}")
                feitos.append(r)
    verificar(len(feitos) == 9, "aperto de mão e conversa nos dois sentidos com o openssl s_server: "
              "chaves RSA 2048, P-256 e P-384 x ChaCha20-Poly1305, AES-128-GCM e AES-256-GCM (9 combinações)")

    with Servidor("folha-rsa") as s:
        r = conversa_invertida(s)
    verificar(r == "conectado TLS_CHACHA20_POLY1305_SHA256", "com o servidor deixando escolher, vale a nossa preferência: ChaCha20-Poly1305")

    outros = [("folha-ed25519", ("inter",), ("raiz",), "chave Ed25519"),
              ("folha-pss", ("inter",), ("raiz",), "RSA 3072 com certificado assinado em PSS"),
              ("folha-ec", ("inter-ec",), ("raiz-ec",), "cadeia toda ECDSA"),
              ("folha-rsa", ("inter", "raiz"), ("raiz",), "raiz mandada pelo servidor")]
    for folha, cadeia, raizes, descricao in outros:
        with Servidor(folha, cadeia=cadeia) as s:
            r = conversa_invertida(s, raizes=raizes)
            if not r.startswith("conectado"):
                raise FalhaTeste(f"aperto de mão ({descricao}): {r}")
    verificar(True, "aperto de mão com: " + "; ".join(d for *_, d in outros))

    with Servidor("folha-p256") as s:
        r = conversa_invertida(s, nome="qualquer.teste.sagui")
    verificar(r.startswith("conectado"), "nome coberto por curinga (*.teste.sagui) é aceito")

    with Servidor("folha-p256", extras=("-rev", "-verify", "1")) as s:
        r = conversa_invertida(s)
    verificar(r.startswith("conectado"), "servidor que pede certificado de cliente: respondemos que não temos e a conversa segue")

    with Servidor("folha-p256", extras=("-rev", "-alpn", "http/1.1")) as s:
        r = conversa_invertida(s, ambiente={"TLS_ALPN": "http/1.1"})
    verificar(r.startswith("conectado"), "ALPN: protocolo http/1.1 anunciado e aceito")

    with Servidor("folha-rsa", conjunto="TLS_AES_256_GCM_SHA384") as s:
        r = conversa_invertida(s, ambiente={"TLS_PICADO": "1"})
    verificar(r.startswith("conectado"), "rede entregando 3 bytes por vez e aceitando 100: registros e mensagens são remontados")


def testar_dados_grandes():
    # Descida: o s_server no modo -WWW serve um arquivo da pasta.
    pasta = os.path.join(PASTA, "www")
    os.makedirs(pasta, exist_ok=True)
    conteudo = bytes(sorte.getrandbits(8) for _ in range(300000))
    open(os.path.join(pasta, "grande.bin"), "wb").write(conteudo)
    for conjunto, picado in (("TLS_CHACHA20_POLY1305_SHA256", False), ("TLS_AES_128_GCM_SHA256", False),
                             ("TLS_AES_256_GCM_SHA384", True)):
        with Servidor("folha-p256", extras=("-WWW",), conjunto=conjunto, pasta=pasta) as s:
            c = Cliente(s.porta, ambiente={"TLS_PICADO": "1"} if picado else None)
            if not c.inicio.startswith("conectado"):
                raise FalhaTeste(f"aperto de mão para a descida: {c.inicio}")
            c.escrever(b"GET /grande.bin HTTP/1.0\r\n\r\n")
            comeco = c.ler(200)
            corte = comeco.index(b"\r\n\r\n") + 4
            if not comeco.startswith(b"HTTP/1.0 200"):
                raise FalhaTeste(f"resposta inesperada do s_server: {comeco[:60]!r}")
            resto = c.ler(len(conteudo) - (200 - corte))
            if comeco[corte:] + resto != conteudo:
                raise FalhaTeste(f"o arquivo de 300 KB chegou diferente ({conjunto})")
            # Depois do arquivo o servidor encerra direito: close_notify.
            fim = c.comando("t")
            if not fim.startswith("fim -1 0 "):
                raise FalhaTeste(f"esperava o fim limpo da conexão (close_notify), veio: {fim}")
            c.fechar()
    verificar(True, "descida de 300 KB (vários registros de 16 KB) nos três conjuntos, um deles com a rede picada; "
              "o close_notify do servidor vira TLS_FECHADO")

    # Subida: o s_server comum despeja na saída o que recebe.
    for conjunto in CONJUNTOS:
        with Servidor("folha-p256", extras=(), conjunto=conjunto) as s:
            c = Cliente(s.porta)
            if not c.inicio.startswith("conectado"):
                raise FalhaTeste(f"aperto de mão para a subida: {c.inicio}")
            n, semente = 200000, 7
            padrao = bytes((i * 31 + semente + (i >> 8)) & 0xff for i in range(n))
            r = c.comando(f"g {n} {semente}")
            if r != f"ok {n}":
                raise FalhaTeste(f"escrita grande: {r}")
            c.fechar()
            if not s.esperar_saida(padrao):
                raise FalhaTeste(f"os 200 KB mandados não chegaram iguais ao servidor ({conjunto})")
    verificar(True, "subida de 200 KB (a escrita parte em registros de 16 KB) nos três conjuntos")


def testar_troca_de_chaves():
    for conjunto in CONJUNTOS:
        with Servidor("folha-p256", extras=("-msg",), conjunto=conjunto) as s:
            c = Cliente(s.porta)
            if not c.inicio.startswith("conectado"):
                raise FalhaTeste(f"aperto de mão: {c.inicio}")
            c.escrever(b"antes da troca\n")
            if not s.esperar_saida(b"antes da troca"):
                raise FalhaTeste("o servidor não recebeu a primeira linha")
            # "K" na entrada do s_server manda um KeyUpdate pedindo que o
            # cliente troque a chave também; "k" manda sem pedir.
            # (Cada linha vai sozinha: o s_server lê a entrada aos blocos e só
            # olha a primeira letra de cada bloco.)
            s.digitar(b"K\n")
            time.sleep(0.3)
            s.digitar(b"primeira resposta\n")
            if c.ler(18) != b"primeira resposta\n":
                raise FalhaTeste("depois do KeyUpdate o texto do servidor chegou errado")
            c.escrever(b"depois da troca\n")
            if not s.esperar_saida(b"depois da troca"):
                raise FalhaTeste("o servidor não entendeu o que mandamos com a chave nova")
            s.digitar(b"k\n")
            time.sleep(0.3)
            s.digitar(b"segunda resposta\n")
            if c.ler(17) != b"segunda resposta\n":
                raise FalhaTeste("depois do segundo KeyUpdate o texto do servidor chegou errado")
            c.escrever(b"ainda aqui\n")
            if not s.esperar_saida(b"ainda aqui"):
                raise FalhaTeste("o servidor não recebeu a última linha")
            c.fechar()
            # O registro de mensagens do s_server (-msg): ele mandou dois
            # KeyUpdate (>>>) e recebeu exatamente um nosso (<<<), a resposta
            # ao que pedia a troca.
            s.esperar_saida(b"DONE", 5)
            mandados = len(re.findall(rb">>> TLS 1.3, Handshake \[length 0005\], KeyUpdate", s.conteudo()))
            recebidos = len(re.findall(rb"<<< TLS 1.3, Handshake \[length 0005\], KeyUpdate", s.conteudo()))
            if (mandados, recebidos) != (2, 1):
                raise FalhaTeste(f"KeyUpdate: o servidor mandou {mandados} e recebeu {recebidos} (esperava 2 e 1)")
    verificar(True, "KeyUpdate do servidor, com e sem pedido de troca da nossa chave, nos três conjuntos; "
              "bilhetes de sessão (NewSessionTicket) ignorados")


def testar_recusas():
    def recusa(folha, cadeia, trecho, descricao, **kw):
        with Servidor(folha, cadeia=cadeia, extras=("-rev", "-msg")) as s:
            c = Cliente(s.porta, **kw)
            c.fechar()
            esperar_erro(c.inicio, trecho, "aperto de mão recusado: " + descricao)
            # O servidor tem de ter recebido o nosso alerta de certificado ruim.
            if not s.esperar_saida(b"fatal bad_certificate", 5):
                raise FalhaTeste("o servidor não recebeu o alerta bad_certificate")

    recusa("folha-rsa", ("inter",), "nome não confere", "nome errado", nome="outro.sagui")
    recusa("folha-rsa", ("inter",), "vencido", "certificado vencido (relógio em 2099)", data="20990101000000")
    recusa("folha-rsa", ("inter",), "ainda não é válido", "certificado ainda não válido (relógio em 2000)", data="20000101000000")
    recusa("folha-estranha", ("inter-estranha",), "autoridade desconhecida", "raiz desconhecida")
    recusa("folha-rsa", (), "cadeia quebrada", "cadeia quebrada (sem a intermediária)")
    recusa("neta", ("folha-rsa", "inter"), "não é uma autoridade", "folha emitida por quem não é autoridade")
    recusa("adulterada", ("inter",), "assinatura inválida", "assinatura adulterada")
    recusa("autoassinado", (), "autoassinado", "certificado autoassinado")
    recusa("folha-funda", ("inter-funda", "inter"), "limite de caminho", "limite de caminho estourado")
    verificar(True, "em todas as recusas o servidor recebeu o nosso alerta bad_certificate")


def servidor_de_mentira(comportamento):
    """Um servidor TCP simples, para os casos que o openssl não faz. Devolve a porta."""
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", 0))
    s.listen(1)

    def atender():
        try:
            c, _ = s.accept()
            comportamento(c)
            c.close()
        except OSError:
            pass
        s.close()

    threading.Thread(target=atender, daemon=True).start()
    return s.getsockname()[1]


def receber_exato(c, n):
    dados = b""
    while len(dados) < n:
        p = c.recv(n - len(dados))
        if not p:
            return dados
        dados += p
    return dados


def ponte(porta_destino, mexer):
    """Fica no meio entre o cliente e o s_server. "mexer(sentido, numero, registro)"
    recebe cada registro inteiro e devolve o que passar adiante (ou None para
    cortar a conexão)."""
    def comportamento(c):
        d = socket.create_connection(("127.0.0.1", porta_destino))

        def bombear(origem, destino, sentido):
            numero = 0
            try:
                while True:
                    cab = receber_exato(origem, 5)
                    if len(cab) < 5:
                        break
                    reg = cab + receber_exato(origem, int.from_bytes(cab[3:5], "big"))
                    reg = mexer(sentido, numero, reg)
                    numero += 1
                    if reg is None:
                        break
                    destino.sendall(reg)
            except OSError:
                pass
            for x in (origem, destino):
                try:
                    x.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass

        t = threading.Thread(target=bombear, args=(d, c, "servidor"), daemon=True)
        t.start()
        bombear(c, d, "cliente")
        t.join(5)
        d.close()

    return servidor_de_mentira(comportamento)


def testar_falhas_do_protocolo():
    with Servidor("folha-p256", extras=("-rev", "-groups", "P-256")) as s:
        c = Cliente(s.porta)
        c.fechar()
    esperar_erro(c.inicio, "falha na negociação", "servidor sem x25519: alerta handshake_failure traduzido")

    with Servidor("folha-p256", versao="-tls1_2") as s:
        c = Cliente(s.porta)
        c.fechar()
    esperar_erro(c.inicio, "TLS 1.3", "servidor só de TLS 1.2")

    porta = servidor_de_mentira(lambda c: (c.recv(4096), c.sendall(b"HTTP/1.1 400 Bad Request\r\n\r\n"), time.sleep(0.3)))
    c = Cliente(porta)
    c.fechar()
    esperar_erro(c.inicio, "não respondeu em TLS", "servidor que não fala TLS")

    porta = servidor_de_mentira(lambda c: c.recv(4096))
    c = Cliente(porta)
    c.fechar()
    esperar_erro(c.inicio, "fechou a conexão no meio do aperto", "servidor que desliga na cara")

    porta = servidor_de_mentira(lambda c: (c.recv(4096), time.sleep(4)))
    c = Cliente(porta, ambiente={"TLS_LIMITE": "1000"})
    c.fechar()
    esperar_erro(c.inicio, "tempo esgotado", "servidor mudo: limite de tempo do aperto de mão (1 s)")

    # Um HelloRetryRequest fabricado: um ServerHello com o "random" especial.
    def nova_tentativa(c):
        ola = c.recv(4096)
        sessao = ola[5 + 4 + 2 + 32 + 1:][:32]
        corpo = (b"\x03\x03" + hashlib.sha256(b"HelloRetryRequest").digest() + b"\x20" + sessao + b"\x13\x01\x00"
                 + b"\x00\x0c" + b"\x00\x2b\x00\x02\x03\x04" + b"\x00\x33\x00\x02\x00\x17")
        msg = b"\x02" + len(corpo).to_bytes(3, "big") + corpo
        c.sendall(b"\x16\x03\x03" + len(msg).to_bytes(2, "big") + msg)
        time.sleep(0.3)

    c = Cliente(servidor_de_mentira(nova_tentativa))
    c.fechar()
    esperar_erro(c.inicio, "HelloRetryRequest", "pedido de nova tentativa (HelloRetryRequest) recusado com explicação")

    # Alguém no caminho troca um bit de um registro cifrado.
    def trocar_bit(alvo):
        def mexer(sentido, numero, reg):
            if sentido == "servidor" and reg[0] == 23:
                mexer.cifrados += 1
                if mexer.cifrados == alvo:
                    return reg[:-3] + bytes([reg[-3] ^ 0x10]) + reg[-2:]
            return reg
        mexer.cifrados = 0
        return mexer

    with Servidor("folha-p256") as s:
        c = Cliente(ponte(s.porta, trocar_bit(1)))
        c.fechar()
    esperar_erro(c.inicio, "registro adulterado", "bit trocado no caminho durante o aperto de mão")

    with Servidor("folha-p256") as s:
        mexer = trocar_bit(10 ** 9)
        c = Cliente(ponte(s.porta, mexer))
        if not c.inicio.startswith("conectado"):
            raise FalhaTeste(f"aperto de mão através da ponte: {c.inicio}")
        c.escrever(b"primeira\n")
        if c.ler(9) != b"ariemirp\n":
            raise FalhaTeste("a conversa através da ponte não funcionou")
        mexer.cifrados = 10 ** 9 - 1         # o próximo registro do servidor sai estragado
        c.escrever(b"segunda\n")
        r = c.comando("l 8")
        c.fechar()
    verificar(r.startswith("fim -2 0 ") and "registro adulterado" in r,
              "bit trocado no caminho em dados de aplicação: o registro é recusado inteiro, nenhum byte é entregue")

    # A conexão é cortada sem o close_notify: tem de dar TLS_CORTADO, não fim limpo.
    def cortar(sentido, numero, reg):
        if sentido == "cliente" and cortar.agora:
            return None
        return reg
    cortar.agora = False
    with Servidor("folha-p256") as s:
        c = Cliente(ponte(s.porta, cortar))
        c.escrever(b"primeira\n")
        c.ler(9)
        cortar.agora = True
        c.escrever(b"segunda\n")
        r = c.comando("t")
        c.fechar()
    verificar(r.startswith("fim -3 0 ") and "sem o aviso de fim" in r,
              "conexão cortada sem close_notify é avisada como truncada (TLS_CORTADO), diferente do fim limpo")

    # E o nosso close_notify chega ao servidor.
    with Servidor("folha-p256", extras=("-msg",)) as s:
        c = Cliente(s.porta)
        c.escrever(b"tchau\n")
        c.fechar()
        chegou = s.esperar_saida(b"warning close_notify", 5) and s.esperar_saida(b"DONE", 5)
    verificar(chegou, "ao fechar, o servidor recebe o nosso close_notify e encerra a conexão direito")


# ------------------------------------------------------------ servidores de verdade

def raizes_do_sistema_para(host):
    """Descobre, com o openssl s_client, qual raiz do sistema assina a cadeia
    desse servidor, e exporta só ela (em DER) de /etc/ssl/certs."""
    r = subprocess.run(["openssl", "s_client", "-connect", f"{host}:443", "-servername", host, "-showcerts"],
                       input=b"", capture_output=True, timeout=30)
    pems = re.findall(rb"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", r.stdout, re.S)
    if not pems:
        return None
    # De cima para baixo na cadeia: o primeiro emissor que existir no
    # sistema é a raiz. (O certificado do topo pode ser uma assinatura
    # cruzada por uma raiz antiga que o sistema nem tem mais.)
    pasta = os.path.join(PASTA, "raizes-" + host)
    os.makedirs(pasta, exist_ok=True)
    caminhos = []
    for pem in reversed(pems):
        marca = openssl("x509", "-noout", "-issuer_hash", entrada=pem).decode().strip()
        for i, origem in enumerate(sorted(glob.glob(f"/etc/ssl/certs/{marca}.*"))):
            destino = os.path.join(pasta, f"{marca}-{i}.der")
            openssl("x509", "-in", origem, "-outform", "DER", "-out", destino)
            caminhos.append(destino)
        if caminhos:
            break
    return caminhos


def testar_internet():
    try:
        socket.create_connection(("example.com", 443), timeout=8).close()
    except OSError:
        print("  pulado: sem internet, os testes com servidores de verdade não rodaram")
        return
    for host, caminho in (("example.com", "/"), ("pt.wikipedia.org", "/wiki/Brasil")):
        raizes = raizes_do_sistema_para(host)
        if not raizes:
            raise FalhaTeste(f"não achei no sistema a raiz que assina {host}")
        c = Cliente(443, nome=host, host=host, arquivos_raizes=raizes, ambiente={"TLS_ALPN": "http/1.1"})
        if not c.inicio.startswith("conectado"):
            raise FalhaTeste(f"aperto de mão com {host}: {c.inicio}")
        pedido = (f"GET {caminho} HTTP/1.1\r\nHost: {host}\r\nUser-Agent: SaguiOS-teste/1.0 (teste automatico de TLS)\r\n"
                  "Accept: text/html\r\nConnection: close\r\n\r\n").encode()
        c.escrever(pedido)
        comeco = c.ler(15)
        fim = c.comando("t").split()
        c.fechar()
        verificar(comeco == b"HTTP/1.1 200 OK" and int(fim[2]) > 200,
                  f"{host}: certificado validado só com a raiz necessária do sistema ({len(raizes)} arquivo), "
                  f"{c.inicio.split()[1]}, GET devolveu HTTP/1.1 200 OK e mais {fim[2]} bytes")

        # Com a raiz errada, o mesmo servidor tem de ser recusado.
        c = Cliente(443, nome=host, host=host, raizes=("raiz",))
        c.fechar()
        esperar_erro(c.inicio, "autoridade desconhecida", f"{host} sem a raiz dele entre as de confiança")
    c = Cliente(443, nome="example.com", host="pt.wikipedia.org", arquivos_raizes=raizes_do_sistema_para("pt.wikipedia.org"))
    c.fechar()
    esperar_erro(c.inicio, "nome não confere", "o servidor da Wikipédia apresentado como example.com")


def main():
    compilar()
    fabricar_certificados()
    print("H52 - certificados X.509")
    testar_leitura()
    testar_nomes()
    testar_cadeias()
    print("H53 - cliente TLS 1.3")
    testar_apertos()
    testar_dados_grandes()
    testar_troca_de_chaves()
    testar_recusas()
    testar_falhas_do_protocolo()
    testar_internet()
    return 0


if __name__ == "__main__":
    sys.exit(main())
