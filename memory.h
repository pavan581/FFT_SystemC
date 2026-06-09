// ============================================================================
// MEMORY.H - Multi-Port Memory Module (AXI4 & MatchLib version)
// ============================================================================
// This file defines a generic, configurable multi-port memory module for use
// in SystemC. It supports multiple AXI4 write slave ports and multiple AXI4
// read slave ports.
//
// Features:
// - Configurable memory depth, data width, and port counts
// - Implements AXI4 handshake flow control with internal buffers
// - Synchronous clear (reset) functionality
// ============================================================================

#ifndef MEMORY_H
#define MEMORY_H

#include <systemc.h>
#include "matchlib_axi.h"

using namespace std;
using namespace sc_core;

// ============================================================================
// Memory Module Definition
// ============================================================================
template<int NUM_WPORTS=1, int NUM_RPORTS=1, unsigned DEPTH=1024, typename AxiCfg=void>
SC_MODULE(Memory) {
    // Clock and synchronous active-high reset
    sc_in<bool> clk;
    sc_in<bool> rst;
    
    // AXI read/write slave interfaces
    sc_vector<axi::axi4_read_slave<AxiCfg>> read_ports;
    sc_vector<axi::axi4_write_slave<AxiCfg>> write_ports;

    sc_uint<AxiCfg::dataWidth> mem[DEPTH];

    // Read Channel registers
    sc_vector<sc_signal<bool>> r_pending;
    sc_vector<sc_signal<axi::ReadPayload<AxiCfg>>> r_data_regs;

    // Write Channel registers
    sc_vector<sc_signal<bool>> aw_pending;
    sc_vector<sc_signal<sc_uint<AxiCfg::addrWidth>>> aw_addr_regs;
    sc_vector<sc_signal<sc_uint<AxiCfg::idWidth>>> aw_id_regs;
    sc_vector<sc_signal<bool>> b_pending;
    sc_vector<sc_signal<sc_uint<AxiCfg::idWidth>>> b_id_regs;

    // Combined process for memory operations
    void memory_process() {
        if (rst.read()) {
            // Synchronous clear memory
            for (unsigned int k = 0; k < DEPTH; ++k) {
                mem[k] = 0;
            }

            // Reset Read Ports State
            for (int i = 0; i < NUM_RPORTS; ++i) {
                r_pending[i].write(false);
                r_data_regs[i].write(axi::ReadPayload<AxiCfg>{});
                
                read_ports[i].r.vld.write(false);
                read_ports[i].r.msg.write(axi::ReadPayload<AxiCfg>{});
                read_ports[i].ar.rdy.write(true);
            }

            // Reset Write Ports State
            for (int i = 0; i < NUM_WPORTS; ++i) {
                aw_pending[i].write(false);
                aw_addr_regs[i].write(0);
                aw_id_regs[i].write(0);
                b_pending[i].write(false);
                b_id_regs[i].write(0);
                
                write_ports[i].aw.rdy.write(true);
                write_ports[i].w.rdy.write(true);
                write_ports[i].b.vld.write(false);
                write_ports[i].b.msg.write(axi::WRespPayload<AxiCfg>{});
            }
        } else {
            // 1. Handle Read Ports
            for (int i = 0; i < NUM_RPORTS; ++i) {
                bool r_pend = r_pending[i].read();
                axi::ReadPayload<AxiCfg> r_pay = r_data_regs[i].read();

                if (r_pend) {
                    if (read_ports[i].r.rdy.read()) {
                        r_pend = false;
                    }
                }

                if (!r_pend) {
                    if (read_ports[i].ar.vld.read()) {
                        unsigned int addr = read_ports[i].ar.msg.read().addr;
                        axi::ReadPayload<AxiCfg> resp;
                        resp.data = (addr < DEPTH) ? mem[addr] : (sc_uint<AxiCfg::dataWidth>)0;
                        resp.id = read_ports[i].ar.msg.read().id;
                        resp.resp = 0; // OKAY
                        resp.last = true;

                        r_pay = resp;
                        r_pend = true;
                        read_ports[i].ar.rdy.write(true);
                    } else {
                        read_ports[i].ar.rdy.write(true);
                    }
                }

                r_pending[i].write(r_pend);
                r_data_regs[i].write(r_pay);
                
                read_ports[i].r.vld.write(r_pend);
                read_ports[i].r.msg.write(r_pay);

                if (r_pend && !read_ports[i].r.rdy.read()) {
                    read_ports[i].ar.rdy.write(false);
                }
            }

            // 2. Handle Write Ports
            for (int i = 0; i < NUM_WPORTS; ++i) {
                bool aw_pend = aw_pending[i].read();
                sc_uint<AxiCfg::addrWidth> aw_addr = aw_addr_regs[i].read();
                sc_uint<AxiCfg::idWidth> aw_id = aw_id_regs[i].read();
                bool b_pend = b_pending[i].read();
                sc_uint<AxiCfg::idWidth> b_id = b_id_regs[i].read();

                if (b_pend) {
                    if (write_ports[i].b.rdy.read()) {
                        b_pend = false;
                    }
                }

                if (b_pend) {
                    // Block new writes while write response is pending and not accepted
                    write_ports[i].aw.rdy.write(false);
                    write_ports[i].w.rdy.write(false);
                } else {
                    write_ports[i].aw.rdy.write(true);
                    write_ports[i].w.rdy.write(true);

                    if (aw_pend) {
                        if (write_ports[i].w.vld.read()) {
                            if (aw_addr < DEPTH) {
                                mem[aw_addr] = write_ports[i].w.msg.read().data;
                            }
                            b_id = aw_id;
                            b_pend = true;
                            aw_pend = false;
                        }
                    } else {
                        if (write_ports[i].aw.vld.read() && write_ports[i].w.vld.read()) {
                            unsigned int addr = write_ports[i].aw.msg.read().addr;
                            if (addr < DEPTH) {
                                mem[addr] = write_ports[i].w.msg.read().data;
                            }
                            b_id = write_ports[i].aw.msg.read().id;
                            b_pend = true;
                        } else if (write_ports[i].aw.vld.read()) {
                            aw_pend = true;
                            aw_addr = write_ports[i].aw.msg.read().addr;
                            aw_id = write_ports[i].aw.msg.read().id;
                        }
                    }
                }

                aw_pending[i].write(aw_pend);
                aw_addr_regs[i].write(aw_addr);
                aw_id_regs[i].write(aw_id);
                b_pending[i].write(b_pend);
                b_id_regs[i].write(b_id);

                write_ports[i].b.vld.write(b_pend);
                axi::WRespPayload<AxiCfg> b_resp;
                b_resp.id = b_id;
                b_resp.resp = 0; // OKAY
                write_ports[i].b.msg.write(b_resp);
            }
        }
    }

    SC_CTOR(Memory)
        : read_ports("read_ports", NUM_RPORTS),
          write_ports("write_ports", NUM_WPORTS),
          r_pending("r_pending", NUM_RPORTS),
          r_data_regs("r_data_regs", NUM_RPORTS),
          aw_pending("aw_pending", NUM_WPORTS),
          aw_addr_regs("aw_addr_regs", NUM_WPORTS),
          aw_id_regs("aw_id_regs", NUM_WPORTS),
          b_pending("b_pending", NUM_WPORTS),
          b_id_regs("b_id_regs", NUM_WPORTS) 
    {
        SC_METHOD(memory_process);
        sensitive << clk.pos();
    }
};

#endif // MEMORY_H