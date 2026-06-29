#!/usr/bin/env python3
import os
import shutil
import subprocess
import time
import json
from generate_stimulus import generate_stimulus_file

# List of test configurations
# Loaded from test_configs.json if it exists, otherwise falls back to a default list
config_file = "test_configs.json"
if os.path.exists(config_file):
    with open(config_file, "r") as f:
        TEST_CONFIGS = json.load(f)
else:
    TEST_CONFIGS = [
    {"case": "test_default", 
    "params": {
        "N": 8, 
        "NUM_CORES": 6, 
        "HOP": 1, 
        "SAMPLES": 256, 
        "use_file_stim": False } 
    }
]

def run_command(cmd, env=None):
    """Runs a shell command and returns output, error, and return code."""
    res = subprocess.run(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
    return res.stdout, res.stderr, res.returncode

def main():
    print("=" * 60)
    print(" FFT SystemC Multi-Configuration Test Runner")
    print("=" * 60)
    
    # Base directory for test runs
    test_runs_dir = "out/test_runs"
    # if os.path.exists(test_runs_dir):
    #     shutil.rmtree(test_runs_dir)
    os.makedirs(test_runs_dir, exist_ok=True)
    
    results = []
    
    for config in TEST_CONFIGS:
        name = config["case"]
        params = config["params"]
        N = params["N"]
        num_cores = params["NUM_CORES"]
        hop = params["HOP"] if "HOP" in params else 1
        samples = params["SAMPLES"]
        num_adds = params["NUM_ADDS"] if "NUM_ADDS" in params else 6
        num_muls = params["NUM_MULS"] if "NUM_MULS" in params else 4
        use_file_stim = params["use_file_stim"]
        print(f"\n[ RUNNING ] {name} (N={N}, Cores={num_cores}, Hop={hop}, Samples={samples}, Adds={num_adds}, Muls={num_muls})")
        
        # Clean build artifacts of tb_system to force rebuild with new parameters
        run_command("rm -f build/tb_system.o build/tb_system")
        
        # Build command with configuration parameters passed as compiler macros
        cxx_flags = (
            f"-DFFT_N={N} "
            f"-DFFT_NUM_CORES={num_cores} "
            f"-DFFT_HOP={hop} "
            f"-DFFT_SAMPLES={samples} "
            f"-DFFT_NUM_MULT={num_muls} "
            f"-DFFT_NUM_ADD={num_adds}"
        )
        if use_file_stim:
            cxx_flags += " -DUSE_SLAVE_FROM_FILE"
            
        build_cmd = f"make build/tb_system EXTRA_CXXFLAGS=\"{cxx_flags}\""
        
        print("  Compiling testbench...")
        build_out, build_err, build_rc = run_command(build_cmd)
        if build_rc != 0:
            print("  [ FAILED ] Compilation failed!")
            print(build_err)
            results.append((name, "COMPILATION_FAILED", 0.0))
            continue
            
        # Prepare run environment
        run_env = os.environ.copy()
        sim_out_dir = os.path.join(test_runs_dir, name)
        run_env["SIM_OUT_DIR"] = sim_out_dir
        if os.path.exists(sim_out_dir):
            print("Overwriting previous test run")
        os.makedirs(sim_out_dir, exist_ok=True)
        
        if use_file_stim:
            run_env["STIMULUS_FILE"] = os.path.join(sim_out_dir, "stimulus_core_%d.csv")
            
        # Execute simulation
        print("  Simulating...")
        start_time = time.time()
        # Direct execution of compiled binary
        sim_out, sim_err, sim_rc = run_command("./build/tb_system", env=run_env)
        elapsed = time.time() - start_time
        
        # Save logs
        with open(os.path.join(sim_out_dir, "sim_log.txt"), "w") as f:
            f.write("=== STDOUT ===\n")
            f.write(sim_out)
            f.write("\n=== STDERR ===\n")
            f.write(sim_err)
            
        # Analyze outcome
        if "TESTBENCH PASS" in sim_out and sim_rc == 0:
            print(f"  [ PASSED ] in {elapsed:.2f} seconds")
            results.append((name, "PASSED", elapsed))
            
            # Run plot output script
            print("  Generating output plots...")
            plot_cmd = (
                f"python3 plot_fft_output.py "
                f"--num_cores {num_cores} "
                f"--n {N} "
                f"--input_files \"{os.path.join(sim_out_dir, 'data/core{}_input.csv')}\" "
                f"--output_files \"{os.path.join(sim_out_dir, 'data/core{}_output.csv')}\" "
                f"--out_img_dir \"{os.path.join(sim_out_dir, 'img')}\""
            )
            plot_out, plot_err, plot_rc = run_command(plot_cmd)
            if plot_rc != 0:
                print(f"  [ WARNING ] Plot generation failed: {plot_err.strip()}")
        else:
            print(f"  [ FAILED ] Simulation failed! Check logs at {sim_out_dir}/sim_log.txt")
            results.append((name, "FAILED", elapsed))
            
    # Print Summary Table
    print("\n" + "=" * 60)
    print(" Test Execution Summary")
    print("=" * 60)
    print(f"{'Test Name':<25} | {'Status':<20} | {'Duration (s)':<12}")
    print("-" * 60)
    for name, status, duration in results:
        print(f"{name:<25} | {status:<20} | {duration:<12.2f}")
    print("=" * 60)
    
if __name__ == "__main__":
    main()
