#!/usr/bin/env python3
"""H56 a H58 - cliente SSH (userland/ssh).

Critério: a biblioteca compila com as opções do SaguiOS sem depender de nada
além da userland/cripto; gera chaves que o OpenSSH entende; e conversa com um
servidor OpenSSH de verdade: troca de chaves com cada cifra e cada tipo de
chave de servidor, autenticação por chave pública, sessão com terminal,
janelas nos dois sentidos, troca de chaves no meio da sessão e recusa clara
de tudo que está errado.

Este teste não usa o QEMU: a biblioteca é ligada a um programa de teste
(tests/ssh/teste_ssh.c) que roda aqui mesmo, em 32 bits, sobre soquetes do
Linux. O sshd e o ssh-keygen do sistema entram só como ferramentas: o sshd
roda como o usuário atual, numa porta alta de 127.0.0.1, com chaves e
configuração temporárias.

O que NÃO dá para testar assim: o login por senha que dá certo (um sshd sem
ser root não consegue conferir senhas). Do método "password" fica testado só
o caminho da recusa.
"""
import base64
import getpass
import glob
import hashlib
import os
import random
import re
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True
from qemu import FalhaTeste, verificar, RAIZ, SAIDA  # noqa: E402

FONTES_SSH = os.path.join(RAIZ, "userland", "ssh")
FONTES_CRIPTO = os.path.join(RAIZ, "userland", "cripto")
PASTA = os.path.join(SAIDA, "h56-ssh")
PROGRAMA = os.path.join(PASTA, "teste_ssh")
SSHD = shutil.which("sshd") or "/usr/sbin/sshd"
USUARIO = getpass.getuser()

# As mesmas opções com que os programas do SaguiOS são compilados (ver
# h50_cripto.py).
OPCOES = ["-m32", "-march=i686", "-std=gnu11", "-O2", "-ffreestanding", "-fno-builtin", "-nostdlib",
          "-fno-pic", "-fno-pie", "-fno-stack-protector", "-mgeneral-regs-only",
          "-Wall", "-Wextra", "-Werror"]

# Maior quadro de pilha aceito para uma função da userland/ssh. A mais gulosa
# é a ssh_i_trocar (uns 1 KB, por causa da assinatura RSA realinhada); somada
# à conferência de assinatura da cripto (uns 6 KB) fica longe dos 16 KB.
PILHA_MAX = 2048

CHAVE = os.path.join(PASTA, "chave")            # a chave do usuário, feita pela NOSSA biblioteca
CHAVE_ERRADA = os.path.join(PASTA, "intrusa")   # outra, que o servidor não conhece
AUTORIZADAS = os.path.join(PASTA, "authorized_keys")

sorte = random.Random(56)


def executar(args, entrada=None, ambiente=None, aceitar_falha=False):
    r = subprocess.run(args, input=entrada, capture_output=True, env=ambiente)
    if r.returncode != 0 and not aceitar_falha:
        raise FalhaTeste(f"{' '.join(args[:4])}... falhou: {(r.stderr or r.stdout).decode(errors='replace')[-600:]}")
    return r.stdout


# ------------------------------------------------------------ compilação

def compilar():
    shutil.rmtree(PASTA, ignore_errors=True)
    os.makedirs(PASTA)
    objetos_ssh, objetos_cripto = [], []
    for fonte in sorted(glob.glob(os.path.join(FONTES_SSH, "*.c"))):
        obj = os.path.join(PASTA, "ssh_" + os.path.basename(fonte)[:-2] + ".o")
        executar(["gcc", *OPCOES, "-fstack-usage", "-c", "-o", obj, fonte])
        objetos_ssh.append(obj)
    for fonte in sorted(glob.glob(os.path.join(FONTES_CRIPTO, "*.c"))):
        obj = os.path.join(PASTA, "cripto_" + os.path.basename(fonte)[:-2] + ".o")
        executar(["gcc", *OPCOES, "-c", "-o", obj, fonte])
        objetos_cripto.append(obj)
    verificar(len(objetos_ssh) >= 5,
              f"userland/ssh compila com as opções do SaguiOS, sem avisos ({len(objetos_ssh)} arquivos)")

    # Tudo que a userland/ssh pede tem de estar nela mesma ou na cripto: nem
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

    def_ssh, ped_ssh = simbolos(objetos_ssh)
    def_cripto, _ = simbolos(objetos_cripto)
    faltam = sorted(ped_ssh - def_ssh - def_cripto)
    verificar(not faltam, "userland/ssh só depende da userland/cripto (nada de libc, libgcc ou sistema)"
              + (f": faltam {faltam}" if faltam else ""))

    maior, dono = 0, ""
    for su in glob.glob(os.path.join(PASTA, "ssh_*.su")):
        for linha in open(su):
            partes = linha.rstrip("\n").split("\t")
            if len(partes) >= 2 and int(partes[1]) > maior:
                maior, dono = int(partes[1]), partes[0].split(":")[-1]
    verificar(0 < maior <= PILHA_MAX, f"nenhuma função usa mais de {PILHA_MAX} bytes de pilha (a maior: {dono}, {maior})")

    tamanhos = executar(["size", "-t", *objetos_ssh]).decode().strip().split("\n")[-1].split()
    print(f"      (userland/ssh: {tamanhos[0]} bytes de código, {tamanhos[1]} de dados, {tamanhos[2]} de bss)")

    executar(["gcc", "-m32", "-std=gnu11", "-no-pie", "-O1", "-D_GNU_SOURCE", "-Wall", "-Wextra", "-Werror", "-I", FONTES_SSH,
              "-o", PROGRAMA, os.path.join(RAIZ, "tests", "ssh", "teste_ssh.c"), *objetos_ssh, *objetos_cripto])
    verificar(os.path.exists(PROGRAMA), "programa de teste ligado à biblioteca (32 bits)")
    tamanho = int(executar([PROGRAMA, "tamanho"]))
    verificar(tamanho < 64 * 1024, f"uma conexão ocupa {tamanho} bytes (o contexto Ssh, com todos os buffers)")


