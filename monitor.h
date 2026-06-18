/*
 * monitor.h
 *
 * Implements a standalone AXI4-aware simulation monitor module.
 *
 * It is connected to the top-level AXI read and write channels by reference.
 * It monitors AR read requests, AW write requests, and W write data handshakes
 * to log the address transfers and print the computed FFT output values printed
 * by each core at the precise simulation cycle.
 */

#ifndef MONITOR_H
#define MONITOR_H

#include <systemc.h>
#include <axi/axi4.h>
#include <connections/connections.h>
#include "fft_types.h"
#include <queue>
#include <iostream>
#include <iomanip>

using namespace sc_core;
using namespace axi;
using namespace Connections;

// Monitor module for tracing AXI transactional handshakes and logging processed FFT output values.
template<int NUM_CORES, int N, typename AxiCfg>
SC_MODULE(Monitor) {
    sc_in<bool> clk;
    sc_in<bool> rst;
    
    // Monitored global channels
    const sc_vector<typename axi4<AxiCfg>::read::template chan<Connections::SYN_PORT>>& mem_read_chans;
    const sc_vector<typename axi4<AxiCfg>::write::template chan<Connections::SYN_PORT>>& mem_write_chans;
    const sc_vector<sc_signal<sc_uint<AxiCfg::addrWidth>>>& base_addrs;

    sc_uint<AxiCfg::addrWidth> last_addr[NUM_CORES];
    std::queue<unsigned int> aw_addr_queue[NUM_CORES];
    unsigned int current_write_addr[NUM_CORES];
    unsigned int write_beat_count[NUM_CORES];

    SC_HAS_PROCESS(Monitor);

    Monitor(sc_module_name name,
            const sc_vector<typename axi4<AxiCfg>::read::template chan<Connections::SYN_PORT>>& r_chans,
            const sc_vector<typename axi4<AxiCfg>::write::template chan<Connections::SYN_PORT>>& w_chans,
            const sc_vector<sc_signal<sc_uint<AxiCfg::addrWidth>>>& b_addrs)
        : sc_module(name),
          clk("clk"),
          rst("rst"),
          mem_read_chans(r_chans),
          mem_write_chans(w_chans),
          base_addrs(b_addrs)
    {
        SC_METHOD(monitor_process);
        sensitive << clk.pos();
        
        for (int i = 0; i < NUM_CORES; ++i) {
            last_addr[i] = 0xFFF;
            current_write_addr[i] = 0;
            write_beat_count[i] = 0;
        }
    }

    void monitor_process() {
        // Track read requests
        for (int i = 0; i < NUM_CORES; i++) {
            if (mem_read_chans[i].ar.val.read() && mem_read_chans[i].ar.rdy.read()) {
                auto ar_pay = BitsToType<typename axi4<AxiCfg>::AddrPayload>(mem_read_chans[i].ar.msg.read());
                sc_uint<AxiCfg::addrWidth> addr = ar_pay.addr;
                if (addr != last_addr[i]) {
                    std::cout << "DEBUG DMA[" << i << "] Read Addr: " << addr << " @ " << sc_time_stamp() << std::endl;
                    last_addr[i] = addr;
                }
            }
        }
        
        // Track write address transfers
        for (int i = 0; i < NUM_CORES; i++) {
            if (mem_write_chans[i].aw.val.read() && mem_write_chans[i].aw.rdy.read()) {
                auto aw_pay = BitsToType<typename axi4<AxiCfg>::AddrPayload>(mem_write_chans[i].aw.msg.read());
                aw_addr_queue[i].push(aw_pay.addr.to_uint());
            }
        }
        
        // Trace and print output samples on AXI W channel handshakes
        for (int i = 0; i < NUM_CORES; i++) {
            if (mem_write_chans[i].w.val.read() && mem_write_chans[i].w.rdy.read()) {
                auto w_pay = BitsToType<typename axi4<AxiCfg>::WritePayload>(mem_write_chans[i].w.msg.read());
                
                unsigned int addr = 0;
                if (write_beat_count[i] == 0) {
                    if (!aw_addr_queue[i].empty()) {
                        addr = aw_addr_queue[i].front();
                        aw_addr_queue[i].pop();
                    } else {
                        addr = base_addrs[i].read() + N;
                    }
                    current_write_addr[i] = addr;
                } else {
                    current_write_addr[i] += 1;
                    addr = current_write_addr[i];
                }
                
                int index = addr - (base_addrs[i].read() + N);
                complex_t out_val = unpack_complex<AxiCfg>(w_pay.data);
                
                std::cout << "@" << std::setw(5) << sc_time_stamp() 
                          << " [Core " << i << "] Out[" << std::setw(2) << index << "] = " 
                          << out_val << std::endl;
                          
                if (w_pay.last) {
                    write_beat_count[i] = 0;
                } else {
                    write_beat_count[i] += 1;
                }
            }
        }
    }
};

#endif // MONITOR_H
