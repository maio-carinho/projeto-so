# projeto-so

Repositório para o desenvolvimento da 2ª VA de Sistemas Operacionais (BCC).

Simulador de sistema operacional em C, feito em cinco etapas que se integram. É um programa de linha de comando: tudo acontece na memória RAM e, ao sair com `exit`, o estado é perdido.

> **Manutenção:** a cada etapa, atualize a tabela abaixo, acrescente um capítulo curto antes das limitações e ajuste a estrutura do Capítulo 3.

## 1. Andamento

| Etapa | Tema | Entrega | Status |
|---|---|---|---|
| 1 | Sistema de Arquivos | 1 nov | Concluída |
| 2 | Entrada/Saída (escalonamento de disco, RAID, SSD) | 8 nov | Concluída |
| 3 | Segurança e Proteção (usuários, ACL, criptografia, antivírus) | 15 nov | Em andamento |
| 4 | Gerenciamento de Memória | 22 nov | Pendente |
| 5 | Processos e Concorrência | 29 nov | Pendente |

## 2. Compilar e executar

Requer `gcc` e `make` (desenvolvido no Windows com MinGW 6.3.0). No PowerShell:

```powershell
mingw32-make
chcp 65001            # UTF-8, para os acentos e a árvore aparecerem certos
.\fs_simulator.exe    # digite help para ver os comandos e exit para sair
```

Em Linux/WSL, use `make` e `./fs_simulator`.

## 3. Estrutura e convenções

```
src/
├── main.c                 # shell: lê o comando e chama a função certa
├── comum/                 # utils: constantes e registro de erros
├── criptografia/          # sha256, cripto: hash e cifra (não dependem de nada)
├── armazenamento/         # disco (blocos lógicos), raid, ssd, escalonador (braço do disco)
│                          # e es (comandos io, raid, ssd e meio)          [Etapas 1 e 2]
├── sistema_arquivos/      # fs: árvore, CRUD, permissões e criptografia de arquivos
├── seguranca/             # seguranca: usuários, grupos, sessão, ACL e auditoria
└── antivirus/             # antivirus: integridade dos blocos e assinaturas
```

Uma pasta só usa as pastas de baixo: `comum` e `criptografia` não dependem de nada; `armazenamento` e `seguranca` usam `comum` e `criptografia`; `sistema_arquivos` usa essas três; `antivirus` usa `sistema_arquivos`, `armazenamento` e `seguranca`; o `main` usa tudo. O `fs` se registra no `seguranca` para criar o `/home/<usuário>`, sem que o `seguranca` precise conhecer o sistema de arquivos. Novas etapas ganham pasta própria (`memoria/`, `processos/`).

- Funções que podem falhar devolvem `0` (ok) ou `-1`, com a mensagem em `reg_ultimo_erro()`; `ERRO_JA_EXIBIDO` indica que o comando já mostrou na tela o motivo da recusa.
- Todo comando recebe `(int argc, char **argv)`; novos comandos entram como uma linha na tabela `comandos` do `main.c`.
- Os `#include` levam o caminho da pasta (`#include "armazenamento/disco.h"`). Nomes de pasta não podem ter espaços, por causa do `make`. O Makefile compila `src/*.c` e `src/*/*.c` sozinho, então criar um módulo ou uma pasta não exige mexer nele.
- Prefixos: `disco_`, `fs_`, `raid_`, `escalonador_`, `ssd_`, `es_`, `seguranca_`, `cripto_`, `cmd_` (comandos) e `reg_` (erros). Nomes em português, sem acento.

## 4. Etapa 1: Sistema de Arquivos

Árvore de diretórios e arquivos com caminhos absolutos e relativos, CRUD e um disco de **16 blocos de 4 KB**, com **alocação indexada**: cada arquivo usa 1 bloco de i-node (a lista de onde estão os dados) mais N blocos de dados, que podem ficar espalhados. O bloco 0 é do i-node da raiz, então sobram 15 para arquivos.

| Comando | Função |
|---|---|
| `mkdir <caminho>` / `cd [caminho]` / `ls [caminho]` / `tree` | navegar e listar |
| `create <caminho> --size <tam>` | cria arquivo (`512B`, `10KB`, `1MB`) |
| `read <arquivo>` | lê o i-node do disco e o começo do conteúdo |
| `update <arquivo> --size <tam>` | muda o tamanho (não altera nada se não couber) |
| `rm <caminho> [-r]` | remove arquivo ou diretório |
| `fs_status` | mapa do disco e fragmentação |

O `fs_status` mostra o mapa de blocos e o diagnóstico (`R` = raiz, minúscula = i-node, maiúscula = dados, `·` = livre):

```
[MAPA VISUAL]  [R] [a] [A] [A] [A] [·] [·] ...      (create /sistema/logs/app.log --size 10KB)
Fragmentação Interna (total) : 2048 bytes             (10 KB ocupam 3 blocos de 4 KB)
Fragmentação Externa         : 0 bytes
Necessidade de Compactação   : NÃO
```

