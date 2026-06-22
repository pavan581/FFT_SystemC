#ifndef TB_MEMORY_H
#define TB_MEMORY_H

#define BOOST_NULLPTR nullptr
#define HLS_CATAPULT

#include <systemc.h>
#include <axi/axi4.h>
#include <connections/connections.h>
#include <axi/testbench/Master.h>
#include "memory.h"

using namespace sc_core;
using namespace axi;
using namespace Connections;

typedef axi::cfg::standard AxiCfg;

struct MyMasterCfg {
    enum {
        numWrites = 50,
        numReads = 50,
        readDelay = 10,
        seed = 42,
    };
    static const uint64_t addrBoundLower = 0x0;
    static const uint64_t addrBoundUpper = 0x1F8; // 504 bytes, 8-byte aligned (63 words)
};

SC_MODULE(Testbench) {
    sc_clock clk;
    sc_signal<bool> rst_n; // Unified active-low reset for Master and Memory
    sc_signal<bool> done;

    // AXI channels (default port types)
    typename axi4<AxiCfg>::read::template chan<> read_chan;
    typename axi4<AxiCfg>::write::template chan<> write_chan;

    Master<AxiCfg, MyMasterCfg>* master;
    Memory<1, 1, 1024, AxiCfg>* mem;

    sc_trace_file* tf;

    SC_CTOR(Testbench);
    ~Testbench();

    void stimuli();
};

#endif // TB_MEMORY_H