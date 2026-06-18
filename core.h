/*
 * core.h
 *
 * Couples a single DMA controller and an FFT computation core into a unified
 * hardware block.
 *
 * It instantiates the DMA and FFT sub-modules, routes shared clock, reset, 
 * and control signals, and hooks up the DMA read/write master interfaces 
 * to external AXI port boundaries. The DMA and FFT communicate internally
 * through point-to-point Combinational handshake channels.
 */

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

// Core wrapper integrating the DMA controller and the FFT processing pipeline.
template<int N_SIZE, typename AxiCfg, int NUM_MULT=4, int NUM_ADD=6>
SC_MODULE(Core) {
    sc_in<bool> clk;
    sc_in<bool> rst;
    
    // Control interface
    sc_in<bool> start;
    sc_in<sc_uint<AxiCfg::addrWidth>> base_addr;
    sc_in<int> num_samples;
    sc_out<bool> busy;
    
    // External AXI memory interface
    typename axi4<AxiCfg>::read::template master<Connections::SYN_PORT> mem_read_port;
    typename axi4<AxiCfg>::write::template master<Connections::SYN_PORT> mem_write_port;
    
    // Inter-module channels connecting DMA <-> FFT
    Combinational<complex_t> dma_to_fft_chan;
    Combinational<complex_t> fft_to_dma_chan;
    
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
        // DMA port bindings
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
        
        // FFT port bindings
        fft.clk(clk);
        fft.rst(rst);
        fft.in_data(dma_to_fft_chan);
        fft.out_data(fft_to_dma_chan);
    }
};

#endif // CORE_H