def programa(*args, aceitar_falha=True):
    return executar([PROGRAMA, *args], aceitar_falha=aceitar_falha).decode().strip()


# ------------------------------------------------------------ H56: base64 e chaves

def testar_base64():
    for n in list(range(0, 20)) + [sorte.randrange(20, 2000) for _ in range(30)]:
        dados = bytes(sorte.randrange(256) for _ in range(n))
        certo = base64.b64encode(dados).decode()
        if n == 0:
            continue    # o programa de teste não recebe argumento vazio
        saida = programa("b64", dados.hex()).split()
        if saida != [certo, certo.rstrip("=")]:
            raise FalhaTeste(f"base64 de {n} bytes: {saida} em vez de {certo}")
        for texto in (certo, certo.rstrip("=")):
            volta = programa("b64d", texto)
            if volta != "ok " + dados.hex():
                raise FalhaTeste(f"base64 de volta ({n} bytes): {volta}")
    verificar(True, "base64: 49 tamanhos de 1 a 2000 bytes codificados igual ao Python, com e sem '=', e decodificados de volta")
    ruins = ["A", "AP8Q=", "AP*Q", "AP 8Q", "AB==", "AAB=", "=AAA", "AA=A", "AAAAA"]
    aceitos = [t for t in ruins if programa("b64d", t) != "erro"]
    verificar(not aceitos, f"base64: {len(ruins)} textos inválidos (letra estranha, sobra de bits, '=' fora do lugar) recusados"
              + (f": aceitou {aceitos}" if aceitos else ""))


def testar_chaves():
    pub = CHAVE + ".pub"
    verificar(programa("gerar", CHAVE, pub, "sagui@teste") == "ok" and
              programa("gerar", CHAVE_ERRADA, CHAVE_ERRADA + ".pub", "intrusa") == "ok",
              "par de chaves Ed25519 gerado pela biblioteca (arquivo privado e linha pública)")
    linha = open(pub).read()
    partes = linha.split()
    blob = base64.b64decode(partes[1])
    verificar(linha.endswith("\n") and partes[0] == "ssh-ed25519" and partes[2] == "sagui@teste" and len(blob) == 51
              and blob[:19] == b"\0\0\0\x0bssh-ed25519\0\0\0\x20",
              "linha pública no formato do OpenSSH: tipo, blob da RFC 8709 em base64, comentário")

    # O ssh-keygen do sistema tem de entender a nossa linha e tirar dela a
    # mesma impressão digital que nós.
    deles = executar(["ssh-keygen", "-lf", pub, "-E", "sha256"]).decode().split()
    nossa = programa("digital", pub)
    verificar(deles[0] == "256" and deles[1] == nossa and deles[-1] == "(ED25519)",
              f"o ssh-keygen lê a nossa chave pública e a impressão digital confere ({nossa})")

    privado = open(CHAVE).read()
    verificar(privado.startswith("SaguiOS chave SSH 1\ntipo: ssh-ed25519\nsemente: ") and
              programa("publica", CHAVE, "sagui@teste") == linha.strip(),
              "arquivo de chave privada lido de volta dá a mesma chave pública")

    def ler_alterado(texto):
        caminho = os.path.join(PASTA, "alterada")
        with open(caminho, "w") as f:
            f.write(texto)
        return programa("publica", caminho, "x")

    semente = re.search(r"semente: (\S+)", privado).group(1)
    outra = ("B" if semente[0] != "B" else "C") + semente[1:]
    ruins = {
        "semente alterada (não bate com a pública)": privado.replace(semente, outra),
        "cabeçalho errado": privado.replace("SaguiOS chave SSH 1", "SaguiOS chave SSH 2"),
        "sem semente": "\n".join(x for x in privado.split("\n") if not x.startswith("semente")),
        "semente curta": privado.replace(semente, semente[:20]),
        "tipo desconhecido": privado.replace("tipo: ssh-ed25519", "tipo: ssh-rsa"),
        "arquivo vazio": "",
    }
    aceitos = [nome for nome, texto in ruins.items() if ler_alterado(texto) != "erro"]
    verificar(not aceitos, f"arquivos de chave privada estragados recusados ({', '.join(ruins)})"
              + (f": aceitou {aceitos}" if aceitos else ""))
    sem_publica = "\n".join(x for x in privado.split("\n") if not x.startswith("publica")).replace("\n", "\r\n")
    verificar(ler_alterado(sem_publica).split()[1] == partes[1],
              "arquivo sem a linha 'publica' e com fins de linha CR LF ainda é lido")

    # As chaves do servidor de teste, feitas com a ferramenta do sistema.
    for tipo, extra in (("ed25519", []), ("rsa", ["-b", "3072"])):
        executar(["ssh-keygen", "-q", "-t", tipo, *extra, "-N", "", "-f", os.path.join(PASTA, "servidor_" + tipo)])
    shutil.copy(pub, AUTORIZADAS)


