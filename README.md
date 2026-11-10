# Benchmark TCP — adaptação do chat do ZIP

Projeto adaptado de `chat_server.c` e `chat_client.c` da aplicação de chat TCP fornecida. Os originais permanecem preservados em `originais/`. A implementação de benchmark se baseia também em `chat_server2.c` e `chat_client2.c` fornecidos, com correções e automação de experimentos.

## 1. Objetivo

Medir o **throughput agregado de recepção no servidor** para 1 a 10 clientes transmitindo simultaneamente por TCP. Dados são gerados automaticamente e enviados sem teclado; cada cliente é atendido por uma thread do servidor. O relatório é calculado com bytes que as chamadas `recv()` retornam **dentro de uma janela de medição compartilhada**, e não com bytes enfileirados em `send()` pelos clientes.

## 2. Requisitos

- Linux ou WSL com `gcc`, `make`, `python3` (POSIX/C11/pthreads).
- Para gerar gráfico: `python3 -m pip install matplotlib` (opcional para compilação/testes).
- Para teste real em duas máquinas: ambas na mesma rede, `chat_server` compilado no servidor e `chat_client` compilado na máquina de clientes; no modo automatizado remoto, SSH com autenticação sem senha interativa e SSH servidor ativo **na máquina cliente**.

No Windows nativo, este código POSIX não compila diretamente pelo MSVC. Use Linux/WSL; caso meça uma rede física entre máquinas, considere o possível efeito da virtualização/NAT do WSL.

## 3. Compilação

Dentro da pasta `benchmark_chat_tcp`:

```bash
make
```

Produz `chat_server` e `chat_client` (as versões **modificadas**, voltadas a benchmark). Outros alvos:

```bash
make original   # opcional, compila o chat interativo preservado
make clean      # remove executaveis
```

## 4. Execução manual básica (mesma máquina)

Abra um terminal para o servidor:

```bash
./chat_server 5000 3 15 65536 2 127.0.0.1
```

Argumentos do servidor: `PORTA CLIENTES SEGUNDOS_MEDICAO [BLOCO_BYTES] [AQUECIMENTO_SEGUNDOS] [IP_BIND]`. O exemplo solicita 3 clientes, 15 s de medição, blocos de 65.536 bytes e 2 s de aquecimento.

Abra três outros terminais, executando em cada um:

```bash
./chat_client 127.0.0.1 5000
```

O servidor imprime JSON contendo `bytes_received`, `mbps`, `per_client`, consumo de CPU e os parâmetros. Cada cliente apresenta o total de bytes aceitos em `send()` e a quantidade registrada pelo servidor.

## 5. Execução automática: 1 a 10 clientes, 5 repetições

Para rodar em **uma máquina** usando TCP no endereço de loopback:

```bash
python3 run_experiments.py \
  --clients-min 1 --clients-max 10 \
  --repetitions 5 --duration 15 --warmup 2 \
  --block 65536 --cooldown 2
```

**Atenção:** quando servidor e clientes usam `127.0.0.1`, mede-se TCP/CPU/memória dentro da própria máquina; a capacidade de Ethernet/Wi-Fi **não** é testada.

O script sorteia a ordem dos cenários (semente fixa 2026), executa cada cenário a quantidade de vezes especificada, faz pausa entre execuções e registra resultados inválidos sem misturá-los às médias.

Arquivos gerados na pasta `resultados_DATA_HORA/`:

- `configuracao.json`: parâmetros do experimento;
- `dados_brutos.csv`: throughput, bytes, CPU e validade de **cada repetição**;
- `resultados_detalhados.jsonl`: JSON bruto do servidor e dados dos clientes, para auditoria.

Após terminar, gere a tabela e o gráfico:

```bash
python3 plot_results.py resultados_DATA_HORA/dados_brutos.csv
```

Substitua `resultados_DATA_HORA` pela pasta que apareceu no terminal. A análise gera:

- `resumo.csv`: média, mediana, desvio-padrão e máximo observado por número de clientes;
- `tabela_resultados.md`: tabela formatada para colar no relatório;
- `grafico_throughput.png`: gráfico de throughput × número de clientes, com média ± desvio-padrão e máximo observado (se houver `matplotlib`).

O **máximo observado** dentre 5 repetições é o dado diretamente solicitado pelo enunciado. Ele é uma estimativa experimental do pico, não uma prova do limite físico máximo da rede.

## 6. Testes em máquinas diferentes (recomendado)

