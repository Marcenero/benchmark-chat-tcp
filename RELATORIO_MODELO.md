# Relatório técnico — Capacidade efetiva máxima de recepção de dados em chat TCP

**Disciplina:** [preencher]  
**Integrantes:** [preencher]  
**Data:** [preencher]  
**Repositório GitHub:** [URL REAL A PREENCHER APÓS PUBLICAÇÃO]

> **Atenção:** substitua os campos entre colchetes pelos dados do experimento. Não use valores inventados. O máximo observado é o maior valor registrado nas repetições válidas, não necessariamente o limite teórico da rede.

## 1. Objetivo

Investigar a capacidade efetiva de recepção de um servidor TCP modificado a partir de uma aplicação de chat, avaliando a taxa de transmissão agregada (Mbps) em cenários com 1 a 10 clientes simultâneos. Examinar como o número de conexões concorrentes afeta a vazão de dados e identificar possíveis gargalos relacionados a processamento, memória, buffers de socket e infraestrutura de rede.

## 2. Ambiente experimental

### 2.1. Hardware e software

| Característica | Servidor | Máquina(s) cliente(s) |
|---|---|---|
| Processador (modelo, núcleos/threads) | [preencher] | [preencher] |
| Memória RAM total | [preencher] | [preencher] |
| Sistema operacional e versão | [preencher] | [preencher] |
| Arquitetura e virtualização/WSL | [preencher] | [preencher] |
| Compilador e versão | [preencher] | [preencher] |
| Opções de compilação | `-O3 -std=c11 -Wall -Wextra -Wpedantic -pthread` | [preencher] |
| Interface e velocidade nominal | [preencher] | [preencher] |
| Endereço IP usado | [preencher] | [preencher] |

### 2.2. Configuração da rede

- Tipo: [mesma máquina com loopback / Ethernet física / Wi-Fi].
- Topologia: [descrever se há switch, roteador, conexão direta ou VM].
- Velocidade nominal e duplex: [preencher, se conhecido].
- Outras condições: [tráfego concorrente, uso de energia, Wi-Fi, VPN, firewall, virtualização, etc.].

> Em loopback (`127.0.0.1`), o teste mede o caminho TCP interno do SO, não a capacidade de um enlace Ethernet externo. Não confundir os dois experimentos.

## 3. Modificações na aplicação original

A aplicação original (`originais/chat_server.c` e `originais/chat_client.c`) possuía interface de chat, identificação de usuários, leitura do teclado e retransmissão (broadcast) de mensagens a outros clientes. A versão modificada mantém a arquitetura TCP cliente-servidor com sockets e threads, mas substitui o chat interativo por geração e envio contínuo de bytes.

O servidor aceita o número especificado de clientes, inicializa uma thread receptora por conexão, aguarda a prontidão das threads e sinaliza o início simultâneo da coleta. O cliente cria um buffer determinístico e envia seus bytes repetidamente até receber a indicação de fim. Cada thread contabiliza os bytes retornados por `recv()` na janela de medição. O servidor reúne essas contagens, calcula Mbps e produz JSON com métricas agregadas e individuais.

Foram adicionados sincronização de início, duração configurável, aquecimento, timeouts de segurança, contadores de 64 bits e monitoramento de CPU. Os códigos usados estão em [URL DIRETA PARA chat_client.c] e [URL DIRETA PARA chat_server.c].

## 4. Metodologia

### 4.1. Parâmetros

| Parâmetro | Valor usado |
|---|---|
| Clientes simultâneos | 1 a 10 |
| Duração de cada medição | [15] s |
| Aquecimento excluído da medição | [2] s |
| Tamanho do bloco | [65.536] bytes |
| Repetições por número de clientes | [5] |
| Intervalo de descanso | [2] s |
| Porta TCP | [5000] |
| Ordem dos cenários | Aleatorizada por semente fixa [2026] |

### 4.2. Procedimentos de comparabilidade

1. Compilar os programas na mesma configuração em todos os cenários.
2. Iniciar o servidor e o número correspondente de clientes.
3. Esperar que todos estejam conectados antes de liberar o início, com tempo monotônico comum no servidor.
4. Desconsiderar os bytes da etapa de aquecimento e registrar apenas os recebidos nos [15] s de medição.
5. Registrar JSON bruto, número de bytes, métricas de CPU e eventuais falhas.
6. Encerrar os processos, aguardar descanso e repetir [5] vezes por cenário.
7. Intercalar a ordem dos números de clientes para reduzir viés de aquecimento ou de carga progressiva.
8. Considerar somente testes válidos para cálculos de média, desvio-padrão e pico observado; comunicar quaisquer execuções inválidas.