def digital_do_servidor(tipo):
    return executar(["ssh-keygen", "-lf", os.path.join(PASTA, f"servidor_{tipo}.pub"), "-E", "sha256"]).decode().split()[1]


# ------------------------------------------------------------ o servidor e o cliente

def porta_livre():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    porta = s.getsockname()[1]
    s.close()
    return porta


class Servidor:
    """Um sshd do sistema, rodando como o usuário atual numa porta alta."""
    contador = 0

    def __init__(self, *extras):
        Servidor.contador += 1
        self.porta = porta_livre()
        base = os.path.join(PASTA, f"sshd{Servidor.contador}")
        self.registro = base + ".log"
        linhas = [
            "ListenAddress 127.0.0.1",
            f"HostKey {PASTA}/servidor_ed25519",
            f"HostKey {PASTA}/servidor_rsa",
            f"PidFile {base}.pid",
            f"AuthorizedKeysFile {AUTORIZADAS}",
            "StrictModes no",
            "UsePAM no",
            "KbdInteractiveAuthentication no",
            "PrintMotd no",
            "PrintLastLog no",
            "LogLevel DEBUG1",
        ]
        if not any(x.startswith("PasswordAuthentication") for x in extras):
            linhas.append("PasswordAuthentication no")
        with open(base + ".conf", "w") as f:
            f.write("\n".join(linhas + list(extras)) + "\n")
        self.saida = open(self.registro, "wb")
        self.p = subprocess.Popen([SSHD, "-D", "-e", "-f", base + ".conf", "-p", str(self.porta)],
                                  stdout=self.saida, stderr=subprocess.STDOUT)
        for _ in range(100):
            if self.p.poll() is not None:
                break
            if b"Server listening" in open(self.registro, "rb").read():
                return
            time.sleep(0.05)
        raise FalhaTeste("o sshd de teste não subiu: " + open(self.registro, errors="replace").read()[-600:])

    def log(self):
        return open(self.registro, errors="replace").read()

    def filhos(self):
        """Os processos do sshd que atendem conexões (filhos do que escuta)."""
        achados, pais = [], [self.p.pid]
        while pais:
            pai = pais.pop()
            for pid in executar(["pgrep", "-P", str(pai)], aceitar_falha=True).decode().split():
                achados.append(int(pid))
                pais.append(int(pid))
        return achados

    def parar(self):
        self.p.terminate()
        try:
            self.p.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.p.kill()
        self.saida.close()

    def __enter__(self):
        return self

    def __exit__(self, *resto):
        self.parar()


