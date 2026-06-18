// ============================================================================
// FFT.H - Top-Level Pipelined FFT Module (Connections / SC_THREAD version)
// ============================================================================

#ifndef FFT_H
#define FFT_H

#include "fft_types.h"
#include "stage.h"
#include <connections/connections.h>
#include <vector>
#include <string>

using namespace Connections;

// ============================================================================
// StageInstantiator - Recursive Helper Struct
// ============================================================================
template<int STAGE_SIZE, int NUM_MULT, int NUM_ADD>
struct StageInstantiator {
    static void instantiate(std::vector<StageBase*>& stages,
                            std::vector<Combinational<complex_t>*>& stage_signals,
                            int index,
                            sc_in<bool>& clk,
                            sc_in<bool>& rst) {
        std::string s_name = "stage_" + std::to_string(index);
        Stage<STAGE_SIZE>* stage = new Stage<STAGE_SIZE>(s_name.c_str(), NUM_MULT, NUM_ADD);
        stage->clk(clk);
        stage->rst(rst);
        stages.push_back(stage);
        
        if (STAGE_SIZE > 2) {
            std::string sig_name = "sig_stage_" + std::to_string(index);
            auto* chan = new Combinational<complex_t>(sig_name.c_str());
            stage_signals.push_back(chan);
            
            // Connect output of current stage to channel
            stage->out_data(*chan);
            
            // Recurse to instantiate the next stage
            StageInstantiator<STAGE_SIZE / 2, NUM_MULT, NUM_ADD>::instantiate(
                stages, stage_signals, index + 1, clk, rst
            );
        }
    }
};

// Base case specialization for N_STAGE = 2
template<int NUM_MULT, int NUM_ADD>
struct StageInstantiator<2, NUM_MULT, NUM_ADD> {
    static void instantiate(std::vector<StageBase*>& stages,
                            std::vector<Combinational<complex_t>*>& stage_signals,
                            int index,
                            sc_in<bool>& clk,
                            sc_in<bool>& rst) {
        std::string s_name = "stage_" + std::to_string(index);
        Stage<2>* stage = new Stage<2>(s_name.c_str(), NUM_MULT, NUM_ADD);
        stage->clk(clk);
        stage->rst(rst);
        stages.push_back(stage);
    }
};

// ============================================================================
// FFT Top Module
// ============================================================================
template<int N, int NUM_MULT=4, int NUM_ADD=6>
SC_MODULE(FFT) {
    sc_in<bool> clk;
    sc_in<bool> rst;
    
    In<complex_t> in_data;
    Out<complex_t> out_data;
    
    std::vector<StageBase*> stages;
    std::vector<Combinational<complex_t>*> stage_signals;
    
    SC_CTOR(FFT) {
        // Instantiate stages recursively
        StageInstantiator<N, NUM_MULT, NUM_ADD>::instantiate(stages, stage_signals, 0, clk, rst);
        
        // Connect stages in series
        for (size_t i = 0; i < stages.size(); ++i) {
            if (i == 0) {
                stages[i]->get_in_port()(in_data);
            } else {
                stages[i]->get_in_port()(*stage_signals[i-1]);
            }
        }
        // Connect final stage output to top module output
        stages.back()->get_out_port()(out_data);
    }
    
    ~FFT() {
        for (auto* stage : stages) {
            delete stage;
        }
        for (auto* sig : stage_signals) {
            delete sig;
        }
    }
};

#endif  // FFT_H
