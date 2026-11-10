#!/usr/bin/env bash
# Execute em cada maquina participante: bash coletar_ambiente.sh > ambiente_maquina.txt
set -u
printf 'Data/hora: '; date -Is
printf '\n== Sistema ==\n'; uname -a
if [ -f /etc/os-release ]; then cat /etc/os-release; fi
printf '\n== CPU ==\n'; if command -v lscpu >/dev/null 2>&1; then lscpu; else grep -m 1 'model name' /proc/cpuinfo; fi
printf '\n== Memoria ==\n'; if command -v free >/dev/null 2>&1; then free -h; else cat /proc/meminfo | head; fi
printf '\n== Compilador ==\n'; gcc --version | head -n 1
printf '\n== Interfaces de rede ==\n'; if command -v ip >/dev/null 2>&1; then ip -brief address; ip route; fi
printf '\n== NICs e velocidades disponiveis ==\n'
if command -v ip >/dev/null 2>&1 && command -v ethtool >/dev/null 2>&1; then
  for iface in $(ls /sys/class/net/); do
    echo "Interface: $iface"
    ethtool "$iface" 2>/dev/null | grep -E 'Speed:|Duplex:|Link detected:' || true
  done
fi
printf '\n== Limites ==\n'; ulimit -n
