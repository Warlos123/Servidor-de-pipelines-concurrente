#!/usr/bin/env bash
#SBATCH --job-name=pipe_tests
#SBATCH --output=tests_%j.out
#SBATCH --error=tests_%j.err
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --time=00:20:00

set -euo pipefail

# Ubicar la raíz del repositorio
if [ -n "${SLURM_SUBMIT_DIR:-}" ]; then
    if [ -f "${SLURM_SUBMIT_DIR}/Makefile" ]; then
        cd "$SLURM_SUBMIT_DIR"
    elif [ -f "${SLURM_SUBMIT_DIR}/../../Makefile" ]; then
        cd "${SLURM_SUBMIT_DIR}/../.."
    fi
elif [ -f "Makefile" ]; then
    : # Ya estamos en la raíz
elif [ -d "$HOME/Servidor-de-pipelines-concurrente" ]; then
    cd "$HOME/Servidor-de-pipelines-concurrente"
fi

echo "=========================================================="
echo " Ejecutando Suite de Pruebas en Kabré"
echo " Fecha:      $(date)"
echo " Host/Nodo:  ${SLURMD_NODENAME:-$(hostname)}"
echo " Directorio: $(pwd)"
echo " CPUs:       ${SLURM_CPUS_PER_TASK:-$(nproc)}"
echo "=========================================================="

# Cargar módulos si están disponibles
if command -v module &>/dev/null || type module &>/dev/null; then
    module load gcc/11* 2>/dev/null || module load gcc 2>/dev/null || true
    module load python/3* 2>/dev/null || module load python 2>/dev/null || true
fi

# Variables de entorno para librerías en $HOME/.local
export CPATH="$HOME/.local/include${CPATH:+:$CPATH}"
export LIBRARY_PATH="$HOME/.local/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"
export LD_LIBRARY_PATH="$HOME/.local/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PATH="$HOME/.local/bin:$PATH"

echo "[1/2] Compilando el proyecto..."
make clean
make -j"${SLURM_CPUS_PER_TASK:-$(nproc)}"

echo "[2/2] Corriendo suite de pruebas (make test)..."
make test

echo "=========================================================="
echo " [✓] Todas las pruebas pasaron exitosamente en Kabré."
echo "=========================================================="
