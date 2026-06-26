import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from scipy.fft import fft, fftfreq


NUM_CORES = 6
N = 8
dt = 1e-9

input_files = "out/data/core{}_input.csv"
output_files = "out/data/core{}_output.csv"

for i in range(NUM_CORES):
    input_df = pd.read_csv(input_files.format(i))
    output_df = pd.read_csv(output_files.format(i))
    
    input_df['Timestamp'] = pd.to_timedelta(input_df["Timestamp"]).dt.total_seconds().mul(1e9).astype(int)
    input_df['data'] = input_df['Real'] + 1j * input_df['Imaginary']
    
    output_df['Timestamp'] = pd.to_timedelta(output_df["Timestamp"]).dt.total_seconds().mul(1e9).astype(int)
    output_df['data'] = output_df['Real'] + 1j * output_df['Imaginary']

    in_data = np.pad(input_df['data'], (0, -len(input_df) % N)).reshape(-1, N)
    expected_output = fft(in_data, axis=-1).flatten()

    num_bits = int(np.log2(N))
    order = [int(f"{b:0{num_bits}b}"[::-1], 2) for b in range(N)]
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
    plt.stem(fftfreq(len(output_df), dt) / 1e6, np.abs(output_data), linefmt='orangered', markerfmt='or')
    plt.title("Simulated Output Signal")
    plt.xlabel("Frequency (Hz)")
    plt.ylabel("Magnitude")
    plt.grid(True)
    
    plt.tight_layout()
    plt.savefig("out/img/core{}_output_plot.png".format(i))
    plt.close()