Na alocação indexada qualquer bloco livre serve, então a fragmentação externa é sempre 0 e a compactação nunca é necessária.

## 5. Etapa 2: Entrada/Saída

A E/S entra **por baixo do `disco`**: cada leitura ou gravação de bloco do FS vira requisições nos discos físicos (ou operações de página no SSD), e o `fs.c` não precisou mudar, só ganhou o `write_file`. O disco lógico continua com 16 blocos, e trocar o RAID ou o meio migra os dados já gravados.

| Comando | Função |
|---|---|
| `write_file <caminho> "<texto>"` ou `--size <tam>` | grava texto (ou conteúdo simulado), criando ou sobrescrevendo |
| `io auto <on\|off>` | mostra o escalonamento depois de cada comando do FS (começa desligado) |
| `io algoritmo <fcfs\|sstf\|scan\|cscan> [sobe\|desce]` | escolhe o algoritmo (padrão: SSTF) |
| `io add <trilha...>`, `io cabeca <trilha>` | enfileira requisições manuais e posiciona a cabeça (`--disco N` escolhe o disco) |
| `io fila`, `io escalonar [alg]`, `io comparar`, `io status` | ver a fila, atendê-la, comparar os algoritmos, estatísticas |
| `raid <nenhum\|0\|1\|4\|5>`, `raid status` | escolhe o arranjo e mostra discos e mapa físico |
| `raid falhar\|reconstruir\|testar <disco>` | simula falha, reconstrói e roda o teste completo de recuperação |
| `meio <hdd\|ssd>` | escolhe o dispositivo de armazenamento |
| `ssd status`, `ssd wear <on\|off>`, `ssd estresse <n>` | desgaste, nivelamento de desgaste e teste de regravação |

**Escalonamento.** Com a fila `98, 183, 37, 122, 14` e a cabeça na trilha 53 (Exemplo 2.1; trilhas de 0 a 199):

| Algoritmo | Como decide | Deslocamento |
|---|---|---|
| FCFS | ordem de chegada | 469 |
| SSTF | trilha mais próxima | 208 (`53 -> 37 -> 14 -> 98 -> 122 -> 183`) |
| SCAN | varre até a extremidade e inverte | 331 |
| C-SCAN | varre até a extremidade e salta para a outra, sem atender | 382 |

SCAN e C-SCAN vão até a extremidade do disco, e o salto do C-SCAN conta como deslocamento.

**RAID.** A paridade é o XOR dos blocos de dados da faixa: se um disco falha, o bloco perdido é recalculado com o XOR dos que sobraram.

| Nível | Discos | Organização | Tolera falha |
|---|---|---|---|
| Sem RAID | 1 | disco único (padrão) | não |
| RAID 0 | 2 | blocos alternados entre os discos | não |
| RAID 1 | 2 | espelhamento | 1 disco |
| RAID 4 | 3 | paridade sempre no Disco 3 | 1 disco |
| RAID 5 | 3 | paridade que gira entre os discos | 1 disco |

No RAID 4/5, cada bloco gravado gera 4 requisições (lê dado e paridade antigos, grava dado e paridade novos): a penalidade de escrita. Com um disco falho, leituras e gravações continuam em modo degradado. O `raid testar 2` falha o disco, lê tudo por reconstrução e confere: `Recuperando Bloco 01 via operação XOR: (Disco 1 XOR Disco 3) -> Reconstrução [OK]`.

**SSD.** Não tem cabeça nem trilhas: guarda dados em páginas, uma regravação vai para outra página (a antiga fica inválida) e só se apaga um bloco inteiro de cada vez. O simulador tem tabela de tradução, coleta de lixo e TRIM (`rm` libera a página). O **nivelamento de desgaste** espalha os apagamentos: em um simulador novo com `/a` (12 KB) e `/b` (8 KB), `ssd estresse 400` dá diferença de apagamentos entre blocos de 24 sem o nivelamento e 1 com ele, ao custo de mais cópias de página (amplificação de escrita de 1,12). Com outros arquivos no disco os valores mudam.

**Quando há E/S:** `create`, `update`, `write_file` (grava i-node e dados) e `read` (lê i-node e primeiro bloco). `ls`, `tree`, `cd` e `mkdir` não geram E/S, e `rm` só marca blocos livres.

## 6. Etapa 3: Segurança e Proteção

O simulador **inicia como `root` em MODO_KERNEL** (senha inicial `root`), para as etapas anteriores continuarem funcionando; `login` troca de usuário. Operações críticas exigem MODO_KERNEL, o acesso a arquivos depende de permissões, o conteúdo pode ser cifrado antes de ir para o bloco e cada bloco tem um hash de integridade.

