// ============================================================================
// MEMORY.H - Multi-Port Memory Module (Official MatchLib version)
// ============================================================================
// This file defines a generic, configurable multi-port memory module for use
// in SystemC. It supports multiple AXI4 write slave ports and multiple AXI4
// read slave ports.
// ============================================================================

#ifndef MEMORY_H
#define MEMORY_H

#include <systemc.h>
#include <axi/axi4.h>
#include <string>

using namespace sc_core;
using namespace axi;

// ============================================================================
// Memory Module Definition
// ============================================================================
template<int NUM_WPORTS=1, int NUM_RPORTS=1, unsigned DEPTH=1024, typename AxiCfg=void>
SC_MODULE(Memory) {
    // Clock and synchronous active-high reset
    sc_in<bool> clk;
    sc_in<bool> rst;
    
    // AXI read/write slave interfaces using official MatchLib
    sc_vector<typename axi4<AxiCfg>::read::template slave<Connections::SYN_PORT>> read_ports;
    sc_vector<typename axi4<AxiCfg>::write::template slave<Connections::SYN_PORT>> write_ports;

    sc_uint<AxiCfg::dataWidth> mem[DEPTH];

    // Read Port thread process
    void read_port_process(int port_idx) {
        read_ports[port_idx].reset();
        wait();
        
        while (true) {
            if (rst.read()) {
                read_ports[port_idx].reset();
                wait();
                continue;
            }
            
            typename axi4<AxiCfg>::AddrPayload req;
            if (read_ports[port_idx].ar.PopNB(req)) {
                unsigned int addr = req.addr;
                int len = req.len;
                for (int beat = 0; beat <= len; ++beat) {
                    typename axi4<AxiCfg>::ReadPayload resp;
                    resp.data = (addr < DEPTH) ? mem[addr] : (sc_uint<AxiCfg::dataWidth>)0;
                    resp.id = req.id;
                    resp.resp = 0; // OKAY
                    resp.last = (beat == len);
                    
                    read_ports[port_idx].rwrite(resp);
                    
                    // Increment address
                    addr = addr + 1;
                }
            } else {
                wait();
            }
        }
    }

    // Write Port thread process
    void write_port_process(int port_idx) {
        write_ports[port_idx].reset();
        wait();
        
        while (true) {
            if (rst.read()) {
                write_ports[port_idx].reset();
                wait();
                continue;
            }
            
            typename axi4<AxiCfg>::AddrPayload req;
            if (write_ports[port_idx].aw.PopNB(req)) {
                unsigned int addr = req.addr;
                int len = req.len;
                for (int beat = 0; beat <= len; ++beat) {
                    typename axi4<AxiCfg>::WritePayload data = write_ports[port_idx].w.Pop();
                    if (addr < DEPTH) {
                        mem[addr] = data.data;
                    }
                    // Increment address
                    addr = addr + 1;
                }
                
                typename axi4<AxiCfg>::WRespPayload resp;
                resp.id = req.id;
                resp.resp = 0; // OKAY
                
                write_ports[port_idx].bwrite(resp);
            } else {
                wait();
            }
        }
    }

    SC_CTOR(Memory)
        : read_ports("read_ports", NUM_RPORTS),
          write_ports("write_ports", NUM_WPORTS) 
    {
        for (unsigned int k = 0; k < DEPTH; ++k) {
            mem[k] = 0;
        }

        // Spawn read port threads
        for (int i = 0; i < NUM_RPORTS; ++i) {
            sc_spawn_options opt;
            opt.set_sensitivity(&clk.pos());
            opt.async_reset_signal_is(rst, true);
            sc_spawn([this, i]() { this->read_port_process(i); }, 
                     sc_gen_unique_name("read_port_process"), &opt);
        }

        // Spawn write port threads
        for (int i = 0; i < NUM_WPORTS; ++i) {
            sc_spawn_options opt;
            opt.set_sensitivity(&clk.pos());
            opt.async_reset_signal_is(rst, true);
            sc_spawn([this, i]() { this->write_port_process(i); }, 
                     sc_gen_unique_name("write_port_process"), &opt);
        }
    }
};

#endif // MEMORY_H