// ============================================================================
// DMA.H - Direct Memory Access Controller (Official MatchLib version)
// ============================================================================

#ifndef DMA_H
#define DMA_H

#include <systemc.h>
#include <axi/axi4.h>
#include <connections/connections.h>
#include "fft_types.h"
#include "stage.h"
#include <cmath>

using namespace sc_core;
using namespace axi;
using namespace Connections;

// ============================================================================
// DMA Module Definition
// ============================================================================
template<typename AxiCfg, int N_SIZE = 4, int NUM_MULT = 4, int NUM_ADD = 6>
SC_MODULE(DMA) {
    // Clock and active-high synchronous reset
    sc_in<bool> clk;
    sc_in<bool> rst;

    // Control Interface
    sc_in<bool> start;
    sc_in<sc_uint<AxiCfg::addrWidth>> base_addr;
    sc_in<int> num_samples;
    sc_out<bool> busy;

    // AXI Master Interfaces
    typename axi4<AxiCfg>::read::template master<Connections::SYN_PORT> mem_read_port;
    typename axi4<AxiCfg>::write::template master<Connections::SYN_PORT> mem_write_port;

    // Internal Connections Interfaces to FFT
    Out<complex_t> fft_out;
    In<complex_t> fft_in;

    // Calculates total pipeline delay / latency based on size and ALU constraints
    static int calc_pipeline_latency() {
        int num_stages = (int)std::log2(N_SIZE);
        int total_latency = 0;
        int alu_cycles = Stage<2>::calc_latency(NUM_MULT, NUM_ADD);
        
        for (int i = 0; i < num_stages; i++) {
            int current_N = N_SIZE >> i;
            int stage_latency = (current_N / 2);
            total_latency += stage_latency + (alu_cycles - 1);
        }
        return total_latency;
    }

    // Thread 1: Requests read addresses on AXI AR channel
    void read_addr_thread() {
        mem_read_port.ar.Reset();
        wait();
        
        while (true) {
            while (!start.read()) {
                wait();
            }
            
            int total = num_samples.read();
            if (total > 0) {
                sc_uint<AxiCfg::addrWidth> addr = base_addr.read();
                
                for (int i = 0; i < total; ++i) {
                    typename axi4<AxiCfg>::AddrPayload req;
                    req.addr = addr;
                    req.id = 0;
                    req.len = 0;
                    req.size = AxiCfg::dataWidth == 64 ? 3 : 2;
                    req.burst = 1; // INCR Mode
                    
                    mem_read_port.ar.Push(req);
                    addr = addr + 1; // Increment address by 1 word
                }
            }
            
            // Wait for busy to deassert before listening for next start
            while (busy.read()) {
                wait();
            }
            
            while (start.read()) {
                wait();
            }
        }
    }

    // Thread 2: Receives AXI read data on R channel and pushes to FFT, flushing at end
    void read_data_thread() {
        mem_read_port.r.Reset();
        fft_out.Reset();
        wait();
        
        while (true) {
            while (!start.read()) {
                wait();
            }
            
            int total = num_samples.read();
            if (total > 0) {
                int latency = calc_pipeline_latency();
                int total_inputs = ((total + latency + N_SIZE - 1) / N_SIZE) * N_SIZE;
                int dummy_inputs_to_push = total_inputs - total;
                
                // Push real samples
                for (int i = 0; i < total; ++i) {
                    typename axi4<AxiCfg>::ReadPayload resp = mem_read_port.r.Pop();
                    
                    double real_part, imag_part;
                    static const int HALF_WIDTH = AxiCfg::dataWidth / 2;
                    if (AxiCfg::dataWidth == 64) {
                        int r_int = resp.data.range(AxiCfg::dataWidth - 1, HALF_WIDTH).to_int();
                        int i_int = resp.data.range(HALF_WIDTH - 1, 0).to_int();
                        real_part = (double)r_int;
                        imag_part = (double)i_int;
                    } else {
                        int r_int = resp.data.range(AxiCfg::dataWidth - 1, 0).to_int();
                        real_part = (double)r_int;
                        imag_part = 0.0;
                    }
                    
                    complex_t sample(real_part, imag_part);
                    fft_out.Push(sample);
                }
                
                // Push dummy samples to flush the pipeline to a block boundary
                for (int i = 0; i < dummy_inputs_to_push; ++i) {
                    fft_out.Push(complex_t(0.0, 0.0));
                }
            }
            
            while (start.read()) {
                wait();
            }
        }
    }

    // Thread 3: Collects FFT results, discards initial latency, and writes real outputs back
    void write_thread() {
        mem_write_port.aw.Reset();
        mem_write_port.w.Reset();
        mem_write_port.b.Reset();
        fft_in.Reset();
        busy.write(false);
        wait();
        
        while (true) {
            while (!start.read()) {
                wait();
            }
            busy.write(true);
            
            int total = num_samples.read();
            if (total > 0) {
                static const int HALF_WIDTH = AxiCfg::dataWidth / 2;
                int latency = calc_pipeline_latency();
                int total_inputs = ((total + latency + N_SIZE - 1) / N_SIZE) * N_SIZE;
                int remaining_to_discard = total_inputs - latency - total;
                sc_uint<AxiCfg::addrWidth> addr = base_addr.read() + N_SIZE;
                
                // Discard the initial pipeline latency/dummy outputs
                for (int i = 0; i < latency; ++i) {
                    fft_in.Pop();
                }
                
                // Write real outputs to memory
                for (int i = 0; i < total; ++i) {
                    complex_t out_val = fft_in.Pop();
                    
                    typename axi4<AxiCfg>::AddrPayload aw_pay;
                    aw_pay.addr = addr + i;
                    aw_pay.id = 0;
                    aw_pay.len = 0;
                    aw_pay.size = AxiCfg::dataWidth == 64 ? 3 : 2;
                    aw_pay.burst = 1;
                    
                    sc_int<32> r_sc = (int)std::round(out_val.real);
                    sc_int<32> i_sc = (int)std::round(out_val.imag);
                    sc_uint<AxiCfg::dataWidth> packed = 0;
                    if (AxiCfg::dataWidth == 64) {
                        packed.range(AxiCfg::dataWidth - 1, HALF_WIDTH) = r_sc;
                        packed.range(HALF_WIDTH - 1, 0) = i_sc;
                    } else {
                        packed = r_sc;
                    }
                    
                    typename axi4<AxiCfg>::WritePayload w_pay;
                    w_pay.data = packed;
                    w_pay.wstrb = ~0;
                    w_pay.last = true;
                    
                    mem_write_port.aw.Push(aw_pay);
                    mem_write_port.w.Push(w_pay);
                    
                    // Discard write response handshake
                    mem_write_port.b.Pop();
                }
                
                // Discard the remaining trailing dummy outputs to clear the pipeline
                for (int i = 0; i < remaining_to_discard; ++i) {
                    fft_in.Pop();
                }
            }
            
            busy.write(false);
            
            while (start.read()) {
                wait();
            }
        }
    }

    SC_HAS_PROCESS(DMA);
    DMA(sc_module_name name) 
        : sc_module(name),
          mem_read_port("mem_read_port"),
          mem_write_port("mem_write_port"),
          fft_out("fft_out"),
          fft_in("fft_in") 
    {
        SC_THREAD(read_addr_thread);
        sensitive << clk.pos();
        async_reset_signal_is(rst, true);

        SC_THREAD(read_data_thread);
        sensitive << clk.pos();
        async_reset_signal_is(rst, true);

        SC_THREAD(write_thread);
        sensitive << clk.pos();
        async_reset_signal_is(rst, true);
    }
};

#endif // DMA_H
