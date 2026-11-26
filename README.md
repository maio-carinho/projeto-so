# projeto-so

Repositório para o desenvolvimento da 2ª VA de Sistemas Operacionais (BCC).

Simulador de sistema operacional em C, feito em cinco etapas que se integram. É um programa de linha de comando: tudo acontece na memória RAM e, ao sair com `exit`, o estado é perdido.

> **Manutenção:** a cada etapa, atualize a tabela abaixo, acrescente um capítulo curto antes das limitações e ajuste a estrutura do Capítulo 3.

## 1. Andamento

| Etapa | Tema | Entrega | Status |
|---|---|---|---|
| 1 | Sistema de Arquivos | 1 nov | Concluída |
| 2 | Entrada/Saída (escalonamento de disco, RAID, SSD) | 8 nov | Concluída |
| 3 | Segurança e Proteção (usuários, ACL, criptografia, antivírus) | 15 nov | Concluída |
| 4 | Gerenciamento de Memória (área reservada, paginação, swap) | 22 nov | Concluída |
| 5 | Processos e Concorrência (escalonador de CPU, syscalls, threads e mutex) | 29 nov | Em andamento |

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
├── memoria/               # processo (tabela), reservada (área contígua), paginacao (RAM dinâmica),
│                          # swap e memoria (comandos)                                [Etapa 4]
├── processos/             # execucao (relógio e estados), cpu (escalonador e métricas),
│                          # concorrencia (threads e mutex), processos (comandos)       [Etapa 5]
└── antivirus/             # antivirus: integridade dos blocos e assinaturas
```

Uma pasta só usa as pastas de baixo: `comum` e `criptografia` não dependem de nada; `armazenamento` e `seguranca` usam `comum` e `criptografia`; `sistema_arquivos` usa essas três; `antivirus` usa `sistema_arquivos`, `armazenamento` e `seguranca`; `memoria` usa `comum`, `armazenamento`, `seguranca` e `sistema_arquivos`; `processos` usa todas essas e a `memoria`; o `main` usa tudo. O `fs` se registra no `seguranca` para criar o `/home/<usuário>`, sem que o `seguranca` precise conhecer o sistema de arquivos.

- Funções que podem falhar devolvem `0` (ok) ou `-1`, com a mensagem em `reg_ultimo_erro()`; `ERRO_JA_EXIBIDO` indica que o comando já mostrou na tela o motivo da recusa.
- Todo comando recebe `(int argc, char **argv)`; novos comandos entram como uma linha na tabela `comandos` do `main.c`.
- Os `#include` levam o caminho da pasta (`#include "armazenamento/disco.h"`). Nomes de pasta não podem ter espaços, por causa do `make`. O Makefile compila `src/*.c` e `src/*/*.c` sozinho, então criar um módulo ou uma pasta não exige mexer nele.
- Prefixos: `disco_`, `fs_`, `raid_`, `escalonador_`, `ssd_`, `es_`, `seguranca_`, `cripto_`, `processo_`, `reservada_`, `paginacao_`, `swap_`, `memoria_`, `execucao_`, `cpu_`, `concorrencia_`, `processos_`, `argumento_`, `cmd_` (comandos) e `reg_` (erros). Nomes em português, sem acento.

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

## 7. Etapa 4: Gerenciamento de Memória

A RAM simulada tem duas áreas. A **Área Reservada** (4 MB, o primeiro 1 MB é o núcleo do SO) guarda processos **críticos** em blocos contíguos, sem paginação. A **RAM Dinâmica** (16 molduras de 4 KB) guarda os processos **normais**, divididos em páginas de 4 KB carregadas só quando usadas (paginação por demanda). Página que sai da RAM vai para o **swap** (128 posições de 4 KB no Disco 1), e cada leitura ou gravação de swap entra na fila do escalonador da Etapa 2 (`io fila`, `io auto on`).

