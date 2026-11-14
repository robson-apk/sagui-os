# Integração e entrega contínuas (CI/CD)

O SaguiOS é desenvolvido no **Windows 11**, mas é compilado e testado no
**Ubuntu 24.04** do GitHub Actions. O computador do desenvolvedor não precisa
de Linux, WSL, Docker, QEMU nem de um compilador cruzado: o `git push` leva o
trabalho para o runner remoto, que instala as ferramentas, compila com o
`Makefile` do projeto e roda a suíte inteira no QEMU.

O workflow fica em [`.github/workflows/ci.yml`](../.github/workflows/ci.yml).
Ele usa somente ações oficiais do GitHub (`actions/checkout` e
`actions/upload-artifact`), com permissão `contents: read`, sem segredos e sem
`pull_request_target`.

## 1. Quando o CI roda

| Evento | Quando | Entrega |
| --- | --- | --- |
| `push` na `main` | Todo commit que entra na `main` | Sim, se compilar e todos os testes passarem |
| `push` em `feat/**`, `fix/**`, `ci/**` | Branches de trabalho | Não |
| `pull_request` para a `main` | Pull requests, inclusive de forks | Não |
| `workflow_dispatch` | Execução manual pela aba **Actions** | Depende da branch escolhida |

Pull requests vindos de forks rodam o mesmo workflow. Como não há segredo
configurado, nada sensível é exposto; o token do GitHub fica somente leitura e
não é guardado no repositório (`persist-credentials: false`).

O controle de concorrência separa dois casos:

- em branches e pull requests, o grupo é `ci-<workflow>-<ref>` e uma execução
  nova **cancela** a anterior, para não gastar tempo com um commit que já foi
  substituído;
- na `main`, o grupo inclui o SHA do commit (`ci-<workflow>-<ref>-<sha>`).
  Commits diferentes ficam em grupos diferentes e podem rodar em paralelo;
  assim, cada commit validado tem a sua chance de gerar a entrega.

O GitHub mantém no máximo **uma execução ativa e uma pendente por grupo**. Se
a `main` usasse um grupo único, a chegada de um terceiro commit poderia
substituir o segundo que estava pendente. O grupo por SHA evita essa perda.
Execuções do mesmo SHA — por exemplo, um push e uma execução manual — ainda
compartilham o grupo; nesse caso, `cancel-in-progress: false` garante que a
execução ativa não seja interrompida, mas a pendente mais antiga pode ser
substituída. Como o SHA é o mesmo, a entrega continua sendo do mesmo commit.

O job tem limite de **4 horas**. A suíte tem dezenas de testes (QEMU e host),
com limite de até 600 segundos por teste e vários deles dependendo da internet
real; o limite do job é folgado, mas impede que uma execução fique presa por
um dia.

## 2. Arquitetura da execução

```
Windows 11 (desenvolvedor)
  │  git push / pull request
  ▼
GitHub Actions — ubuntu-24.04
  ├─ checkout do repositório (somente leitura)
  ├─ preparação de build/testes/resultado.txt e de build/testes/capturas
  ├─ apt-get install das dependências (saída também vai para o resultado.txt)
  ├─ conferência das ferramentas (idem; inclusive gcc -m32 com ASan/UBSan e ffmpeg com H.264/AAC)
  ├─ make                         → build/saguios.img + SHA-256 + build/tamanho.txt
  ├─ python3 tests/rodar_todos.py → suíte completa no QEMU
  └─ somente na main e com sucesso:
       ├─ make vdi                → build/saguios.vdi + SHA-256
       └─ upload-artifact         → saguios-<commit> (14 dias)
```

Não há outro sistema de compilação: o CI chama exatamente `make` e
`python3 tests/rodar_todos.py`, os mesmos comandos do `README.md` e do
`CONTRIBUTING.md`. O executor de testes também roda `make` antes de começar,
como sempre fez; a etapa explícita de compilação só torna a falha mais fácil
de localizar.

## 3. Dependências no runner

Tudo é instalado dentro do workflow, no Ubuntu 24.04, com pacotes da própria
distribuição:

| Pacote | Para quê |
| --- | --- |
| `gcc`, `gcc-multilib`, `binutils` | compilar em 32 bits (`-m32`), ligar (`ld`) e copiar (`objcopy`) |
| `nasm`, `make` | carregador de boot, kernel e alvos do `Makefile` |
| `python3`, `python3-pil` | ferramentas de build e testes que leem imagens (Pillow) |
| `qemu-system-x86`, `qemu-utils` | `qemu-system-i386`, `qemu-img` e o VDI |
| `imagemagick` | `convert` e `identify`, usados nas capturas e comparações |
| `mtools`, `dosfstools`, `fdisk` | `mcopy`, `mmd`, `mkfs.vfat`, `fsck.vfat`, `sfdisk` nos testes de FAT32 |
| `procps` | `pgrep`, usado nos testes de SSH |
| `openssl` | certificados e servidores TLS de teste |
| `openssh-client`, `openssh-server` | `ssh-keygen` e o `sshd` real dos testes H56 a H58 |
| `vim`, `htop` | programas que o H58 executa no servidor SSH de teste |
| `ffmpeg` | gera o MP4 H.264/AAC do teste H82 |

