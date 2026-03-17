// ============================================================================
// MONITOR.H - Standalone Monitoring Module for FFT System
// ============================================================================
// This module monitors the output of the FFT cores and the memory read addresses.
// ============================================================================

#ifndef MONITOR_H
#define MONITOR_H

#include <systemc.h>
#include "fft_types.h"
#include <iostream>
#include <iomanip>
#include <vector>

template<int NUM_CORES, int ADDR_WIDTH>
SC_MODULE(Monitor) {
    sc_in<bool> clk;
    
    sc_vector<sc_in<complex_t>> out_data;
    sc_vector<sc_in<bool>> out_valids;
    sc_vector<sc_in<int>> out_indices;
    
    sc_vector<sc_in<sc_uint<ADDR_WIDTH>>> mem_raddrs;

    SC_HAS_PROCESS(Monitor);

    Monitor(sc_module_name name) :
        sc_module(name),
        clk("clk"),
        out_data("out_data", NUM_CORES),
        out_valids("out_valid", NUM_CORES),
        out_indices("out_index", NUM_CORES),
        mem_raddrs("mem_raddrs", NUM_CORES)
    {
        SC_METHOD(monitor_process);
        sensitive << clk.pos();
        
        for(int i=0; i<NUM_CORES; i++) {
            last_addr[i] = 0;
        }
    }

private:
    sc_uint<ADDR_WIDTH> last_addr[NUM_CORES];

    void monitor_process() {
        // Monitor FFT Core Outputs
        for (int i = 0; i < NUM_CORES; i++) {
            if (out_valids[i].read()) {
                std::cout << "@" << std::setw(5) << sc_time_stamp() 
                          << " [Core " << i << "] Out[" << std::setw(2) << out_indices[i].read() << "] = " 
                          << out_data[i].read() << std::endl;
            }
        }
        
        // Monitor Memory Read Addresses (DMA activity)
        for(int i=0; i<NUM_CORES; i++) {
            if (mem_raddrs[i].read() != last_addr[i]) {
                 std::cout << "DEBUG DMA[" << i << "] Read Addr: " << mem_raddrs[i].read() << " @ " << sc_time_stamp() << std::endl;
                 last_addr[i] = mem_raddrs[i].read();
            }
        }
    }
};

#endif // MONITOR_H
