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
    
    // ALU Resource FSM State
    int num_mult;
    int num_add;
    int alu_cycles;
    sc_signal<int> bf_state;
    sc_signal<complex_t> bf_sum;
    sc_signal<complex_t> bf_diff;
    sc_signal<int> bf_k;
    int computed_alu_cycles;
    
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
            bf_state.write(0);
            
            std::fill(buf_in.begin(), buf_in.end(), complex_t(0,0));
            std::fill(buf_out.begin(), buf_out.end(), complex_t(0,0));
        } else {
            // ================================================================
            // FSM: Multi-cycle Butterfly Computation (runs every clock)
            // ================================================================
            if (alu_cycles > 1 && bf_state.read() > 0) {
                int current_state = bf_state.read();
                if (current_state == 1) {
                    // Compute finished: Output sum and store diff
                    out_data.write(bf_sum.read());
                    buf_out[bf_k.read()] = bf_diff.read();
                    bf_state.write(0);
                } else {
                    bf_state.write(current_state - 1);
                }
            }

            // ================================================================
            // Pipeline Advancement (only when enabled by global schedule)
            // ================================================================
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
                    out_data.write(output_val);
                } else {
                    // ------------------------------------------------------------
                    // PHASE 2: COMPUTE
                    // ------------------------------------------------------------
                    int k = c - delay_len;
                    complex_t val_a = buf_in[k];
                    complex_t val_b = input;
                    
                    complex_t w = get_twiddle(k, N_stage);
                    
                    // Hardware modeled Inline Butterfly Computation
                    complex_t sum = val_a + val_b;
                    complex_t diff = (val_a - val_b) * w;
                    
                    if (alu_cycles > 1) {
                        // Latch intermediate results into FSM
                        bf_sum.write(sum);
                        bf_diff.write(diff);
                        bf_k.write(k);
                        bf_state.write(alu_cycles - 1);
                    } else {
                        // 1-cycle compute (Unlimited ALUs)
                        output_val = sum; 
                        buf_out[k] = diff;
                        out_data.write(output_val);
                    }
                }
                
                cnt.write((c + 1) % N_stage);
            }
        }
    }

    static int calc_latency(int n_mult, int n_add) {
        if (n_mult >= 4 && n_add >= 6) {
            return 1; // Fully parallel, single-cycle combinational execution
        } else {
            // Resource constrained: must schedule operations sequentially over multiple cycles
            int adds1 = (4 + n_add - 1) / n_add;
            int mults = (4 + n_mult - 1) / n_mult;
            int adds2 = (2 + n_add - 1) / n_add;
            return adds1 + mults + adds2;
        }
    }

    SC_HAS_PROCESS(Stage);
    Stage(sc_module_name name, int n, int id, int offset = 0, int n_mult = 4, int n_add = 6) : 
            sc_module(name), N_stage(n), init_offset(offset), num_mult(n_mult), num_add(n_add) {
        
        alu_cycles = calc_latency(num_mult, num_add);
        computed_alu_cycles = alu_cycles;
        
        delay_len = N_stage / 2;
        buf_in.resize(delay_len);
        buf_out.resize(delay_len);
        
        // Calculate dynamic hardware latency based on available ALUs
        alu_cycles = calc_latency(num_mult, num_add);
        
        SC_METHOD(process);
        sensitive << clk.pos();
    }
};

#endif
