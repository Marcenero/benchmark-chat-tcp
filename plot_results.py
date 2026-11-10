#!/usr/bin/env python3
"""Gera tabela de estatisticas e grafico a partir de dados_brutos.csv.

Requer matplotlib apenas para o PNG; cria resumo.csv e tabela Markdown sem ele.
"""
import csv
from collections import defaultdict
from pathlib import Path
import statistics
import sys


def f(x):
    return f'{x:.2f}' if x is not None else '—'


def main():
    if len(sys.argv) != 2:
        sys.exit('Uso: python3 plot_results.py resultados_.../dados_brutos.csv')
    path = Path(sys.argv[1])
    if not path.is_file():
        sys.exit(f'Arquivo nao encontrado: {path}')
    by_n = defaultdict(list)
    all_n = set()
    with path.open(newline='', encoding='utf-8') as src:
        for row in csv.DictReader(src):
            n = int(row['clients'])
            all_n.add(n)
            if row['valid'].lower() == 'true' and row['mbps']:
                by_n[n].append(float(row['mbps']))
    if not any(by_n.values()):
        sys.exit('Nenhuma medicao valida encontrada.')

    rows = []
    for n in range(min(all_n), max(all_n) + 1):
        samples = by_n[n]
        rows.append({'clientes': n, 'repeticoes_validas': len(samples),
                     'media_mbps': statistics.mean(samples) if samples else None,
                     'mediana_mbps': statistics.median(samples) if samples else None,
                     'desvio_padrao_mbps': statistics.stdev(samples) if len(samples) >= 2 else None,
                     'maximo_observado_mbps': max(samples) if samples else None})
    with (path.parent / 'resumo.csv').open('w', newline='', encoding='utf-8') as dest:
        writer = csv.DictWriter(dest, fieldnames=list(rows[0]))
        writer.writeheader()
        for r in rows:
            writer.writerow({k: '' if v is None else v for k, v in r.items()})

    lines = ['| Clientes | Repetições válidas | Média (Mbps) | Desvio-padrão (Mbps) | Máximo observado (Mbps) |',
             '|---:|---:|---:|---:|---:|']
    for r in rows:
        lines.append(f'| {r["clientes"]} | {r["repeticoes_validas"]} | '
                     f'{f(r["media_mbps"])} | {f(r["desvio_padrao_mbps"])} | '
                     f'{f(r["maximo_observado_mbps"])} |')
    (path.parent / 'tabela_resultados.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print('\n'.join(lines))

    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
    except ImportError:
        print('matplotlib indisponivel; instale: python3 -m pip install matplotlib')
        return
    plotrows = [r for r in rows if r['repeticoes_validas']]
    xs = [r['clientes'] for r in plotrows]
    means = [r['media_mbps'] for r in plotrows]
    err = [r['desvio_padrao_mbps'] or 0.0 for r in plotrows]
    maximum = [r['maximo_observado_mbps'] for r in plotrows]
    fig, ax = plt.subplots(figsize=(9, 5.2))
    ax.errorbar(xs, means, yerr=err, fmt='o-', capsize=4,
                label='Media ± desvio-padrao')
    ax.plot(xs, maximum, 's--', label='Maximo observado')
    ax.set_xlabel('Numero de clientes simultaneos')
    ax.set_ylabel('Throughput agregado (Mbps)')
    ax.set_title('Benchmark de transmissao TCP: clientes → servidor')
    ax.set_xticks(xs)
    ax.grid(True, alpha=0.25)
    ax.legend()
    fig.tight_layout()
    output = path.parent / 'grafico_throughput.png'
    fig.savefig(output, dpi=180)
    plt.close(fig)
    print(f'Grafico salvo: {output}')


if __name__ == '__main__':
    main()