O workflow também cria `/run/sshd`, que o OpenSSH procura antes de abrir o
`sshd` — mesmo rodando como o usuário comum do runner, nunca como root.
A suíte inteira roda como `runner`; `sudo` é usado apenas para instalar
pacotes, criar esse diretório e liberar soquetes ICMP sem privilégio
(`net.ipv4.ping_group_range`), que o QEMU usa para o `ping google.com` do H45.
Isso não eleva os testes: o QEMU continua rodando como usuário comum.

Antes de compilar, uma etapa de conferência verifica cada comando, compila e
executa um programa mínimo com `gcc -m32 -fsanitize=address,undefined` e exige
que o `ffmpeg` tenha os codificadores `libx264` e AAC. Assim, uma dependência
faltando aparece com mensagem clara no começo, e não como um teste que "passou"
sem testar o vídeo.

## 4. Compilação

```bash
make
```

O resultado esperado é `build/saguios.img`. A etapa confere:

- o código de saída do `make` (com `set -o pipefail`, o `tee` não esconde erro);
- a existência e o tamanho maior que zero da imagem;
- `build/saguios.img.sha256`, gerado pelo próprio `Makefile`;
- a conferência do SHA-256 com `sha256sum -c`;
- `build/tamanho.txt`, com o tamanho útil da imagem.

O workflow usa os alvos `make` e `make vdi` que o projeto já tinha. A correção
do H63 acrescenta `-mstackrealign` ao `UCFLAGS` (erro 22 em
[`erros.md`](erros.md)); o fluxo do CI em si não muda o `Makefile`.

## 5. Testes

```bash
python3 tests/rodar_todos.py
```

O comando roda **todos** os `tests/hNN_*.py` e `tests/bN_*.py` em ordem. Não
há seleção, `continue-on-error` nem teste desativado no CI. A suíte cobre,
entre outros:

- boot, modo protegido, memória, processos, paginação e chamadas de sistema;
- terminal, shell, pipes, teclado ABNT2, mouse, janelas e área de trabalho;
- USB, EHCI, FAT32 com leitura e gravação, conferidos com `mtools` e `fsck.vfat`;
- rede `e1000`, DHCP, DNS, TCP, HTTP, NTP, TLS 1.3 e HTTPS;
- SSH de verdade contra o `sshd` do OpenSSH, incluindo chave Ed25519, `vim` e `htop`;
- Cipó, HTML, CSS, JavaScript, Canvas, imagens PNG/JPEG/WebP;
- o compilador C do SaguiOS, o Sagui Code com servidores simulados e o vídeo H.264/AAC;
- resoluções gráficas e o desligamento do sistema.

### Testes que precisam de internet

Os testes **H45** (DNS e ping), **H53** (HTTPS) e **H63** (sites de aceite)
usam a internet real do runner, como já faziam na VPS do projeto. Eles não
foram substituídos por dublês. Em particular:

- H45 resolve `example.com` e `google.com` pelo DNS do QEMU e pinga o Google;
- H53 baixa `https://example.com` e a Wikipédia em português com o TLS do
  SaguiOS, e testa certificados errados com um `openssl s_server` local;
- H63 abre Wikipédia, `example.com`, a man page do `ls` e o `README.md` do
  repositório no GitHub.

Se a rede do GitHub estiver indisponível ou um site externo mudar, o teste
correspondente falha — de propósito. O CI não mascara esse tipo de falha.

### SSH e vídeo

Os testes H56 a H58 sobem um `sshd` real do OpenSSH como o usuário do runner,
em porta alta, com chaves geradas na hora. O SaguiOS se conecta a ele pela
rede do QEMU (`10.0.2.2`). Nenhum serviço real é falsificado.

O H82 gera um MP4 com `ffmpeg` (H.264 Baseline + AAC-LC), baixa pelo HTTP e
confere os quadros e o tom de 440 Hz. O CI exige que o `ffmpeg` exista e tenha
os codificadores `libx264` e AAC; sem isso, a execução falha na conferência de
dependências, em vez de o teste avisar que não testou o vídeo.

### Sagui Code

Os testes do Sagui Code usam os servidores simulados que já existem no
repositório. **Nunca** é preciso configurar uma chave do OpenRouter no CI, e
nenhuma chave é usada.

## 6. Registro e diagnóstico

Logo depois do checkout, o workflow cria `build/testes/capturas` e
`build/testes/resultado.txt`. A instalação dos pacotes e a conferência das
ferramentas também escrevem nesse arquivo; em seguida vêm a compilação, a
suíte e, na `main`, a geração do VDI. Assim, mesmo uma falha no `apt-get` ou
na conferência deixa um `resultado.txt` para o artefato de diagnóstico.