| Comando | Função |
|---|---|
| `create_process --name <nome> --size <tam> [--type critical\|normal]` | cria o processo (padrão: normal); `--file <caminho>` usa o tamanho de um arquivo e exige permissão `x` (o comando está em `processos/`, veja a Etapa 5) |
| `terminate_process --name <nome>` | encerra e libera molduras, swap ou o bloco reservado |
| `memory_status [--reserved\|--virtual]`, `memory_status --process <nome>` | mapa, ocupação, fragmentações, estatísticas e tabela de páginas |
| `memory_compact --reserved` | junta as lacunas da Área Reservada |
| `translate_address --process <nome> --page <N> [--offset <bytes>]` | página lógica -> moldura física (só consulta a tabela) |
| `execute_step --process <nome> --instruction "LOAD_DATA Página <N>"` | acessa a página; `STORE_DATA` escreve nela |
| `memory_algoritmo [fifo\|lru\|segunda_chance]`, `memory_relatorio [n]` | algoritmo de troca (padrão: LRU) e intervalo do relatório periódico (padrão: 5; 0 desliga) |

**Termos.** *Moldura* é um pedaço da RAM do tamanho de uma página. *Page fault* é o acesso a uma página que não está na RAM: o sistema traz a página do swap e, se não houver moldura livre, escolhe uma *vítima* (FIFO: a mais antiga; LRU: a usada há mais tempo; Segunda Chance: FIFO que poupa uma vez a página usada recentemente). A vítima só é gravada no swap na primeira saída ou se foi modificada. *Fragmentação interna* é o espaço perdido na última página de um processo; *externa* é a memória livre dividida em lacunas que impedem um bloco contíguo grande, resolvida pela *compactação*.

**Segurança.** Criar ou encerrar processo crítico, `memory_status --reserved`, `memory_compact`, `memory_algoritmo` e `memory_relatorio` exigem MODO_KERNEL. Um processo normal só é acessado ou encerrado pelo dono (ou pelo kernel), e o acesso a página inexistente é recusado e vai para o `audit_log`.

```
> create_process --name Kernel_NetDriver --size 1024KB --type critical
- Alocado de forma contígua na Área Reservada: [1.5 MB até 2.5 MB] (1024 KB)
> execute_step --process App_Navegador --instruction "LOAD_DATA Página 2"
[PAGE FAULT DETECTADO]: Página Lógica 2 ausente na RAM!
Memória RAM cheia. Executando Substituição de Página (Algoritmo LRU):
- Removendo Página Lógica 0 do PID 100 (App_Navegador) da RAM -> Salvando no Swap (Disco) [...]
```

## 8. Etapa 5: Processos e Concorrência

Cada processo tem um **PCB** (bloco de controle: estado, prioridade, burst, tempos, syscalls, threads) e passa por **NOVO → PRONTO → EXECUTANDO → BLOQUEADO → CONCLUÍDO**. O relógio simulado anda de 1 em 1 ms. A cada ms o processo na CPU referencia uma página (HIT ou page fault, com o LRU da Etapa 4); ao terminar, devolve suas molduras e posições de swap. Syscalls de disco (`read_disk`, `write_disk`) entram na fila do escalonador de disco da Etapa 2 (`io fila`).

| Comando | Função |
|---|---|
| `create_process --name <nome> --burst <N>ms --priority <1-3> [--syscall "read_disk at <N>ms (duration: <N>ms)"\|random] [--size <tam>] [--type critical\|normal] [--arrival <N>ms]` | cria o processo com perfil de CPU e E/S (sem `--size`: 32 KB; prioridade 1 = Alta exige kernel; `--syscall` pode repetir até 3 vezes) |
| `run_simulation [--process <nome>] [--steps <N>]` | executa passo a passo, mostrando a página acessada e a tabela das molduras; sem `--process` roda o que o escalonador escolher (kernel) |
| `schedule [--algorithm Priority_RR\|RR\|FCFS] [--quantum <N>ms] [--trace]` | escalona tudo até o fim, mostra a linha do tempo e as métricas (kernel) |
| `metrics` / `process_status` | métricas (WT, TAT, resposta, trocas de contexto, CPU) / estados, threads e mutexes |
| `create_thread --process <p> --name <t>` | cria uma thread no processo |
| `mutex_lock` / `mutex_unlock --process <p> --thread <t> --mutex <m>` | trava/libera; thread que encontra o mutex ocupado vai para BLOQUEADO e a primeira da fila o recebe no `unlock` |

