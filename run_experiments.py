#!/usr/bin/env python3
"""Executa o benchmark TCP para N clientes e repeticoes (somente Python stdlib).

Padrao: servidor e clientes na mesma maquina (loopback).
Opcional: clientes em maquina remota via SSH para medir uma rede real.
"""

from __future__ import annotations

import argparse
import csv
from datetime import datetime
import json
from pathlib import Path
import random
import shlex
import subprocess
import sys
import time


def args_parser() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--clients-min', type=int, default=1)
    p.add_argument('--clients-max', type=int, default=10)
    p.add_argument('--repetitions', type=int, default=5)
    p.add_argument('--duration', type=int, default=15,
                   help='Segundos de MEDICAO, sem contar aquecimento')
    p.add_argument('--warmup', type=int, default=2)
    p.add_argument('--block', type=int, default=65536, help='Bytes por bloco TCP')
    p.add_argument('--port', type=int, default=5000)
    p.add_argument('--server-bind', default='127.0.0.1')
    p.add_argument('--server-ip', default='127.0.0.1',
                   help='IPv4 acessivel pelos clientes')
    p.add_argument('--remote-client', default=None,
                   help='Endereco SSH da maquina cliente, como usuario@192.168.1.20')
    p.add_argument('--remote-dir', default=None,
                   help='Diretorio NA MAQUINA REMOTA com binario chat_client')
    p.add_argument('--cooldown', type=float, default=2.0,
                   help='Pausa entre testes em segundos')
    p.add_argument('--seed', type=int, default=2026)
    p.add_argument('--output-dir', default=None)
    a = p.parse_args()
    if not (1 <= a.clients_min <= a.clients_max <= 10):
        p.error('Quantidade de clientes deve estar entre 1 e 10.')
    if a.repetitions < 1 or a.duration < 2 or a.warmup < 1:
        p.error('Repeticoes >= 1, duration >= 2 e warmup >= 1.')
    if not (1024 <= a.block <= 4194304) or a.duration > 3600 or a.warmup > 60:
        p.error('block entre 1024 e 4194304; duration <= 3600; warmup <= 60.')
    if not (1 <= a.port <= 65535) or a.cooldown < 0:
        p.error('Porta invalida ou cooldown negativo.')
    if bool(a.remote_client) != bool(a.remote_dir):
        p.error('Use --remote-client e --remote-dir juntos.')
    return a


def terminate(p: subprocess.Popen | None) -> None:
    if p is not None and p.poll() is None:
        p.terminate()
        try:
            p.wait(timeout=2)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait(timeout=2)


def client_cmd(a: argparse.Namespace, base: Path) -> list[str]:
    if a.remote_client:
        remote = f'cd {shlex.quote(a.remote_dir)} && ./chat_client {shlex.quote(a.server_ip)} {a.port}'
        return ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10',
                a.remote_client, remote]
    return [str(base / 'chat_client'), a.server_ip, str(a.port)]


