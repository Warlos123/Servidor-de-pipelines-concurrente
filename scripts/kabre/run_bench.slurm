#!/usr/bin/env bash
#SBATCH --job-name=pipe_bench
#SBATCH --output=bench_%j.out
#SBATCH --error=bench_%j.err
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --time=01:30:00

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
echo " Ejecutando Benchmarks y Métricas en Kabré"
echo " Fecha:      $(date)"
echo " Host/Nodo:  ${SLURMD_NODENAME:-$(hostname)}"
echo " Directorio: $(pwd)"
echo " CPUs:       ${SLURM_CPUS_PER_TASK:-$(nproc)}"
echo "=========================================================="

if command -v module &>/dev/null || type module &>/dev/null; then
    module load gcc/11* 2>/dev/null || module load gcc 2>/dev/null || true
    module load python/3* 2>/dev/null || module load python 2>/dev/null || true
fi

export CPATH="$HOME/.local/include${CPATH:+:$CPATH}"
export LIBRARY_PATH="$HOME/.local/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"
export LD_LIBRARY_PATH="$HOME/.local/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PATH="$HOME/.local/bin:$PATH"

echo "[1/3] Compilando binarios de servidor, cliente y generador de corpus..."
make -j"${SLURM_CPUS_PER_TASK:-$(nproc)}"

echo "[2/3] Ejecutando batería de métricas (bench/run.sh all)..."
bash bench/run.sh all

echo "[3/3] Generando análisis de Speedup, Eficiencia y Ley de Amdahl..."
python3 bench/analyze.py all

echo "=========================================================="
echo " [✓] Benchmarks completados. Los resultados están en bench/results/"
echo "=========================================================="