class Cliente:
    """O programa de teste conectado; conversa por linhas."""

    def __init__(self, porta, chave=CHAVE, usuario=USUARIO, **ambiente):
        amb = {k: v for k, v in os.environ.items() if not k.startswith("SSH_")}
        if chave:
            amb["SSH_CHAVE"] = chave
        for nome, valor in ambiente.items():
            if valor is not None:
                amb["SSH_" + nome.upper()] = str(valor)
        self.p = subprocess.Popen([PROGRAMA, "conectar", "127.0.0.1", str(porta), usuario],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, env=amb)
        self.linhas = []
        while True:
            linha = self.p.stdout.readline().decode(errors="replace").rstrip("\n")
            if not linha:
                self.linhas.append("erro o programa de teste morreu")
                break
            self.linhas.append(linha)
            if linha == "conectado" or linha.startswith("erro"):
                break
        self.inicio = self.linhas[-1]
        self.ok = self.inicio == "conectado"

    def linha(self, comeco):
        for x in self.linhas:
            if x.startswith(comeco):
                return x
        return ""

    def comando(self, texto):
        self.p.stdin.write(texto.encode() + b"\n")
        self.p.stdin.flush()
        return self.p.stdout.readline().decode(errors="replace").rstrip("\n")

    def escrever(self, dados):
        r = self.comando("e " + dados.hex())
        if r != f"ok {len(dados)}":
            raise FalhaTeste(f"escrita de {len(dados)} bytes: {r}")

    def ate(self, marca, ms=10000):
        """Lê até aparecer a marca: devolve (achou, o que chegou)."""
        partes = self.comando(f"u {ms} {marca.hex()}").split(" ")
        return int(partes[1]), bytes.fromhex(partes[2] if len(partes) > 2 else "")

    def fim(self, ms=30000):
        """Lê até o canal fechar: (código, total, sha256, como terminou e erro)."""
        partes = self.comando(f"c {ms}").split(" ", 4)
        return int(partes[1]), int(partes[2]), partes[3], partes[4] if len(partes) > 4 else ""

    def trocas(self):
        return int(re.search(r"trocas=(\d+)", self.comando("i")).group(1))

    def fechar(self):
        try:
            self.p.stdin.write(b"q\n")
            self.p.stdin.flush()
            self.p.stdin.close()
        except OSError:
            pass
        try:
            self.p.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.p.kill()


def conectado(c, descricao):
    if not c.ok:
        raise FalhaTeste(f"{descricao}: {' / '.join(c.linhas)}")
    return c


def esperar_erro(c, trecho, descricao):
    c.fechar()
    if c.ok:
        raise FalhaTeste(f"{descricao}: conectou, e devia ter sido recusado")
    verificar(trecho in c.inicio, f"{descricao}: recusado ({c.inicio[5:].strip()[:110]})")


def comando_remoto(porta, comando, entrada=None, **ambiente):
    """Executa um comando sem terminal e devolve (código, total, sha256, fim, trocas)."""
    c = conectado(Cliente(porta, comando=comando, **ambiente), comando)
    if entrada is not None:
        if isinstance(entrada, int):
            r = c.comando(f"g {entrada}")
            if r != f"ok {entrada}":
                raise FalhaTeste(f"{comando}: envio de {entrada} bytes: {r}")
        else:
            c.escrever(entrada)
        if c.comando("f") != "ok":
            raise FalhaTeste(f"{comando}: envio do fim da entrada")
    resultado = c.fim()
    trocas = c.trocas()
    c.fechar()
    return (*resultado, trocas)


def padrao(n):
    """Os mesmos bytes que o comando "g" do programa de teste manda."""
    return bytes(10 if i % 100 == 99 else 97 + i % 23 for i in range(n))


SAIDA_GRANDE = "head -c 2000000 /dev/zero | base64"
TEXTO_GRANDE = base64.encodebytes(bytes(2000000))       # igual ao base64 do sistema: linhas de 76 letras


# ------------------------------------------------------------ H56: transporte

def testar_transporte(s):
    c = conectado(Cliente(s.porta, comando="true"), "conexão simples")
    c.fim()
    c.fechar()
    chave = c.linha("chave ").split()
    verificar(c.linha("transporte SSH-2.0-OpenSSH") != "" and
              c.linha("transporte").endswith("| curve25519-sha256 ssh-ed25519 aes128-ctr aes128-ctr hmac-sha2-256"),
              "troca de chaves com o OpenSSH: " + c.linha("transporte").split("| ")[1])
    arquivo = open(os.path.join(PASTA, "servidor_ed25519.pub")).read().split()
    verificar(chave[1] == "ssh-ed25519" and chave[2] == digital_do_servidor("ed25519") and chave[3] == arquivo[1],
              f"a função de conferência recebe a chave do servidor e a impressão digital igual à do ssh-keygen -l ({chave[2]})")

    for cifra in ("aes128-ctr", "aes256-ctr"):
        for tipo, nome in (("ssh-ed25519", "ed25519"), ("rsa-sha2-256", "rsa")):
            c = conectado(Cliente(s.porta, comando="echo $((6*7))", cifras=cifra, chaves_servidor=tipo), f"{cifra} com {tipo}")
            codigo, total, _, fim = c.fim()
            c.fechar()
            chave = c.linha("chave ").split()
            verificar(c.linha("transporte").endswith(f"| curve25519-sha256 {tipo} {cifra} {cifra} hmac-sha2-256")
                      and chave[2] == digital_do_servidor(nome) and (codigo, total, fim) == (-1, 3, "status=0"),
                      f"cifra {cifra} com chave de servidor {tipo}: sessão funciona e a impressão digital confere")

    c = Cliente(s.porta, cifras="aes128-ctr,chacha20")
    esperar_erro(c, "cifra desconhecida", "opção com cifra que não temos")


