# projeto-so

Repositório para o desenvolvimento da 2ª VA de Sistemas Operacionais (BCC).

Simulador de sistema operacional em C, feito em cinco etapas que se integram. É um programa de linha de comando: tudo acontece na memória RAM e, ao sair com `exit`, o estado é perdido.

> **Manutenção:** a cada etapa, atualize a tabela abaixo, acrescente um capítulo curto antes das limitações e ajuste a estrutura do Capítulo 3.

## 1. Andamento

| Etapa | Tema | Entrega | Status |
|---|---|---|---|
| 1 | Sistema de Arquivos | 1 nov | Concluída |
| 2 | Entrada/Saída (escalonamento de disco, RAID, SSD) | 8 nov | Concluída |
| 3 | Segurança e Proteção | 15 nov | Pendente |
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
├── main.c          # shell: lê o comando e chama a função certa
├── utils.h / .c    # constantes e registro de erros
├── disco.h / .c    # blocos lógicos (livre/ocupado) sobre o meio escolhido
├── fs.h / .c       # Etapa 1: árvore, CRUD, alocação indexada
├── raid.h / .c     # Etapa 2: discos físicos, paridade, falhas
├── escalonador.h / .c  # Etapa 2: fila e algoritmos do braço do disco
├── ssd.h / .c      # Etapa 2: páginas flash, coleta de lixo, desgaste
└── es.h / .c       # Etapa 2: comandos io, raid, ssd e meio
```

Cada camada chama só a de baixo: `main → fs → disco → (raid → escalonador | ssd)`; o `main` também chama o `es`.

- Funções que podem falhar devolvem `0` (ok) ou `-1`, com a mensagem em `reg_ultimo_erro()`.
- Todo comando recebe `(int argc, char **argv)`; novos comandos entram como uma linha na tabela `comandos` do `main.c`.
- Prefixos: `disco_`, `fs_`, `raid_`, `escalonador_`, `ssd_`, `es_`, `cmd_` (comandos) e `reg_` (erros). Nomes em português, sem acento.

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

## 6. Limitações

- **Etapa 1:** tudo na RAM; 16 blocos (no máximo 15 arquivos); nomes de até 63 caracteres; o `read` mostra só 16 bytes do conteúdo.
- **Etapa 2:** só o deslocamento da cabeça é simulado (sem tempo de rotação), com 200 trilhas distribuídas igualmente pelos blocos; fila de 128 requisições por disco (se encher, é atendida em silêncio); um disco falho por vez, e RAID 0 ou "sem RAID" perdem os dados (reinicie o simulador); SSD em modelo reduzido (8 blocos de apagamento de 4 páginas), sem limite de apagamentos; sem relógio simulado.
- **Comandos:** até 16 palavras; aspas duplas agrupam palavras, sem escape de aspa.
- **Windows:** o terminal precisa de UTF-8 (`chcp 65001`; no PowerShell, redirecionando a saída, use também `[Console]::OutputEncoding = [System.Text.Encoding]::UTF8`), e o `make clean` usa `rm`, que não existe no PowerShell puro.