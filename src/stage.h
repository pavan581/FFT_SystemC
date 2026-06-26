/*
 * stage.h
 *
 * Implements a single processing stage in the radix-2 Decimation-In-Frequency (DIF)
 * FFT computation pipeline.
 *
 * Each stage operates in a two-phase loop: storing the first half of the block into
 * a feedback delay buffer, and performing butterfly additions and subtractions 
 * with the second half. Twiddle factors are precomputed at construction time and
 * stored in a vector lookup table to avoid runtime trigonometric execution overhead.
 */

#ifndef STAGE_H
#define STAGE_H

#include "fft_types.h"
#include <connections/connections.h>
#include <cmath>
#include <vector>

using namespace Connections;

// Abstract base class for all FFT pipeline stages.
class StageBase : public sc_module {
public:
    StageBase(sc_module_name name) : sc_module(name) {}
    virtual ~StageBase() {}
    virtual In<complex_t>& get_in_port() = 0;
    virtual Out<complex_t>& get_out_port() = 0;
};

// A single pipeline stage of the Decimation-in-Frequency FFT.
template<int N_STAGE>
class Stage : public StageBase {
public:
    sc_in<bool> clk;
    sc_in<bool> rst_n;
    
    In<complex_t> in_data;
    Out<complex_t> out_data;

    In<complex_t>& get_in_port() override { return in_data; }
    Out<complex_t>& get_out_port() override { return out_data; }
    
    int delay_len;
    int alu_cycles;
    
    std::vector<complex_t> buf;
    std::vector<complex_t> twiddles;
    bool has_valid_diffs;
    
    void stage_thread() {
        in_data.Reset();
        out_data.Reset();
        
        // Initialize delay buffer
        for (int i = 0; i < delay_len; ++i) {
            buf[i] = complex_t(0.0, 0.0);
        }
        has_valid_diffs = false;
        
        wait();
        
        while (true) {
            // Phase 1: Store & Forward
            // Buffer incoming inputs while pushing out stored differences
            for (int c = 0; c < delay_len; ++c) {
                complex_t input = in_data.Pop();
                
                if (has_valid_diffs) {
                    complex_t output_val = buf[c];
                    out_data.Push(output_val);
                }
                
                buf[c] = input;
            }
            
            // Phase 2: Compute
            // Perform butterfly calculation on the second half of the inputs
            for (int k = 0; k < delay_len; ++k) {
                complex_t val_b = in_data.Pop();
                complex_t val_a = buf[k];
                
                // Retrieve precomputed twiddle factor
                complex_t w = twiddles[k];
                
                // Model multi-cycle butterfly latency
                if (alu_cycles > 1) {
                    wait(alu_cycles - 1);
                }
                
                complex_t sum = val_a + val_b;
                complex_t diff = (val_a - val_b) * w;
                
                out_data.Push(sum);
                buf[k] = diff;
            }
            has_valid_diffs = true;
        }
    }
    
    // Calculates butterfly execution cycles based on available hardware units.
    static int calc_latency(int n_mult, int n_add) {
        if (n_mult >= 4 && n_add >= 6) {
            return 1; // Single-cycle butterfly
        } else {
            int adds1 = (4 + n_add - 1) / n_add;
            int mults = (4 + n_mult - 1) / n_mult;
            int adds2 = (2 + n_add - 1) / n_add;
            return adds1 + mults + adds2;
        }
    }
    
    SC_HAS_PROCESS(Stage);
    Stage(sc_module_name name, int n_mult = 4, int n_add = 6) : 
        StageBase(name),
        clk("clk"),
        rst_n("rst_n"),
        in_data("in_data"),
        out_data("out_data"),
        delay_len(N_STAGE / 2),
        buf(N_STAGE / 2, complex_t(0, 0)),
        has_valid_diffs(false)
    {
        alu_cycles = calc_latency(n_mult, n_add);
        
        // Precalculate twiddle factors to avoid runtime cos/sin overhead
        twiddles.resize(delay_len);
        const double PI = 3.14159265358979323846;
        for (int k = 0; k < delay_len; ++k) {
            double angle = -2.0 * PI * k / N_STAGE;
            twiddles[k] = complex_t(cos(angle), sin(angle));
        }
        
        SC_THREAD(stage_thread);
        sensitive << clk.pos();
        async_reset_signal_is(rst_n, false); // Active low reset
    }
};

#endif // STAGE_H
