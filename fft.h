/*
 * fft.h
 *
 * Implements the N-point Fast Fourier Transform (FFT) core cascade.
 *
 * It dynamically instantiates log2(N) serial pipeline stages using a recursive
 * helper structure ('StageInstantiator') and routes internal Combinational channels
 * between adjacent stages to implement the complete DIF processing pipeline.
 */

#ifndef FFT_H
#define FFT_H

#include "fft_types.h"
#include "stage.h"
#include <connections/connections.h>
#include <vector>
#include <string>

using namespace Connections;

// Helper template to recursively instantiate FFT stages.
template<int STAGE_SIZE, int NUM_MULT, int NUM_ADD>
struct StageInstantiator {
    static void instantiate(std::vector<StageBase*>& stages,
                            std::vector<Combinational<complex_t>*>& stage_signals,
                            int index,
                            sc_in<bool>& clk,
                            sc_in<bool>& rst_n) {
        std::string s_name = "stage_" + std::to_string(index);
        auto* stage = new Stage<STAGE_SIZE>(s_name.c_str(), NUM_MULT, NUM_ADD);
        stage->clk(clk);
        stage->rst_n(rst_n);
        stages.push_back(stage);
        
        if (STAGE_SIZE > 2) {
            std::string sig_name = "sig_stage_" + std::to_string(index);
            auto* chan = new Combinational<complex_t>(sig_name.c_str());
            stage_signals.push_back(chan);
            
            stage->out_data(*chan);
            
            // Recurse to instantiate the next stage
            StageInstantiator<STAGE_SIZE / 2, NUM_MULT, NUM_ADD>::instantiate(
                stages, stage_signals, index + 1, clk, rst_n
            );
        }
    }
};

// Base case for recursion (FFT stage of size 2).
template<int NUM_MULT, int NUM_ADD>
struct StageInstantiator<2, NUM_MULT, NUM_ADD> {
    static void instantiate(std::vector<StageBase*>& stages,
                            std::vector<Combinational<complex_t>*>& stage_signals,
                            int index,
                            sc_in<bool>& clk,
                            sc_in<bool>& rst_n) {
        std::string s_name = "stage_" + std::to_string(index);
        auto* stage = new Stage<2>(s_name.c_str(), NUM_MULT, NUM_ADD);
        stage->clk(clk);
        stage->rst_n(rst_n);
        stages.push_back(stage);
    }
};

// Top-level FFT processor module.
template<int N, int NUM_MULT=4, int NUM_ADD=6>
SC_MODULE(FFT) {
    sc_in<bool> clk;
    sc_in<bool> rst_n;
    
    In<complex_t> in_data;
    Out<complex_t> out_data;
    
    std::vector<StageBase*> stages;
    std::vector<Combinational<complex_t>*> stage_signals;
    
    SC_CTOR(FFT) {
        // Instantiate the cascade of stages recursively
        StageInstantiator<N, NUM_MULT, NUM_ADD>::instantiate(stages, stage_signals, 0, clk, rst_n);
        
        // Connect stages in series
        for (size_t i = 0; i < stages.size(); ++i) {
            if (i == 0) {
                stages[i]->get_in_port()(in_data);
            } else {
                stages[i]->get_in_port()(*stage_signals[i-1]);
            }
        }
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
