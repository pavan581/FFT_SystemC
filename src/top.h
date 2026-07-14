/*
 * top.h
 *
 * Interleaved multi-core FFT wrapper module.
 * Controls the staggered activation of individual processing cores using a HOP_SIZE
 * delay state machine to optimize memory bus bandwidth and throughput.
 */

#ifndef TOP_FFT_H
#define TOP_FFT_H

#include <systemc.h>
#include <vector>
#include <axi/axi4.h>
#include "core.h"

using namespace sc_core;
using namespace axi;

// Multi-core staggered FFT coordinator
template<int N_SIZE, int NUM_CORES, int HOP_SIZE, typename AxiCfg, int NUM_MULT=4, int NUM_ADD=6>
SC_MODULE(Top) {
    sc_in<bool> clk;
    sc_in<bool> rst_n; // Active-low reset
    sc_in<bool> start;

    // External AXI ports
    sc_vector<typename axi4<AxiCfg>::read::template master<>> mem_read_ports;
    sc_vector<typename axi4<AxiCfg>::write::template master<>> mem_write_ports;
    
    sc_vector<sc_in<sc_uint<AxiCfg::addrWidth>>> base_addrs;
    sc_vector<sc_in<int>> num_samples; 

    // Inter-core control signals
    sc_vector<sc_signal<bool>> core_starts;
    sc_vector<sc_signal<bool>> core_busy;

    sc_vector<Core<N_SIZE, AxiCfg, NUM_MULT, NUM_ADD>> cores;

    // Stagger logic state signals
    sc_signal<bool> active_stagger;
    sc_signal<int> stagger_counter;

    SC_HAS_PROCESS(Top);

    Top(sc_module_name name)
        : sc_module(name),
          clk("clk"),
          rst_n("rst_n"),
          start("start"),
          mem_read_ports("mem_read_ports", NUM_CORES),
          mem_write_ports("mem_write_ports", NUM_CORES),
          base_addrs("base_addrs", NUM_CORES),
          num_samples("num_samples", NUM_CORES),
          core_starts("core_starts", NUM_CORES),
          core_busy("core_busy", NUM_CORES),
          cores("core", NUM_CORES)
    {
        for (int i = 0; i < NUM_CORES; ++i) {
            cores[i].clk(clk);
            cores[i].rst_n(rst_n);
            cores[i].start(core_starts[i]);
            cores[i].base_addr(base_addrs[i]);
            cores[i].num_samples(num_samples[i]);
            cores[i].busy(core_busy[i]);
            
            cores[i].mem_read_port(mem_read_ports[i]);
            cores[i].mem_write_port(mem_write_ports[i]);
        }

        SC_METHOD(control_logic);
        sensitive << clk.pos() << rst_n.neg();
    }

    // Stagger launches to smooth bus traffic
    void control_logic() {
        if (!rst_n.read()) {
            active_stagger.write(false);
            stagger_counter.write(0);
            for (int i = 0; i < NUM_CORES; ++i) {
                core_starts[i].write(false);
            }
        } else {
            bool is_active = active_stagger.read();
            int cnt = stagger_counter.read();

            if (start.read() && !is_active) {
                is_active = true;
                cnt = 0;
                active_stagger.write(true);
                stagger_counter.write(0);
            }

            if (is_active) {
                for (int i = 0; i < NUM_CORES; ++i) {
                    if (cnt == i * HOP_SIZE) {
                        core_starts[i].write(true);
                    } else {
                        core_starts[i].write(false);
                    }
                }
                stagger_counter.write(cnt + 1);
                
                if (cnt > NUM_CORES * HOP_SIZE + 2) {
                    active_stagger.write(false);
                }
            } else {
                 for (int i = 0; i < NUM_CORES; ++i) {
                    core_starts[i].write(false);
                }
            }
        }
    }
};

#endif // TOP_FFT_H