Exemplo de topologia: servidor Linux = `192.168.1.10`; máquina geradora dos clientes = `192.168.1.20`, via **Ethernet cabeada**, idealmente no mesmo switch ou roteador. Evite Wi-Fi, VPN e outras transferências intensas durante os testes.

1. Compile com `make` em **ambas** as máquinas (transfira a pasta para a máquina cliente).
2. Confirme que o cliente consegue conectar na porta escolhida e que os firewalls permitem tráfego TCP entre elas.
3. Na máquina cliente, configure acesso SSH para que o **servidor** possa iniciar os processos clientes de maneira automatizada (`ssh usuario@192.168.1.20` sem senha interativa). Esse acesso é apenas para orquestração.
4. Na máquina **servidora**, execute:

```bash
python3 run_experiments.py \
  --clients-min 1 --clients-max 10 \
  --repetitions 5 --duration 15 --warmup 2 --block 65536 \
  --server-bind 0.0.0.0 --server-ip 192.168.1.10 \
  --remote-client usuario@192.168.1.20 \
  --remote-dir /home/usuario/benchmark_chat_tcp
```

`--remote-dir` indica a localização da pasta compilada **na máquina cliente**. O SSH inicia os N processos ao mesmo tempo aproximado; eles aguardam a autorização de início do servidor para sincronizar a medição. Não compartilhe SSH de uma máquina fora de sua confiança e limite o acesso pela rede local.

**Alternativa sem SSH**: rode `./chat_server 5000 N 15 65536 2 0.0.0.0` na máquina servidora e execute N cópias de `./chat_client 192.168.1.10 5000` na outra máquina. Repita para cada N e guarde o JSON do servidor. Para obter mais confiabilidade e 50 testes, a automação via SSH é preferível.

## 7. Coletar hardware, sistema operacional e rede

Em **cada máquina participante**, execute:

```bash
bash coletar_ambiente.sh > ambiente_servidor.txt
# Na outra maquina:
bash coletar_ambiente.sh > ambiente_cliente.txt
```

O script registra CPU, RAM, SO, GCC, interfaces de rede e, quando disponível, a velocidade nominal da interface. Preencha no relatório também o **meio de conexão real** (loopback, Ethernet 1 Gbps, Wi-Fi etc.), topologia, quantidade de cores, informações do switch, e se a máquina usa contêiner/VM/WSL. Dados de rede sem acesso ao hardware podem não ser detectados automaticamente.

## 8. Métrica e cuidados de validade

O servidor cria uma thread de recepção por cliente, com tempo monotônico (`CLOCK_MONOTONIC`). As threads contam bytes retornados por `recv()` **somente durante a janela `[begin, end)`**. Após a janela, o servidor soma os contadores e divide pela duração nominal definida no teste:

```text
throughput_agregado_Mbps = (8 * soma(bytes_recebidos_pelas_threads)) / (duracao_segundos * 1_000_000)
```

Não conte bytes das chamadas `send()` como bytes efetivamente recebidos: os bytes enfileirados no cliente podem diferir. Não some picos de velocidades individuais obtidos em janelas diferentes.

Para comparabilidade: use sempre a mesma configuração de compilação, tamanho de bloco, duração, aquecimento, máquinas, rede e condições ambientais. Evite executar outros benchmarks simultaneamente, reinicie o servidor a cada repetição, mantenha períodos de descanso e documente interferências.

**Limitações metodológicas:** a medição conta bytes retornados por `recv()` entre timestamps do servidor, não mede tempo de transmissão no enlace, perdas de pacotes, latência individual ou bytes ainda nos buffers do kernel após o deadline. A execução de 10 processos clientes numa máquina também pode saturar a CPU do gerador antes da capacidade do servidor. Esse comportamento deve ser discutido, não interpretado automaticamente como limite do servidor.

## 9. GitHub e relatório

O arquivo `RELATORIO_MODELO.md` contém a estrutura solicitada. Crie um repositório público ou privado acessível ao professor, por exemplo:

```bash
git init
# Crie o repositório VAZIO no GitHub antes de definir o remote
# Evite subir binarios e arquivos de resultados contendo informacoes privadas
printf 'chat_server\nchat_client\nchat_server_original\nchat_client_original\n__pycache__/\n' > .gitignore
git add .
git commit -m "Benchmark de throughput TCP para 1 a 10 clientes"
git branch -M main
git remote add origin https://github.com/SEU_USUARIO/SEU_REPOSITORIO.git
git push -u origin main
```

**Substitua a URL de exemplo pelo link real** do repositório após criá-lo. No relatório, inclua os links diretos `.../blob/main/chat_client.c` e `.../blob/main/chat_server.c`, além do link do README.