def run_one(a: argparse.Namespace, base: Path, n: int, repetition: int) -> dict:
    result = {
        'datetime': datetime.now().astimezone().isoformat(),
        'clients': n, 'repetition': repetition, 'valid': False,
        'duration_s': a.duration, 'warmup_s': a.warmup,
        'block_bytes': a.block, 'bytes_received': '', 'mbps': '',
        'cpu_user_s': '', 'cpu_system_s': '', 'cpu_sample_s': '',
        'error': '',
    }
    server = None
    clients = []
    raw = {'clients': n, 'repetition': repetition}
    try:
        server_cmd = [str(base / 'chat_server'), str(a.port), str(n),
                      str(a.duration), str(a.block), str(a.warmup), a.server_bind]
        server = subprocess.Popen(server_cmd, cwd=base,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  text=True)
        ready = server.stderr.readline().strip()
        if not ready.startswith('LISTENING '):
            raise RuntimeError(f'Servidor nao ficou pronto: {ready}')
        raw['server_start'] = ready

        for _ in range(n):
            clients.append(subprocess.Popen(client_cmd(a, base), cwd=base,
                                            stdout=subprocess.PIPE,
                                            stderr=subprocess.PIPE, text=True))

        so, se = server.communicate(timeout=a.duration + a.warmup + 45)
        raw['server_stdout'], raw['server_stderr'] = so, se
        raw['server_returncode'] = server.returncode
        if server.returncode not in (0, 2):
            raise RuntimeError(f'Servidor falhou (exit={server.returncode}): {se.strip()}')
        server_data = json.loads(so)
        raw['server_data'] = server_data
        client_data = []
        for i, p in enumerate(clients, start=1):
            co, ce = p.communicate(timeout=12)
            raw[f'client_{i}'] = {'stdout': co, 'stderr': ce, 'exit': p.returncode}
            if p.returncode != 0:
                raise RuntimeError(f'Cliente {i} falhou (exit={p.returncode}): {ce.strip()}')
            client_data.append(json.loads(co))

        server_bytes = [int(x['bytes']) for x in server_data['per_client']]
        client_bytes = [int(x['server_measured_bytes']) for x in client_data]
        ok = (server.returncode == 0 and server_data['valid'] is True and
              int(server_data['clients']) == n and len(server_bytes) == n and
              sum(server_bytes) == int(server_data['bytes_received']) and
              sorted(server_bytes) == sorted(client_bytes))
        if not ok:
            raise RuntimeError('Resultado invalido ou bytes reportados pelos clientes divergem do servidor')

        result.update(valid=True, bytes_received=server_data['bytes_received'],
                      mbps=server_data['mbps'],
                      cpu_user_s=server_data['server_cpu_user_s'],
                      cpu_system_s=server_data['server_cpu_system_s'],
                      cpu_sample_s=server_data['cpu_sample_s'])
    except (OSError, ValueError, KeyError, TypeError, subprocess.TimeoutExpired, RuntimeError) as exc:
        result['error'] = str(exc)
        raw['error'] = str(exc)
    finally:
        for p in clients:
            terminate(p)
        terminate(server)
    return {'row': result, 'raw': raw}


def main() -> int:
    a = args_parser()
    base = Path(__file__).resolve().parent
    if not (base / 'chat_server').exists() or (not a.remote_client and not (base / 'chat_client').exists()):
        sys.exit('Binarios ausentes. Execute: make')
    out = Path(a.output_dir) if a.output_dir else Path(
        f'resultados_{datetime.now().strftime("%Y%m%d_%H%M%S")}')
    out.mkdir(parents=True, exist_ok=True)
    csvpath = out / 'dados_brutos.csv'
    if csvpath.exists():
        sys.exit(f'O arquivo {csvpath} ja existe; escolha outra pasta para nao sobrescrever.')
    conf = {k: v for k, v in vars(a).items() if k not in ('output_dir',)}
    (out / 'configuracao.json').write_text(json.dumps(conf, indent=2, ensure_ascii=False) + '\n')

    order = [(n, rep) for n in range(a.clients_min, a.clients_max + 1)
             for rep in range(1, a.repetitions + 1)]
    random.Random(a.seed).shuffle(order)
    print(f'{len(order)} testes; resultados em {out.resolve()}', flush=True)

    fields = ['datetime', 'clients', 'repetition', 'valid', 'duration_s',
              'warmup_s', 'block_bytes', 'bytes_received', 'mbps',
              'cpu_user_s', 'cpu_system_s', 'cpu_sample_s', 'error']
    with csvpath.open('w', newline='', encoding='utf-8') as cf, \
         (out / 'resultados_detalhados.jsonl').open('w', encoding='utf-8') as jf:
        writer = csv.DictWriter(cf, fieldnames=fields)
        writer.writeheader()
        for i, (n, rep) in enumerate(order, start=1):
            print(f'[{i:02}/{len(order):02}] {n} cliente(s), repeticao {rep}: ',
                  end='', flush=True)
            data = run_one(a, base, n, rep)
            writer.writerow(data['row'])
            cf.flush()
            jf.write(json.dumps(data['raw'], ensure_ascii=False) + '\n')
            jf.flush()
            if data['row']['valid']:
                print(f'{float(data["row"]["mbps"]):.2f} Mbps', flush=True)
            else:
                print(f'INVALIDO: {data["row"]["error"]}', flush=True)
            if i < len(order):
                time.sleep(a.cooldown)

    print('Concluido. Execute: python3 plot_results.py', csvpath)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
