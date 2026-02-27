// ============================================================================
// DMA.H - Direct Memory Access Controller
// ============================================================================
// This module implements a dedicated DMA controller designed to stream data
// from a shared memory unit to the FFT processing pipeline. 
//
// Features:
// - Programmable base address and transfer length
// - Automatically generates consecutive memory addresses
// - Unpacks 64-bit raw memory words into complex_t samples
// - Generates a valid signal (fft_valid) to synchronize the FFT pipeline
// - Pipelines the memory read request to handle 1-cycle memory latency
// ============================================================================

#ifndef DMA_H
#define DMA_H

#include <systemc.h>
#include "fft_types.h"

using namespace sc_core;
using namespace std;

// ============================================================================
// DMA Module Definition
// ============================================================================
// ADDR_WIDTH: Width of the address bus for memory indexing
// DATA_WIDTH: Width of the memory data bus (typically 64 bits to hold Two 32-bit words)
// ============================================================================
template<unsigned ADDR_WIDTH=12, unsigned DATA_WIDTH=64>
SC_MODULE(DMA) {
    // Clock and synchronous active-high reset
    sc_in<bool> clk;
    sc_in<bool> rst;

    // Control Interface
    sc_in<bool> start;                 // Start signal
    sc_in<sc_uint<ADDR_WIDTH>> base_addr; // Starting address in memory
    sc_in<int> num_samples;            // Number of samples to transfer
    sc_out<bool> busy;                 // DMA is active

    // Memory Interface
    sc_out<sc_uint<ADDR_WIDTH>> mem_addr;
    sc_in<sc_uint<DATA_WIDTH>> mem_data;

    // FFT Interface
    sc_out<complex_t> fft_data;
    sc_out<bool> fft_valid;

    // Internal Signals/Registers
    sc_signal<int> sample_counter;
    sc_signal<bool> active;
    sc_signal<sc_uint<ADDR_WIDTH>> current_addr;
    
    // For timing alignment
    sc_signal<bool> read_req;   // Request issued
    sc_signal<bool> read_wait;  // Wait for memory
    
    // Sequential logic for address generation and control pipeline.
    // Handles state transitions, address increments, and valid signal generation.
    void seq_logic() {
        if (rst.read()) {
            // Reset state
            busy.write(false);
            mem_addr.write(0);
            active.write(false);
            sample_counter.write(0);
            read_req.write(false);
            read_wait.write(false);
            current_addr.write(0);
        } else {
            bool issuing_read = false;

            if (start.read() && !active.read()) {
                active.write(true);
                busy.write(true);
                mem_addr.write(base_addr.read());
                current_addr.write(base_addr.read() + 1);
                sample_counter.write(1);
                read_req.write(false); // Pipeline flush
                
                issuing_read = true;
            }
            
            if (active.read()) {
                int count = sample_counter.read();
                int total = num_samples.read();

                if (count < total) {
                   mem_addr.write(current_addr.read());
                   
                   // Prepare next address
                   current_addr.write(current_addr.read() + 1);
                   sample_counter.write(count + 1);
                   issuing_read = true;
                } else {
                    if (!read_req.read() && !read_wait.read()) {
                        active.write(false);
                        busy.write(false);
                    }
                }
            }
            read_wait.write(issuing_read);
            read_req.write(read_wait.read());
        }
    }

    // Combinational logic for data pass-through.
    // Unpacks 64-bit memory data into double-precision complex samples.
    void comb_logic() {
        if (read_req.read()) {
            fft_valid.write(true);
             
            sc_uint<DATA_WIDTH> raw = mem_data.read();
            double real_part, imag_part;
            
            if (DATA_WIDTH == 64) {
                unsigned int r_int = raw.range(63, 32).to_uint();
                unsigned int i_int = raw.range(31, 0).to_uint();
                real_part = (double)r_int;
                imag_part = (double)i_int;
            } else {
                real_part = (double)raw.to_uint();
                imag_part = 0.0;
            }
            fft_data.write(complex_t(real_part, imag_part));
        } else {
            fft_valid.write(false);
            fft_data.write(complex_t(0, 0));    
        }
    }

    SC_CTOR(DMA) {
        SC_METHOD(seq_logic);
        sensitive << clk.pos();
        
        SC_METHOD(comb_logic);
        sensitive << read_req << mem_data;
    }
};

#endif
