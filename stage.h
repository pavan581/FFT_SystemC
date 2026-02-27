// ============================================================================
// STAGE.H - FFT Pipeline Stage Module
// ============================================================================
// Each stage implements one level of the DIF FFT butterfly decomposition.
// The stage operates in TWO PHASES per N_stage-cycle period:
//
// PHASE 1 (First N/2 cycles): STORE & FORWARD
//   - Store incoming samples in input buffer
//   - Output previous block's difference results from output buffer
//   - Butterfly unit is idle
//
// PHASE 2 (Second N/2 cycles): COMPUTE
//   - Read paired samples from buffer and current input
//   - Perform butterfly operation with appropriate twiddle factor
//   - Output sum immediately
//   - Store difference in output buffer for next block
//
// Features:
// - Two-phase operation pipeline (Store & Forward, Compute)
// - Memory Requirements: N_stage/2 complex samples for input and output buffers
// - Latency Contribution: N_stage/2 + 2 cycles per stage
// ============================================================================

#ifndef STAGE_H
#define STAGE_H

#include "fft_types.h"
#include <vector>
#include <cmath>

SC_MODULE(Stage) {
    sc_in<bool> clk;
    sc_in<bool> rst;
    sc_in<bool> enable;
    sc_in<bool> sync;
    
    sc_in<complex_t> in_data;     // Input sample stream
    sc_out<complex_t> out_data;   // Output sample stream
    
    // ========================================================================
    // Configuration Parameters (set during construction)
    // ========================================================================
    int N_stage;                  // FFT size for this stage (N, N/2, N/4, ...)
    int delay_len;                // Buffer size = N_stage/2
    int init_offset;              // Counter offset for pipeline alignment

    // ========================================================================
    // State Registers
    // ========================================================================
    sc_signal<int> cnt;           // Cycle counter (0 to N_stage-1)
    
    // ========================================================================
    // Memory Buffers
    // ========================================================================
    // Input buffer: Stores first N/2 samples for pairing in second half
    std::vector<complex_t> buf_in;
    
    // Output buffer: Stores difference results for output in next block
    std::vector<complex_t> buf_out;
    
    // ========================================================================
    // Twiddle Factor Calculation
    // ========================================================================
    // Computes the twiddle factor: W_N^k = e^(-j*2π*k/N)
    //   = cos(-2πk/N) + j*sin(-2πk/N)
    //
    // Parameters:
    //   k - Index (0 to N/2-1 for this stage)
    //   n - FFT size for this stage (N_stage)
    //
    // Returns: Complex twiddle factor for rotation
    //
    // Note: For hardware implementation, consider using a lookup table (ROM)
    //       instead of computing trigonometric functions on-the-fly.
    // ========================================================================
    complex_t get_twiddle(int k, int n) {
        const double PI = 3.14159265358979323846;
        double angle = -2.0 * PI * k / n;
        return complex_t(cos(angle), sin(angle));
    }
    // ========================================================================
    // Main Processing Method
    // ========================================================================
    // Main processing method (Sequential logic).
    // Clock-edge triggered method that implements the two-phase pipeline with 0-cycle computational latency.
    //   Phase 1 (count < N/2): Store input data, output stored difference from previous block.
    //   Phase 2 (count >= N/2): Compute butterfly operation, output sum immediately, store difference.
    // ========================================================================
    void process() {
        if (rst.read()) {
            cnt.write(init_offset);
            out_data.write(complex_t(0,0));
            
            std::fill(buf_in.begin(), buf_in.end(), complex_t(0,0));
            std::fill(buf_out.begin(), buf_out.end(), complex_t(0,0));
        } else {
            // Only advance state if enabled
            if (enable.read()) {
                int c = cnt.read();
                if (sync.read()) {
                    c = init_offset;
                    std::fill(buf_in.begin(), buf_in.end(), complex_t(0,0));
                    std::fill(buf_out.begin(), buf_out.end(), complex_t(0,0));
                }
                
                complex_t input = in_data.read();
                complex_t output_val = complex_t(0,0);
                
                // ================================================================
                // INPUT SCHEDULING: Determine operation based on phase
                // ================================================================
                if (c < delay_len) {
                    // ------------------------------------------------------------
                    // PHASE 1: STORE & FORWARD
                    // ------------------------------------------------------------
                    // Store incoming sample for later pairing
                    buf_in[c] = input;
                    
                    // Output stored difference from PREVIOUS block
                    output_val = buf_out[c];
                } else {
                    // ------------------------------------------------------------
                    // PHASE 2: COMPUTE
                    // ------------------------------------------------------------
                    int k = c - delay_len;
                    complex_t val_a = buf_in[k];
                    complex_t val_b = input;
                    
                    complex_t w = get_twiddle(k, N_stage);
                    
                    // Inline Butterfly Computation
                    complex_t sum = val_a + val_b;
                    complex_t diff = (val_a - val_b) * w;
                    
                    // Output Sum Immediately
                    output_val = sum; 
                    
                    // Store Diff for next block
                    buf_out[k] = diff;
                }
                
                out_data.write(output_val);
                cnt.write((c + 1) % N_stage);
            }
        }
    }

    SC_HAS_PROCESS(Stage);
    Stage(sc_module_name name, int n, int id, int offset = 0) : sc_module(name), N_stage(n), init_offset(offset) {
        delay_len = N_stage / 2;
        buf_in.resize(delay_len);
        buf_out.resize(delay_len);
        
        SC_METHOD(process);
        sensitive << clk.pos();
    }
};

#endif