def testar_servidor_restrito():
    with Servidor("KexAlgorithms curve25519-sha256@libssh.org", "Ciphers aes256-ctr", "MACs hmac-sha2-256",
                  "HostKeyAlgorithms rsa-sha2-256") as s:
        c = conectado(Cliente(s.porta, comando="echo $((6*7))"), "servidor restrito")
        codigo, total, _, fim = c.fim()
        c.fechar()
        verificar(c.linha("transporte").endswith("| curve25519-sha256@libssh.org rsa-sha2-256 aes256-ctr aes256-ctr hmac-sha2-256")
                  and c.linha("chave ").split()[1] == "ssh-rsa" and (codigo, total, fim) == (-1, 3, "status=0"),
                  "servidor que só aceita curve25519-sha256@libssh.org, rsa-sha2-256 e aes256-ctr: negociado e funcionando")
        c = Cliente(s.porta, chaves_servidor="ssh-ed25519")
        esperar_erro(c, "não tem chave de um tipo que conhecemos", "servidor só com RSA e cliente exigindo Ed25519")
    with Servidor("Ciphers aes128-gcm@openssh.com") as s:
        esperar_erro(Cliente(s.porta), "nenhuma cifra em comum", "servidor só com cifra que não temos")
        verificar("no matching cipher" in s.log() or "Received disconnect" in s.log() or "nenhuma cifra" in s.log(),
                  "o servidor ficou sabendo do motivo (recebeu o nosso DISCONNECT)")
    with Servidor("MACs hmac-sha2-512") as s:
        esperar_erro(Cliente(s.porta), "hmac-sha2-256", "servidor sem o hmac-sha2-256")
    with Servidor("KexAlgorithms diffie-hellman-group14-sha256") as s:
        esperar_erro(Cliente(s.porta), "curve25519-sha256", "servidor sem a curve25519-sha256")


def testar_chave_do_servidor(s):
    c = Cliente(s.porta, aceitar="nao")
    esperar_erro(c, "a chave do servidor foi recusada", "função de conferência devolve 'não'")
    verificar(c.linha("metodos") == "" and c.linha("auth") == "",
              "depois da recusa nada mais foi dito ao servidor (nem o nome do usuário)")
    c = conectado(Cliente(s.porta, comando="true", aceitar=digital_do_servidor("ed25519")), "impressão digital certa")
    c.fim()
    c.fechar()
    verificar(True, "com a impressão digital esperada (como num arquivo de servidores conhecidos) a conexão segue")
    c = Cliente(s.porta, aceitar=digital_do_servidor("rsa"))
    esperar_erro(c, "a chave do servidor foi recusada", "impressão digital diferente da esperada (chave trocada)")


class Intermediario:
    """Um repassador entre o cliente e o sshd, para mexer na conversa."""

    def __init__(self, porta_destino, prefacio=b"", so_isto=None):
        self.escuta = socket.socket()
        self.escuta.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.escuta.bind(("127.0.0.1", 0))
        self.escuta.listen(1)
        self.porta = self.escuta.getsockname()[1]
        self.destino, self.prefacio, self.so_isto = porta_destino, prefacio, so_isto
        threading.Thread(target=self.rodar, daemon=True).start()

    def rodar(self):
        try:
            cliente, _ = self.escuta.accept()
            if self.so_isto is not None:
                cliente.sendall(self.so_isto)
                time.sleep(1.5)
                cliente.close()
                return
            servidor = socket.create_connection(("127.0.0.1", self.destino))
            cliente.sendall(self.prefacio)

            def repassar(de, para):
                try:
                    while True:
                        dados = de.recv(65536)
                        if not dados:
                            break
                        para.sendall(dados)
                except OSError:
                    pass
                for x in (de, para):
                    try:
                        x.shutdown(socket.SHUT_RDWR)
                    except OSError:
                        pass

            threading.Thread(target=repassar, args=(cliente, servidor), daemon=True).start()
            repassar(servidor, cliente)
        except OSError:
            pass


