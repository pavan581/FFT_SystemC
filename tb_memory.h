// ============================================================================
// TB_MEMORY.H - Memory Module Testbench
// ============================================================================
// This testbench verifies the Multi-Port Memory module's functionality.
// It performs write and read sequences to ensure data is correctly stored
// and retrieved, and tests the reset logic.
//
// Verification Scenarios:
// - Sequential write operations
// - Sequential read operations
// - Reset functionality and memory clearing
// ============================================================================

#ifndef TB_MEMORY_H
#define TB_MEMORY_H

#include <systemc.h>
#include "memory.h"

using namespace std;
using namespace sc_core;

// Testbench for Memory Module
SC_MODULE(Testbench) {
    // Clock and Reset
    sc_clock clk;
    sc_signal<bool> rst;

    // Internal Signals
    sc_signal<bool> wrt_en;
    sc_signal<sc_uint<32>> data_in;
    sc_signal<sc_uint<32>> data_out;
    sc_signal<sc_uint<12>> raddr;
    sc_signal<sc_uint<12>> waddr;
    
    // DUT
    Memory<1024, 32, 12>* mem;
    
    // Tracing
    sc_trace_file* tf;

    // Constructor
    SC_CTOR(Testbench) : clk("clk", 10, SC_NS) {
        mem = new Memory<1024, 32, 12>("mem");
        mem->clk(clk);
        mem->rst(rst);
        mem->wrt_en(wrt_en);
        mem->data_in(data_in);
        mem->data_out(data_out);
        mem->raddr(raddr);
        mem->waddr(waddr);
        
        // Tracing
        tf = sc_create_vcd_trace_file("./out/vcd/memory_trace_070126");
        tf->set_time_unit(1, SC_NS);
        
        sc_trace(tf, clk, "clk");
        sc_trace(tf, rst, "rst");
        sc_trace(tf, wrt_en, "wrt_en");
        sc_trace(tf, data_in, "data_in");
        sc_trace(tf, data_out, "data_out");
        sc_trace(tf, raddr, "raddr");
        sc_trace(tf, waddr, "waddr");

        SC_THREAD(stimuli);
        sensitive << clk.posedge_event();
        SC_THREAD(reset_process);
    }
    
    ~Testbench() {
        delete mem;
        sc_close_vcd_trace_file(tf);
    }

    // Reset process to toggle the reset signal at appropriate times.
    void reset_process() {
        rst.write(true);
        wait(20, SC_NS);
        rst.write(false);
        wait(671, SC_NS);
        rst.write(true);
        wait(10, SC_NS);
        rst.write(false);
    }

    // Main stimulation thread to drive memory operations.
    void stimuli() {
        // Initial state
        wrt_en.write(false);
        raddr.write(0);
        waddr.write(0);
        data_in.write(0);
        wait(20, SC_NS);

        // Write sequence
        for (int i = 0; i < 16; i++) {
            wait(clk.posedge_event());
            wrt_en.write(true);
            waddr.write(i);
            data_in.write(i + 0xA0);
        }

        
        wait(20);
        waddr.write(2222);
        data_in.write(555);
        wrt_en.write(true);
        wait(20);
        wrt_en.write(false);

        // Read sequence
        for (int i = 0; i < 16; i++) {
            wait(clk.posedge_event());
            raddr.write(i);
        }

        wait(20);
        raddr.write(2222);
        wait(20);

        wait(100, SC_NS);
        sc_stop();
    }

};

#endif