# Benchmark de throughput TCP

Este projeto parte da aplicação de chat TCP, formada por `chat_server.c` e `chat_client.c`, e adapta a comunicação para medir a quantidade de dados que um servidor consegue receber de vários clientes ao mesmo tempo. Os arquivos do chat original foram mantidos em `originais/`, para facilitar a comparação com a versão modificada. Também foram aproveitadas as implementações de `chat_server2.c` e `chat_client2.c`, com correções e scripts para executar os experimentos.

## 1. O que o projeto mede

O teste avalia o throughput agregado de recepção do servidor TCP com 1 a 10 clientes simultâneos. Cada cliente envia blocos de bytes gerados automaticamente, sem precisar digitar mensagens. No servidor, uma thread recebe os dados de cada conexão.

A taxa é calculada com a soma dos bytes que as chamadas `recv()` entregam à aplicação durante um mesmo intervalo de medição. Os dados que o cliente coloca na fila de envio com `send()` são registrados separadamente e não entram nesse cálculo.

## 2. Requisitos

Para compilar e executar o projeto, é necessário um ambiente Linux ou WSL com `gcc`, `make` e `python3`. O código utiliza C11 e recursos POSIX, incluindo `pthread`.

O gráfico de resultados depende da biblioteca `matplotlib`. Caso ela ainda não esteja instalada, use:

```bash
python3 -m pip install matplotlib
```

Para executar os testes em duas máquinas, as duas precisam estar na mesma rede. O servidor deve ter o executável `chat_server`, e a máquina responsável por gerar as conexões deve ter `chat_client`. Na execução automatizada entre máquinas, o computador servidor também precisa conseguir acessar a máquina cliente por SSH sem solicitar senha durante a execução. Por isso, o serviço SSH deve estar ativo na máquina cliente.

O código não foi preparado para compilação direta pelo MSVC no Windows. Nesse sistema, utilize Linux ou WSL. Se a intenção for medir uma rede física, registre o uso de WSL no relatório, pois a virtualização e a configuração de rede podem influenciar os resultados.

## 3. Compilação

Abra o terminal na pasta `benchmark_chat_tcp` e execute:

```bash
make
```

O comando compila `chat_server` e `chat_client`, que são as versões adaptadas para o benchmark. Para compilar o chat interativo preservado ou remover os executáveis, utilize:

```bash
make original   # opcional, compila o chat interativo preservado
make clean      # remove executaveis
```

## 4. Teste manual na mesma máquina

Abra um terminal e inicie o servidor:

```bash
./chat_server 5000 3 15 65536 2 127.0.0.1
```

O servidor recebe os argumentos na ordem `PORTA CLIENTES SEGUNDOS_MEDICAO [BLOCO_BYTES] [AQUECIMENTO_SEGUNDOS] [IP_BIND]`. Nesse exemplo, ele espera três clientes, realiza 15 segundos de medição e utiliza blocos de 65.536 bytes, com dois segundos iniciais de aquecimento.

Depois, abra mais três terminais e execute o comando abaixo em cada um:

```bash
./chat_client 127.0.0.1 5000
```

Quando todos estiverem conectados, o servidor libera o envio de dados. Ao terminar, ele apresenta um JSON com a quantidade total de bytes recebidos (`bytes_received`), a taxa agregada (`mbps`), as medições por cliente (`per_client`) e o consumo de CPU. Cada cliente também informa quantos bytes foram aceitos em suas chamadas `send()` e quantos o servidor contabilizou.

## 5. Testes automáticos de 1 a 10 clientes

O script `run_experiments.py` executa os cenários com 1 a 10 clientes e repete cada cenário cinco vezes. Para testar na própria máquina, usando o endereço de loopback, execute:

```bash
python3 run_experiments.py \
  --clients-min 1 --clients-max 10 \
  --repetitions 5 --duration 15 --warmup 2 \
  --block 65536 --cooldown 2
```

Como o endereço utilizado nesse caso é `127.0.0.1`, os dados circulam pela pilha TCP do próprio sistema operacional. Essa configuração permite observar o desempenho local de processamento e comunicação, mas não mede a capacidade de uma conexão física Ethernet ou Wi-Fi.

Para evitar que a sequência dos testes favoreça sempre os mesmos cenários, o script sorteia a ordem de execução usando a semente fixa 2026. Ele também respeita um intervalo de descanso entre medições e registra eventuais testes inválidos, que ficam fora do cálculo das médias.

Os arquivos são salvos em uma pasta chamada `resultados_DATA_HORA/`:

- `configuracao.json`: parâmetros utilizados na execução;
- `dados_brutos.csv`: bytes recebidos, throughput, uso de CPU e validade de cada repetição;
- `resultados_detalhados.jsonl`: saídas completas do servidor e informações dos clientes, úteis para conferir as medições.

Depois que os testes terminarem, gere a tabela e o gráfico com:

