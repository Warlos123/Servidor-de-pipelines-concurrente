#!/usr/bin/env python3
"""
Generador de gráficos comparativos para los benchmarks del servidor de pipelines.
Genera gráficos vectoriales SVG (sin dependencias externas) en bench/results/plots/.

Uso:
    python3 bench/plot.py
"""
import glob
import math
import os
import sys
from collections import defaultdict

SCENARIOS = {
    "wide": "Escenario Wide (16 tareas independientes, varía agentes)",
    "deep": "Escenario Deep (16 tareas encadenadas por dependencias)",
    "data_wc": "Escenario WordCount (Data task, varía OpenMP threads)",
    "data_hist": "Escenario Histogram (Data task, varía OpenMP threads)",
    "combined": "Escenario Combined (3 pipelines compitiendo por agentes)",
}

COLORS = {
    "kabre": "#2563eb",  # Azul fuerte
    "local": "#ea580c",  # Naranja
    "ideal": "#9ca3af",  # Gris punteado (ideal)
    "amdahl": "#10b981", # Verde
}


def load_csv(path):
    data = defaultdict(lambda: defaultdict(list))
    meta = {}
    with open(path, newline="", encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("#"):
                clean = line.lstrip("# ").strip()
                if ":" in clean:
                    k, v = clean.split(":", 1)
                    meta[k.strip().lower()] = v.strip()
                continue
            parts = line.split(",")
            if len(parts) == 4:
                scen, param, run, sec = parts
                try:
                    data[scen][int(param)].append(float(sec))
                except ValueError:
                    continue

    medians = defaultdict(dict)
    for sc, by_param in data.items():
        for param, vals in by_param.items():
            vals.sort()
            n = len(vals)
            med = vals[n // 2] if n % 2 else (vals[n // 2 - 1] + vals[n // 2]) / 2.0
            medians[sc][param] = med

    raw_host = meta.get("host", os.path.basename(path).split("_")[0]).split("|")[0].strip()
    if "kura" in raw_host or "kabre" in raw_host or "cnca" in raw_host:
        label = "Kabré"
    else:
        label = "Local"

    cpu = meta.get("cpu", "CPU").split("|")[0].strip()
    return {
        "path": path,
        "label": label,
        "cpu": cpu,
        "meta": meta,
        "medians": medians,
    }


def compute_metrics(env_data):
    """Calcula Speedup S(p) y Eficiencia E(p) respecto a p=1 para cada escenario."""
    metrics = defaultdict(dict)
    for scen, points in env_data["medians"].items():
        t1 = points.get(1)
        if not t1:
            continue
        for p, tp in sorted(points.items()):
            sp = t1 / tp if tp > 0 else 0
            ep = sp / p if p > 0 else 0
            metrics[scen][p] = {
                "t": tp,
                "sp": sp,
                "ep": ep,
            }
    return metrics


def generate_svg_line_chart(title, x_label, y_label, series_dict, x_vals, y_max=None, is_efficiency=False):
    """Genera un archivo SVG con un gráfico de líneas limpio, estilizado y legible."""
    width, height = 700, 420
    margin_l, margin_r, margin_t, margin_b = 80, 140, 60, 60
    plot_w = width - margin_l - margin_r
    plot_h = height - margin_t - margin_b

    all_y = [v for s in series_dict.values() for v in s["data"].values()]
    if not all_y:
        all_y = [1.0]

    if y_max is None:
        y_max = max(all_y) * 1.15
        if is_efficiency:
            y_max = max(1.1, y_max)
    y_min = 0.0

    def get_x(val):
        idx = x_vals.index(val)
        return margin_l + (idx / max(1, len(x_vals) - 1)) * plot_w

    def get_y(val):
        return margin_t + plot_h - ((val - y_min) / max(1e-6, y_max - y_min)) * plot_h

    svg = []
    svg.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" '
               f'style="background-color: #ffffff; font-family: -apple-system, BlinkMacSystemFont, Segoe UI, Roboto, Helvetica, Arial, sans-serif;">')

    # Fondo y grid
    svg.append(f'  <rect width="{width}" height="{height}" fill="#ffffff" rx="8"/>')
    svg.append(f'  <text x="{width / 2}" y="32" text-anchor="middle" font-size="16" font-weight="bold" fill="#1f2937">{title}</text>')

    # Grid horizontal
    n_ticks = 5
    for i in range(n_ticks + 1):
        y_val = y_min + (y_max - y_min) * (i / n_ticks)
        y_pos = get_y(y_val)
        svg.append(f'  <line x1="{margin_l}" y1="{y_pos:.1f}" x2="{margin_l + plot_w}" y2="{y_pos:.1f}" stroke="#e5e7eb" stroke-width="1"/>')
        val_str = f"{y_val:.2f}" if y_max < 5 else f"{y_val:.1f}"
        svg.append(f'  <text x="{margin_l - 12}" y="{y_pos + 4:.1f}" text-anchor="end" font-size="11" fill="#6b7280">{val_str}</text>')

    # Grid vertical y etiquetas eje X
    for p in x_vals:
        x_pos = get_x(p)
        svg.append(f'  <line x1="{x_pos:.1f}" y1="{margin_t}" x2="{x_pos:.1f}" y2="{margin_t + plot_h}" stroke="#f3f4f6" stroke-width="1"/>')
        svg.append(f'  <text x="{x_pos:.1f}" y="{margin_t + plot_h + 20}" text-anchor="middle" font-size="12" font-weight="500" fill="#374151">{p}</text>')

    # Títulos de ejes
    svg.append(f'  <text x="{margin_l + plot_w / 2}" y="{height - 15}" text-anchor="middle" font-size="12" fill="#4b5563">{x_label}</text>')
    svg.append(f'  <text x="25" y="{margin_t + plot_h / 2}" text-anchor="middle" transform="rotate(-90 25 {margin_t + plot_h / 2})" font-size="12" fill="#4b5563">{y_label}</text>')

    # Ejes principales
    svg.append(f'  <line x1="{margin_l}" y1="{margin_t}" x2="{margin_l}" y2="{margin_t + plot_h}" stroke="#9ca3af" stroke-width="1.5"/>')
    svg.append(f'  <line x1="{margin_l}" y1="{margin_t + plot_h}" x2="{margin_l + plot_w}" y2="{margin_t + plot_h}" stroke="#9ca3af" stroke-width="1.5"/>')

    # Línea ideal para speedup o eficiencia si corresponde
    if is_efficiency:
        y_ideal = get_y(1.0)
        svg.append(f'  <line x1="{margin_l}" y1="{y_ideal:.1f}" x2="{margin_l + plot_w}" y2="{y_ideal:.1f}" stroke="#9ca3af" stroke-width="1.5" stroke-dasharray="4,4"/>')

    # Dibujar cada serie
    legend_y = margin_t + 10
    for name, s in series_dict.items():
        color = s["color"]
        data = s["data"]
        points = []
        for p in x_vals:
            if p in data:
                points.append((get_x(p), get_y(data[p])))

        if len(points) >= 2:
            path_d = f"M {points[0][0]:.1f} {points[0][1]:.1f} " + " ".join([f"L {pt[0]:.1f} {pt[1]:.1f}" for pt in points[1:]])
            dash = ' stroke-dasharray="5,5"' if s.get("dashed") else ''
            svg.append(f'  <path d="{path_d}" fill="none" stroke="{color}" stroke-width="2.5"{dash}/>')

        for pt in points:
            svg.append(f'  <circle cx="{pt[0]:.1f}" cy="{pt[1]:.1f}" r="4.5" fill="{color}" stroke="#ffffff" stroke-width="1.5"/>')

        # Leyenda
        svg.append(f'  <line x1="{margin_l + plot_w + 15}" y1="{legend_y}" x2="{margin_l + plot_w + 35}" y2="{legend_y}" stroke="{color}" stroke-width="2.5"/>')
        svg.append(f'  <circle cx="{margin_l + plot_w + 25}" cy="{legend_y}" r="3.5" fill="{color}"/>')
        svg.append(f'  <text x="{margin_l + plot_w + 42}" y="{legend_y + 4}" font-size="11" fill="#374151">{name}</text>')
        legend_y += 24

    svg.append('</svg>')
    return "\n".join(svg)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    results_dir = os.path.join(root, "bench", "results")
    plots_dir = os.path.join(results_dir, "plots")
    os.makedirs(plots_dir, exist_ok=True)

    csv_files = glob.glob(os.path.join(results_dir, "*.csv"))
    if not csv_files:
        print("No se encontraron archivos CSV en bench/results/")
        sys.exit(1)

    print(f"[+] Archivos CSV detectados ({len(csv_files)}):")
    envs = []
    for f in sorted(csv_files):
        env = load_csv(f)
        env["metrics"] = compute_metrics(env)
        envs.append(env)
        print(f"    - {env['label']} ({env['cpu']}) -> {os.path.basename(f)}")

    for scen, desc in SCENARIOS.items():
        # Obtener lista unificada de x_vals (p.ej. 1, 2, 4, 8)
        all_p = set()
        for env in envs:
            all_p.update(env["metrics"].get(scen, {}).keys())
        if not all_p:
            continue
        x_vals = sorted(all_p)

        x_title = "Número de hilos OpenMP" if "data" in scen else "Número de agentes"

        # 1. Gráfico de Speedup S(p)
        speedup_series = {}
        for idx, env in enumerate(envs):
            lbl = env["label"].lower()
            c = COLORS["kabre"] if ("kura" in lbl or "kabr" in lbl) else COLORS["local"]
            data_sp = {p: env["metrics"][scen][p]["sp"] for p in x_vals if p in env["metrics"].get(scen, {})}
            speedup_series[env["label"]] = {"color": c, "data": data_sp}

        # Serie ideal lineal S(p) = p
        speedup_series["Ideal (Lineal)"] = {
            "color": COLORS["ideal"],
            "data": {p: float(p) for p in x_vals},
            "dashed": True
        }

        sp_svg = generate_svg_line_chart(
            f"Speedup S(p) - {scen.upper()}",
            x_title,
            "Speedup S(p)",
            speedup_series,
            x_vals
        )
        sp_file = f"{scen}_speedup.svg"
        with open(os.path.join(plots_dir, sp_file), "w", encoding="utf-8") as f:
            f.write(sp_svg)

        # 2. Gráfico de Eficiencia E(p)
        eff_series = {}
        for idx, env in enumerate(envs):
            lbl = env["label"].lower()
            c = COLORS["kabre"] if ("kura" in lbl or "kabr" in lbl) else COLORS["local"]
            data_ep = {p: env["metrics"][scen][p]["ep"] for p in x_vals if p in env["metrics"].get(scen, {})}
            eff_series[env["label"]] = {"color": c, "data": data_ep}

        eff_svg = generate_svg_line_chart(
            f"Eficiencia E(p) - {scen.upper()}",
            x_title,
            "Eficiencia E(p)",
            eff_series,
            x_vals,
            y_max=1.1,
            is_efficiency=True
        )
        eff_file = f"{scen}_efficiency.svg"
        with open(os.path.join(plots_dir, eff_file), "w", encoding="utf-8") as f:
            f.write(eff_svg)

        # 3. Gráfico de Tiempo de Ejecución T(p)
        time_series = {}
        for idx, env in enumerate(envs):
            lbl = env["label"].lower()
            c = COLORS["kabre"] if ("kura" in lbl or "kabr" in lbl) else COLORS["local"]
            data_tp = {p: env["metrics"][scen][p]["t"] for p in x_vals if p in env["metrics"].get(scen, {})}
            time_series[env["label"]] = {"color": c, "data": data_tp}

        time_svg = generate_svg_line_chart(
            f"Tiempo T(p) [segundos] - {scen.upper()}",
            x_title,
            "Tiempo de ejecución (s)",
            time_series,
            x_vals
        )
        time_file = f"{scen}_time.svg"
        with open(os.path.join(plots_dir, time_file), "w", encoding="utf-8") as f:
            f.write(time_svg)

    print(f"\n[✓] Gráficos vectoriales SVG generados exitosamente en:")
    print(f"    {plots_dir}/")


if __name__ == "__main__":
    main()
