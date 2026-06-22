#ifndef TB_DMA_H
#define TB_DMA_H

#include <systemc.h>
#include <axi/axi4.h>
#include <connections/connections.h>
#include <axi/testbench/Slave.h>
#include "dma.h"

using namespace sc_core;
using namespace axi;
using namespace Connections;

typedef axi::cfg::standard AxiCfg;

SC_MODULE(Testbench) {
    sc_clock clk;
    sc_signal<bool> rst_n; // Unified active-low reset for DMA and Slave
    
    // Control signals
    sc_signal<bool> start;
    sc_signal<sc_uint<AxiCfg::addrWidth>> base_addr;
    sc_signal<int> num_samples;
    sc_signal<bool> busy;
    
    // AXI channels (default port types)
    typename axi4<AxiCfg>::read::template chan<> read_chan;
    typename axi4<AxiCfg>::write::template chan<> write_chan;
    
    // FFT Data stream channels to interface with DMA
    Combinational<complex_t> fft_out_chan;
    Combinational<complex_t> fft_in_chan;
    
    // Testbench ports connecting to FFT data stream channels
    In<complex_t> tb_fft_in;
    Out<complex_t> tb_fft_out;

    Slave<AxiCfg>* slave_inst;
    DMA<AxiCfg, 4>* dma_inst;

    sc_trace_file *tf;

    SC_CTOR(Testbench);
    ~Testbench();
    
    void stimuli();
    void monitor();
};

#endif // TB_DMA_H
