#include <stdio.h>
#include <string.h>
#include "memoria/memoria.h"
#include "processos/argumentos.h"
#include "processos/concorrencia.h"
#include "processos/execucao.h"

static int indice_da_thread(const Processo *processo, const char *nome) {
    for (int i = 0; i < MAX_THREADS_POR_PROCESSO; i++)
        if (processo->threads[i].em_uso && strcmp(processo->threads[i].nome, nome) == 0) return i;
    return -1;
}

static int indice_do_mutex(const Processo *processo, const char *nome) {
    for (int i = 0; i < MAX_MUTEXES_POR_PROCESSO; i++)
        if (processo->mutexes[i].em_uso && strcmp(processo->mutexes[i].nome, nome) == 0) return i;
    return -1;
}

static int obter_processo(int argc, char **argv, Processo **processo) {
    const char *nome = argumento_valor(argc, argv, "--process");
    if (!nome) return reg_erro("Informe o processo: --process <nome>");
    *processo = processo_por_nome(nome);
    if (!*processo) return reg_erro("Processo '%s' não encontrado.", nome);
    if ((*processo)->estado == ESTADO_CONCLUIDO)
        return reg_erro("O processo '%s' já está CONCLUÍDO.", nome);
    return memoria_exigir_dono_ou_kernel(*processo, "usar threads do processo");
}

int cmd_create_thread(int argc, char **argv) {
    Processo *processo;
    int negado = obter_processo(argc, argv, &processo);
    if (negado != 0) return negado;

    const char *nome = argumento_valor(argc, argv, "--name");
    if (!nome || nome[0] == '\0') return reg_erro("Uso: create_thread --process <processo> --name <thread>");
    if (strlen(nome) >= TAM_NOME) return reg_erro("Nome de thread muito longo.");
    if (indice_da_thread(processo, nome) >= 0) return reg_erro("O processo já tem uma thread '%s'.", nome);

    for (int i = 0; i < MAX_THREADS_POR_PROCESSO; i++) {
        if (processo->threads[i].em_uso) continue;
        Thread *thread = &processo->threads[i];
        thread->em_uso = 1;
        snprintf(thread->nome, sizeof thread->nome, "%s", nome);
        thread->estado = ESTADO_PRONTO;
        thread->mutex_esperado = -1;
        printf("[THREAD CRIADA] %s em PID %d (%s) | Estado Inicial: PRONTO\n", nome, processo->identificador, processo->nome);
        return 0;
    }
    return reg_erro("O processo já tem o máximo de %d threads.", MAX_THREADS_POR_PROCESSO);
}

static void colocar_em_execucao(Processo *processo, int indice) {
    for (int i = 0; i < MAX_THREADS_POR_PROCESSO; i++)
        if (processo->threads[i].em_uso && processo->threads[i].estado == ESTADO_EXECUTANDO)
            processo->threads[i].estado = ESTADO_PRONTO;
    processo->threads[indice].estado = ESTADO_EXECUTANDO;
}

static int forma_deadlock(const Processo *processo, int thread, int mutex) {
    int atual = processo->mutexes[mutex].dono;
    for (int passos = 0; atual >= 0 && passos < MAX_THREADS_POR_PROCESSO; passos++) {
        if (atual == thread) return 1;
        int esperado = processo->threads[atual].mutex_esperado;
        if (esperado < 0) return 0;
        atual = processo->mutexes[esperado].dono;
    }
    return 0;
}

