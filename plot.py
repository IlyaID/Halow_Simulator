#!/usr/bin/env python3
import os
import json
import glob
import pandas as pd
import matplotlib.pyplot as plt

# Базовый каталог с сериями экспериментов
BASE_DIR = "runs"  # структура вида runs/exp1/{summary.csv,energy_per_node.csv,throughput_per_node.csv,scenario.json}

def load_experiment(run_dir):
    """Читает summary, energy_per_node, throughput_per_node и scenario.json из каталога run_dir."""
    summary_path = os.path.join(run_dir, "summary.csv")
    energy_path = os.path.join(run_dir, "energy_per_node.csv")
    thr_node_path = os.path.join(run_dir, "throughput_per_node.csv")
    scenario_path = os.path.join(run_dir, "scenario.json")

    if not os.path.exists(summary_path):
        return None

    summary = pd.read_csv(summary_path)
    energy = pd.read_csv(energy_path) if os.path.exists(energy_path) else None
    thr_node = pd.read_csv(thr_node_path) if os.path.exists(thr_node_path) else None

    scenario = {}
    if os.path.exists(scenario_path):
      with open(scenario_path, "r") as f:
          scenario = json.load(f)

    return {
        "dir": run_dir,
        "summary": summary,
        "energy": energy,
        "thr_node": thr_node,
        "scenario": scenario,
    }

def collect_experiments(base_dir):
    runs = []
    for d in sorted(glob.glob(os.path.join(base_dir, "*"))):
        if os.path.isdir(d):
            exp = load_experiment(d)
            if exp is not None:
                runs.append(exp)
    return runs

def plot_throughput_vs_nsta(runs, out_png="throughput_vs_nsta.png"):
    xs = []
    ys = []
    for r in runs:
        sc = r["scenario"]
        if "nSta" not in sc:
            continue
        nsta = sc["nSta"]
        thr = float(r["summary"]["throughput_bps"].iloc[0])
        xs.append(nsta)
        ys.append(thr)
    if not xs:
        print("No data for throughput_vs_nsta")
        return
    df = pd.DataFrame({"nSta": xs, "throughput_bps": ys}).sort_values("nSta")
    plt.figure()
    plt.plot(df["nSta"], df["throughput_bps"], marker="o")
    plt.xlabel("Number of STAs")
    plt.ylabel("Aggregate throughput (bps)")
    plt.grid(True)
    plt.tight_layout()
    plt.savefig(out_png)
    plt.close()
    print(f"Saved {out_png}")

def plot_bits_per_joule_vs_nsta(runs, out_png="bits_per_joule_vs_nsta.png"):
    xs = []
    ys = []
    for r in runs:
        sc = r["scenario"]
        if "nSta" not in sc:
            continue
        nsta = sc["nSta"]
        bpj = float(r["summary"]["bits_per_joule"].iloc[0])
        xs.append(nsta)
        ys.append(bpj)
    if not xs:
        print("No data for bits_per_joule_vs_nsta")
        return
    df = pd.DataFrame({"nSta": xs, "bits_per_joule": ys}).sort_values("nSta")
    plt.figure()
    plt.plot(df["nSta"], df["bits_per_joule"], marker="o")
    plt.xlabel("Number of STAs")
    plt.ylabel("Bits per Joule")
    plt.grid(True)
    plt.tight_layout()
    plt.savefig(out_png)
    plt.close()
    print(f"Saved {out_png}")

def plot_delay_cdf(results_csv, out_png="delay_cdf.png"):
    """Строит CDF задержки по results.csv одного эксперимента."""
    df = pd.read_csv(results_csv)
    # delay_us есть только для успешных пакетов (как ты записывал)
    if "delay_us" not in df.columns:
        print("No delay_us in", results_csv)
        return
    delays = df["delay_us"]
    delays = delays[delays > 0]
    if delays.empty:
        print("No delays in", results_csv)
        return
    delays = delays.sort_values()
    y = (pd.Series(range(1, len(delays)+1)) / len(delays))
    plt.figure()
    plt.step(delays.values, y.values, where="post")
    plt.xlabel("Delay (us)")
    plt.ylabel("CDF")
    plt.grid(True)
    plt.tight_layout()
    plt.savefig(out_png)
    plt.close()
    print(f"Saved {out_png}")

def main():
    runs = collect_experiments(BASE_DIR)
    if not runs:
        print("No experiments found under", BASE_DIR)
        return

    # График throughput vs nSta
    plot_throughput_vs_nsta(runs)

    # График bits/J vs nSta
    plot_bits_per_joule_vs_nsta(runs)

    # Пример: CDF задержки для одного эксперимента (первого)
    first_results = os.path.join(runs[0]["dir"], "results.csv")
    if os.path.exists(first_results):
        plot_delay_cdf(first_results)

if __name__ == "__main__":
    main()
