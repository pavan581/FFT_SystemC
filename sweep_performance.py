#!/usr/bin/env python3
import os
import subprocess
import shutil
import re
import json
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

def run_cmd(cmd):
    """Runs a shell command and returns stdout, stderr, and exit code."""
    res = subprocess.run(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    return res.stdout, res.stderr, res.returncode

def main():
    print("=" * 60)
    print(" FFT SystemC Performance Sweep & Visualization (Constant Workload)")
    print("=" * 60)

    # 1. Configuration Setup
    sizes = [4, 8, 16, 256, 1024, 2048]
    configs = [
        {"name": "Config A (1 Mul, 1 Add)", "mult": 1, "add": 1, "color": "#E05A47"}, # Deep Coral
        {"name": "Config B (2 Muls, 2 Adds)", "mult": 2, "add": 2, "color": "#3B82F6"}, # Steel Blue
        {"name": "Config C (4 Muls, 4 Adds)", "mult": 4, "add": 4, "color": "#10B981"}, # Emerald Green
        {"name": "Config D (4 Muls, 6 Adds)", "mult": 4, "add": 6, "color": "#8B5CF6"}  # Royal Purple
    ]
    
    num_cores = 1  # Standalone FFT Core profiling
    hop = 1
    samples = 4096  # Constant workload across all sizes (e.g. 2 blocks for N=2048, 1024 blocks for N=4)
    
    results = {cfg["name"]: [] for cfg in configs}
    raw_data = []

    # Container execution wrapper prefix
    container_prefix = ""

    # Ensure output sweep directories exist
    os.makedirs("out/sweep", exist_ok=True)

    # 2. Run Sweep
    for size in sizes:
        for cfg in configs:
            cfg_name = cfg["name"]
            mult = cfg["mult"]
            add = cfg["add"]
            
            print(f"\n[ SWEEP ] Size={size}, Config={cfg_name} (Mult={mult}, Add={add})")
            
            # Clean container build files
            run_cmd(f"{container_prefix} rm -f build/tb_system.o build/tb_system")
            
            # Compile command inside the container
            cxx_flags = (
                f"-DFFT_N={size} "
                f"-DFFT_NUM_CORES={num_cores} "
                f"-DFFT_HOP={hop} "
                f"-DFFT_SAMPLES={samples} "
                f"-DFFT_NUM_MULT={mult} "
                f"-DFFT_NUM_ADD={add}"
            )
            build_cmd = f"{container_prefix} make build/tb_system EXTRA_CXXFLAGS=\"{cxx_flags}\""
            
            print("  Compiling inside container...")
            build_out, build_err, build_rc = run_cmd(build_cmd)
            if build_rc != 0:
                print(f"  [ ERROR ] Compilation failed! {build_err.strip()}")
                continue
            
            print("  Simulating inside container...")
            sim_out, sim_err, sim_rc = run_cmd(f"{container_prefix} ./build/tb_system")
            if sim_rc != 0:
                print(f"  [ ERROR ] Simulation failed! {sim_err.strip()}")
                continue

            # Parse simulation logs for PERFORMANCE_RESULT
            perf_match = re.search(r"PERFORMANCE_RESULT:\s*(.*)", sim_out)
            if perf_match:
                perf_line = perf_match.group(1)
                kv_pairs = dict(item.split("=") for item in perf_line.split())
                
                cycles = float(kv_pairs["CYCLES"])
                ideal = float(kv_pairs["IDEAL"])
                overhead = float(kv_pairs["OVERHEAD"])
                total_samples = float(kv_pairs["SAMPLES"]) * float(kv_pairs["CORES"])
                
                latency_per_sample = cycles / total_samples
                ideal_per_sample = ideal / total_samples
                overhead_per_sample = overhead / total_samples
                throughput = total_samples / cycles
                
                data_point = {
                    "size": size,
                    "samples": samples,
                    "cycles": cycles,
                    "ideal": ideal,
                    "overhead": overhead,
                    "latency_per_sample": latency_per_sample,
                    "ideal_per_sample": ideal_per_sample,
                    "overhead_per_sample": overhead_per_sample,
                    "throughput": throughput
                }
                
                results[cfg_name].append(data_point)
                raw_data.append({
                    "size": size,
                    "config": cfg_name,
                    "mult": mult,
                    "add": add,
                    "cycles": cycles,
                    "ideal": ideal,
                    "overhead": overhead,
                    "latency_per_sample": latency_per_sample,
                    "ideal_per_sample": ideal_per_sample,
                    "overhead_per_sample": overhead_per_sample,
                    "throughput": throughput
                })
                print(f"  [ SUCCESS ] Cycles={cycles:.0f} (Ideal={ideal:.0f}, Overhead={overhead:.0f}), Throughput={throughput:.4f} samples/cycle")
                
                # Archive trace and logs for this config point into out/sweep/
                log_target = f"out/sweep/sim_N{size}_M{mult}_A{add}.log"
                with open(log_target, "w") as f_log:
                    f_log.write(sim_out)
                    
                trace_source = "out/trace.vcd"
                trace_target = f"out/sweep/trace_N{size}_M{mult}_A{add}.vcd"
                if os.path.exists(trace_source):
                    shutil.copy(trace_source, trace_target)
            else:
                print("  [ ERROR ] Could not parse performance metrics from simulation logs!")
                print(sim_out)

    # 3. Save Raw Data
    with open("out/performance_sweep_results.json", "w") as f:
        json.dump(raw_data, f, indent=4)
    print("\n[ INFO ] Performance sweep results saved to out/performance_sweep_results.json")

    # 4. Generate Light-Themed Dashboard Visualization
    print("[ INFO ] Generating plots...")
    
    # Use clean default light style
    plt.style.use('default')
    matplotlib.rcParams['font.sans-serif'] = "DejaVu Sans"
    matplotlib.rcParams['font.family'] = "sans-serif"
    matplotlib.rcParams['text.color'] = "#2B2B2B"
    matplotlib.rcParams['axes.labelcolor'] = "#2B2B2B"
    matplotlib.rcParams['xtick.color'] = "#2B2B2B"
    matplotlib.rcParams['ytick.color'] = "#2B2B2B"

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 7))
    fig.patch.set_facecolor("#FAFAFA")
    ax1.set_facecolor("#FFFFFF")
    ax2.set_facecolor("#FFFFFF")
    
    fig.suptitle("Performance Of The System With Single FFT Core", fontsize=16, fontweight='bold')
    fig.text(
        0.05, 0.95,
        f"Workload: {samples} samples\
        \n*Config: per Butterfly stage",
        fontsize=12, style='italic'
    )

    x_indices = np.arange(len(sizes))
    bar_width = 0.18

    # 4.1 Plot Throughput (Grouped Column Chart)
    # Add horizontal dashed line at Y=1.0 for peak streaming throughput
    ax1.axhline(1.0, color="#718096", linestyle="--", linewidth=1.2, alpha=0.8)

    for idx, cfg in enumerate(configs):
        cfg_name = cfg["name"]
        data = results[cfg_name]
        if not data:
            continue
        
        throughputs = []
        for s in sizes:
            match = next((d for d in data if d["size"] == s), None)
            throughputs.append(match["throughput"] if match else 0.0)
            
        bar_pos = x_indices + (idx - 1.5) * bar_width
        rects = ax1.bar(bar_pos, throughputs, width=bar_width, color=cfg["color"], 
                        edgecolor="#E2E8F0", linewidth=0.8, alpha=0.95, label=cfg_name)
        
        # Add value labels on top of the throughput bars (rotated 90 deg to avoid overlap)
        ax1.bar_label(rects, fmt='%.3f', padding=4, fontsize=7, rotation=90, color="#4A5568")

    ax1.set_xticks(x_indices)
    ax1.set_xticklabels([f"N={s}" for s in sizes], fontsize=10, fontweight='semibold')
    ax1.set_title("Throughput", fontsize=13, fontweight='bold', pad=15)
    ax1.set_xlabel("FFT Point Size", fontsize=11, fontweight='semibold', labelpad=8)
    ax1.set_ylabel("Throughput (Samples / Clock Cycle)", fontsize=11, fontweight='semibold', labelpad=8)
    ax1.grid(True, which="both", ls=":", color="#E2E8F0", alpha=0.7)
    ax1.legend(frameon=True, facecolor="#FFFFFF", edgecolor="#E2E8F0", loc="best")
    ax1.set_ylim(0.0, 1.8)

    # 4.2 Plot Latency per Sample Breakdown (Grouped Stacked Column Chart)
    for idx, cfg in enumerate(configs):
        cfg_name = cfg["name"]
        data = results[cfg_name]
        if not data:
            continue
        
        ideals = []
        overheads = []
        for s in sizes:
            match = next((d for d in data if d["size"] == s), None)
            if match:
                ideals.append(match["ideal_per_sample"], int(match["ideal"]))
                overheads.append((match["overhead_per_sample"], int(match["overhead"])))
            else:
                ideals.append(0.0)
                overheads.append((0.0, 0))
                
        bar_pos = x_indices + (idx - 1.5) * bar_width
        
        # Plot ideal segment (Solid color)
        ax2.bar(bar_pos, ideals, width=bar_width, color=cfg["color"], 
                edgecolor="#E2E8F0", linewidth=0.8, alpha=0.95)
        
        # Plot overhead segment stacked on top (Semi-transparent + Hatched)
        ax2.bar(bar_pos, list(zip(*overheads))[0], bottom=ideals, width=bar_width, color=cfg["color"], 
                edgecolor=cfg["color"], linewidth=0.8, alpha=0.35, hatch="///")
        
        ## Add percentage overhead labels on top of the stacked bars
        # Add cycle count labels to the stacked bars (X ideal + n overhead)
        for x_pos, ideal_h, overhead_h in zip(bar_pos, ideals, overheads):
            total_h = ideal_h[0] + overhead_h[0]
            if total_h > 0:
                # pct = (overhead_h / total_h) * 100
                # label = f"{pct:.0f}% stalls" if pct >= 1.0 else "0% stalls"
                # ax2.text(x_pos, total_h + 0.15, label, ha='center', va='bottom', 
                #          fontsize=6.5, rotation=90, color="#4A5568")
                # Ideal cycles label (placed inside the bottom bar if tall enough)
                if ideal_h[0] > 0.4:
                    ax2.text(x_pos, ideal_h[0] / 2, f"{ideal_h[1]}", ha='center', va='center', 
                             fontsize=6, rotation=90, color="#FFFFFF", fontweight='bold')
                # Overhead cycles label (placed above the stacked bar)
                ax2.text(x_pos, total_h + 0.15, f"+{overhead_h[1]} cycles", ha='center', va='bottom', 
                         fontsize=6.5, rotation=90, color="#4A5568", fontweight='semibold')

    ax2.set_xticks(x_indices)
    ax2.set_xticklabels([f"N={s}" for s in sizes], fontsize=10, fontweight='semibold')
    ax2.set_title("Latency per Sample Breakdown (Compute + Overhead)", fontsize=13, fontweight='bold', pad=15)
    ax2.set_xlabel("FFT Point Size", fontsize=11, fontweight='semibold', labelpad=8)
    ax2.set_ylabel("Latency (Cycles / Sample)", fontsize=11, fontweight='semibold', labelpad=8)
    ax2.grid(True, which="both", ls=":", color="#E2E8F0", alpha=0.7)
    ax2.set_ylim(0.0, 14.0)

    # Custom legend explaining the configs and stacked breakdown elements
    legend_elements = [
        Patch(facecolor=configs[0]["color"], edgecolor="#E2E8F0", label="Config A (1 Mul, 1 Add)"),
        Patch(facecolor=configs[1]["color"], edgecolor="#E2E8F0", label="Config B (2 Muls, 2 Adds)"),
        Patch(facecolor=configs[2]["color"], edgecolor="#E2E8F0", label="Config C (4 Muls, 4 Adds)"),
        Patch(facecolor=configs[3]["color"], edgecolor="#E2E8F0", label="Config D (4 Muls, 6 Adds)")
    ]
    legend1 = ax2.legend(handles=legend_elements, loc="upper left", frameon=True, facecolor="#FFFFFF", edgecolor="#E2E8F0")
    ax2.add_artist(legend1)
    legend_elements = [
        Patch(facecolor="#718096", edgecolor="#E2E8F0", alpha=0.95, label="Core Compute"),
        Patch(facecolor="#718096", edgecolor="#4A5568", alpha=0.35, hatch="///", label="+n cycles Setup/Overhead/Stalls")
    ]
    ax2.legend(handles=legend_elements, loc="upper left", frameon=True, facecolor="#FFFFFF", edgecolor="#E2E8F0", bbox_to_anchor=(0, 0.85))

    plt.tight_layout()
    plt.savefig("out/fft_performance.png", dpi=150, facecolor=fig.get_facecolor(), edgecolor='none')
    plt.close()

    print("[ SUCCESS ] Plot saved successfully to out/fft_performance.png")
    print("=" * 60)

if __name__ == "__main__":
    main()
