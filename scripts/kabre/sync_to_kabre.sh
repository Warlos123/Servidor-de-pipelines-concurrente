#!/usr/bin/env bash
#
# sync_to_kabre.sh: Sincroniza este repositorio local hacia el cluster Kabré vía rsync.
# Uso:
#   ./scripts/kabre/sync_to_kabre.sh <tu_usuario> [directorio_remoto] [puerto]
#
set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Uso: $0 <usuario_kabre> [directorio_remoto] [puerto]"
    echo "Ejemplo: $0 usuario"
    echo "         $0 usuario ~/Servidor-de-pipelines-concurrente"
    echo "Nota: El puerto por defecto en Kabré es 22022."
    exit 1
fi

USER_KABRE="$1"
REMOTE_HOST="kabre.cenat.ac.cr"
REMOTE_TARGET="${2:-~/Servidor-de-pipelines-concurrente}"
SSH_PORT="${3:-22022}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "=========================================================="
echo " Sincronizando repositorio local con Kabré"
echo " Origen:  $REPO_ROOT"
echo " Destino: ${USER_KABRE}@${REMOTE_HOST}:${REMOTE_TARGET}"
echo " Puerto:  ${SSH_PORT}"
echo "=========================================================="

rsync -avz --progress -e "ssh -p ${SSH_PORT}" \
    --exclude='.git' \
    --exclude='build/' \
    --exclude='bin/' \
    --exclude='bench/data/' \
    --exclude='bench/results/*.csv' \
    --exclude='*.out' \
    --exclude='*.err' \
    --exclude='*.o' \
    "$REPO_ROOT/" "${USER_KABRE}@${REMOTE_HOST}:${REMOTE_TARGET}/"

echo "=========================================================="
echo " [✓] Sincronización finalizada con éxito."
echo ""
echo " Pasos siguientes en Kabré:"
echo "   1. ssh -p ${SSH_PORT} ${USER_KABRE}@${REMOTE_HOST}"
echo "   2. cd ${REMOTE_TARGET}"
echo "   3. bash scripts/kabre/setup_env.sh"
echo "   4. sbatch scripts/kabre/run_tests.slurm"
echo "=========================================================="
