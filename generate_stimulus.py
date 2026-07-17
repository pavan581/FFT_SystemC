import numpy as np
import os

def generate_stimulus_file(filename, samples, core_id):
    """Generates standard address_hex,data_hex CSV file matching SlaveFromFile format."""
    os.makedirs(os.path.dirname(filename), exist_ok=True)
    
    # We align the waveforms to a base period of 8 samples so they are periodic
    # and fit cleanly within FFT block sizes of 8, 16, etc.
    period = 8.0
    amplitude = 20.0  # Fits inside signed 16-bit range (-32768 to 32767)
    
    real_data = np.zeros(samples)
    imag_data = np.zeros(samples)
    
    t = np.arange(samples)
    
    if core_id == 0:
        # Core 0: Multi-tone signal matching user's config with fs = 128
        t_digital = t / 128.0
        real_data = amplitude * (
            1.0 * np.sin(2 * np.pi * 10.0 * t_digital) +
            0.7 * np.sin(2 * np.pi * 25.0 * t_digital) +
            0.8 * np.sin(2 * np.pi * 35.0 * t_digital) +
            0.5 * np.sin(2 * np.pi * 50.0 * t_digital)
        )
        
    elif core_id == 1:
        # Core 1: Pure Cosine Wave (2 cycles per 8 samples)
        real_data = amplitude * np.cos(2 * np.pi * 2.0 * t / period)
        
    elif core_id == 2:
        # Core 2: Complex Exponential (1 cycle per 8 samples)
        # x(t) = exp(j * 2 * pi * t / period)
        real_data = amplitude * np.cos(2 * np.pi * t / period)
        imag_data = amplitude * np.sin(2 * np.pi * t / period)
        
    elif core_id == 3:
        # Core 3: Dual Tone (sum of two sine waves of different frequencies)
        real_data = 0.5 * amplitude * (np.sin(2 * np.pi * t / period) + np.sin(2 * np.pi * 3.0 * t / period))
        
    elif core_id == 4:
        # Core 4: Square Wave (period 8)
        real_data = amplitude * np.sign(np.sin(2 * np.pi * t / period))
        # Ensure we don't have zeros from np.sign if sin is exactly zero
        real_data[real_data == 0] = amplitude
        
    elif core_id == 5:
        # Core 5: Triangle Wave (period 8)
        real_data = amplitude * (2.0 * np.abs(2.0 * (t / period - np.floor(t / period + 0.5))) - 1.0)
        
    else:
        # Default fallback: Sine wave with varying frequency
        freq = 1.0 + (core_id % 3)
        real_data = amplitude * np.sin(2 * np.pi * freq * t / period)

    with open(filename, 'w') as f:
        for i in range(samples):
            byte_addr = i * 8
            # Sign-extend the 16-bit values to 32 bits to correctly represent negative numbers
            real_val = int(np.round(real_data[i])) & 0xFFFFFFFF
            imag_val = int(np.round(imag_data[i])) & 0xFFFFFFFF
            packed_val = (real_val << 32) | imag_val
            f.write(f"0x{byte_addr:08x},0x{packed_val:016x}\n")

if __name__ == "__main__":
    NUM_CORES = 2
    SAMPLES = 128
    for i in range(NUM_CORES):
        generate_stimulus_file("out/test_runs/test_n8_file_stim/stimulus_core_{}.csv".format(i+1), SAMPLES, i)
