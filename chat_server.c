/*
 * Linux/POSIX, C11. Uma thread por conexão; contagem local, sem broadcast.
 * Uso: ./chat_server PORTA CLIENTES SEGUNDOS [BLOCO] [AQUECIMENTO] [IP_BIND]
 */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define MAGIC UINT32_C(0x54435031) // "TCP1"
#define DONE  UINT32_C(0x444f4e45) // "DONE"
#define MAX_CLIENTS 100

/* Estrutura compartilhada para gerenciar o estado global do experimento */
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int ready, go;         /* Contadores de prontidão e flag de disparo */
    double begin, end;     /* Timestamps absolutos de início e fim da medição */
    size_t block;          /* Tamanho do bloco de transferência */
} Experiment;

/* Estrutura de dados individual para cada cliente conectado */
typedef struct {
    int fd, error;
    unsigned id;
    Experiment *experiment;
    unsigned char *buffer;
    uint64_t bytes, calls, warmup_bytes;
    int receive_buffer;    /* Tamanho do buffer SO_RCVBUF do socket */
} Client;

/* Função auxiliar para tratamento de erros fatais */
static void die(const char *message) { 
    perror(message); exit(EXIT_FAILURE); 
}

/* Converte argumentos numéricos de linha de comando */
static unsigned number(const char *s, unsigned lo, unsigned hi) {
    char *end;
    errno = 0;
    unsigned long n = strtoul(s, &end, 10);

    if (errno || !*s || *s == '-' || *end || n < lo || n > hi) {
        fprintf(stderr, "Parametro fora do intervalo: %s\n", s);
        exit(EXIT_FAILURE);
    }

    return (unsigned)n;
}

/* Retorna o tempo monotônico atual de alta precisão em segundos */
static double now(void) {
    struct timespec t;

    if (clock_gettime(CLOCK_MONOTONIC, &t)) die("clock_gettime");

    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* Dorme de forma precisa até um instante de tempo absoluto (deadline) */
static void sleep_until(double deadline) {
    struct timespec t = {(time_t)deadline, 0};
    t.tv_nsec = (long)((deadline - (double)t.tv_sec) * 1e9);
    int rc;

    do { 
        rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL); 
    } while (rc == EINTR);

    if (rc) { 
        errno = rc; die("clock_nanosleep"); 
    }
}

/* Garante a transferência completa (envio ou recebimento) de N bytes via TCP */
static int transfer_all(int fd, void *buffer, size_t size, int sending) {
    size_t offset = 0;

    while (offset < size) {
        ssize_t n = sending
            ? send(fd, (char *)buffer + offset, size - offset, MSG_NOSIGNAL)
            : recv(fd, (char *)buffer + offset, size - offset, 0);

        if (n < 0 && errno == EINTR) continue;

        if (n <= 0) return -1;

        offset += (size_t)n;
    }

    return 0;
}

/* Converte struct timeval para segundos em ponto flutuante */
static double seconds(struct timeval t) {
    return (double)t.tv_sec + (double)t.tv_usec / 1e6;
}

