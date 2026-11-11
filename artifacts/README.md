# Artefactos de demostración

Artefactos producidos por **ejecuciones reales** del motor (`pipelined 9377 4`),
no simulados. Cada subcarpeta corresponde a un artefacto publicado con
`- publish:` en su YAML; la carpeta lleva el nombre del pipeline y dentro,
el nombre del artefacto.

## Resumen de ejecuciones

| Pipeline (examples/*.yaml) | Estado | Duración | Artefactos |
|---|---|---|---|
| `ci_cd.yaml` | succeeded | 0.73s | 7 (servidor bare, estados del remoto, binario, release) |
| `gutenberg.yaml` | succeeded | 0.73s | 4 (libro, wordcount, histograma, estadisticas) |
| `fan_out_fan_in.yaml` | succeeded | 0.19s | 3 |
| `etl.yaml` (camino verde) | succeeded | 13.83s | 13 (raw, limpia, gate, SQLite, dashboard, PNG) |
| `etl.yaml` (camino rojo, `FORZAR_FALLO=1`) | **failed (a propósito)** | 4.65s | gate rechaza → cuarentena corre → load skip |
| `invalid.yaml` | **rechazado en SUBMIT** | — | — |

El camino rojo de `etl.yaml` demuestra las condiciones del motor: el quality gate
rechaza datos corruptos (`condition` del gate falla), `cuarentena` corre gracias a
`condition: failed()`, `limpieza_final` corre siempre (`condition: always()`) y
`cargar`/`visualizar` se saltan para que datos malos no lleguen a la base.
`invalid.yaml` demuestra el validador: se rechaza con `unknown key 'trigger'`,
`timeoutInSeconds` inválido, `condition` inválido.

## Inventario

### ci_cd (ci_cd.yaml)
- `servidor_git/origin.git` — servidor git remoto (bare) con el commit inicial en `main`.
- `servidor_greet/origin.git`, `servidor_math/origin.git` — el server tras cada
  `git push` de las ramas de feature (2 jobs en paralelo).
- `servidor_integrado/origin.git` — el server tras los merges y el `git push origin main`.
- `servidor_final/origin.git` — estado final: main integrado + tag anotado v1.0.0.
- `app_binary/ci_demo` — binario compilado con `gcc -Wall -Wextra -pedantic`.
- `release_v1_0_0/ci_demo_v1.0.0.tar.gz` — release con el binario, fuentes y RELEASE_NOTES.

### gutenberg (gutenberg.yaml)
- `libro_txt/pride_and_prejudice.txt` — 738 KB descargados de gutenberg.org con curl
  (job con `timeoutInSeconds: 120` como red de seguridad de red).
- `resultado_palabras/wordcount.txt` — 128720 palabras, 6837 distintas, top 20.
- `resultado_histograma/histograma.csv` — 32 bins de distribución de bytes.
- `resultado_estadisticas/estadisticas.txt` — 61 capítulos, palabra más larga, etc.

### fan_out_fan_in (fan_out_fan_in.yaml)
- `corpus_shared/` — corpus compartido por 3 jobs en paralelo (fan-out).
- `resultado_words/words.txt`, `resultado_hist/hist.csv` — consolidados en el fan-in.

### etl (etl.yaml) — camino verde
- `raw_*/` — JSON crudos descargados de Open-Meteo (San José, Madrid, Tokio).
- `limpia_*/` — CSV limpios: dedup, fechas DD/MM/YYYY, nulos descartados, t_media.
- `reporte_calidad/reporte_calidad.json` — veredicto del quality gate (APROBADO).
- `resumen/resumen.csv` — agregados por ciudad (AVG/SUM/MAX en SQL).
- `dashboard/dashboard.html` + `grafica_*/` — dashboard HTML autocontenido
  (PNG embebidos en base64) y las 2 gráficas matplotlib.

## Qué features del motor cubre cada ejemplo

| Feature | Demonstrada en |
|---|---|
| Stages con `dependsOn` y jobs en paralelo | ci_cd (5 stages), gutenberg, fan_out_fan_in |
| `publish` / `download` entre stages | ci_cd, gutenberg, fan_out_fan_in |
| Tasks nativos `WordCount@1` / `Histogram@1` | gutenberg, fan_out_fan_in |
| Variables del pipeline + `$(VAR)` | gutenberg |
| `timeoutInSeconds` (job de red) | gutenberg (job `fetch`) |
| `condition: failed()` / `always()` | etl.yaml (cuarentena y limpieza_final) |
| Validación estricta de YAML | invalid.yaml (rechazado con todos los errores) |
| Compilación real, tests con exit 1, release | ci_cd |

## Cómo regenerar

```sh
./bin/pipelined 9377 4 /tmp/pipelines &
for f in ci_cd gutenberg fan_out_fan_in etl; do
  ./bin/pipectl 127.0.0.1 9377 SUBMIT examples/$f.yaml
done
# artefactos en /tmp/pipelines/artifacts/<run_id>/<artifact>/
./bin/pipectl 127.0.0.1 9377 SHUTDOWN
```

## Verificación independiente

```sh
git ls-remote artifacts/ci_cd/servidor_final/origin.git      # main + tag v1.0.0
git clone artifacts/ci_cd/servidor_final/origin.git /tmp/prueba && git -C /tmp/prueba log --oneline --graph
./artifacts/ci_cd/app_binary/ci_demo                        # 7/7 checks
tar -tzf artifacts/ci_cd/release_v1_0_0/ci_demo_v1.0.0.tar.gz
sha256sum artifacts/gutenberg/libro_txt/pride_and_prejudice.txt
sqlite3 artifacts/etl/etl_db/etl.db "SELECT * FROM agregados"   # datos cargados
python3 -c "import json; print(json.load(open('artifacts/etl/reporte_calidad/reporte_calidad.json'))['resultado'])"
```

## Notas

- `libro_txt/` (~738 KB, dominio público) y los corpus sintéticos no se versionan:
  se regeneran con el pipeline. Ver `.gitignore`.
- Correcciones aplicadas a los YAML para que pasaran validación/ejecución:
  `fan_out_fan_in.yaml` generaba su corpus con un `script: >` *folded* que
  colapsaba los saltos del python (`IndentationError`); se cambió a bloque
  literal `|`.