**Termos.** *Burst* é o tempo de CPU que o processo precisa. *Syscall* é um pedido de E/S ao sistema: no instante marcado (contado em ms de CPU já usados) o processo vira BLOQUEADO pela duração dada. *Quantum* é a fatia de tempo do Round-Robin. Em `Priority_RR`, a fila de maior prioridade sempre vai primeiro, quem tem a mesma prioridade reveza por quantum, e um processo de prioridade maior que fica pronto toma a CPU (preempção). *WT* é o tempo esperando na fila de prontos; *TAT* é do chegar ao terminar (burst + WT + tempo em E/S). *Mutex* garante que só uma thread por vez entre na região crítica.

**Segurança.** `schedule`, `run_simulation` sem `--process` e prioridade 1 exigem MODO_KERNEL; só o dono (ou o kernel) executa um processo ou usa suas threads; criar processo crítico continua só para o kernel.

```
> schedule --algorithm Priority_RR --quantum 2ms
[ 0ms -  2ms] Executando PID 101 (A) | Quantum 2ms expirou -> Volta p/ Fila P1
[ 4ms -  5ms] Executando PID 101 (A) | Dispara E/S de Disco -> Estado BLOQUEADO (até t=9ms)
```

## 9. Limitações

- **Etapa 1:** tudo na RAM; 16 blocos (no máximo 15 arquivos); nomes de até 63 caracteres; o `read` mostra só 16 bytes do conteúdo.
- **Etapa 2:** só o deslocamento da cabeça é simulado (sem tempo de rotação), com 200 trilhas distribuídas igualmente pelos blocos; fila de 128 requisições por disco (se encher, é atendida em silêncio); um disco falho por vez, e RAID 0 ou "sem RAID" perdem os dados (reinicie o simulador); SSD em modelo reduzido (8 blocos de apagamento de 4 páginas), sem limite de apagamentos; sem relógio simulado.
- **Etapa 3:** as permissões são conferidas no arquivo ou diretório pedido, não em cada diretório do caminho; até 8 usuários, 8 grupos e 4 grupos extras por usuário; o SHA-256 e a cifra são didáticos (não servem para uso real); o registro de auditoria guarda os últimos 64 eventos, só na memória e sem horário; a leitura de senha sem eco foi escrita para Windows (`_getch`) e Linux, e com a entrada redirecionada a senha vem da linha seguinte.
- **Etapa 4:** o swap guarda só o controle das páginas (sem conteúdo); a Área Reservada usa first-fit (primeiro bloco livre que couber) e blocos em KB inteiros; até 16 processos, 64 páginas cada, e o total de páginas não passa das 128 posições do swap; cada acesso avança o relógio simulado em 5 ms; processo crítico não é paginado nem usa o swap.
- **Etapa 5:** uma só CPU; a página acessada em cada ms é sorteada (com localidade) com sequência sempre igual, e as syscalls `random` também; threads só se movem pelos comandos `mutex_lock`/`mutex_unlock` (não há escalonamento de threads), e o bloqueio por mutex vale para a thread, não para o processo; PIDs começam em 101; no máximo 3 syscalls, 4 threads e 4 mutexes por processo.
- **Comandos:** até 16 palavras; aspas duplas agrupam palavras, sem escape de aspa.
- **Windows:** o terminal precisa de UTF-8 (`chcp 65001`; no PowerShell, redirecionando a saída, use também `[Console]::OutputEncoding = [System.Text.Encoding]::UTF8`), e o `make clean` usa `rm`, que não existe no PowerShell puro.