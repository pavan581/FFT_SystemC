// ============================================================================
// TB_DMA.H - DMA Module Testbench Header
// ============================================================================
// This file defines the testbench for the DMA module. It instantiates the 
// DMA module and a connected Memory module to verify data streaming functionality.
//
// Verification Scenarios:
// - Standard DMA block transfer
// - Consecutive DMA transfers to verify state machine reset and pipelining
// ============================================================================

#ifndef TB_DMA_H
#define TB_DMA_H

#include <systemc.h>
#include "dma.h"
#include "memory.h"

// Configuration
const unsigned ADDR_WIDTH = 12;
const unsigned DATA_WIDTH = 64;
const unsigned MEM_SIZE = 1024;

SC_MODULE(Testbench) {
    // Signals
    sc_clock clk;
    sc_signal<bool> rst;
    
    // Control
    sc_signal<bool> start;
    sc_signal<sc_uint<ADDR_WIDTH>> base_addr;
    sc_signal<int> num_samples;
    sc_signal<bool> busy;
    
    // Memory Interface
    sc_signal<sc_uint<ADDR_WIDTH>> mem_addr;
    sc_signal<sc_uint<DATA_WIDTH>> mem_data;
    
    // FFT Interface
    sc_signal<complex_t> fft_data;
    sc_signal<bool> fft_valid;

    // Internal signals for Memory Connection
    sc_signal<bool> mem_wr_en_sig;
    sc_signal<sc_uint<ADDR_WIDTH>> mem_waddr_sig;
    sc_signal<sc_uint<DATA_WIDTH>> mem_wdata_sig; 

    // Modules
    Memory<MEM_SIZE, DATA_WIDTH, ADDR_WIDTH>* mem_inst;
    DMA<ADDR_WIDTH, DATA_WIDTH>* dma_inst;

    // Tracing
    sc_trace_file *tf;

    SC_CTOR(Testbench);
    ~Testbench();
    
    void stimuli();
};

#endif
