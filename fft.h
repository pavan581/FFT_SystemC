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

    sc_signal<complex_t> final_stage_out; // Internal signal from last stage
    
    // ALU Parameters and Step Signals
    int num_mult;
    int num_add;
    int alu_cycles;
    std::vector<int> stage_valid_idx;
    sc_signal<int> internal_cnt;  // Global cycle counter for time tracking
    sc_out<bool> pipeline_step_sig; // Trigger for processing stages (Handshake out)
    sc_signal<int> step_cnt;           // Counts alu_cycles down to 0
    sc_signal<int> next_step_cnt;      // Combinational next value for step_cnt

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
    FFT(sc_module_name name, int n_points, int n_mult=4, int n_add=6) : 
            sc_module(name), N(n_points), num_mult(n_mult), num_add(n_add), valid_pipe("valid_pipe") {
        
        alu_cycles = Stage::calc_latency(num_mult, num_add);
        
        // Calculate pipeline depth
        num_stages = (int)log2(N);  // log₂(N) butterfly stages
        
        stages.resize(num_stages);
        
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
            
            // Instantiate stage with physical ALU constraints
            stages[i] = new Stage(stage_name.c_str(), current_N, i, offset, num_mult, num_add);
            
            stages[i]->clk(clk);
            stages[i]->rst(rst);
            stages[i]->enable(pipeline_step_sig); // Stages now step on global schedule
            stages[i]->sync(stage_sync);
            
            // Calculate this stage's latency contribution
            stage_valid_idx.push_back(total_latency);
            int stage_latency = (current_N / 2);      // Buffer fill (Compute is now 0-cycle)
            total_latency += stage_latency + 1;
        }
        
        latency_cycles = total_latency;
        for (int i = 0; i < num_stages; i++) {
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
        valid_pipe.init(latency_cycles);
        
        SC_METHOD(control_logic);
        sensitive << in_valid << samples_in_cnt << flushing;
        for (int i = 0; i < latency_cycles; ++i) {
            sensitive << valid_pipe[i];
        }
        for (int i = 0; i < num_stages; ++i) {
            sensitive << stages[i]->cnt;
        }
        sensitive << step_cnt;
        
        SC_METHOD(seq_logic);
        sensitive << clk.pos();
        
        SC_METHOD(comb_logic);
        sensitive << internal_cnt << rst << final_stage_out;
        for (int i = 0; i < latency_cycles; ++i) {
            sensitive << valid_pipe[i];
        }
    }

    ~FFT() {
        for (auto stage : stages) {
            delete stage;
        }
        for (auto sig : stage_signals) {
            delete sig;
        }
    }


    // ========================================================================
    // Control Logic: Smart Enable Generation (Combinational).
    // Generates internal enable and sync signals based on input validity.
    //
    // DYNAMIC LATENCY SCHEDULER:
    // If the butterfly computing units in the stages are resource-constrained 
    // (i.e. num_mult < 4 or num_add < 6), they require multiple cycles to complete.
    //
    // This controller loops through all active stages. If a stage currently holds 
    // valid data and is in its computation phase (c >= delay_len), we query its 
    // required math execution latency (`computed_alu_cycles`). We compute the maximum 
    // latency among all active stages and stall the pipeline advance signal 
    // (`pipeline_step_sig`) until the counter counts down to 0. This guarantees 
    // that no stage gets overwritten before completing its computation.
    // ========================================================================
    void control_logic() {
        bool valid_in = in_valid.read();
        
        // Find if there is any valid data in the pipeline
        bool any_valid_in_pipe = false;
        for(int i = 0; i < valid_pipe.size(); ++i) {
            if(valid_pipe[i].read()) { any_valid_in_pipe = true; break; }
        }
        
        // Pipeline is active if we are receiving new data or flushing existing data
        bool active = valid_in || any_valid_in_pipe;
        internal_enable.write(active);
        
        // Assert sync to reset/align internal stage counters on the first sample of a block
        bool sync = (samples_in_cnt.read() == 0 && valid_in && !any_valid_in_pipe);
        stage_sync.write(sync);
        
        int max_cycles = 1;
        if (active) {
            for (int i = 0; i < num_stages; i++) {
                bool stage_has_valid_data = false;
                int v_idx = stage_valid_idx[i];
                if (v_idx == 0) {
                    stage_has_valid_data = valid_in;
                } else if (v_idx > 0 && v_idx <= valid_pipe.size()) {
                    stage_has_valid_data = valid_pipe[v_idx - 1].read();
                }
                
                // If the stage has valid data and is in compute phase, track its ALU latency
                if (stage_has_valid_data) {
                    int c = stages[i]->cnt.read();
                    if (sync) c = stages[i]->init_offset;
                    
                    if (c >= stages[i]->delay_len) {
                        if (stages[i]->computed_alu_cycles > max_cycles) {
                            max_cycles = stages[i]->computed_alu_cycles;
                        }
                    }
                }
            }
        }
        
        // Downcounter logic for multi-cycle execution stalls
        bool do_step = false;
        if (active) {
            if (step_cnt.read() == 0) {
                do_step = true;
                next_step_cnt.write(max_cycles > 0 ? max_cycles - 1 : 0);
            } else {
                next_step_cnt.write(step_cnt.read() - 1);
            }
        } else {
            next_step_cnt.write(0);
        }
        
        pipeline_step_sig.write(do_step);
    }

    // ========================================================================
    // Sequential Logic: Global Counter & Pipe Register Shift
    // ========================================================================
    // Increments cycles, shifts valid trackers, and routes output streams.
    // Pipeline advancement shifts only occur when `do_step` is high (controlled by the scheduler).
    // ========================================================================
    void seq_logic() {
        if (rst.read()) {
            internal_cnt.write(0);
            samples_in_cnt.write(0);
            out_cnt_reg.write(N - 1);
            step_cnt.write(0);
            for(int i = 0; i < valid_pipe.size(); ++i) {
                valid_pipe[i].write(false);
            }
        } else {
            bool enabled = internal_enable.read();
            bool valid_in = in_valid.read();
            
            if (enabled) {
                internal_cnt.write(internal_cnt.read() + 1);
                
                bool do_step = pipeline_step_sig.read();
                step_cnt.write(next_step_cnt.read());
                
                if (do_step) {
                    int s_in = samples_in_cnt.read();
                    
                    // 1. Shift the Validity Pipe (models pipeline delay steps)
                    if(valid_pipe.size() > 0) {
                        for(int i = valid_pipe.size()-1; i > 0; i--) {
                            valid_pipe[i].write(valid_pipe[i-1].read());
                        }
                        valid_pipe[0].write(valid_in);
                    }
                    
                    // 2. Manage Input Sample Counter
                    if (valid_in) {
                        if (s_in == N - 1) {
                            samples_in_cnt.write(0);
                        } else {
                            samples_in_cnt.write(s_in + 1);
                        }
                    }
                    
                    // 3. Determine if the output will be valid next cycle
                    bool will_be_valid = false;
                    if (valid_pipe.size() > 1) {
                        will_be_valid = valid_pipe[valid_pipe.size() - 2].read();
                    } else if (valid_pipe.size() == 1) {
                        will_be_valid = valid_in;
                    }
                    
                    // Allow pipeline to flush out remaining computed samples
                    flushing.write(will_be_valid);
                    
                    // Manage Output index tracking
                    if (will_be_valid) {
                        out_cnt_reg.write((out_cnt_reg.read() + 1) % N);
                    }
                    if (stage_sync.read()) {
                        out_cnt_reg.write(N - 1);
                    }
                    
                    // Drive top-level output ports
                    bool is_valid = false;
                    if (valid_pipe.size() > 0) {
                        is_valid = valid_pipe[valid_pipe.size() - 1].read();
                    }
                    
                    if (is_valid) {
                         out_valid.write(true);
                         out_index.write(out_cnt_reg.read());
                         out_data.write(final_stage_out.read());
                    } else {
                         out_valid.write(false);
                         out_index.write(-1);
                         out_data.write(complex_t(0,0));
                    }
                } else {
                    out_valid.write(false);
                }
            } else {
                out_valid.write(false);
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
        } else {
            status.write(internal_enable.read() || flushing.read()); 
            in_index.write(samples_in_cnt.read());
        }
    }
};

#endif  // FFT_H
