// ============================================================================
// MONITOR.H - Standalone Monitoring Module for FFT System (AXI4-aware)
// ============================================================================

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

// ============================================================================
// Monitor Module Definition
// ============================================================================
template<int NUM_CORES, int N, typename AxiCfg>
SC_MODULE(Monitor) {
    sc_in<bool> clk;
    sc_in<bool> rst;
    
    // Reference to top-level channels monitored by reference
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
            last_addr[i] = 0xFFF; // Initialize to dummy address to trigger first print
            current_write_addr[i] = 0;
            write_beat_count[i] = 0;
        }
    }

    void monitor_process() {
        static const int HALF_WIDTH = AxiCfg::dataWidth / 2;
        
        // Monitor DMA read address handshakes
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
        
        // Track AW handshakes in queues
        for (int i = 0; i < NUM_CORES; i++) {
            if (mem_write_chans[i].aw.val.read() && mem_write_chans[i].aw.rdy.read()) {
                auto aw_pay = BitsToType<typename axi4<AxiCfg>::AddrPayload>(mem_write_chans[i].aw.msg.read());
                aw_addr_queue[i].push(aw_pay.addr.to_uint());
            }
        }
        
        // Monitor Core output write backs on W handshakes
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
                
                sc_uint<AxiCfg::dataWidth> raw = w_pay.data;
                double r, imag_val;
                if (AxiCfg::dataWidth == 64) {
                    int r_int = raw.range(AxiCfg::dataWidth - 1, HALF_WIDTH).to_int();
                    int i_int = raw.range(HALF_WIDTH - 1, 0).to_int();
                    r = (double)r_int;
                    imag_val = (double)i_int;
                } else {
                    int r_int = raw.range(AxiCfg::dataWidth - 1, 0).to_int();
                    r = (double)r_int;
                    imag_val = 0.0;
                }
                
                std::cout << "@" << std::setw(5) << sc_time_stamp() 
                          << " [Core " << i << "] Out[" << std::setw(2) << index << "] = " 
                          << complex_t(r, imag_val) << std::endl;
                          
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