def testar_identificacao(s):
    meio = Intermediario(s.porta, prefacio=b"Bem-vindo ao servidor de teste\r\nmais uma linha de conversa\r\n")
    c = conectado(Cliente(meio.porta, comando="echo $((6*7))"), "linhas antes da identificação")
    codigo, total, _, fim = c.fim()
    c.fechar()
    verificar((codigo, total, fim) == (-1, 3, "status=0"),
              "linhas de conversa antes da identificação do servidor são puladas")
    esperar_erro(Cliente(Intermediario(0, so_isto=b"SSH-1.5-antigo\r\n").porta), "versão do SSH que não conhecemos",
                 "servidor do protocolo SSH 1")
    esperar_erro(Cliente(Intermediario(0, so_isto=b"HTTP/1.1 400 Bad Request\r\n" + b"x" * 300 + b"\r\n").porta),
                 "identificação inválida", "servidor que não é SSH (linha longa demais)")
    esperar_erro(Cliente(Intermediario(0, so_isto=b"").porta, limite=500), "tempo esgotado",
                 "servidor mudo (limite de 0,5 s)")
    esperar_erro(Cliente(Intermediario(0, so_isto=b"SSH-2.0-mudo\r\n").porta, limite=500), "tempo esgotado",
                 "servidor que se identifica e emudece")


def testar_dados_estragados(s):
    # O programa de teste troca um bit do primeiro byte do k-ésimo "corpo de
    # pacote" que chega. O 1º é o KEXINIT do servidor, em claro: ninguém
    # nota na hora, mas a assinatura da troca cobre a negociação.
    c = Cliente(s.porta, estragar=1)
    esperar_erro(c, "assinatura do servidor na troca de chaves não confere", "bit trocado no KEXINIT do servidor")
    verificar(c.linha("chave ") == "", "a chave do servidor nem chegou a ser mostrada a quem chama")
    # Do 3º em diante os pacotes vêm cifrados e com MAC.
    c = Cliente(s.porta, estragar=3)
    esperar_erro(c, "MAC não confere", "bit trocado no primeiro pacote cifrado")
    time.sleep(0.2)
    verificar(re.search(r"Received disconnect from 127.0.0.1 port \d+:5:", s.log()) is not None,
              "o servidor recebeu o nosso DISCONNECT com o motivo 5 (erro de MAC)")
    c = conectado(Cliente(s.porta, comando=SAIDA_GRANDE, estragar=40), "estrago no meio da sessão")
    codigo, total, _, fim = c.fim()
    c.fechar()
    verificar(codigo == -2 and "MAC não confere" in fim and total < len(TEXTO_GRANDE),
              f"bit trocado no meio de uma transferência: sessão encerrada com erro de MAC depois de {total} bytes bons")


# ------------------------------------------------------------ H57: autenticação

def testar_autenticacao(s):
    c = conectado(Cliente(s.porta, comando="id -un"), "login com chave")
    codigo, total, _, fim = c.fim()
    c.fechar()
    verificar(c.linha("metodos") == "metodos -4 publickey" and c.linha("auth chave 0") != "" and
              (codigo, fim) == (-1, "status=0") and total == len(USUARIO) + 1,
              f"consulta 'none' devolve os métodos (publickey) e o login com a chave Ed25519 gerada por nós entra como {USUARIO}")

    c = Cliente(s.porta, chave=CHAVE_ERRADA)
    esperar_erro(c, "não autenticado", "chave que o servidor não conhece")
    verificar("auth chave -4 autenticação recusada; métodos aceitos pelo servidor: publickey" in c.linhas,
              "a recusa vem como SSH_RECUSADO, com a mensagem e os métodos que restam, sem derrubar a conexão")

    c = Cliente(s.porta, chave=None, senha="segredo")
    esperar_erro(c, "não autenticado", "senha num servidor que não aceita senha")
    verificar("auth senha -4 autenticação recusada; métodos aceitos pelo servidor: publickey" in c.linhas,
              "pedido de senha recusado com a lista de métodos (sem 'password')")

    c = conectado(Cliente(s.porta, chave=CHAVE, chave_vezes=1, senha="x", comando="true"), "chave e senha")
    c.fim()
    c.fechar()
    verificar(c.linha("auth senha") == "", "com a chave aceita, a senha nem é tentada")

    c = Cliente(s.porta, usuario="usuario-que-nao-existe-" + "x" * 20)
    esperar_erro(c, "não autenticado", "usuário que não existe")


