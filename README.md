# projeto-so

Repositório para o desenvolvimento da 2ª VA de Sistemas Operacionais (Turma de BCC).

O projeto é um **simulador de sistema operacional em C**, construído em cinco etapas. Cada etapa acrescentará um módulo e se integra às anteriores.

---

## Sumário

1. [Visão geral e andamento](#1-visão-geral-e-andamento)
2. [Como compilar e executar](#2-como-compilar-e-executar)
3. [Estrutura do projeto e convenções](#3-estrutura-do-projeto-e-convenções)
4. [Etapa 1: Sistema de Arquivos](#4-etapa-1-sistema-de-arquivos)
5. [Limitações conhecidas](#5-limitações-conhecidas)

---

## 1. Visão geral e andamento

O simulador é um programa de linha de comando: o usuário digita comandos e o programa mostra o que o "SO" faria. Nada é gravado de verdade no computador, pois tudo acontece na memória RAM enquanto o programa roda. Ao sair com `exit`, o estado é perdido.

| Etapa | Tema | Entrega | Status |
|---|---|---|---|
| 1 | Sistema de Arquivos (FS) | 1 nov | Concluída |
| 2 | Entrada/Saída (escalonamento de disco, RAID) | 8 nov | Pendente |
| 3 | Segurança e Proteção | 15 nov | Pendente |
| 4 | Gerenciamento de Memória | 22 nov | Pendente |
| 5 | Processos e Concorrência | 29 nov | Pendente |

---

## 2. Como compilar e executar

**Requisitos:** um compilador C (`gcc`) e o `make`. Este projeto foi desenvolvido no Windows com o MinGW (gcc 6.3.0), mas compila também em Linux e no WSL.

### Windows (PowerShell, com MinGW)

```powershell
mingw32-make               # compila e gera fs_simulator.exe
chcp 65001                 # terminal em UTF-8 (acentos e caracteres da árvore)
.\fs_simulator.exe         # executa
```

---

## 3. Estrutura do projeto e convenções

```
projeto-so/
├── Makefile
├── README.md
├── .gitignore
└── src/
    ├── main.c          # laço do shell e tabela de comandos
    ├── utils.h / .c    # constantes e registro de erros, usados por todos os módulos
    ├── disco.h / .c    # camada física: disco dividido em blocos
    └── fs.h / .c       # Etapa 1: árvore de diretórios, CRUD, alocação indexada
```

### Como os módulos se relacionam

Cada camada só chama a de baixo:

```
shell (main.c)  ──►  fs (árvore, i-nodes)  ──►  disco (blocos)
                          └───────────────┬──────────┘
                                   utils (constantes e erros)
```

Um **módulo** é um par de arquivos: o `.h` (header) declara *o que* o módulo oferece, e o `.c` contém *como* ele faz. Para acrescentar comandos de uma etapa nova, inclua o `.h` do módulo em `main.c` e adicione uma linha na tabela `comandos`.

### O que cada módulo oferece

| Módulo | Funções públicas | Papel |
|---|---|---|
| `utils` | `reg_erro`, `reg_ultimo_erro` e as constantes `TAM_BLOCO`, `TOTAL_BLOCOS`, `TAM_NOME`, `TAM_PATH` | registrar e consultar a última mensagem de erro; valores compartilhados |
| `disco` | `disco_init`, `disco_alocar_bloco`, `disco_liberar_bloco`, `disco_livre`, `livres`, `disco_ler_bloco`, `disco_gravar_bloco` | reservar, liberar, ler e gravar blocos. Não sabe o que é arquivo |
| `fs` | `fs_init`, `fs_shutdown`, `fs_cwd_path` e os comandos `cmd_mkdir`, `cmd_create`, `cmd_ls`, `cmd_cd`, `cmd_tree`, `cmd_read`, `cmd_update`, `cmd_rm`, `cmd_fs_status` | árvore de diretórios e arquivos; dá significado aos blocos |

### Convenções de código

- **Retorno das funções:** as que podem falhar devolvem `0` em caso de sucesso e `-1` em caso de erro. A mensagem fica disponível em `reg_ultimo_erro()`.
- **Assinatura dos comandos:** todos recebem `(int argc, char **argv)`, ou seja, os argumentos digitados sem o nome do comando.
- **Prefixos:** `disco_` para o módulo de disco, `fs_` para a interface pública do sistema de arquivos, `cmd_` para comandos do usuário e `reg_` para o registro de erros.
- **Nomes em português**, sem acentos nos identificadores. Os textos exibidos ao usuário podem ter acentos.
- **Textos dos comandos digitados** (`mkdir`, `create`, `fs_status`...) seguem os exemplos do enunciado.

---

## 4. Etapa 1: Sistema de Arquivos

### 4.1 O que foi implementado

- **Hierarquia em árvore** de diretórios e arquivos, com caminhos absolutos (`/sistema/logs/app.log`) e relativos (`logs/app.log`, `..`, `.`).
- **CRUD:** criar, ler, atualizar e deletar diretórios e arquivos.
- **Disco simulado** com 16 blocos de 4 KB (64 KB no total).
- **Alocação indexada (i-node)** como estratégia de alocação física.
- **Diagnóstico do disco:** mapa de ocupação, fragmentação interna, fragmentação externa e indicação de necessidade de compactação.

### 4.2 Comandos

| Comando | Função |
|---|---|
| `mkdir <caminho>` | cria um diretório |
| `create <caminho> --size <tam>` | cria um arquivo (tamanhos: `512B`, `10KB`, `1MB`; maiúsculas ou minúsculas) |
| `ls [caminho]` | lista o conteúdo de um diretório |
| `cd [caminho]` | muda o diretório atual (sem argumento, vai para `/`) |
| `tree` | mostra a árvore completa |
| `read <arquivo>` | lê o i-node do disco e mostra o começo do conteúdo |
| `update <arquivo> --size <tam>` | muda o tamanho do arquivo (libera e realoca os blocos) |
| `rm <caminho> [-r]` | remove arquivo ou diretório (`-r` para diretório com conteúdo) |
| `fs_status` | mapa do disco e relatório de fragmentação |
| `help` / `exit` (ou `quit`) | ajuda / sair |

O comando `fs_status` aceita o argumento `--details` (como no enunciado), mas o relatório detalhado é sempre exibido.

### 4.3 Como funciona a alocação indexada

Cada arquivo usa **1 bloco de i-node + N blocos de dados**. O i-node é uma "tabela de índices": ele guarda a lista de quais blocos contêm os dados do arquivo. Por isso os blocos de dados **não precisam ser vizinhos** e podem ficar espalhados pelo disco.

Exemplo: um arquivo de 10 KB, com blocos de 4 KB, precisa de 3 blocos de dados (4 KB + 4 KB + 2 KB) mais 1 bloco de i-node.

```
Bloco 01 [i-node]  ──►  lista: [2, 3, 4]
Bloco 02 [dado 1]   4096 bytes
Bloco 03 [dado 2]   4096 bytes
Bloco 04 [dado 3]   2048 bytes (o restante do bloco fica desperdiçado)
```

O bloco 0 é sempre reservado ao i-node do diretório raiz. Diretórios comuns não ocupam blocos no disco, apenas existem na árvore em memória. Um arquivo de tamanho 0 ocupa só o bloco do i-node. Como o bloco 0 pertence à raiz, sobram **15 blocos** para os arquivos.

O conteúdo dos arquivos é **simulado**: o `create` grava letras `a` a `z` repetidas, só para que os blocos contenham bytes reais. Isso prepara as etapas seguintes (paridade do RAID, criptografia e verificação de integridade, que precisam do conteúdo dos blocos).

### 4.4 Diagnóstico do disco

- **Fragmentação interna:** espaço desperdiçado *dentro* do último bloco de um arquivo. No exemplo acima, o arquivo de 10 KB ocupa 12 KB e desperdiça 2 KB.
- **Fragmentação externa:** blocos livres espalhados e pequenos demais para abrigar um arquivo contíguo. **Na alocação indexada ela é sempre 0**, porque qualquer bloco livre serve. O relatório mostra os trechos livres apenas para visualização.
- **Compactação:** reorganizar o disco para juntar o espaço livre. Na alocação indexada ela nunca é necessária, e o relatório sempre indica `NÃO`.

### 4.5 Exemplo de uso

```
/ > mkdir /sistema
/ > mkdir /sistema/logs
/ > create /sistema/logs/app.log --size 10KB
/ > tree
[ÁRVORE DO SISTEMA DE ARQUIVOS]
/
└── sistema/ [DIR]
    └── logs/ [DIR]
        └── app.log (10 KB) [ARQ]
/ > fs_status
Tamanho do Bloco de Disco: 4 KB | Total de Blocos: 16 (64 KB) | Alocação: Indexada

[MAPA VISUAL]  [R] [a] [A] [A] [A] [·] [·] [·] [·] [·] [·] [·] [·] [·] [·] [·]
   A = /sistema/logs/app.log

[MAPA DE ALOCAÇÃO DO DISCO]
Bloco 00     : [INODE_RAIZ] : Diretório '/'
Bloco 01     : [INODE] : Arquivo '/sistema/logs/app.log'
Bloco 02     : [DADO]  : '/sistema/logs/app.log' (Bloco 1 - 4096 bytes ocupados)
Bloco 03     : [DADO]  : '/sistema/logs/app.log' (Bloco 2 - 4096 bytes ocupados)
Bloco 04     : [DADO]  : '/sistema/logs/app.log' (Bloco 3 - 2048 bytes ocupados)
Bloco 05..15 : [LIVRE]

--- RELATÓRIO DE FRAGMENTAÇÃO ---
'/sistema/logs/app.log': real 10240 B | alocado 12288 B (3 bloco(s) de dados) | frag. interna 2048 B (17%)
Fragmentação Interna (total) : 2048 bytes
Fragmentação Externa         : 0 bytes (1 trecho(s) livre(s): [(5, 11)])
Necessidade de Compactação   : NÃO
```

No **mapa visual**: `R` é o i-node da raiz, letra minúscula é o i-node de um arquivo, letra maiúscula são os blocos de dados desse arquivo e `·` é bloco livre.

Para ver o i-node sendo lido do disco:

```
/ > read /sistema/logs/app.log
Arquivo: /sistema/logs/app.log | 10240 bytes
  i-node (lido do bloco 01): [2, 3, 4]
  conteúdo (início): "abcdefghijklmnop"
```

Para ver a vantagem da alocação indexada (blocos espalhados), crie `/a` e `/b` com 8 KB cada, apague o `/a` e crie `/c` com 20 KB: o `read /c` mostra `[2, 3, 7, 8, 9]`, pois o arquivo ocupa os buracos deixados pelo `/a` e continua depois do `/b`, sem qualquer fragmentação externa.

### 4.6 Decisões de projeto

- **Disco separado do FS** (`disco.c`): o disco só sabe reservar, liberar, ler e gravar blocos. Quem dá significado a eles (arquivo, i-node) é o `fs.c`. Na Etapa 2, o escalonador de disco e o RAID entram por trás da interface do disco, sem alterar o FS.
- **`update` seguro:** antes de liberar os blocos antigos, o comando confere se o novo tamanho cabe. Se não couber, nada é alterado.
- **Quem é dono de cada bloco** é descoberto pelo FS, percorrendo a árvore na hora de montar o `fs_status`. O disco guarda apenas se o bloco está livre ou ocupado.
- **`rm` de um diretório** só funciona com `-r` se ele não estiver vazio, e, se o usuário estiver dentro dele, o diretório atual volta para o pai.

### 4.7 Roteiro de testes manuais

Cada linha abaixo é um cenário. Reinicie o programa entre os cenários, pois o disco tem só 16 blocos.

| Cenário | Comandos | Resultado esperado |
|---|---|---|
| Exemplos do enunciado | `mkdir`, `create ... --size 10KB`, `tree`, `fs_status` | mapa `[R] [a] [A] [A] [A]`, frag. interna 2048 B, externa 0, compactação NÃO |
| Caminhos relativos | `cd`, `mkdir b`, `create b/f.txt`, `cd ..`, `cd ../a/b` | o prompt acompanha o diretório; `read f.txt` mostra `[2]` |
| Atualizar | `update` para maior, menor e maior que o disco | o último falha com `Disco cheio` e **não altera** o arquivo |
| Blocos espalhados | criar `/a`, `/b`, apagar `/a`, criar `/c` de 20 KB | `read /c` mostra `[2, 3, 7, 8, 9]` |
| Disco cheio exato | `create /grande --size 56KB` (14 blocos + i-node) | cabe; qualquer arquivo a mais dá `Disco cheio` |
| Tamanhos extremos | `--size 0` e `--size 1B` | tamanho 0 usa só o i-node; 1 B desperdiça 4095 B (100%) |
| Remover recursivo | `rm /p -r` estando dentro de `/p/q` | prompt volta para `/`; todos os blocos são liberados |
| Erros | nome repetido, tamanho inválido, caminho inexistente, `cd` em arquivo, `rm /` | uma mensagem clara de erro para cada caso |

---

## 5. Limitações conhecidas

**Etapa 1**

- Tudo vive na memória RAM: ao sair do simulador (`exit`), nada é salvo.
- O disco tem só 16 blocos, então cabem no máximo 15 arquivos (cada um usa pelo menos o bloco do i-node).
- Nomes têm até 63 caracteres, e cada comando aceita até 8 palavras.
- Os comandos não aceitam aspas, então nomes com espaço não são suportados.
- O conteúdo dos arquivos é gerado automaticamente; ainda não é possível gravar texto escolhido pelo usuário.
- No Windows, o terminal precisa estar em UTF-8 (`chcp 65001`) para exibir os acentos e a árvore corretamente. Ao redirecionar a saída no PowerShell, defina também `[Console]::OutputEncoding = [System.Text.Encoding]::UTF8`.
- O alvo `make clean` usa `rm`, que não existe no PowerShell puro.