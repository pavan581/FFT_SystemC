// ============================================================================
// FFT.H - Top-Level Pipelined FFT Module
// ============================================================================
// This module implements the complete N-point FFT processing pipeline by:
//   1. Calculating the number of stages: log₂(N)
//   2. Dynamically instantiating Stage modules
//   3. Connecting stages in series with inter-stage signals
//   4. Computing pipeline alignment offsets for each stage
//   5. Tracking total pipeline latency
//
// Architecture:
//   in_data --> Stage_0 --> Stage_1 --> ... --> Stage_k --> out_data
//               (N pts)     (N/2 pts)            (2 pts)
//
// Features:
// - Throughput: 1 sample/cycle (continuous streaming capability)
// - Latency: ~N + 2*log₂(N) cycles total pipeline depth
// - Output order: Bit-reversed (inherent to DIF architecture)
// ============================================================================

#ifndef FFT_H
#define FFT_H

#include "fft_types.h"
#include "stage.h"
#include <vector>

// ============================================================================
// FFT Top Module
// ============================================================================
SC_MODULE(FFT) {
    sc_in<bool> clk;
    sc_in<bool> rst;
    
    sc_in<complex_t> in_data;
    sc_in<bool> in_valid;
    
    sc_out<complex_t> out_data;     // Output sample stream (bit-reversed)
    sc_out<bool> status;            // Active processing flag
    
    sc_out<int> in_index;           // Current input sample index (0..N-1)
    sc_out<int> out_index;          // Current output sample index
    sc_out<bool> out_valid;         // Output validity flag
    
    // ========================================================================
    // Configuration Parameters
    // ========================================================================
    int N;                          // FFT size (must be power  of 2)
    int num_stages;                 // Number of butterfly stages = log₂(N)
    int latency_cycles;             // Total pipeline latency cycles
    
    // ========================================================================
    // Sub-Modules and Internal Signals
    // ========================================================================
    std::vector<Stage*> stages;                       // Array of stage instances
    std::vector<sc_signal<complex_t>*> stage_signals; // Inter-stage connections
    
    // Internal Enable Signal (Broadcast to all stages)
    sc_signal<bool> internal_enable;
    sc_signal<bool> stage_sync;

    // Tracking Counters
    sc_signal<int> samples_in_cnt;
    sc_signal<bool> flushing;
    
    // Valid Pipeline (Shift Register)
    sc_vector<sc_signal<bool>> valid_pipe;
    sc_signal<int> out_cnt_reg;

    // Internal Counter Signal
    sc_signal<int> internal_cnt;  // Global cycle counter for time tracking
    sc_signal<complex_t> final_stage_out; // Internal signal from last stage

    SC_HAS_PROCESS(FFT);

    // ========================================================================
    // Constructor
    // ========================================================================
    // Dynamically creates and connects the FFT pipeline.
    //
    // Initialization Sequence:
    //   1. Calculate number of stages (log₂N)
    //   2. Create inter-stage signals
    //   3. For each stage:
    //      - Calculate stage size (N, N/2, N/4, ...)
    //      - Calculate counter offset for alignment
    //      - Instantiate Stage module
    //      - Connect clock, reset, and data ports
    //      - Track cumulative latency
    //   4. Register sequential and combinational logic
    // ========================================================================
    FFT(sc_module_name name, int n_points) : sc_module(name), N(n_points), valid_pipe("valid_pipe") {
        // Calculate pipeline depth
        num_stages = (int)log2(N);  // log₂(N) butterfly stages
        
        // ====================================================================
        // Create Inter-Stage Signals
        // ====================================================================
        // Need (num_stages - 1) signals to connect stages
        for (int i = 0; i < num_stages - 1; i++) {
            std::string sig_name = "sig_stage_" + std::to_string(i);
            stage_signals.push_back(new sc_signal<complex_t>(sig_name.c_str()));
        }
        
        // ====================================================================
        // Instantiate and Connect Stages
        // ====================================================================
        int total_latency = 0;  // Cumulative pipeline latency
        
        for (int i = 0; i < num_stages; i++) {
            // Calculate stage parameters
            int current_N = N >> i;  // Stage size: N / 2^i
            std::string stage_name = "stage_" + std::to_string(i);
            
            // Calculate counter offset for this stage
            // Ensures counter aligns with data arrival timing
            int offset = (current_N - (total_latency % current_N)) % current_N;
            
            // Instantiate stage
            stages.push_back(new Stage(stage_name.c_str(), current_N, i, offset));
            
            stages[i]->clk(clk);
            stages[i]->rst(rst);
            stages[i]->enable(internal_enable);
            stages[i]->sync(stage_sync);
            
            // Calculate this stage's latency contribution
            int stage_latency = (current_N / 2);      // Buffer fill (Compute is now 0-cycle)
            total_latency += stage_latency + 1;
            
            if (i == 0) {
                // First stage: connect to module input
                stages[i]->in_data(in_data);
            } else {
                // Middle stages: connect to previous stage output
                stages[i]->in_data(*stage_signals[i-1]);
            }
            
            if (i == num_stages - 1) {
                // Last stage: connect to internal signal
                stages[i]->out_data(final_stage_out);
            } else {
                // Earlier stages: connect to inter-stage signal
                stages[i]->out_data(*stage_signals[i]);
            }
        }
        latency_cycles = total_latency;
        valid_pipe.init(latency_cycles);
        
        SC_METHOD(control_logic);
        sensitive << in_valid << samples_in_cnt << flushing;
        for (int i = 0; i < latency_cycles; ++i) {
            sensitive << valid_pipe[i];
        }
        
        SC_METHOD(seq_logic);
        sensitive << clk.pos();
        
        SC_METHOD(comb_logic);
        sensitive << internal_cnt << rst << final_stage_out;
        for (int i = 0; i < latency_cycles; ++i) {
            sensitive << valid_pipe[i];
        }
    }


    // ========================================================================
    // Control Logic: Smart Enable Generation (Combinational).
    // Generates internal enable and sync signals based on input validity.
    // Ensures the pipeline only advances when new data is available or flushing is required.
    // ========================================================================
    void control_logic() {
        bool valid_in = in_valid.read();
        
        bool any_valid_in_pipe = false;
        for(int i = 0; i < valid_pipe.size(); ++i) {
            if(valid_pipe[i].read()) { any_valid_in_pipe = true; break; }
        }
        
        bool active = valid_in || any_valid_in_pipe;
        internal_enable.write(active);
        
        bool sync = (samples_in_cnt.read() == 0 && valid_in && !any_valid_in_pipe);
        stage_sync.write(sync);
    }

    // ========================================================================
    // Sequential Logic: Global Counter
    // ========================================================================
    // Increments a global counter on each clock cycle (after reset release).
    // Used to track pipeline timing and determine output validity.
    // ========================================================================
    void seq_logic() {
        if (rst.read()) {
            internal_cnt.write(0);
            samples_in_cnt.write(0);
            out_cnt_reg.write(N - 1);
            for(int i = 0; i < valid_pipe.size(); ++i) {
                valid_pipe[i].write(false);
            }
        } else {
            bool enabled = internal_enable.read();
            bool valid_in = in_valid.read();
            int s_in = samples_in_cnt.read();
            
            if (enabled) {
                internal_cnt.write(internal_cnt.read() + 1);
                
                // 1. Shift Valid Pipe
                // Shift Right
                if(valid_pipe.size() > 0) {
                    for(int i = valid_pipe.size()-1; i > 0; i--) {
                        valid_pipe[i].write(valid_pipe[i-1].read());
                    }
                    valid_pipe[0].write(valid_in);
                }
                
                // 2. Input Counter (Modulo N)
                if (valid_in) {
                    if (s_in == N - 1) {
                        samples_in_cnt.write(0);
                    } else {
                        samples_in_cnt.write(s_in + 1);
                    }
                }
                
                // 3. Output Counter
                bool will_be_valid = false;
                if (valid_pipe.size() > 1) {
                    will_be_valid = valid_pipe[valid_pipe.size() - 2].read();
                } else if (valid_pipe.size() == 1) {
                    will_be_valid = valid_in;
                }
                
                // Allow pipeline to flush out the last sample
                flushing.write(will_be_valid);
                
                // Increments if it will be valid
                if (will_be_valid) {
                    out_cnt_reg.write((out_cnt_reg.read() + 1) % N);
                }
                if (stage_sync.read()) {
                    out_cnt_reg.write(N - 1);
                }
            }
        }
    }
    
    // ========================================================================
    // Combinational Logic: Observability Outputs
    // ========================================================================
    // Calculates debug/monitoring signals based on current cycle count:
    //   - in_index:  Which input sample is being consumed (0..N-1, wraps)
    //   - out_index: Which output sample is being produced
    //   - out_valid: True once pipeline latency has elapsed
    // ========================================================================
    void comb_logic() {
        if (rst.read()) {
            status.write(false);
            in_index.write(-1);
            out_index.write(-1);
            out_valid.write(false);
            out_data.write(complex_t(0,0));
        } else {
            status.write(internal_enable.read() || flushing.read()); 
            in_index.write(samples_in_cnt.read());
            
            bool is_valid = false;
            
            if (valid_pipe.size() > 0) {
                is_valid = valid_pipe[valid_pipe.size() - 1].read();
            }
            
            out_valid.write(is_valid);
            
            if (is_valid) {
                 out_index.write(out_cnt_reg.read());
                 out_data.write(final_stage_out.read());
            } else {
                 out_index.write(-1);
                 out_valid.write(false);
                 out_data.write(complex_t(0,0));
            }
        }
    }
};

#endif  // FFT_H
