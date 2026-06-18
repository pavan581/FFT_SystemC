#ifndef TB_DMA_H
#define TB_DMA_H

#include <systemc.h>
#include <axi/axi4.h>
#include <connections/connections.h>
#include "dma.h"
#include "memory.h"

using namespace sc_core;
using namespace axi;
using namespace Connections;

typedef axi::cfg::standard AxiCfg;

SC_MODULE(Testbench) {
    sc_clock clk;
    sc_signal<bool> rst;
    
    // Control signals
    sc_signal<bool> start;
    sc_signal<sc_uint<AxiCfg::addrWidth>> base_addr;
    sc_signal<int> num_samples;
    sc_signal<bool> busy;
    
    // AXI Channels
    sc_vector<typename axi4<AxiCfg>::read::template chan<Connections::SYN_PORT>> mem_read_chans;
    sc_vector<typename axi4<AxiCfg>::write::template chan<Connections::SYN_PORT>> mem_write_chans;
    
    // Testbench Write Master for initializing Memory
    typename axi4<AxiCfg>::write::template master<Connections::SYN_PORT> tb_write_master;

    // FFT Data stream channels to interface with DMA
    Combinational<complex_t> fft_out_chan;
    Combinational<complex_t> fft_in_chan;
    
    // Testbench ports connecting to FFT data stream channels
    In<complex_t> tb_fft_in;
    Out<complex_t> tb_fft_out;

    Memory<2, 1, 1024, AxiCfg>* mem_inst;
    DMA<AxiCfg, 4>* dma_inst;

    sc_trace_file *tf;

    SC_CTOR(Testbench);
    ~Testbench();
    
    void stimuli();
    void monitor();
    void axi_write(unsigned int addr, sc_uint<AxiCfg::dataWidth> data);
};

#endif // TB_DMA_H