| Comando | Função |
|---|---|
| `login --user <nome>`, `logout`, `whoami` | sessão (a senha é pedida sem eco, com `*`) |
| `sudo <comando>` | executa um comando como root (só o grupo `admin`; pede a senha do usuário) |
| `useradd <nome> --group <grupo>`, `groupadd`, `usermod <usuário> --add-group <grupo>`, `passwd [usuário]`, `users` | contas e grupos |
| `chmod <octal> <caminho>`, `chown <usuário>[:<grupo>] <caminho>`, `stat <caminho>` | permissões e propriedade |
| `read_file <caminho> [--key "<chave>"]`, `encrypt_file`, `decrypt_file` (`--key "<chave>"`) | conteúdo e criptografia |
| `antivirus_scan [caminho]`, `antivirus_sig [add <texto>]`, `adulterar_bloco <arquivo> [--bloco N]`, `audit_log [n]` | integridade e auditoria |

**Modos.** São críticos (só em MODO_KERNEL): `fs_status`, `io`, `raid`, `ssd`, `meio`, `useradd`, `groupadd`, `usermod`, `users`, `chown`, `audit_log`, `antivirus_*` e `adulterar_bloco`. Fora do modo kernel, a tentativa é recusada, mostrada na tela e registrada no log. O prompt de quem não é root mostra o usuário (`carlos:/ >`), e `logout` deixa a sessão vazia até um novo `login`.

**Contas.** As senhas são guardadas como SHA-256 com sal (2000 rodadas), nunca em texto. Cada usuário tem um grupo principal e até 4 extras, e ganha o diretório `/home/<nome>`.

**Permissões (ACL).** Cada arquivo e diretório tem proprietário, grupo e permissões `rwx` para dono, grupo e outros (como `rwxr-x---`, `750`); o root ignora a verificação. Ler um arquivo exige `r`, gravar exige `w`, criar ou remover entradas exige `w` no diretório pai, `ls` exige `r` e `cd` exige `x`. Arquivos novos nascem com `644` (diretórios, `755`).

```
carlos:/ > read_file /sistema/config.sys
[SISTEMA DE SEGURANÇA - VERIFICAÇÃO DE ACL]
Objeto : /sistema/config.sys
Proprietário: root (Grupo: admin)
Permissões : rwxr-x--- (750)
Ação Pedida : LEITURA por 'carlos' (Grupo: devs)

[NEGADO]: O usuário 'carlos' não possui permissão de leitura no objeto '/sistema/config.sys'.
Tentativa não autorizada registrada nos logs do sistema.
```

**Criptografia.** `encrypt_file` cifra o arquivo com uma cifra de fluxo (SHA-256 em modo contador) e uma chave derivada da senha digitada e de um sal próprio do arquivo; só o texto cifrado vai para os blocos, e o simulador guarda apenas um verificador da chave. `read_file --key` decifra só na memória, e `write_file` e `update` ficam bloqueados enquanto o arquivo estiver cifrado.

**Integridade.** Toda gravação de bloco guarda o hash SHA-256 dele (o "registro oficial"). O `antivirus_scan` confere esses hashes (i-node e dados) e procura assinaturas maliciosas, que são textos inventados como `VIRUS_TESTE_SIMULADO`, nos arquivos não cifrados. O `adulterar_bloco` simula uma alteração direta no disco, sem passar pelo sistema de arquivos:

```
- /home/carlos/segredo.txt : [OK - Criptografado e Íntegro]
- /sistema/logs/app.log : [ALERTA] Hash do bloco difere do registro oficial! (bloco 02)
Resultado: Possível modificação direta não autorizada no bloco do disco.
```

O `audit_log` mostra o registro de auditoria: negações de acesso, logins (inclusive os que falham), `chmod`, `chown`, criptografia e varreduras.

## 7. Limitações

- **Etapa 1:** tudo na RAM; 16 blocos (no máximo 15 arquivos); nomes de até 63 caracteres; o `read` mostra só 16 bytes do conteúdo.
- **Etapa 2:** só o deslocamento da cabeça é simulado (sem tempo de rotação), com 200 trilhas distribuídas igualmente pelos blocos; fila de 128 requisições por disco (se encher, é atendida em silêncio); um disco falho por vez, e RAID 0 ou "sem RAID" perdem os dados (reinicie o simulador); SSD em modelo reduzido (8 blocos de apagamento de 4 páginas), sem limite de apagamentos; sem relógio simulado.
- **Etapa 3:** as permissões são conferidas no arquivo ou diretório pedido, não em cada diretório do caminho; até 8 usuários, 8 grupos e 4 grupos extras por usuário; o SHA-256 e a cifra são didáticos (não servem para uso real); o registro de auditoria guarda os últimos 64 eventos, só na memória e sem horário; a leitura de senha sem eco foi escrita para Windows (`_getch`) e Linux, e com a entrada redirecionada a senha vem da linha seguinte.
- **Comandos:** até 16 palavras; aspas duplas agrupam palavras, sem escape de aspa.
- **Windows:** o terminal precisa de UTF-8 (`chcp 65001`; no PowerShell, redirecionando a saída, use também `[Console]::OutputEncoding = [System.Text.Encoding]::UTF8`), e o `make clean` usa `rm`, que não existe no PowerShell puro.