def testar_senha_e_insistencia():
    with Servidor("PasswordAuthentication yes", "MaxAuthTries 3") as s:
        c = Cliente(s.porta, chave=None, senha="senha-errada")
        esperar_erro(c, "não autenticado", "senha errada num servidor que aceita senha")
        verificar(c.linha("metodos") == "metodos -4 publickey,password" and
                  c.linha("auth senha -4 autenticação recusada") != "",
                  "o servidor anuncia 'password' e a senha errada volta como SSH_RECUSADO")
        c = Cliente(s.porta, chave=CHAVE_ERRADA, chave_vezes=6)
        esperar_erro(c, "o servidor desconectou: Too many authentication failures",
                     "insistir com a chave errada até o servidor cansar (SSH_MSG_DISCONNECT)")
        # Uma recusa não estraga a conexão: senha errada e, em seguida, a chave certa.
        c = conectado(Cliente(s.porta, chave=CHAVE, senha_antes="senha-errada", comando="true"), "senha e depois chave")
        c.fim()
        c.fechar()
        verificar(c.linha("auth senha -4") != "" and c.linha("auth chave 0") != "",
                  "na mesma conexão, depois da senha recusada, o login com a chave certa entra")


# ------------------------------------------------------------ H58: sessão

def texto_limpo(dados):
    return dados.decode(errors="replace")


def testar_shell(s, **ambiente):
    nome = "shell interativo" + (" (rede picada: 3 bytes por vez)" if ambiente.get("picado") else "")
    c = conectado(Cliente(s.porta, term="xterm-256color", colunas=132, linhas=43, **ambiente), nome)
    c.escrever(b"stty size; tty; echo $TERM; echo M$((1+1))K\n")
    achou, dados = c.ate(b"M2K")
    texto = texto_limpo(dados)
    verificar(achou == 1 and re.search(r"\b43 132\r\n", texto) is not None and "/dev/pts/" in texto
              and "xterm-256color\r\n" in texto,
              f"{nome}: terminal criado (/dev/pts/N), TERM=xterm-256color e 'stty size' mostra 43 132")
    if c.comando("j 100 30") != "ok":
        raise FalhaTeste("mudança de tamanho")
    c.escrever(b"stty size; echo N$((1+2))K\n")
    achou, dados = c.ate(b"N3K")
    verificar(achou == 1 and re.search(r"\b30 100\r\n", texto_limpo(dados)) is not None,
              f"{nome}: depois de ssh_mudar_tamanho(100, 30), 'stty size' mostra 30 100")
    c.escrever(b"echo fora >&2; echo SAGUI_$((40+2)); exit 3\n")
    achou, dados = c.ate(b"SAGUI_42")
    codigo, _, _, fim = c.fim()
    c.fechar()
    verificar(achou == 1 and b"fora" in dados and (codigo, fim) == (-1, "status=3"),
              f"{nome}: 'echo SAGUI_$((40+2)); exit 3' devolve SAGUI_42, o canal fecha e o código de saída é 3")


def testar_fim_de_sessao(s):
    codigo, total, _, fim, _ = comando_remoto(s.porta, "echo saida; echo erro >&2; exit 7")
    verificar((codigo, total, fim) == (-1, 11, "status=7"),
              "comando sem terminal: saída e saída de erro entregues (11 bytes) e código de saída 7")
    codigo, _, _, fim, _ = comando_remoto(s.porta, "kill -KILL $$")
    verificar((codigo, fim) == (-1, "sinal=KILL"), "comando morto por sinal: ssh_saida conta qual (KILL)")
    codigo, total, resumo, fim, _ = comando_remoto(s.porta, "cat", entrada=b"")
    verificar((codigo, total, fim) == (-1, 0, "status=0"),
              "fim da entrada (EOF) enviado: o 'cat' remoto termina e o canal fecha nos dois sentidos")
    time.sleep(0.2)
    verificar("at\\303\\251 logo" in s.log(), "ssh_fechar despede-se do servidor com DISCONNECT (aparece no registro dele)")


def testar_dados_grandes(s, descricao, trocas_min=0, trocas_min_saida=None, **ambiente):
    certo = hashlib.sha256(TEXTO_GRANDE).hexdigest()
    if trocas_min_saida is None:
        trocas_min_saida = trocas_min
    codigo, total, resumo, fim, trocas = comando_remoto(s.porta, SAIDA_GRANDE, **ambiente)
    verificar((codigo, total, resumo, fim) == (-1, len(TEXTO_GRANDE), certo, "status=0") and trocas - 1 >= trocas_min_saida,
              f"{descricao}: saída de {total} bytes recebida inteira (SHA-256 confere)"
              + (f", com {trocas - 1} trocas de chaves no caminho" if trocas_min else ""))
    n = 1000000
    codigo, total, resumo, fim, trocas = comando_remoto(s.porta, "wc -c", entrada=n, **ambiente)
    verificar((codigo, total, fim) == (-1, len(str(n)) + 1, "status=0") and trocas - 1 >= trocas_min,
              f"{descricao}: entrada de {n} bytes enviada a 'wc -c', que contou {n}"
              + (f", com {trocas - 1} trocas de chaves" if trocas_min else ""))
    codigo, total, resumo, fim, trocas = comando_remoto(s.porta, "cat", entrada=n, **ambiente)
    verificar((codigo, total, resumo, fim) == (-1, n, hashlib.sha256(padrao(n)).hexdigest(), "status=0")
              and trocas - 1 >= trocas_min,
              f"{descricao}: {n} bytes mandados a 'cat' voltam iguais (ida e volta ao mesmo tempo)"
              + (f", com {trocas - 1} trocas de chaves" if trocas_min else ""))


