// ============================================================================
// INTERLEAVED_FFT.H - Multi-Core Interleaved FFT Processor
// ============================================================================
// This module implements a high-throughput, multi-core FFT architecture that 
// uses temporal interleaving to process continuous streams of data. By staggering 
// the start times of multiple independent FFT cores, it achieves a higher 
// overall data processing rate.
//
// Features:
// - Instantiates multiple DMA and FFT core pairs
// - Staggered execution based on a configurable hop size
// - Independent memory interfaces for each core
// - Vectorized ports for easy system integration
//
// Architecture:
//                 +---> DMA_0 ---> FFT_0 ---> Out_0
//                 |
//   Start Signal -+---> DMA_1 ---> FFT_1 ---> Out_1
//                 |
//                 +---> DMA_N ---> FFT_N ---> Out_N
// ============================================================================

#ifndef INTERLEAVED_FFT_H
#define INTERLEAVED_FFT_H

#include <systemc.h>
#include <vector>
#include "fft.h"
#include "dma.h"

// ============================================================================
// InterleavedFFT Module Definition
// ============================================================================
// Parameters:
//   N_SIZE     : Number of points per FFT core
//   NUM_CORES  : Number of parallel FFT cores to instantiate
//   HOP_SIZE   : Stagger offset in clock cycles between core starts
//   DATA_WIDTH : Width of the memory data bus
//   ADDR_WIDTH : Width of the address bus
// ============================================================================
template<int N_SIZE, int NUM_CORES, int HOP_SIZE, int DATA_WIDTH, int ADDR_WIDTH>
SC_MODULE(InterleavedFFT) {
    // Clock and synchronous active-high reset
    sc_in<bool> clk;
    sc_in<bool> rst;
    
    // Global start trigger
    sc_in<bool> start;

    // Independent Memory Interfaces for each core
    sc_vector<sc_out<sc_uint<ADDR_WIDTH>>> mem_addrs;
    sc_vector<sc_in<sc_uint<DATA_WIDTH>>> mem_data;
    
    sc_vector<sc_in<sc_uint<ADDR_WIDTH>>> base_addrs;
    sc_vector<sc_in<int>> num_samples; 

    // System Outputs
    sc_vector<sc_out<complex_t>> out_data;
    sc_vector<sc_out<bool>> out_valids;
    
    // Observability / Status
    sc_vector<sc_out<int>> out_indices;

    // Internal Signals
    sc_vector<sc_signal<bool>> dma_starts;
    sc_vector<sc_signal<bool>> dma_busy;
    
    // Inter-module signals (DMA -> FFT)
    sc_vector<sc_signal<complex_t>> fft_in_data;
    sc_vector<sc_signal<bool>> fft_in_valids;
    
    // Dummy signals for unused FFT ports
    sc_vector<sc_signal<bool>> fft_status;
    sc_vector<sc_signal<int>> fft_in_index;

    // Sub-modules
    sc_vector<FFT> fft_cores;
    sc_vector<DMA<ADDR_WIDTH, DATA_WIDTH>> dma_cores;

    int N;
    int num_cores;
    int hop;
    
    // Stagger Logic State
    sc_signal<bool> active_stagger;
    sc_signal<int> stagger_counter;

    SC_HAS_PROCESS(InterleavedFFT);

    InterleavedFFT(sc_module_name name)
        : sc_module(name), 
          N(N_SIZE), 
          num_cores(NUM_CORES), 
          hop(HOP_SIZE),
          mem_addrs("mem_addrs", NUM_CORES),
          mem_data("mem_data", NUM_CORES),
          base_addrs("base_addrs", NUM_CORES),
          num_samples("num_samples", NUM_CORES),
          out_data("out_data", NUM_CORES),
          out_valids("out_valids", NUM_CORES),
          out_indices("out_indices", NUM_CORES),
          dma_starts("dma_starts", NUM_CORES),
          dma_busy("dma_busy", NUM_CORES),
          fft_in_data("fft_in_data", NUM_CORES),
          fft_in_valids("fft_in_valids", NUM_CORES),
          fft_status("fft_status", NUM_CORES),
          fft_in_index("fft_in_index", NUM_CORES),
          fft_cores("fft_core", NUM_CORES, [&](const char* name, int i) {
              return new FFT(name, N_SIZE);
          }),
          dma_cores("dma_core", NUM_CORES)
    {
        // Connect Cores
        for (int i = 0; i < num_cores; ++i) {
            // DMA Connections
            dma_cores[i].clk(clk);
            dma_cores[i].rst(rst);
            dma_cores[i].start(dma_starts[i]);
            dma_cores[i].base_addr(base_addrs[i]);
            dma_cores[i].num_samples(num_samples[i]);
            dma_cores[i].busy(dma_busy[i]);
            
            dma_cores[i].mem_addr(mem_addrs[i]);
            dma_cores[i].mem_data(mem_data[i]);
            
            dma_cores[i].fft_data(fft_in_data[i]);
            dma_cores[i].fft_valid(fft_in_valids[i]);

            // FFT Connections
            fft_cores[i].clk(clk);
            fft_cores[i].rst(rst);
            fft_cores[i].in_data(fft_in_data[i]);
            fft_cores[i].in_valid(fft_in_valids[i]);
            
            fft_cores[i].out_data(out_data[i]);
            fft_cores[i].out_valid(out_valids[i]);
            fft_cores[i].out_index(out_indices[i]);
            
            fft_cores[i].status(fft_status[i]);
            fft_cores[i].in_index(fft_in_index[i]);
        }

        SC_METHOD(control_logic);
        sensitive << clk.pos();
    }

    // Staggered Start Control Logic (Sequential).
    // Manages the delayed startup of individual FFT cores based on HOP_SIZE.
    void control_logic() {
        if (rst.read()) {
            active_stagger.write(false);
            stagger_counter.write(0);
            for (int i = 0; i < num_cores; ++i) {
                dma_starts[i].write(false);
            }
        } else {
            bool is_active = active_stagger.read();
            int cnt = stagger_counter.read();

            if (start.read() && !is_active) {
                is_active = true;
                cnt = 0;
                active_stagger.write(true);
                stagger_counter.write(0);
            }

            if (is_active) {
                for (int i = 0; i < num_cores; ++i) {
                    if (cnt == i * hop) {
                        dma_starts[i].write(true);
                    } else {
                        dma_starts[i].write(false);
                    }
                }
                
                stagger_counter.write(cnt + 1);
                
                // Ensure all cores are triggered
                if (cnt > num_cores * hop + 2) {
                    active_stagger.write(false);
                }
            } else {
                 for (int i = 0; i < num_cores; ++i) {
                    dma_starts[i].write(false);
                }
            }
        }
    }
};

#endif
