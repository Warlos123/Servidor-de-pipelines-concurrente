#!/usr/bin/env bash
#
# pull_results.sh: Descarga los resultados de Kabré hacia tu máquina local.
# Uso:
#   ./scripts/kabre/pull_results.sh [usuario_kabre]
#
set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Uso: $0 <usuario_kabre> [directorio_remoto] [puerto]"
    echo "Ejemplo: $0 usuario"
    exit 1
fi

USER_KABRE="$1"
REMOTE_HOST="kabre.cenat.ac.cr"
SSH_PORT="${3:-22022}"
REMOTE_TARGET="${2:-~/Servidor-de-pipelines-concurrente}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

mkdir -p "$REPO_ROOT/bench/results"

echo "=========================================================="
echo " Descargando resultados de Kabré a tu máquina local"
echo " Origen:  ${USER_KABRE}@${REMOTE_HOST}:${REMOTE_TARGET}/bench/results/"
echo " Destino: $REPO_ROOT/bench/results/"
echo "=========================================================="

rsync -avz --progress -e "ssh -p ${SSH_PORT}" \
    "${USER_KABRE}@${REMOTE_HOST}:${REMOTE_TARGET}/bench/results/*.csv" \
    "$REPO_ROOT/bench/results/"

echo "=========================================================="
echo " [✓] Resultados descargados exitosamente."
echo " Archivos en local:"
ls -la "$REPO_ROOT/bench/results/"
echo "=========================================================="
