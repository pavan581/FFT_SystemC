/*
 * core.h
 *
 * Unified processing core wrapping one DMA controller and one FFT compute pipeline.
 * Connects external AXI memory ports and manages internal handshake signals between
 * the DMA and FFT computation pipeline.
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

// Integrated processing core
template<int N_SIZE, typename AxiCfg, int NUM_MULT=4, int NUM_ADD=6>
SC_MODULE(Core) {
    sc_in<bool> clk;
    sc_in<bool> rst_n; // Active-low reset
    
    // Control interface
    sc_in<bool> start;
    sc_in<sc_uint<AxiCfg::addrWidth>> base_addr;
    sc_in<int> num_samples;
    sc_out<bool> busy;
    
    // AXI memory interface
    typename axi4<AxiCfg>::read::template master <> mem_read_port;
    typename axi4<AxiCfg>::write::template master <> mem_write_port;
    
    // Internal DMA <-> FFT channels
    Combinational<complex_t> dma_to_fft_chan;
    Combinational<complex_t> fft_to_dma_chan;
    
    DMA<AxiCfg, N_SIZE, NUM_MULT, NUM_ADD> dma;
    FFT<N_SIZE, NUM_MULT, NUM_ADD> fft;
    
    SC_CTOR(Core)
        : clk("clk"),
          rst_n("rst_n"),
          start("start"),
          base_addr("base_addr"),
          num_samples("num_samples"),
          busy("busy"),
          mem_read_port("mem_read_port"),
          mem_write_port("mem_write_port"),
          dma_to_fft_chan("dma_to_fft_chan"),
          fft_to_dma_chan("fft_to_dma_chan"),
          dma("dma"),
          fft("fft")
    {
        // DMA bindings
        dma.clk(clk);
        dma.rst_n(rst_n);
        dma.start(start);
        dma.base_addr(base_addr);
        dma.num_samples(num_samples);
        dma.busy(busy);
        dma.mem_read_port(mem_read_port);
        dma.mem_write_port(mem_write_port);
        dma.fft_out(dma_to_fft_chan);
        dma.fft_in(fft_to_dma_chan);
        
        // FFT bindings
        fft.clk(clk);
        fft.rst_n(rst_n);
        fft.in_data(dma_to_fft_chan);
        fft.out_data(fft_to_dma_chan);
    }
};

#endif // CORE_H
