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
    parser.add_argument('--fs', type=float, default=1e9, help="Sampling frequency (default: 1e9 for 1 GHz)")
    
    args = parser.parse_args()
    
    NUM_CORES = args.num_cores
    N = args.n
    dt = 1.0 / args.fs
    
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
        expected_fft = fft(in_data, axis=-1)
        expected_spectrum = np.mean(np.abs(expected_fft), axis=0)

        # Handle N=1 edge case for bit-reversal
        if N > 1:
            num_bits = int(np.log2(N))
            order = [int(f"{b:0{num_bits}b}"[::-1], 2) for b in range(N)]
        else:
            order = [0]
            
        output_blocks = np.pad(output_df['data'], (0, -len(output_df) % N)).reshape(-1, N)[:, order]
        simulated_spectrum = np.mean(np.abs(output_blocks), axis=0)

        # Normalize FFT magnitude by 2/N (or 1/N for N=1) so the peak represents physical amplitude
        scale_factor = 2.0 / N if N > 1 else 1.0
        expected_spectrum_norm = scale_factor * expected_spectrum
        simulated_spectrum_norm = scale_factor * simulated_spectrum

        # Shift frequencies and spectra to center around 0 Hz
        fs = args.fs
        if fs >= 1e6:
            freq_scale = 1e6
            freq_unit = "MHz"
        elif fs >= 1e3:
            freq_scale = 1e3
            freq_unit = "kHz"
        else:
            freq_scale = 1.0
            freq_unit = "Hz"

        freqs = fftfreq(N, dt)
        freqs_shifted = np.fft.fftshift(freqs) / freq_scale
        expected_shifted = np.fft.fftshift(expected_spectrum_norm)
        simulated_shifted = np.fft.fftshift(simulated_spectrum_norm)

        max_time = input_df['Timestamp'].max()
        show_zoom = max_time > 800
        num_rows = 4 if show_zoom else 3

        plt.figure(figsize=(12, 10 if show_zoom else 8))
        plt.suptitle("Core {} - FFT Analysis (N={})".format(i, N), fontsize=16, fontweight='bold', color='#2C3E50')

        # Time-domain plot
        plt.subplot(num_rows, 1, 1)
        plt.plot(input_df['Timestamp'], np.abs(input_df['data']), color='#2C3E50', linewidth=1.5)
        plt.fill_between(input_df['Timestamp'], np.abs(input_df['data']), color='#34495E', alpha=0.1)
        plt.title("Time Domain Input Signal", fontsize=12, fontweight='bold', color='#2C3E50')
        plt.xlabel("Time (ns)", fontsize=10)
        plt.ylabel("Amplitude", fontsize=10)
        plt.grid(True, linestyle='--', alpha=0.6)

        current_row = 2

        # Zoomed in time domain plot (only shown if time scale > 800 ns)
        if show_zoom:
            plt.subplot(num_rows, 1, current_row)
            plt.plot(input_df['Timestamp'], np.abs(input_df['data']), color='#2C3E50', linewidth=1.5)
            plt.fill_between(input_df['Timestamp'], np.abs(input_df['data']), color='#34495E', alpha=0.1)
            plt.title("Time Domain Input Signal (First 200 ns)", fontsize=12, fontweight='bold', color='#2C3E50')
            plt.xlabel("Time (ns)", fontsize=10)
            plt.ylabel("Amplitude", fontsize=10)
            plt.grid(True, linestyle='--', alpha=0.6)
            plt.xlim(0, 200)
            current_row += 1

        # Expected Spectrum
        plt.subplot(num_rows, 1, current_row)
        markerline, stemlines, baseline = plt.stem(freqs_shifted, expected_shifted, linefmt='#E74C3C', markerfmt='o')
        plt.setp(markerline, 'color', '#E74C3C', 'markersize', 5)
        plt.setp(stemlines, 'color', '#E74C3C', 'linewidth', 1.5)
        plt.setp(baseline, 'color', '#E74C3C', 'linewidth', 1.0)
        plt.title("Expected Frequency Spectrum (Average Magnitude)", fontsize=12, fontweight='bold', color='#2C3E50')
        plt.xlabel(f"Frequency ({freq_unit})", fontsize=10)
        plt.ylabel("Amplitude", fontsize=10)
        plt.grid(True, linestyle='--', alpha=0.6)
        current_row += 1

        # Simulated Spectrum
        plt.subplot(num_rows, 1, current_row)
        markerline, stemlines, baseline = plt.stem(freqs_shifted, simulated_shifted, linefmt='#16A085', markerfmt='o')
        plt.setp(markerline, 'color', '#16A085', 'markersize', 5)
        plt.setp(stemlines, 'color', '#16A085', 'linewidth', 1.5)
        plt.setp(baseline, 'color', '#16A085', 'linewidth', 1.0)
        plt.title("Simulated Frequency Spectrum (Average Magnitude)", fontsize=12, fontweight='bold', color='#2C3E50')
        plt.xlabel(f"Frequency ({freq_unit})", fontsize=10)
        plt.ylabel("Amplitude", fontsize=10)
        plt.grid(True, linestyle='--', alpha=0.6)
        
        plt.tight_layout()
        plt.savefig(os.path.join(out_img_dir, "core{}_output_plot.png".format(i)))
        plt.close()

if __name__ == '__main__':
    main()
