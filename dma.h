/*
 * dma.h
 *
 * Implements the Direct Memory Access (DMA) controller module.
 *
 * The DMA controller manages AXI4 transactional requests to stream inputs from
 * shared memory to the FFT core, and write computed frequency bins back to memory.
 * It uses three concurrent SC_THREADs to request read addresses, unpack and pad input
 * data, and pack and write-back output data.
 */

#ifndef DMA_H
#define DMA_H

#include <systemc.h>
#include <axi/axi4.h>
#include <connections/connections.h>
#include "fft_types.h"
#include "stage.h"
#include <cmath>
#include <iostream>
#include <iomanip>
#include <string>

using namespace sc_core;
using namespace axi;
using namespace Connections;

// Direct Memory Access (DMA) controller for driving FFT core data transfers over AXI.
template<typename AxiCfg, int N_SIZE = 4, int NUM_MULT = 4, int NUM_ADD = 6>
SC_MODULE(DMA) {
    sc_in<bool> clk;
    sc_in<bool> rst_n; // Active-low reset

    // Control interface
    sc_in<bool> start;
    sc_in<sc_uint<AxiCfg::addrWidth>> base_addr;
    sc_in<int> num_samples;
    sc_out<bool> busy;

    // AXI Master interfaces (default port types)
    typename axi4<AxiCfg>::read::template master<> mem_read_port;
    typename axi4<AxiCfg>::write::template master<> mem_write_port;

    // Connections interfaces to the FFT core
    Out<complex_t> fft_out;
    In<complex_t> fft_in;

    typedef typename axi4<AxiCfg>::AddrPayload AddrPayload;
    typedef typename axi4<AxiCfg>::ReadPayload ReadPayload;
    typedef typename axi4<AxiCfg>::WritePayload WritePayload;
    typedef typename axi4<AxiCfg>::WRespPayload WRespPayload;

    static const int bytesPerBeat = AxiCfg::dataWidth / 8;

    // Helper to build AXI address requests
    AddrPayload create_addr_req(typename axi4<AxiCfg>::Addr addr, int len) {
        AddrPayload req;
        req.addr = addr;
        req.id = 0;
        req.len = len;
        req.size = AxiCfg::dataWidth == 64 ? 3 : 2;
        req.burst = 1; // INCR burst
        return req;
    }

    // Helper to build AXI write data payloads
    WritePayload create_write_payload(typename axi4<AxiCfg>::Data data, bool last) {
        WritePayload w_pay;
        w_pay.data = data;
        w_pay.wstrb = ~0;
        w_pay.last = last;
        return w_pay;
    }

    // Calculates overall FFT pipeline latency under given ALU resource constraints
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
                AddrPayload req = create_addr_req(base_addr.read(), total - 1);
                mem_read_port.ar.Push(req);
            }
            
            while (busy.read()) {
                wait();
            }
            while (start.read()) {
                wait();
            }
        }
    }

    // Thread 2: Receives AXI read data, unpacks it, and streams to FFT
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
                int flush_inputs_to_push = total_inputs - total;
                
                // Read and unpack samples from memory
                for (int i = 0; i < total; ++i) {
                    ReadPayload resp = mem_read_port.r.Pop();
                    complex_t sample = unpack_complex<AxiCfg>(resp.data);
                    fft_out.Push(sample);
                }
                
                // Push trailing zeros to flush pipeline to block boundary
                for (int i = 0; i < flush_inputs_to_push; ++i) {
                    fft_out.Push(complex_t(0.0, 0.0));
                }
            }
            
            while (start.read()) {
                wait();
            }
        }
    }

    // Thread 3: Collects FFT results, packs and writes them back over AXI, and discards flush samples
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
                int latency = calc_pipeline_latency();
                int total_inputs = ((total + latency + N_SIZE - 1) / N_SIZE) * N_SIZE;
                int flush_outputs_to_discard = total_inputs - total;
                typename axi4<AxiCfg>::Addr addr = base_addr.read() + N_SIZE * bytesPerBeat;
                
                // Address handshake for the write burst
                AddrPayload aw_pay = create_addr_req(addr, total - 1);
                mem_write_port.aw.Push(aw_pay);
                
                // Write active samples back to memory
                for (int i = 0; i < total; ++i) {
                    complex_t out_val = fft_in.Pop();
                    typename axi4<AxiCfg>::Data packed = pack_complex<AxiCfg>(out_val);
                    WritePayload w_pay = create_write_payload(packed, i == total - 1);
                    mem_write_port.w.Push(w_pay);
                }
                
                // Pop and discard trailing pipeline flush outputs
                for (int i = 0; i < flush_outputs_to_discard; ++i) {
                    fft_in.Pop();
                }
                
                // Receive write response
                mem_write_port.b.Pop();
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
        async_reset_signal_is(rst_n, false);

        SC_THREAD(read_data_thread);
        sensitive << clk.pos();
        async_reset_signal_is(rst_n, false);

        SC_THREAD(write_thread);
        sensitive << clk.pos();
        async_reset_signal_is(rst_n, false);
    }
};

#endif // DMA_H