def testar_wc(s):
    # O 'wc -c' responde com o número seguido de quebra de linha; conferimos
    # o texto, não só o tamanho.
    c = conectado(Cliente(s.porta, comando="wc -c"), "wc -c")
    c.comando("g 300000")
    c.comando("f")
    _, dados = c.ate(b"\n")
    c.fim()
    c.fechar()
    verificar(dados == b"300000\n", "'wc -c' remoto responde 300000 para 300000 bytes enviados")


def testar_trocas_de_chaves(s):
    # Quando somos nós que pedimos, o servidor só para de mandar dados ao
    # ler o nosso KEXINIT: numa descarga rápida passa muita coisa entre um
    # pedido e outro, e as trocas são poucas. No envio, paramos na hora.
    testar_dados_grandes(s, "cliente pedindo chaves novas a cada 200 KB", trocas_min=4, trocas_min_saida=1,
                         trocar=200000)
    with Servidor("RekeyLimit 64K") as r:
        testar_dados_grandes(r, "servidor com RekeyLimit 64K", trocas_min=10)
        testar_dados_grandes(r, "os dois lados pedindo (64K e 50 KB), cifra aes256-ctr e chave RSA", trocas_min=10,
                             trocar=50000, cifras="aes256-ctr", chaves_servidor="rsa-sha2-256")
        c = conectado(Cliente(r.porta, term="xterm", trocar=3000), "shell com trocas")
        for i in range(12):
            c.escrever(f"echo V$(({i}+100))K; head -c 3000 /dev/zero | base64 -w0; echo\n".encode())
            achou, _ = c.ate(f"V{i + 100}K".encode())
            if achou != 1:
                raise FalhaTeste(f"shell com trocas de chaves: volta {i} não respondeu")
        trocas = c.trocas()
        c.escrever(b"exit\n")
        codigo, _, _, fim = c.fim()
        c.fechar()
        verificar(trocas >= 6 and (codigo, fim) == (-1, "status=0"),
                  f"shell interativo atravessa {trocas - 1} trocas de chaves (pedidas pelos dois lados) sem perder nada")


def testar_quedas(s):
    c = conectado(Cliente(s.porta, term="xterm"), "queda")
    c.escrever(b"echo P$((1+1))K\n")
    achou, _ = c.ate(b"P2K")
    antes = set(s.filhos())
    for pid in antes:
        try:
            os.kill(pid, signal.SIGKILL)
        except OSError:
            pass
    codigo, _, _, fim = c.fim(8000)
    c.fechar()
    verificar(achou == 1 and antes and codigo == -3 and "a conexão caiu" in fim,
              "servidor morto no meio da sessão: ssh_ler devolve SSH_CORTADO (a conexão caiu)")


def testar_sinal_de_vida():
    with Servidor("ClientAliveInterval 1", "ClientAliveCountMax 2") as s:
        c = conectado(Cliente(s.porta, term="xterm"), "sinal de vida")
        c.ate(b"", 6000)                # seis segundos sem fazer nada
        c.escrever(b"echo Q$((1+1))K; exit\n")
        achou, _ = c.ate(b"Q2K")
        codigo, _, _, fim = c.fim()
        c.fechar()
        verificar(achou == 1 and (codigo, fim) == (-1, "status=0"),
                  "servidor que confere se estamos vivos a cada segundo (keepalive@openssh.com): "
                  "respondemos e a sessão parada por 6 s continua de pé")


def main():
    compilar()
    print("H56 - chaves e transporte do SSH")
    testar_base64()
    testar_chaves()
    with Servidor() as s:
        testar_transporte(s)
        testar_chave_do_servidor(s)
        testar_identificacao(s)
        testar_dados_estragados(s)
        testar_servidor_restrito()
        print("H57 - autenticação")
        testar_autenticacao(s)
        testar_senha_e_insistencia()
        print("H58 - sessão, terminal e janelas")
        testar_shell(s)
        testar_shell(s, picado=1)
        testar_fim_de_sessao(s)
        testar_wc(s)
        testar_dados_grandes(s, "servidor padrão")
        testar_trocas_de_chaves(s)
        testar_quedas(s)
        testar_sinal_de_vida()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except FalhaTeste as e:
        print("FALHOU:", e)
        sys.exit(1)