int cmd_mutex_lock(int argc, char **argv) {
    Processo *processo;
    int negado = obter_processo(argc, argv, &processo);
    if (negado != 0) return negado;

    const char *nome_thread = argumento_valor(argc, argv, "--thread");
    const char *nome_mutex = argumento_valor(argc, argv, "--mutex");
    if (!nome_thread || !nome_mutex)
        return reg_erro("Uso: mutex_lock --process <processo> --thread <thread> --mutex <mutex>");

    int thread = indice_da_thread(processo, nome_thread);
    if (thread < 0) return reg_erro("Thread '%s' não existe (use create_thread).", nome_thread);
    if (processo->threads[thread].estado == ESTADO_BLOQUEADO)
        return reg_erro("A thread '%s' está BLOQUEADA aguardando um mutex e não pode executar.", nome_thread);

    int mutex = indice_do_mutex(processo, nome_mutex);
    if (mutex < 0) {
        for (int i = 0; i < MAX_MUTEXES_POR_PROCESSO && mutex < 0; i++) {
            if (processo->mutexes[i].em_uso) continue;
            Mutex *novo = &processo->mutexes[i];
            memset(novo, 0, sizeof *novo);
            novo->em_uso = 1;
            snprintf(novo->nome, sizeof novo->nome, "%s", nome_mutex);
            novo->dono = -1;
            mutex = i;
        }
        if (mutex < 0) return reg_erro("O processo já tem o máximo de %d mutexes.", MAX_MUTEXES_POR_PROCESSO);
    }

    Mutex *m = &processo->mutexes[mutex];
    colocar_em_execucao(processo, thread);
    printf("[t=%ldms] Thread %s (PID %d) tenta acessar Região Crítica ('%s').\n",
           execucao_tempo_atual(), nome_thread, processo->identificador, nome_mutex);

    if (m->dono == thread)
        return reg_erro("Thread '%s' já possui o mutex '%s' (o mutex não é recursivo).", nome_thread, nome_mutex);

    if (m->dono < 0) {
        m->dono = thread;
        printf("Mutex [LIVRE] -> %s adquire o Mutex [BLOQUEADO].\n", nome_thread);
        return 0;
    }

    printf("Mutex [BLOQUEADO por %s]!\n", processo->threads[m->dono].nome);
    if (forma_deadlock(processo, thread, mutex))
        printf("[DEADLOCK DETECTADO]: há um ciclo de espera (cada thread segura o que a outra precisa); "
               "elas ficarão bloqueadas para sempre.\n");
    m->fila_espera[m->quantidade_espera++] = thread;
    processo->threads[thread].estado = ESTADO_BLOQUEADO;
    processo->threads[thread].mutex_esperado = mutex;
    printf("- Mudança de Estado de %s: EXECUTANDO -> BLOQUEADO (Aguardando Mutex).\n", nome_thread);
    return 0;
}

int cmd_mutex_unlock(int argc, char **argv) {
    Processo *processo;
    int negado = obter_processo(argc, argv, &processo);
    if (negado != 0) return negado;

    const char *nome_thread = argumento_valor(argc, argv, "--thread");
    const char *nome_mutex = argumento_valor(argc, argv, "--mutex");
    if (!nome_thread || !nome_mutex)
        return reg_erro("Uso: mutex_unlock --process <processo> --thread <thread> --mutex <mutex>");

    int thread = indice_da_thread(processo, nome_thread);
    int mutex = indice_do_mutex(processo, nome_mutex);
    if (thread < 0) return reg_erro("Thread '%s' não existe.", nome_thread);
    if (mutex < 0) return reg_erro("Mutex '%s' não existe neste processo.", nome_mutex);

    Mutex *m = &processo->mutexes[mutex];
    if (m->dono != thread)
        return reg_erro("A thread '%s' não é a dona do mutex '%s'.", nome_thread, nome_mutex);

    printf("[t=%ldms] Thread %s (PID %d) libera o Mutex '%s'.\n", execucao_tempo_atual(), nome_thread,
           processo->identificador, nome_mutex);
    if (m->quantidade_espera == 0) {
        m->dono = -1;
        printf("Mutex [BLOQUEADO] -> [LIVRE].\n");
        return 0;
    }

    int proxima = m->fila_espera[0];
    memmove(&m->fila_espera[0], &m->fila_espera[1], (size_t)(m->quantidade_espera - 1) * sizeof(int));
    m->quantidade_espera--;
    m->dono = proxima;
    processo->threads[proxima].estado = ESTADO_PRONTO;
    processo->threads[proxima].mutex_esperado = -1;
    printf("Mutex passa para %s: BLOQUEADO -> PRONTO (adquiriu o Mutex).\n", processo->threads[proxima].nome);
    return 0;
}

void concorrencia_imprimir(const Processo *processo) {
    int tem_algo = 0;
    for (int i = 0; i < MAX_THREADS_POR_PROCESSO; i++) {
        const Thread *t = &processo->threads[i];
        if (!t->em_uso) continue;
        if (!tem_algo) printf("    Threads de PID %d:", processo->identificador);
        tem_algo = 1;
        printf(" %s [%s", t->nome, execucao_nome_estado(t->estado));
        if (t->mutex_esperado >= 0) printf(": espera '%s'", processo->mutexes[t->mutex_esperado].nome);
        printf("]");
    }
    if (tem_algo) printf("\n");

    for (int i = 0; i < MAX_MUTEXES_POR_PROCESSO; i++) {
        const Mutex *m = &processo->mutexes[i];
        if (!m->em_uso) continue;
        if (m->dono >= 0)
            printf("    Mutex '%s': BLOQUEADO por %s (%d em espera)\n", m->nome, processo->threads[m->dono].nome, m->quantidade_espera);
        else
            printf("    Mutex '%s': LIVRE\n", m->nome);
    }
}