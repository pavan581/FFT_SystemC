/*
 * memory.h
 *
 * Implements a Multi-Port shared SRAM module.
 *
 * It is configurable with template parameters specifying the number of write ports,
 * read ports, total memory depth, and AXI configuration struct. The module spawns
 * separate SystemC threads for each read and write interface to handle AXI transactions
 * concurrently, simulating a multi-port memory subsystem.
 */

#ifndef MEMORY_H
#define MEMORY_H

#include <systemc.h>
#include <axi/axi4.h>
#include <connections/connections.h>
#include <string>

using namespace sc_core;
using namespace axi;

// A generic Single-Port Memory module supporting one AXI4 read slave port and one write slave port.
template<unsigned DEPTH=1024, typename AxiCfg=void>
SC_MODULE(Memory) {
    sc_in<bool> clk;
    sc_in<bool> rst_n; // Active-low reset
    
    // AXI read/write slave interfaces using MatchLib
    typename axi4<AxiCfg>::read::template slave<> read_port;
    typename axi4<AxiCfg>::write::template slave<> write_port;

    sc_uint<AxiCfg::dataWidth> mem[DEPTH];

    static const int bytesPerBeat = AxiCfg::dataWidth / 8;
    static const int addrShift = (AxiCfg::dataWidth == 64) ? 3 : 2;

    // Read Port thread process
    void read_port_process() {
        read_port.reset();
        wait();
        
        while (true) {
            if (!rst_n.read()) {
                read_port.reset();
                wait();
                continue;
            }
            
            typename axi4<AxiCfg>::AddrPayload req;
            if (read_port.ar.PopNB(req)) {
                unsigned int addr = req.addr;
                int len = req.len;
                for (int beat = 0; beat <= len; ++beat) {
                    typename axi4<AxiCfg>::ReadPayload resp;
                    resp.data = ((addr >> addrShift) < DEPTH) ? (typename axi4<AxiCfg>::Data)mem[addr >> addrShift] : (typename axi4<AxiCfg>::Data)0;
                    resp.id = req.id;
                    resp.resp = 0; // OKAY response
                    resp.last = (beat == len);
                    
                    read_port.rwrite(resp);
                    addr = addr + bytesPerBeat;
                }
            } else {
                wait();
            }
        }
    }

    // Write Port thread process
    void write_port_process() {
        write_port.reset();
        wait();
        
        while (true) {
            if (!rst_n.read()) {
                write_port.reset();
                wait();
                continue;
            }
            
            typename axi4<AxiCfg>::AddrPayload req;
            if (write_port.aw.PopNB(req)) {
                unsigned int addr = req.addr;
                int len = req.len;
                for (int beat = 0; beat <= len; ++beat) {
                    typename axi4<AxiCfg>::WritePayload data = write_port.w.Pop();
                    if ((addr >> addrShift) < DEPTH) {
                        if (AxiCfg::useWriteStrobes) {
                            sc_uint<AxiCfg::dataWidth> original = mem[addr >> addrShift];
                            sc_uint<AxiCfg::dataWidth> mask = 0;
                            for (int i = 0; i < bytesPerBeat; ++i) {
                                if (data.wstrb[i]) {
                                    mask.range(8 * i + 7, 8 * i) = 0xFF;
                                }
                            }
                            mem[addr >> addrShift] = (original & ~mask) | ((typename axi4<AxiCfg>::Data)data.data & mask);
                        } else {
                            mem[addr >> addrShift] = (typename axi4<AxiCfg>::Data)data.data;
                        }
                    }
                    addr = addr + bytesPerBeat;
                }
                
                typename axi4<AxiCfg>::WRespPayload resp;
                resp.id = req.id;
                resp.resp = 0; // OKAY response
                
                write_port.bwrite(resp);
            } else {
                wait();
            }
        }
    }

    SC_CTOR(Memory)
        : read_port("read_port"),
          write_port("write_port") 
    {
        // Initialize memory elements
        for (unsigned int k = 0; k < DEPTH; ++k) {
            mem[k] = 0;
        }

        SC_THREAD(read_port_process);
        sensitive << clk.pos();
        async_reset_signal_is(rst_n, false);

        SC_THREAD(write_port_process);
        sensitive << clk.pos();
        async_reset_signal_is(rst_n, false);
    }
};

#endif // MEMORY_H