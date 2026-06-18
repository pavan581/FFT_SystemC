// ============================================================================
// CORE.H - Integrated DMA + FFT Processing Core (Official MatchLib version)
// ============================================================================

#ifndef CORE_H
#define CORE_H

#include <systemc.h>
#include <axi/axi4.h>
#include <connections/connections.h>
#include "dma.h"
#include "fft.h"

using namespace sc_core;
using namespace axi;
using namespace Connections;

// ============================================================================
// Core Module Definition
// ============================================================================
template<int N_SIZE, typename AxiCfg, int NUM_MULT=4, int NUM_ADD=6>
SC_MODULE(Core) {
    // Clock and active-high synchronous reset
    sc_in<bool> clk;
    sc_in<bool> rst;
    
    // Control Interface
    sc_in<bool> start;
    sc_in<sc_uint<AxiCfg::addrWidth>> base_addr;
    sc_in<int> num_samples;
    sc_out<bool> busy;
    
    // Top-Level AXI4 Memory Interfaces
    typename axi4<AxiCfg>::read::template master<Connections::SYN_PORT> mem_read_port;
    typename axi4<AxiCfg>::write::template master<Connections::SYN_PORT> mem_write_port;
    
    // Internal Channels connecting DMA <-> FFT
    Combinational<complex_t> dma_to_fft_chan;
    Combinational<complex_t> fft_to_dma_chan;
    
    // Sub-Modules
    DMA<AxiCfg, N_SIZE, NUM_MULT, NUM_ADD> dma;
    FFT<N_SIZE, NUM_MULT, NUM_ADD> fft;
    
    SC_CTOR(Core)
        : mem_read_port("mem_read_port"),
          mem_write_port("mem_write_port"),
          dma_to_fft_chan("dma_to_fft_chan"),
          fft_to_dma_chan("fft_to_dma_chan"),
          dma("dma"),
          fft("fft")
    {
        // Bind DMA sub-module ports
        dma.clk(clk);
        dma.rst(rst);
        dma.start(start);
        dma.base_addr(base_addr);
        dma.num_samples(num_samples);
        dma.busy(busy);
        dma.mem_read_port(mem_read_port);
        dma.mem_write_port(mem_write_port);
        dma.fft_out(dma_to_fft_chan);
        dma.fft_in(fft_to_dma_chan);
        
        // Bind FFT sub-module ports
        fft.clk(clk);
        fft.rst(rst);
        fft.in_data(dma_to_fft_chan);
        fft.out_data(fft_to_dma_chan);
    }
};

#endif // CORE_H