### 4.3. Cálculo do throughput agregado

Para `N` threads receptoras, o total de bytes é:

\[
B_{\mathrm{total}} = \sum_{i=1}^{N} B_i
\]

A taxa agregada expressa em megabits por segundo (Mbps, com base decimal) é:

\[
T_{\mathrm{Mbps}} = \frac{8B_{\mathrm{total}}}{\Delta t \times 10^6}
\]

Em que `B_i` representa os bytes devolvidos por chamadas `recv()` **durante a janela de medição** da thread `i`, e `Δt` é a duração nominal definida. O cálculo não considera os bytes simplesmente enfileirados pelos clientes em suas chamadas `send()`.

Para cada cenário, reportar a média, o desvio-padrão amostral e o **máximo observado** dentre as repetições válidas.

## 5. Resultados

**Fonte dos números:** `resultados_.../dados_brutos.csv`, processado com `plot_results.py`. Substituir a tabela abaixo pelo conteúdo gerado em `tabela_resultados.md`.

| Clientes | Repetições válidas | Média (Mbps) | Desvio-padrão (Mbps) | Máximo observado (Mbps) |
|---:|---:|---:|---:|---:|
| 1 | [ ] | [ ] | [ ] | [ ] |
| 2 | [ ] | [ ] | [ ] | [ ] |
| 3 | [ ] | [ ] | [ ] | [ ] |
| 4 | [ ] | [ ] | [ ] | [ ] |
| 5 | [ ] | [ ] | [ ] | [ ] |
| 6 | [ ] | [ ] | [ ] | [ ] |
| 7 | [ ] | [ ] | [ ] | [ ] |
| 8 | [ ] | [ ] | [ ] | [ ] |
| 9 | [ ] | [ ] | [ ] | [ ] |
| 10 | [ ] | [ ] | [ ] | [ ] |

**Figura 1 — Número de clientes simultâneos versus taxa agregada de recepção TCP.**

Inserir `resultados_.../grafico_throughput.png` depois de executar os experimentos. O gráfico apresenta a média com desvio-padrão e o máximo observado por cenário.

### 5.1. Discussão

[Após medir, discutir os dados reais e evitar conclusões prévias.]

- A vazão agregada cresceu de N=1 a N=[...]? Descrever o comportamento com valores.
- Houve saturação ou queda quando N aumentou? Em qual cenário? Quantificar.
- Há diferença relevante entre máximo observado e média? As repetições foram consistentes?
- A métrica de CPU do servidor aumentou junto com o throughput? Se a taxa estabilizou, a CPU estava saturada?
- A velocidade nominal do enlace impõe um limite plausível? Qual o papel da CPU da máquina cliente, que pode saturar antes do servidor?
- Se o experimento ocorreu em loopback, reconhecer que não mede a capacidade da rede física.

## 6. Limitações e possíveis gargalos

[Avaliar quais mecanismos são realmente sustentados pelas medições. Possíveis gargalos incluem: CPU do servidor, CPU da máquina geradora, largura de banda do enlace, configuração de buffers de socket, número de threads e escalonamento do sistema operacional, competição com outros processos, aquecimento e duração da medição.]

A metodologia mede bytes efetivamente entregues à aplicação servidor por `recv()` até o limite temporal; não garante que todos os bytes enfileirados pelo cliente tenham sido recebidos, nem isola automaticamente a origem do gargalo. O máximo observado não é o limite teórico absoluto do hardware.

## 7. Conclusão

[Retomar o valor de máximo observado e o número de clientes correspondente, descrever a tendência e identificar o gargalo mais consistente com evidências de CPU e rede.]

## 8. Reprodutibilidade e código-fonte

**Repositório GitHub**: [URL REAL]

**Cliente modificado**: [URL REAL PARA chat_client.c]

**Servidor modificado**: [URL REAL PARA chat_server.c]

**Instruções**: [URL REAL PARA README.md]

Comandos utilizados, a partir da pasta do projeto:

```bash
make
python3 run_experiments.py --clients-min 1 --clients-max 10 --repetitions 5 --duration 15 --warmup 2 --block 65536 --cooldown 2
python3 plot_results.py resultados_DATA_HORA/dados_brutos.csv
```

Se os clientes estiverem em outra máquina, registrar os parâmetros de conexão remota utilizados, descritos no README. Anexar, sempre que possível, os CSVs e o JSONL dos experimentos.
