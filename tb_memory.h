#ifndef TB_MEMORY_H
#define TB_MEMORY_H

#include <systemc.h>
#include <axi/axi4.h>
#include <connections/connections.h>
#include "memory.h"

using namespace sc_core;
using namespace axi;
using namespace Connections;

typedef axi::cfg::standard AxiCfg;

// Testbench module for verifying the AXI4 Multi-Port Memory.
SC_MODULE(Testbench) {
    sc_clock clk;
    sc_signal<bool> rst;

    // AXI Read/Write channels and master interfaces
    sc_vector<typename axi4<AxiCfg>::read::template chan<Connections::SYN_PORT>> read_chans;
    sc_vector<typename axi4<AxiCfg>::write::template chan<Connections::SYN_PORT>> write_chans;

    typename axi4<AxiCfg>::read::template master<Connections::SYN_PORT> tb_read_master;
    typename axi4<AxiCfg>::write::template master<Connections::SYN_PORT> tb_write_master;

    Memory<1, 1, 1024, AxiCfg>* mem;
    sc_trace_file* tf;

    SC_CTOR(Testbench);
    ~Testbench();

    void stimuli();
    void axi_write(unsigned int addr, sc_uint<AxiCfg::dataWidth> data);
    sc_uint<AxiCfg::dataWidth> axi_read(unsigned int addr);
};

#endif // TB_MEMORY_H