```bash
python3 plot_results.py resultados_DATA_HORA/dados_brutos.csv
```

Troque `resultados_DATA_HORA` pelo nome da pasta criada na execução. O script produz `resumo.csv`, com média, mediana, desvio-padrão e máximo observado para cada quantidade de clientes; `tabela_resultados.md`, com uma tabela para o relatório; e `grafico_throughput.png`, com o gráfico de throughput em função do número de clientes. O gráfico inclui média, desvio-padrão e máximo observado, desde que `matplotlib` esteja disponível.

O maior resultado entre as cinco repetições válidas de um cenário será apresentado como máximo observado. Esse valor ajuda a estimar o pico atingido nas condições do teste, mas não estabelece o limite físico absoluto da rede ou do computador.

## 6. Testes com servidor e clientes em máquinas diferentes

Para medir a transmissão em uma rede física, uma opção é utilizar duas máquinas Linux ligadas por Ethernet, de preferência no mesmo switch ou roteador. Neste exemplo, o servidor tem o endereço `192.168.1.10` e a máquina que executa os clientes usa `192.168.1.20`.

Antes de começar, compile o projeto nas duas máquinas e confirme que a porta TCP escolhida está liberada pelos firewalls. Também é recomendável evitar VPN, Wi-Fi e outras transferências de grande volume durante a coleta, para reduzir a interferência nas medições.

Na máquina cliente, configure o SSH de forma que o computador servidor consiga iniciar os processos remotamente usando `ssh usuario@192.168.1.20`, sem autenticação interativa. Em seguida, execute o script abaixo na máquina servidora:

```bash
python3 run_experiments.py \
  --clients-min 1 --clients-max 10 \
  --repetitions 5 --duration 15 --warmup 2 --block 65536 \
  --server-bind 0.0.0.0 --server-ip 192.168.1.10 \
  --remote-client usuario@192.168.1.20 \
  --remote-dir /home/usuario/benchmark_chat_tcp
```

O argumento `--remote-dir` indica onde está a pasta do projeto já compilado na máquina cliente. O SSH inicia a quantidade necessária de processos para cada cenário. Depois de conectados, os clientes aguardam a autorização do servidor, que define a janela de medição para todos.

Mantenha o acesso SSH restrito à rede e às máquinas autorizadas.

Também é possível executar o experimento manualmente, sem SSH: inicie `./chat_server 5000 N 15 65536 2 0.0.0.0` na máquina servidora e abra N processos com `./chat_client 192.168.1.10 5000` na máquina cliente. Repita o procedimento para cada valor de N e guarde o JSON produzido pelo servidor. Para a bateria de 50 execuções, a automação ajuda a manter o mesmo procedimento em todos os cenários.

## 7. Informações do ambiente experimental

O relatório precisa identificar os equipamentos utilizados. Execute o script de coleta em cada máquina:

```bash
bash coletar_ambiente.sh > ambiente_servidor.txt
# Na outra maquina:
bash coletar_ambiente.sh > ambiente_cliente.txt
```

O script registra informações de processador, memória RAM, sistema operacional, compilador e interfaces de rede. Quando possível, também consulta a velocidade nominal da interface.

Confira essas informações antes de incluí-las no relatório. Registre ainda o meio utilizado (loopback, Ethernet ou Wi-Fi), a topologia, a quantidade de núcleos do processador, a presença de switch ou roteador e o uso de máquina virtual, contêiner ou WSL. Nem todos esses dados podem ser identificados automaticamente.

## 8. Como a taxa de transmissão é calculada

O servidor cria uma thread receptora para cada cliente e usa `CLOCK_MONOTONIC` para controlar o intervalo de medição. Cada thread soma os bytes recebidos por `recv()` somente quando a recepção ocorre dentro da janela `[begin, end)`. Ao final, o servidor soma os contadores e calcula:

```text
throughput_agregado_Mbps = (8 * soma(bytes_recebidos_pelas_threads)) / (duracao_segundos * 1_000_000)
```

O fator 8 converte bytes em bits, e a divisão por `1_000_000` apresenta o resultado em Mbps. O cálculo considera o mesmo período para todos os clientes, pois cada cenário mede a taxa agregada do servidor.

Para comparar os resultados, mantenha iguais as opções de compilação, o tamanho dos blocos, a duração, o tempo de aquecimento, as máquinas e a configuração da rede. Reinicie o servidor a cada repetição, respeite o intervalo de descanso e registre situações que possam afetar o desempenho.

A medição representa os bytes entregues à aplicação por `recv()` dentro do intervalo definido. Ela não mede diretamente perdas de pacotes, latência ou o tempo de transmissão no enlace, e também pode deixar de contabilizar bytes que ainda estejam nos buffers do sistema operacional após o fim do teste. Quando vários clientes rodam na mesma máquina, a CPU responsável por gerar os dados pode limitar a taxa antes mesmo de o servidor atingir sua capacidade. A discussão dos resultados deve considerar essas possibilidades.