A saída também aparece ao vivo na página da execução. As etapas de instalação,
conferência, compilação, testes e VDI usam `set -o pipefail`; portanto, se o
`apt-get`, o `make` ou um teste falhar, o job falha mesmo com o `tee`.

Em caso de falha, o workflow publica o artefato
`saguios-ci-logs-<commit>-<tentativa>` com:

- `build/testes/resultado.txt`;
- os logs seriais do QEMU (`*.serial`);
- logs de `sshd` e dos testes;
- as capturas de tela (`*.png`), incluindo as do H68.

O `resultado.txt` é criado logo depois do checkout, então o artefato de
diagnóstico existe até quando o problema acontece na instalação das
dependências.

Para diagnosticar:

1. abra **Actions** → execução → job **Compilar e testar no Ubuntu 24.04**;
2. procure a etapa vermelha e leia as últimas linhas dela;
3. baixe o artefato de logs e veja `resultado.txt` e o `*.serial` do teste
   que falhou — a serial tem o log do kernel e o motivo exato;
4. as capturas mostram a tela do SaguiOS no momento da falha;
5. reproduza localmente no WSL/Ubuntu, se tiver, com os mesmos comandos:
   `make && python3 tests/rodar_todos.py`.

Falhas de rede externa (H45/H53/H63) aparecem como tempo esgotado ou erro de
conexão no teste correspondente. Nesse caso, confira se o site está no ar e
reexecute o job; não desative o teste.

## 7. Entrega contínua

A entrega só acontece quando **todas** estas condições valem:

- a referência é `refs/heads/main` — push ou execução manual na `main`;
- a compilação passou;
- todos os testes passaram.

Pull requests e branches de trabalho nunca geram entrega. Quando as condições
valem, o workflow gera `build/saguios.vdi` com `make vdi` e publica o artefato
**`saguios-<sha-do-commit>`**, com retenção de **14 dias**:

```
saguios.img
saguios.img.sha256
saguios.vdi
saguios.vdi.sha256
tamanho.txt
```

Não há release público automático e nada é gravado em `entrega/`: essa pasta
continua guardando as imagens de referência do projeto, versionadas à mão.

### Como baixar as imagens

1. abra <https://github.com/erigobeli/sagui-os/actions/workflows/ci.yml>;
2. clique na execução da `main` desejada (a mais recente está no topo);
3. na seção **Artifacts**, baixe `saguios-<sha>`;
4. descompacte e confira os arquivos com:

   ```bash
   sha256sum -c saguios.img.sha256
   sha256sum -c saguios.vdi.sha256
   ```

   No Windows/PowerShell, use
   `Get-FileHash .\saguios.img -Algorithm SHA256` (e o mesmo para o `.vdi`) e
   compare com o valor gravado no arquivo `.sha256`.

5. grave `saguios.img` num pendrive em modo DD (veja
   [`como-gravar.md`](como-gravar.md)) ou use `saguios.vdi` no VirtualBox.

O artefato expira em 14 dias. As imagens de uma versão entregue ficam também
em `entrega/`, que é permanente.

## 8. Desenvolvimento no Windows

O desenvolvedor continua no Windows 11, com Git para Windows e PowerShell:

- os arquivos novos usam **LF** e **UTF-8**; o `.gitattributes` garante LF em
  YAML, Markdown, Python, shell e no `Makefile` mesmo com `core.autocrlf=true`;
- editar o workflow e a documentação não exige Linux;
- validar o YAML pode ser feito no próprio Windows, por exemplo com
  `python -c "import yaml; yaml.safe_load(open('.github/workflows/ci.yml', encoding='utf-8'))"`;
- **não** é preciso instalar Ubuntu, WSL, Docker ou QEMU. O runner do GitHub
  faz a validação Linux.

Se houver WSL2 com Ubuntu já instalado, dá para rodar localmente:

```bash
sudo apt-get install -y nasm gcc gcc-multilib binutils make python3 python3-pil \
  qemu-system-x86 qemu-utils imagemagick mtools dosfstools fdisk procps \
  openssl openssh-client openssh-server ffmpeg
sudo install -d -m 0755 /run/sshd
make
python3 tests/rodar_todos.py
```

Isso é opcional e não substitui a execução real no GitHub Actions.

## 9. Segurança e limites

- Permissão do workflow: `contents: read`.
- Nenhum segredo, token pessoal, chave de API ou senha no repositório.
- Nenhum `pull_request_target`.
- Nenhuma ação de terceiros: só `actions/checkout` e `actions/upload-artifact`,
  em versões maiores estáveis.
- O workflow não escreve no repositório: ele só compila e testa com os
  comandos oficiais do projeto. As mudanças de código deste PR são o
  `-mstackrealign` no `Makefile` (H63) e a medição do H82 no teste; nenhuma
  delas toca `entrega/`.
- A suíte não roda como root.
- O job não publica nada em pull requests nem em branches de trabalho.

O limite conhecido é a dependência de serviços externos nos testes de internet
(H45, H53 e H63) e do `sshd`/`ffmpeg` no runner. O CI prefere falhar de forma
visível a fingir que testou.
