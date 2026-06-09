// ============================================================================
// DMA.H - Direct Memory Access Controller (AXI4 & MatchLib version)
// ============================================================================
// This module implements a dedicated DMA controller designed to stream data
// from a shared memory unit to the FFT processing pipeline using AXI4 interfaces.
//
// Features:
// - Programmable base address and transfer length
// - Uses MatchLib AXI4 read initiator (master) interface
// - Automatically generates pipelined read requests
// - Unpacks 64-bit raw memory data into complex_t samples
// - Generates a valid signal (fft_valid) to synchronize the FFT pipeline
// - Implements a 1-stage handshake buffer to prevent ready/valid deadlock
// ============================================================================

#ifndef DMA_H
#define DMA_H

#include <systemc.h>
#include "fft_types.h"
#include "matchlib_axi.h"

using namespace sc_core;
using namespace std;

// ============================================================================
// DMA Module Definition
// ============================================================================
template<typename AxiCfg>
SC_MODULE(DMA) {
    // Clock and synchronous active-high reset
    sc_in<bool> clk;
    sc_in<bool> rst;

    // Control Interface
    sc_in<bool> start;                 // Start signal
    sc_in<bool> fft_ready;             // Handshake with FFT
    sc_in<sc_uint<AxiCfg::addrWidth>> base_addr; // Starting address in memory
    sc_in<int> num_samples;            // Number of samples to transfer
    sc_out<bool> busy;                 // DMA is active

    // AXI Read Master Interface
    axi::axi4_read_master<AxiCfg> mem_read_port;

    // FFT Interface
    sc_out<complex_t> fft_data;
    sc_out<bool> fft_valid;

    // Internal Registers
    sc_signal<bool> active;
    sc_signal<sc_uint<AxiCfg::addrWidth>> req_addr;
    sc_signal<int> req_count;
    sc_signal<int> resp_count;

    // Decoupling Buffer Registers
    sc_signal<bool> buf_valid;
    sc_signal<complex_t> buf_data;

    // Sequential process for AXI read transactions
    // =========================================================================
    // seq_logic - Sequential AXI Read Controller
    // =========================================================================
    // This process acts as the state machine and data path sequencer for AXI 
    // read transfers. It runs on the rising edge of the clock.
    //
    // It controls two main stages:
    // 1. Idle state waiting for a 'start' trigger. When triggered, it initializes 
    //    pointers and requests the very first address payload on the AR channel.
    // 2. Active state where it manages the address (AR) and data response (R) 
    //    channels concurrently to keep the AXI memory-read pipeline full.
    // =========================================================================
    void seq_logic() {
        if (rst.read()) {
            active.write(false);
            busy.write(false);
            req_addr.write(0);
            req_count.write(0);
            resp_count.write(0);
            
            // Clean the internal decoupling buffer
            buf_valid.write(false);
            buf_data.write(complex_t(0, 0));
            
            // Deassert the initial Address Read valid flag
            mem_read_port.ar.vld.write(false);
            mem_read_port.ar.msg.write(axi::AddrPayload<AxiCfg>{});
            
            // Zero out FFT inputs
            fft_valid.write(false);
            fft_data.write(complex_t(0, 0));
        } else {
            // Trigger DMA transfer if we receive a start signal while idle
            if (start.read() && !active.read()) {
                active.write(true);
                busy.write(true);
                req_addr.write(base_addr.read());
                req_count.write(0);
                resp_count.write(0);
                
                buf_valid.write(false);
                buf_data.write(complex_t(0, 0));
                
                // Assert the first address request immediately if length is valid
                if (num_samples.read() > 0) {
                    mem_read_port.ar.vld.write(true);
                    axi::AddrPayload<AxiCfg> req;
                    req.addr = base_addr.read();
                    req.id = 0;
                    req.len = 0;
                    req.size = 0;
                    req.burst = 0;
                    mem_read_port.ar.msg.write(req);
                } else {
                    mem_read_port.ar.vld.write(false);
                }
                
                fft_valid.write(false);
                fft_data.write(complex_t(0, 0));
            } else if (active.read()) {
                int r_count = req_count.read();
                int d_count = resp_count.read();
                sc_uint<AxiCfg::addrWidth> cur_addr = req_addr.read();
                int total = num_samples.read();

                // -------------------------------------------------------------
                // 1. AXI Address Read (AR) Phase
                // -------------------------------------------------------------
                // If the memory slave accepted our address payload last cycle 
                // (AR handshake: VLD & RDY are high), we prepare the next address.
                bool ar_handshake = mem_read_port.ar.vld.read() && mem_read_port.ar.rdy.read();
                int next_r_count = r_count;
                if (ar_handshake) {
                    next_r_count = r_count + 1;
                    cur_addr = cur_addr + 1; // Increment address by 1 word (sequential read)
                    req_addr.write(cur_addr);
                    req_count.write(next_r_count);
                }

                // Keep requesting new addresses until we've sent out 'total' requests
                if (next_r_count < total) {
                    mem_read_port.ar.vld.write(true);
                    axi::AddrPayload<AxiCfg> req;
                    req.addr = cur_addr;
                    req.id = 0;
                    req.len = 0;
                    req.size = 0;
                    req.burst = 0;
                    mem_read_port.ar.msg.write(req);
                } else {
                    // Stop requesting addresses once all requests are outstanding
                    mem_read_port.ar.vld.write(false);
                }

                // -------------------------------------------------------------
                // 2. Decoupling Buffer & AXI Data Read (R) Phase
                // -------------------------------------------------------------
                bool next_buf_valid = buf_valid.read();
                complex_t next_buf_data = buf_data.read();
                
                // If the downstream FFT consumed the previous data this cycle, 
                // we clear the valid flag of our buffer.
                if (buf_valid.read() && fft_ready.read()) {
                    next_buf_valid = false;
                }

                // Check for a new data payload coming from the memory slave
                bool r_handshake = mem_read_port.r.vld.read() && mem_read_port.r.rdy.read();
                int next_d_count = d_count;
                if (r_handshake) {
                    next_d_count = d_count + 1;
                    resp_count.write(next_d_count);

                    // Unpack AXI data payload. 
                    // - 64-bit wide: Real part is packed in upper 32 bits, imaginary part in lower 32 bits.
                    // - 32-bit wide: Only real part is fetched; imaginary part defaults to 0.0.
                    axi::ReadPayload<AxiCfg> resp = mem_read_port.r.msg.read();
                    double real_part, imag_part;
                    if (AxiCfg::dataWidth == 64) {
                        int r_int = resp.data.range(63, 32).to_int();
                        int i_int = resp.data.range(31, 0).to_int();
                        real_part = (double)r_int;
                        imag_part = (double)i_int;
                    } else {
                        int r_int = resp.data.range(31, 0).to_int();
                        real_part = (double)r_int;
                        imag_part = 0.0;
                    }
                    next_buf_data = complex_t(real_part, imag_part);
                    next_buf_valid = true;
                }

                buf_valid.write(next_buf_valid);
                buf_data.write(next_buf_data);
                
                // Expose our decoupled data directly to the FFT pipeline
                fft_valid.write(next_buf_valid);
                fft_data.write(next_buf_data);

                // Exit criteria: Stop and go idle when we have received all expected responses.
                if (next_d_count >= total) {
                    active.write(false);
                    busy.write(false);
                    mem_read_port.ar.vld.write(false);
                }
            } else {
                mem_read_port.ar.vld.write(false);
                fft_valid.write(false);
            }
        }
    }

    // =========================================================================
    // comb_logic - Ready-Valid Flow Control Gate
    // =========================================================================
    // This combinational method determines when the DMA is ready to accept a
    // new AXI data word.
    //
    // To ensure zero-cycle handshake response and prevent head-of-line blocking:
    // We are ready (`r.rdy` = 1) if:
    // - The internal buffer is currently empty (`!buf_valid`)
    // - OR the buffer is being consumed by the FFT core this cycle (`fft_ready` is active)
    // =========================================================================
    void comb_logic() {
        mem_read_port.r.rdy.write(!buf_valid.read() || fft_ready.read());
    }

    SC_HAS_PROCESS(DMA);
    DMA(sc_module_name name) : sc_module(name), mem_read_port("mem_read_port") {
        SC_METHOD(seq_logic);
        sensitive << clk.pos();

        SC_METHOD(comb_logic);
        sensitive << buf_valid << fft_ready;
    }
};

#endif // DMA_H
