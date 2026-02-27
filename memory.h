// ============================================================================
// MEMORY.H - Multi-Port Memory Module
// ============================================================================
// This file defines a generic, configurable multi-port memory module for use
// in SystemC. It supports a single synchronous write port and multiple 
// synchronous read ports, making it suitable for ping-pong buffers or shared
// memory architectures in FFT data paths.
//
// Features:
// - Configurable memory size, data width, and number of read ports
// - Synchronous read/write operations
// - Synchronous clear (reset) functionality
// ============================================================================

#ifndef MEMORY_H
#define MEMORY_H

#include <systemc.h>

using namespace std;
using namespace sc_core;

// ============================================================================
// Memory Module Definition
// ============================================================================
// SC_MODULE: Parameterized memory
//
// Parameters:
//   NUM_PORTS  : Number of independent read ports (default 1)
//   MEM_SIZE   : Depth of the memory in elements (default 1024)
//   DATA_WIDTH : Width of each stored element in bits (default 32)
//   ADDR_WIDTH : Width of the address bus in bits (default 12)
// ============================================================================
template<int NUM_PORTS=1, unsigned MEM_SIZE=1024, unsigned DATA_WIDTH=32, unsigned ADDR_WIDTH=12>
SC_MODULE(Memory) {
    // Clock and synchronous active-high reset
    sc_in<bool> clk;
    sc_in<bool> rst;
    
    // Write Port (Single)
    sc_in<bool> wrt_en;
    sc_in<sc_uint<ADDR_WIDTH>> waddr;
    sc_in<sc_uint<DATA_WIDTH>> data_in;

    // Read Ports (Multiple)
    sc_vector<sc_in<sc_uint<ADDR_WIDTH>>> raddr;
    sc_vector<sc_out<sc_uint<DATA_WIDTH>>> data_out;

    sc_uint<DATA_WIDTH> mem[MEM_SIZE];

    // synchronous write
    void write() {
        unsigned int addr = waddr.read();
        if (wrt_en.read() && !rst.read() && addr < MEM_SIZE) {
            mem[addr] = data_in.read();
        }
    }

     // synchronous read (multi-port)
    void read() {
        if (!rst.read()) {
            for (int i = 0; i < NUM_PORTS; ++i) {
                unsigned int addr = raddr[i].read();
                if (addr < MEM_SIZE) {
                    data_out[i].write(mem[addr]);
                }
                else {
                    data_out[i].write(0);
                }
            }
        } else {
             for (int i = 0; i < NUM_PORTS; ++i) {
                 data_out[i].write(0);
             }
        }
    }

    // Synchronous clear and initialization.
    // Clears all memory contents to zero when reset is asserted.
    void clear() {
        if (rst.read()) {
            for (int i = 0; i < MEM_SIZE; i++) {
                mem[i] = 0;
            }
        }
    }

    SC_CTOR(Memory) : raddr("raddr", NUM_PORTS), data_out("data_out", NUM_PORTS) {
        SC_METHOD(write);
        sensitive << clk.pos();
        SC_METHOD(read);
        sensitive << clk.pos();
        SC_METHOD(clear);
        sensitive << rst.pos() << clk.pos();
    }
};

#endif