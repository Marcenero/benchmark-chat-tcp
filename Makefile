CC ?= gcc
CFLAGS ?= -O3 -std=c11 -Wall -Wextra -Wpedantic
LDLIBS = -pthread

.PHONY: all clean original
all: chat_server chat_client

chat_server: chat_server.c
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

chat_client: chat_client.c
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

# Opcional: compila a aplicação de chat interativo fornecida pelo professor.
original: chat_server_original chat_client_original

chat_server_original: originais/chat_server.c
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

chat_client_original: originais/chat_client.c
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

clean:
	rm -f chat_server chat_client chat_server_original chat_client_original
