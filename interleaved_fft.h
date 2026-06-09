// ============================================================================
// INTERLEAVED_FFT.H - Multi-Core Interleaved FFT Processor (AXI4 version)
// ============================================================================
// This module implements a high-throughput, multi-core FFT architecture that 
// uses temporal interleaving to process continuous streams of data. By staggering 
// the start times of multiple independent FFT cores, it achieves a higher 
// overall data processing rate.
//
// Features:
// - Instantiates multiple DMA and FFT core pairs
// - Staggered execution based on a configurable hop size
// - Independent memory interfaces for each core
// - Vectorized ports for easy system integration
//
// Architecture:
//                 +---> DMA_0 ---> FFT_0 ---> Out_0
//                 |
//   Start Signal -+---> DMA_1 ---> FFT_1 ---> Out_1
//                 |
//                 +---> DMA_N ---> FFT_N ---> Out_N
// ============================================================================

#ifndef INTERLEAVED_FFT_H
#define INTERLEAVED_FFT_H

#include <systemc.h>
#include <vector>
#include <cmath>
#include "fft.h"
#include "dma.h"
#include "matchlib_axi.h"

// ============================================================================
// InterleavedFFT Module Definition
// ============================================================================
template<int N_SIZE, int NUM_CORES, int HOP_SIZE, typename AxiCfg, int NUM_MULT=4, int NUM_ADD=6>
SC_MODULE(InterleavedFFT) {
    // Clock and synchronous active-high reset
    sc_in<bool> clk;
    sc_in<bool> rst;
    
    // Global start trigger
    sc_in<bool> start;

    // AXI Read and Write Interfaces
    sc_vector<axi::axi4_read_master<AxiCfg>> mem_read_ports;
    sc_vector<axi::axi4_write_master<AxiCfg>> mem_write_ports;
    
    sc_vector<sc_in<sc_uint<AxiCfg::addrWidth>>> base_addrs;
    sc_vector<sc_in<int>> num_samples; 

    // System Outputs (for observability/monitoring)
    sc_vector<sc_out<complex_t>> out_data;
    sc_vector<sc_out<bool>> out_valids;
    sc_vector<sc_out<int>> out_indices;

    // Internal Signals
    sc_vector<sc_signal<bool>> dma_starts;
    sc_vector<sc_signal<bool>> dma_busy;
    
    // Inter-module signals (DMA -> FFT)
    sc_vector<sc_signal<complex_t>> fft_in_data;
    sc_vector<sc_signal<bool>> fft_in_valids;

    // Inter-module signals (FFT -> Out/Mem)
    sc_vector<sc_signal<complex_t>> int_out_data;
    sc_vector<sc_signal<bool>> int_out_valids;
    sc_vector<sc_signal<int>> int_out_indices;
    
    // Dummy signals for unused FFT ports
    sc_vector<sc_signal<bool>> fft_status;
    sc_vector<sc_signal<int>> fft_in_index;
    
    // DMA Control Signals
    sc_vector<sc_signal<bool>> fft_ready_sigs;

    // AXI Write registers
    sc_vector<sc_signal<bool>> aw_valid_regs;
    sc_vector<sc_signal<axi::AddrPayload<AxiCfg>>> aw_payload_regs;
    sc_vector<sc_signal<bool>> w_valid_regs;
    sc_vector<sc_signal<axi::WritePayload<AxiCfg>>> w_payload_regs;

    // Sub-modules
    sc_vector<FFT> fft_cores;
    sc_vector<DMA<AxiCfg>> dma_cores;

    int N;
    int num_cores;
    int hop;
    int alu_cycles;
    
    // Stagger Logic State
    sc_signal<bool> active_stagger;
    sc_signal<int> stagger_counter;

    SC_HAS_PROCESS(InterleavedFFT);

    InterleavedFFT(sc_module_name name)
        : sc_module(name), 
          N(N_SIZE), 
          num_cores(NUM_CORES), 
          hop(HOP_SIZE),
          mem_read_ports("mem_read_ports", NUM_CORES),
          mem_write_ports("mem_write_ports", NUM_CORES),
          base_addrs("base_addrs", NUM_CORES),
          num_samples("num_samples", NUM_CORES),
          out_data("out_data", NUM_CORES),
          out_valids("out_valids", NUM_CORES),
          out_indices("out_indices", NUM_CORES),
          dma_starts("dma_starts", NUM_CORES),
          dma_busy("dma_busy", NUM_CORES),
          fft_in_data("fft_in_data", NUM_CORES),
          fft_in_valids("fft_in_valids", NUM_CORES),
          int_out_data("int_out_data", NUM_CORES),
          int_out_valids("int_out_valids", NUM_CORES),
          int_out_indices("int_out_indices", NUM_CORES),
          fft_status("fft_status", NUM_CORES),
          fft_in_index("fft_in_index", NUM_CORES),
          fft_ready_sigs("fft_ready_sigs", NUM_CORES),
          aw_valid_regs("aw_valid_regs", NUM_CORES),
          aw_payload_regs("aw_payload_regs", NUM_CORES),
          w_valid_regs("w_valid_regs", NUM_CORES),
          w_payload_regs("w_payload_regs", NUM_CORES),
          fft_cores("fft_core", NUM_CORES, [&](const char* name, int i) {
              return new FFT(name, N_SIZE, NUM_MULT, NUM_ADD);
          }),
          dma_cores("dma_core", NUM_CORES)
    {
        alu_cycles = Stage::calc_latency(NUM_MULT, NUM_ADD);

        // Connect Cores
        for (int i = 0; i < num_cores; ++i) {
            // DMA Connections
            dma_cores[i].clk(clk);
            dma_cores[i].rst(rst);
            dma_cores[i].start(dma_starts[i]);
            dma_cores[i].fft_ready(fft_ready_sigs[i]);
            dma_cores[i].base_addr(base_addrs[i]);
            dma_cores[i].num_samples(num_samples[i]);
            dma_cores[i].busy(dma_busy[i]);
            
            // Connect AXI read port from top-level to internal DMA
            dma_cores[i].mem_read_port(mem_read_ports[i]);
            
            dma_cores[i].fft_data(fft_in_data[i]);
            dma_cores[i].fft_valid(fft_in_valids[i]);

            // FFT Connections
            fft_cores[i].clk(clk);
            fft_cores[i].rst(rst);
            fft_cores[i].in_data(fft_in_data[i]);
            fft_cores[i].in_valid(fft_in_valids[i]);
            fft_cores[i].pipeline_step_sig(fft_ready_sigs[i]);
            
            fft_cores[i].out_data(int_out_data[i]);
            fft_cores[i].out_valid(int_out_valids[i]);
            fft_cores[i].out_index(int_out_indices[i]);
            
            fft_cores[i].status(fft_status[i]);
            fft_cores[i].in_index(fft_in_index[i]);
        }

        SC_METHOD(control_logic);
        sensitive << clk.pos();

        SC_METHOD(output_pass_logic);
        for(int i = 0; i < num_cores; ++i) {
            sensitive << int_out_valids[i] << int_out_data[i] << int_out_indices[i];
        }
        
        SC_METHOD(write_back_logic);
        sensitive << clk.pos();
    }
    


    // =========================================================================
    // control_logic - Staggered Start Control Logic (Sequential)
    // =========================================================================
    // This process starts individual core-pairs (DMA-FFT) in a staggered fashion
    // after a global 'start' trigger. 
    //
    // The stagger delay between adjacent cores is determined by the `hop` parameter.
    // Core 'i' is triggered exactly at cycle: i * hop.
    // =========================================================================
    void control_logic() {
        if (rst.read()) {
            active_stagger.write(false);
            stagger_counter.write(0);
            for (int i = 0; i < num_cores; ++i) {
                dma_starts[i].write(false);
            }
        } else {
            bool is_active = active_stagger.read();
            int cnt = stagger_counter.read();

            // Detect new global start trigger
            if (start.read() && !is_active) {
                is_active = true;
                cnt = 0;
                active_stagger.write(true);
                stagger_counter.write(0);
            }

            // Distribute staggered starts if sequencing is active
            if (is_active) {
                for (int i = 0; i < num_cores; ++i) {
                    if (cnt == i * hop) {
                        dma_starts[i].write(true); // Start core i
                    } else {
                        dma_starts[i].write(false);
                    }
                }
                
                stagger_counter.write(cnt + 1);
                
                // End the stagger state machine once all cores have been started
                if (cnt > num_cores * hop + 2) {
                    active_stagger.write(false);
                }
            } else {
                 for (int i = 0; i < num_cores; ++i) {
                    dma_starts[i].write(false);
                }
            }
        }
    }

    // =========================================================================
    // output_pass_logic - Combinational Output Pass-Through
    // =========================================================================
    // Combines and routes internal FFT core data, valid, and index signals
    // directly to top-level system output ports.
    // =========================================================================
    void output_pass_logic() {
        for (int i = 0; i < num_cores; ++i) {
             out_data[i].write(int_out_data[i].read());
             out_valids[i].write(int_out_valids[i].read());
             out_indices[i].write(int_out_indices[i].read());
        }
    }

    // =========================================================================
    // write_back_logic - AXI Memory Write-back Handler (Sequential)
    // =========================================================================
    // This sequential block handles AXI write requests. When an FFT core outputs
    // a valid sample, this block is responsible for writing it back to memory at:
    //   addr = base_addr + N_SIZE (input offset) + sample_index
    //
    // It manages the AXI AW (Address Write) and W (Write Data) channels.
    // Real and Imaginary parts of the computed sample are rounded to integers 
    // and packed into a 64-bit word before assertion on the write bus.
    // =========================================================================
    void write_back_logic() {
        if (rst.read()) {
            for (int i = 0; i < num_cores; ++i) {
                aw_valid_regs[i].write(false);
                aw_payload_regs[i].write(axi::AddrPayload<AxiCfg>{});
                w_valid_regs[i].write(false);
                w_payload_regs[i].write(axi::WritePayload<AxiCfg>{});

                mem_write_ports[i].aw.vld.write(false);
                mem_write_ports[i].w.vld.write(false);
                mem_write_ports[i].b.rdy.write(true);
            }
        } else {
            for (int i = 0; i < num_cores; ++i) {
                mem_write_ports[i].b.rdy.write(true); // Always ready for responses

                bool aw_v = aw_valid_regs[i].read();
                axi::AddrPayload<AxiCfg> aw_p = aw_payload_regs[i].read();
                bool w_v = w_valid_regs[i].read();
                axi::WritePayload<AxiCfg> w_p = w_payload_regs[i].read();

                // Clear valid flags once a successful handshake occurs
                if (aw_v && mem_write_ports[i].aw.rdy.read()) {
                    aw_v = false;
                }
                if (w_v && mem_write_ports[i].w.rdy.read()) {
                    w_v = false;
                }

                // If a new valid result is presented at the FFT core output, prepare the write transaction
                if (int_out_valids[i].read()) {
                    // Compute destination memory offset (write output immediately after input block)
                    sc_uint<AxiCfg::addrWidth> addr = base_addrs[i].read() + N_SIZE + int_out_indices[i].read();
                    
                    aw_p.addr = addr;
                    aw_p.id = 0;
                    aw_p.len = 0;
                    aw_p.size = 0;
                    aw_p.burst = 0;
                    aw_v = true;

                    // Round and pack double-precision complex samples into integer words
                    complex_t out_val = int_out_data[i].read();
                    sc_int<32> r_sc = (int)std::round(out_val.real);
                    sc_int<32> i_sc = (int)std::round(out_val.imag);
                    sc_uint<AxiCfg::dataWidth> packed = 0;
                    if (AxiCfg::dataWidth == 64) {
                        packed.range(63, 32) = r_sc; // Real component on high 32 bits
                        packed.range(31, 0) = i_sc;  // Imaginary component on low 32 bits
                    } else {
                        packed = r_sc; // Real component only for 32-bit width
                    }

                    w_p.data = packed;
                    w_p.strb = 0xFF; // Write strobe covers all bytes
                    w_p.last = true; // Signal last burst transfer (single transfer write)
                    w_v = true;
                }

                aw_valid_regs[i].write(aw_v);
                aw_payload_regs[i].write(aw_p);
                w_valid_regs[i].write(w_v);
                w_payload_regs[i].write(w_p);

                // Drive physical ports
                mem_write_ports[i].aw.vld.write(aw_v);
                mem_write_ports[i].aw.msg.write(aw_p);
                mem_write_ports[i].w.vld.write(w_v);
                mem_write_ports[i].w.msg.write(w_p);
            }
        }
    }
};

#endif // INTERLEAVED_FFT_H
