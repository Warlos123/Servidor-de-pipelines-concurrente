# Ejecución y Pruebas en el Cluster Kabré (CeNAT)

Esta carpeta contiene utilidades automatizadas para sincronizar, compilar, configurar y ejecutar la suite de pruebas y métricas del servidor de pipelines concurrente en el supercomputador **Kabré** mediante **SLURM**.

---

## Archivos incluidos

| Archivo | Dónde se ejecuta | Descripción |
| :--- | :--- | :--- |
| [`sync_to_kabre.sh`](file:///home/deck/pipelines/Servidor-de-pipelines-concurrente/scripts/kabre/sync_to_kabre.sh) | **Máquina local** | Sincroniza el código hacia Kabré vía `rsync` usando el puerto `22022`. |
| [`pull_results.sh`](file:///home/deck/pipelines/Servidor-de-pipelines-concurrente/scripts/kabre/pull_results.sh) | **Máquina local** | Descarga los archivos CSV de resultados desde Kabré hacia `bench/results/`. |
| [`setup_env.sh`](file:///home/deck/pipelines/Servidor-de-pipelines-concurrente/scripts/kabre/setup_env.sh) | **En Kabré** | Carga módulos requeridos (GCC, Python) e instala `libyaml` en `$HOME/.local` si no existe a nivel de sistema. |
| [`run_tests.slurm`](file:///home/deck/pipelines/Servidor-de-pipelines-concurrente/scripts/kabre/run_tests.slurm) | **En Kabré** | Job de SLURM (`sbatch`) para compilar y correr la suite completa de pruebas (`make test`). |
| [`run_bench.slurm`](file:///home/deck/pipelines/Servidor-de-pipelines-concurrente/scripts/kabre/run_bench.slurm) | **En Kabré** | Job de SLURM (`sbatch`) para ejecutar la batería de benchmarks (`bench/run.sh`) y Amdahl. |

---

## Consideraciones y Soluciones a Fallos Comunes

1. **Puerto SSH 22022**:
   - Kabré **no** atiende en el puerto estándar 22; utiliza el puerto **`22022`**.
   - Los scripts `sync_to_kabre.sh` y `pull_results.sh` ya lo configuran automáticamente.
   - Para SSH manual: `ssh -p 22022 <usuario>@kabre.cenat.ac.cr`.

2. **Ubicación de ejecución de los scripts**:
   - `sync_to_kabre.sh` y `pull_results.sh` se corren en tu **máquina local** (Steam Deck / laptop).
   - `setup_env.sh`, `run_tests.slurm` y `run_bench.slurm` se corren **dentro de Kabré**.

3. **Directorio de trabajo en SLURM (`$SLURM_SUBMIT_DIR`)**:
   - SLURM copia los scripts de lote a `/var/spool/slurmd/...` antes de lanzarlos.
   - `run_tests.slurm` y `run_bench.slurm` usan `$SLURM_SUBMIT_DIR` para situarse siempre en la raíz real del repositorio.

4. **Compatibilidad con GCC y C11**:
   - Algunos nodos de Kabré cuentan con GCC 8, que no reconoce `-std=c2x`.
   - El `Makefile` detecta automáticamente si el compilador soporta `c23`, `c2x` o hace fallback seguro a `c11`, garantizando compilación sin errores.

5. **Compatibilidad de glibc (`posix_spawn_file_actions_addchdir_np`)**:
   - Las versiones de Linux corporativo en clusters HPC antiguos tienen `glibc < 2.29`, que carece de esa función no-estándar.
   - El código en `src/exec/job_execute.c` implementa el cambio de directorio de forma estándar y portable mediante shell wrapper.

---

## Configuración recomendada de SSH (`~/.ssh/config`)

Para conectarte simplemente con `ssh kabre` sin tener que escribir el puerto ni usuario cada vez, agrega esto a tu archivo local `~/.ssh/config` (fuera del repositorio, en tu `$HOME`):

```sshconfig
Host kabre
    HostName kabre.cenat.ac.cr
    User <tu_usuario>
    Port 22022
```

---

## Flujo de trabajo paso a paso

### Paso 1: Sincronizar el repositorio hacia Kabré (en tu máquina local)
```bash
./scripts/kabre/sync_to_kabre.sh <tu_usuario>
```

### Paso 2: Conectarse e inicializar el entorno (en Kabré)
```bash
ssh kabre
cd ~/Servidor-de-pipelines-concurrente
bash scripts/kabre/setup_env.sh
```
*(Solo se requiere ejecutar `setup_env.sh` una única vez por cuenta).*

### Paso 3: Ejecutar la suite de pruebas
```bash
sbatch scripts/kabre/run_tests.slurm
```
Monitorear el trabajo y revisar resultados:
```bash
squeue -u $USER
cat tests_*.out
cat tests_*.err
```

### Paso 4: Ejecutar benchmarks y métricas de rendimiento (en Kabré)
```bash
sbatch scripts/kabre/run_bench.slurm
```
Revisar el avance y el informe de Amdahl:
```bash
squeue -u $USER
cat bench_*.out
```

### Paso 5: Descargar los resultados y generar gráficos (en tu máquina local)
1. Descargar los CSV generados en Kabré a tu máquina local:
   ```bash
   ./scripts/kabre/pull_results.sh <tu_usuario>
   ```

2. Generar todos los gráficos comparativos (Kabré vs Local vs Ideal):
   ```bash
   python3 bench/plot.py
   ```
   Los gráficos vectoriales `.svg` quedarán disponibles en `bench/results/plots/`.
