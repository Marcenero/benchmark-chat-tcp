/*
 * Uso: ./chat_client IPV4 PORTA
 * Sem digitacao: o servidor informa bloco/duracao e autoriza o inicio.
 */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* Constantes de protocolo mágicas em formato big-endian conceitual */
#define MAGIC UINT32_C(0x54435031) // "TCP1"
#define DONE  UINT32_C(0x444f4e45) // "DONE"

/* Estrutura para controle compartilhado entre a thread principal e a secundária */
typedef struct {
    int fd;
    atomic_int finished; /* Estado: 0=executando, 1=resultado valido, -1=falha */
    uint64_t received;   /* Bytes recebidos/registrados pelo servidor */
} Control;

/* Função auxiliar para garantir que todos os bytes de um buffer sejam enviados ou recebidos */
static int transfer_all(int fd, void *buffer, size_t size, int sending) {
    size_t offset = 0;

    while (offset < size) {
        ssize_t n = sending
            ? send(fd, (char *)buffer + offset, size - offset, MSG_NOSIGNAL)
            : recv(fd, (char *)buffer + offset, size - offset, 0);

        if (n < 0 && errno == EINTR) continue; // Interrompido por sinal, tenta novamente
        
        if (n <= 0) return -1;                // Erro fatal ou conexão fechada
        
        offset += (size_t)n;
    }

    return 0;
}

/* Retorna o tempo monotônico atual em segundos (alta precisão para o watchdog) */
static double now(void) {
    struct timespec t;

    if (clock_gettime(CLOCK_MONOTONIC, &t)) { 
        perror("clock_gettime"); exit(1); 
    }

    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* Thread secundária: aguarda e valida a mensagem de encerramento do servidor */
static void *receive_result(void *argument) {
    Control *c = argument;
    uint32_t result[4];

    // Tenta receber o pacote de conclusão do servidor
    if (transfer_all(c->fd, result, sizeof(result), 0) ||
        ntohl(result[0]) != DONE || ntohl(result[1]) != 0) {
        atomic_store_explicit(&c->finished, -1, memory_order_release);
    } 
    else {
        // Reconstrói o contador de 64 bits enviado pelo servidor (big-endian para host)
        c->received = ((uint64_t)ntohl(result[2]) << 32) | ntohl(result[3]);
        atomic_store_explicit(&c->finished, 1, memory_order_release);
    }

    return NULL;
}

int main(int argc, char **argv) {
    // Validação dos argumentos de linha de comando
    if (argc != 3) {
        fprintf(stderr, "Uso: %s IPV4 PORTA\n", argv[0]); return EXIT_FAILURE;
    }

    char *end;
    errno = 0;
    unsigned long port = strtoul(argv[2], &end, 10);

    if (errno || !*argv[2] || *argv[2] == '-' || *end || port < 1 || port > 65535) {
        fprintf(stderr, "Porta invalida.\n"); return EXIT_FAILURE;
    }

    // Configuração do endereço IP e porta do servidor
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, argv[1], &address.sin_addr) != 1) {
        fprintf(stderr, "IPv4 invalido.\n"); return EXIT_FAILURE;
    }

    // Ignora sinal SIGPIPE para evitar que o programa feche abruptamente se o socket fechar
    signal(SIGPIPE, SIG_IGN);

    // Criação do socket TCP
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return EXIT_FAILURE; }

    // Configuração de timeouts iniciais de socket (recebimento e envio)
    struct timeval receive_timeout = {.tv_sec = 40, .tv_usec = 0};
    struct timeval send_timeout = {.tv_sec = 1, .tv_usec = 0};

    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout)) ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout))) {
        perror("socket timeout"); close(fd); return EXIT_FAILURE;
    }

    // Estabelece conexão com o servidor
    if (connect(fd, (struct sockaddr *)&address, sizeof(address))) {
        perror("connect"); close(fd); return EXIT_FAILURE;
    }

    // Lê o cabeçalho inicial de boas-vindas do servidor
    uint32_t header[3];
    if (transfer_all(fd, header, sizeof(header), 0) || ntohl(header[0]) != MAGIC) {
        fprintf(stderr, "Cabecalho invalido.\n"); close(fd); return EXIT_FAILURE;
    }

    // Extrai tamanho do bloco e duração estipulada pelo servidor
    uint32_t block = ntohl(header[1]), total_seconds = ntohl(header[2]);
    if (block < 1024 || block > 4194304 || total_seconds < 2 || total_seconds > 3660) {
        fprintf(stderr, "Configuracao invalida.\n"); close(fd); return EXIT_FAILURE;
    }

    // Aloca memória para o payload de teste
    unsigned char *payload = malloc(block);

    if (!payload) { 
        perror("malloc"); close(fd); return EXIT_FAILURE; 
    }
    
    /* Preenche o payload com dados pseudoaleatórios determinísticos em memória */
    for (uint32_t i = 0; i < block; ++i) payload[i] = (unsigned char)(i * 31u + 17u);

    // Envia confirmação de prontidão e aguarda o caractere 'G' (Go) do servidor
    uint32_t ready = htonl(MAGIC);
    unsigned char go;
    if (transfer_all(fd, &ready, sizeof(ready), 1) ||
        transfer_all(fd, &go, 1, 0) || go != 'G') {
        fprintf(stderr, "Inicio nao autorizado pelo servidor.\n");
        free(payload); close(fd); return EXIT_FAILURE;
    }

    // Ajusta o timeout de recepção para a duração total do teste + margem de segurança
    receive_timeout.tv_sec = (time_t)total_seconds + 5;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout))) {
        perror("SO_RCVTIMEO"); free(payload); close(fd); return EXIT_FAILURE;
    }

    // Inicializa a estrutura de controle para a thread receptora
    Control control = {.fd = fd, .finished = ATOMIC_VAR_INIT(0), .received = 0};
    pthread_t receiver;
    int rc = pthread_create(&receiver, NULL, receive_result, &control);

    if (rc) {
        errno = rc; perror("pthread_create"); free(payload); close(fd); return EXIT_FAILURE;
    }

    // Configura o watchdog de tempo limite no cliente
    double watchdog = now() + (double)total_seconds + 5.0;
    uint64_t queued = 0;
    size_t offset = 0;

    // Loop principal de envio contínuo de dados (saturação de banda)
    while (atomic_load_explicit(&control.finished, memory_order_acquire) == 0) {
        if (now() >= watchdog) { 
            shutdown(fd, SHUT_RDWR); 
            break; 
        }
        
        ssize_t n = send(fd, payload + offset, (size_t)block - offset, MSG_NOSIGNAL);
        
        if (n > 0) {
            queued += (uint64_t)n;
            offset = (offset + (size_t)n) % (size_t)block; // Mantém o envio circular caso envie parcial
        } 
        else if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue; // Tenta novamente se o socket estiver ocupado (non-blocking style)
        } 
        else {
            break; // Erro de conexão ou encerramento
        }
    }

    // Aguarda o término da thread receptora
    pthread_join(receiver, NULL);
    int success = atomic_load_explicit(&control.finished, memory_order_acquire) == 1;

    // Exibe o resultado formatado em JSON se bem-sucedido
    if (success) {
        printf("{\"bytes_queued_total\":%" PRIu64 ",\"server_measured_bytes\":%" PRIu64 "}\n",
               queued, control.received);
    } 
    else {
        fprintf(stderr, "Teste interrompido ou resultado invalido.\n");
    }

    // Limpeza de recursos
    shutdown(fd, SHUT_RDWR);
    close(fd);
    free(payload);
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}