import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from scipy.fft import fft, fftfreq
import argparse
import os

def main():
    parser = argparse.ArgumentParser(description="Plot FFT Input and Output signals.")
    parser.add_argument('--num_cores', type=int, default=6, help="Number of cores")
    parser.add_argument('--n', type=int, default=8, help="FFT point size N")
    parser.add_argument('--input_files', type=str, default="out/data/core{}_input.csv", help="Input file path template")
    parser.add_argument('--output_files', type=str, default="out/data/core{}_output.csv", help="Output file path template")
    parser.add_argument('--out_img_dir', type=str, default="out/img", help="Output directory for generated plots")
    
    args = parser.parse_args()
    
    NUM_CORES = args.num_cores
    N = args.n
    dt = 1e-9
    
    input_files = args.input_files
    output_files = args.output_files
    out_img_dir = args.out_img_dir
    
    os.makedirs(out_img_dir, exist_ok=True)

    for i in range(NUM_CORES):
        in_path = input_files.format(i)
        out_path = output_files.format(i)
        
        if not os.path.exists(in_path) or not os.path.exists(out_path):
            print(f"Skipping core {i}: files not found ({in_path} or {out_path})")
            continue
            
        input_df = pd.read_csv(in_path)
        output_df = pd.read_csv(out_path)
        
        input_df['Timestamp'] = pd.to_timedelta(input_df["Timestamp"]).dt.total_seconds().mul(1e9).astype(int)
        input_df['data'] = input_df['Real'] + 1j * input_df['Imaginary']
        
        output_df['Timestamp'] = pd.to_timedelta(output_df["Timestamp"]).dt.total_seconds().mul(1e9).astype(int)
        output_df['data'] = output_df['Real'] + 1j * output_df['Imaginary']

        in_data = np.pad(input_df['data'], (0, -len(input_df) % N)).reshape(-1, N)
        expected_output = fft(in_data, axis=-1).flatten()

        # Handle N=1 edge case for bit-reversal
        if N > 1:
            num_bits = int(np.log2(N))
            order = [int(f"{b:0{num_bits}b}"[::-1], 2) for b in range(N)]
        else:
            order = [0]
            
        output_data = np.pad(output_df['data'], (0, -len(output_df) % N)).reshape(-1, N)[:, order].flatten()

        plt.figure(figsize=(12, 5))

        plt.subplot(3, 1, 1)
        plt.plot(input_df['Timestamp'], np.abs(input_df['data']), color='blue', marker='.', markersize=4)
        plt.title("Core{} Input Signal".format(i))
        plt.xlabel("Time (ns)")
        plt.ylabel("Magnitude")
        plt.grid(True)

        plt.subplot(3, 1, 2)
        plt.stem(fftfreq(len(expected_output), dt) / 1e6, np.abs(expected_output), linefmt='orangered', markerfmt='or')
        plt.title("Expected Output Signal")
        plt.xlabel("Frequency (Hz)")
        plt.ylabel("Magnitude")
        plt.grid(True)

        plt.subplot(3, 1, 3)
        plt.stem(fftfreq(len(output_data), dt) / 1e6, np.abs(output_data), linefmt='orangered', markerfmt='or')
        plt.title("Simulated Output Signal")
        plt.xlabel("Frequency (Hz)")
        plt.ylabel("Magnitude")
        plt.grid(True)
        
        plt.tight_layout()
        plt.savefig(os.path.join(out_img_dir, "core{}_output_plot.png".format(i)))
        plt.close()

if __name__ == '__main__':
    main()