/* Função executada por cada thread de cliente */
static void *receive_data(void *argument) {
    Client *c = argument;
    Experiment *e = c->experiment;
    uint64_t bytes = 0, calls = 0, warmup = 0;
    int error = 0;

    /* Sincronização: avisa que está pronto e aguarda o sinal global de início */
    pthread_mutex_lock(&e->mutex);
    ++e->ready;
    pthread_cond_broadcast(&e->cond);

    while (!e->go) pthread_cond_wait(&e->cond, &e->mutex);

    double begin = e->begin, end = e->end;
    pthread_mutex_unlock(&e->mutex);

    /* Autoriza o cliente a comecar enviando o caractere 'G' */
    unsigned char go = 'G';
    if (transfer_all(c->fd, &go, 1, 1)) error = 1;
    
    /* Loop de recepção de dados durante o período do teste */
    while (!error) {
        if (now() >= end) break;

        ssize_t n = recv(c->fd, c->buffer, e->block, 0);
        double received_at = now();

        if (received_at >= end) break;

        if (n > 0) {
            /* Separa dados recebidos na fase de aquecimento dos dados válidos */
            if (received_at >= begin) {
                bytes += (uint64_t)n;
                ++calls;
            } 
            else {
                warmup += (uint64_t)n;
            }
        } 
        else if (n == 0) {
            error = 1; /* Desconexão prematura (EOF) anula a execução */
        } 
        else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            error = 1;
        }
    }

    /* Publica métricas locais na estrutura do cliente */
    c->bytes = bytes;
    c->calls = calls;
    c->warmup_bytes = warmup;
    c->error = error || bytes == 0;
    
    /* Envia o pacote de confirmação DONE com os resultados ao cliente */
    uint32_t response[4] = {
        htonl(DONE), htonl((uint32_t)c->error),
        htonl((uint32_t)(bytes >> 32)), htonl((uint32_t)bytes)
    };

    if (transfer_all(c->fd, response, sizeof(response), 1)) c->error = 1;

    shutdown(c->fd, SHUT_WR);

    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 4 || argc > 7) {
        fprintf(stderr, "Use: %s PORTA CLIENTES SEGUNDOS [BLOCO] "
                        "[AQUECIMENTO] [IP_BIND]\n", argv[0]);

        return EXIT_FAILURE;
    }

    unsigned port = number(argv[1], 1, 65535);
    unsigned count = number(argv[2], 1, 10);
    unsigned duration = number(argv[3], 2, 3600);
    unsigned block = argc > 4 ? number(argv[4], 1024, 4194304) : 65536;
    unsigned warmup = argc > 5 ? number(argv[5], 1, 60) : 1;
    const char *bind_ip = argc > 6 ? argv[6] : "127.0.0.1";

    /* Configuração do endereço de escuta (Bind) */
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, bind_ip, &server_addr.sin_addr) != 1) {
        fprintf(stderr, "IPv4 de bind invalido.\n"); return EXIT_FAILURE;
    }
    
    signal(SIGPIPE, SIG_IGN);
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) die("socket");
    
    int reuse = 1;
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)))
        die("SO_REUSEADDR");

    if (bind(listener, (struct sockaddr *)&server_addr, sizeof(server_addr))) die("bind");

    if (listen(listener, MAX_CLIENTS)) die("listen");
    
    fprintf(stderr, "LISTENING port=%u clients=%u bind=%s\n", port, count, bind_ip);
    fflush(stderr);

    Experiment e = { .mutex = PTHREAD_MUTEX_INITIALIZER,
                     .cond = PTHREAD_COND_INITIALIZER, .block = block };
    Client clients[MAX_CLIENTS] = {0};
    pthread_t threads[MAX_CLIENTS];

    /* Aceitação das conexões dos clientes com timeout global de 30s */
    double connection_deadline = now() + 30.0;
    for (unsigned i = 0; i < count; ++i) {
        struct pollfd p = { .fd = listener, .events = POLLIN };
        int rc;

        do {
            double remaining = connection_deadline - now();
            if (remaining <= 0) { errno = ETIMEDOUT; die("clientes ausentes"); }
            rc = poll(&p, 1, (int)(remaining * 1000) + 1);
        } while (rc < 0 && errno == EINTR);

        if (rc <= 0) { errno = ETIMEDOUT; die("accept timeout"); }
        
        int fd = accept(listener, NULL, NULL);
        if (fd < 0) die("accept");

        /* Configura timeouts curtos para o handshake inicial */
        struct timeval timeout = { .tv_sec = 1, .tv_usec = 0 };
        if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)))
            die("socket timeout");

        /* Realiza o handshake enviando parâmetros do teste (MAGIC, bloco, tempo total) */
        uint32_t header[3] = {htonl(MAGIC), htonl(block), htonl(warmup + duration)};
        uint32_t ready;
        if (transfer_all(fd, header, sizeof(header), 1) ||
            transfer_all(fd, &ready, sizeof(ready), 0) || ntohl(ready) != MAGIC) {
            fprintf(stderr, "Handshake invalido.\n"); return EXIT_FAILURE;
        }
        
        clients[i].fd = fd;
        clients[i].id = i + 1;
        clients[i].experiment = &e;
        
        socklen_t optlen = sizeof(clients[i].receive_buffer);
        if (getsockopt(fd, SOL_SOCKET, SO_RCVBUF,
                       &clients[i].receive_buffer, &optlen)) die("SO_RCVBUF");
                       
        clients[i].buffer = malloc(block);
        if (!clients[i].buffer) die("malloc");
        memset(clients[i].buffer, 0, block);
    }
    close(listener); // Fecha o socket listener pois já aceitou todos os clientes necessários

    /* Cria as threads de recepção para cada cliente */
    for (unsigned i = 0; i < count; ++i) {
        int rc = pthread_create(&threads[i], NULL, receive_data, &clients[i]);
        if (rc) { errno = rc; die("pthread_create"); }
    }

    /* Aguarda todas as threads estarem prontas, define janelas de tempo e dispara o 'go' */
    pthread_mutex_lock(&e.mutex);
    while (e.ready != (int)count) pthread_cond_wait(&e.cond, &e.mutex);
    e.begin = now() + (double)warmup;
    e.end = e.begin + (double)duration;
    e.go = 1;
    pthread_cond_broadcast(&e.cond);
    pthread_mutex_unlock(&e.mutex);

    /* Monitoramento de uso de CPU (rusage) durante a janela de testes */
    struct rusage cpu_begin, cpu_end;
    sleep_until(e.begin);
    double cpu_start_time = now();
    if (getrusage(RUSAGE_SELF, &cpu_begin)) die("getrusage");
    
    sleep_until(e.end); // Mantém a thread principal pausada durante a duração do benchmark
    double wake_lag = now() - e.end;
    double cpu_end_time = now();
    
    if (getrusage(RUSAGE_SELF, &cpu_end)) die("getrusage");
    for (unsigned i = 0; i < count; ++i) shutdown(clients[i].fd, SHUT_RD);

    /* Coleta os resultados de todas as threads via pthread_join */
    uint64_t total = 0;
    int valid = 1;

    for (unsigned i = 0; i < count; ++i) {
        int rc = pthread_join(threads[i], NULL);

        if (rc) { errno = rc; die("pthread_join"); }

        total += clients[i].bytes;

        if (clients[i].error) valid = 0;
    }

    /* Processamento do tempo de CPU consumido (usuário e sistema) */
    double user = seconds(cpu_end.ru_utime) - seconds(cpu_begin.ru_utime);
    double system = seconds(cpu_end.ru_stime) - seconds(cpu_begin.ru_stime);

    /* Impressão do relatório estruturado em JSON contendo métricas globais e por cliente */
    printf("{\"valid\":%s,\"clients\":%u,\"duration_s\":%u,"
           "\"warmup_s\":%u,\"block_bytes\":%u,\"bytes_received\":%" PRIu64 ","
           "\"mbps\":%.9f,\"measurement_start_monotonic_s\":%.9f,"
           "\"measurement_end_monotonic_s\":%.9f,"
           "\"server_cpu_user_s\":%.6f,\"server_cpu_system_s\":%.6f,"
           "\"cpu_sample_s\":%.6f,\"deadline_wake_lag_s\":%.6f,\"per_client\":[",
           valid ? "true" : "false", count, duration, warmup, block, total,
           (double)total * 8.0 / ((double)duration * 1000000.0), e.begin, e.end,
           user, system, cpu_end_time - cpu_start_time, wake_lag);

    for (unsigned i = 0; i < count; ++i) {
        Client *c = &clients[i];
        printf("%s{\"id\":%u,\"bytes\":%" PRIu64 ",\"recv_calls\":%" PRIu64 ","
               "\"warmup_bytes\":%" PRIu64 ",\"initial_rcvbuf\":%d,\"error\":%d}",
               i ? "," : "", c->id, c->bytes, c->calls, c->warmup_bytes,
               c->receive_buffer, c->error);
        close(c->fd);
        free(c->buffer);
    }
    puts("]}");

    /* Limpeza dos recursos de sincronização */
    pthread_cond_destroy(&e.cond);
    pthread_mutex_destroy(&e.mutex);
    return valid ? EXIT_SUCCESS : 2;
}