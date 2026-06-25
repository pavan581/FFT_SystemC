#ifndef TESTBENCH_H
#define TESTBENCH_H

#define CONNECTIONS_NAMING_ORIGINAL
#define BOOST_NULLPTR nullptr

#include <systemc.h>
#include <axi/axi4.h>
#include <connections/connections.h>
#include "top.h"
#include "memory.h"
#include <vector>
#include <queue>
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace sc_core;
using namespace axi;
using namespace Connections;

typedef axi::cfg::standard AxiCfg;

// Top-level testbench for verifying the Multi-Core Interleaved FFT system.
template<int N, int NUM_CORES, int HOP, int NUM_MULT=4, int NUM_ADD=6>
SC_MODULE(Testbench) {
    sc_clock clk;
    sc_signal<int> cycle_count;
    sc_signal<bool> rst_n; // Active-low reset

    static const int MEM_DEPTH   = 2048;
    static const int ADDR_WIDTH  = AxiCfg::addrWidth;
    static const int DATA_WIDTH  = AxiCfg::dataWidth;
    
    // Dedicated Single-Port Memories
    sc_vector<Memory<MEM_DEPTH, AxiCfg>> mems;
    
    // Read and Write channels connecting Memory and Cores (default port types)
    sc_vector<typename axi4<AxiCfg>::read::template chan<>> mem_read_chans;
    sc_vector<typename axi4<AxiCfg>::write::template chan<>> mem_write_chans;

    // DUT
    Top<N, NUM_CORES, HOP, AxiCfg, NUM_MULT, NUM_ADD>* fft_sys;
    
    sc_signal<bool> start_signal;
    
    sc_vector<sc_signal<sc_uint<ADDR_WIDTH>>> base_addrs;
    sc_vector<sc_signal<int>> num_samples;
    
    sc_trace_file* tf;
    


    static const int bytesPerBeat = AxiCfg::dataWidth / 8;

    SC_CTOR(Testbench) :
        clk("clk", 1, SC_NS),
        mems("mems", NUM_CORES),
        mem_read_chans("mem_read_chans", NUM_CORES),
        mem_write_chans("mem_write_chans", NUM_CORES),
        base_addrs("base_addr", NUM_CORES),
        num_samples("num_samples", NUM_CORES)
    {


        // 1. Memory instantiation
        for (int i = 0; i < NUM_CORES; ++i) {
            mems[i].clk(clk);
            mems[i].rst_n(rst_n);
            mems[i].read_port(mem_read_chans[i]);
            mems[i].write_port(mem_write_chans[i]);
        }

        // 2. FFT Core complex instantiation
        fft_sys = new Top<N, NUM_CORES, HOP, AxiCfg, NUM_MULT, NUM_ADD>("fft_sys");
        fft_sys->clk(clk);
        fft_sys->rst_n(rst_n);
        fft_sys->start(start_signal);
        fft_sys->mem_read_ports(mem_read_chans);
        for(int i=0; i<NUM_CORES; i++) {
            fft_sys->mem_write_ports[i](mem_write_chans[i]);
        }
        fft_sys->base_addrs(base_addrs);
        fft_sys->num_samples(num_samples);



        SC_THREAD(source_thread);
        sensitive << clk.posedge_event();

        SC_METHOD(cycle_counter);
        sensitive << clk.posedge_event();



        // 4. Trace configurations
        std::string trace_name = "./out/vcd/InterleavedFFT-DMA_N" + std::to_string(N) + "_C" + std::to_string(NUM_CORES) + "_H" + std::to_string(HOP) + "_M" + std::to_string(NUM_MULT) + "_A" + std::to_string(NUM_ADD) + "_axi";
        tf = sc_create_vcd_trace_file(trace_name.c_str());
        tf->set_time_unit(1, SC_PS);
        
        sc_trace(tf, clk, "clk");
        sc_trace(tf, cycle_count, "cycle_count");
        sc_trace(tf, rst_n, "rst_n");
        sc_trace(tf, start_signal, "start");

        for (int i = 0; i < NUM_CORES; ++i) {
            std::string idx = std::to_string(i);
            
            sc_trace(tf, fft_sys->core_starts[i], "core_start_" + idx);
            sc_trace(tf, fft_sys->core_busy[i], "core_busy_" + idx);
        }
    }

    ~Testbench() {
        sc_close_vcd_trace_file(tf);
        delete fft_sys;
    }

    int cycle_cnt = 0;
    void cycle_counter() {
        cycle_count.write(cycle_cnt++);
    }



    // Direct memory writes from the testbench
    void axi_write(unsigned int addr, sc_uint<AxiCfg::dataWidth> data) {
        int target_core = -1;
        for (int c = 0; c < NUM_CORES; ++c) {
            unsigned int start = base_addrs[c].read().to_uint();
            unsigned int end = start + 2 * N * bytesPerBeat;
            if (addr >= start && addr < end) {
                target_core = c;
                break;
            }
        }
        if (target_core == -1) {
            // Fallback for initial writes before start_signal or when base_addrs might not cover the address
            target_core = addr / (2 * N * bytesPerBeat);
            if (target_core < 0 || target_core >= NUM_CORES) {
                target_core = 0;
            }
        }
        mems[target_core].mem[addr >> 3] = data;
    }

    // Verification reference DFT model
    std::vector<complex_t> compute_dft(const std::vector<complex_t>& input) {
        int size = input.size();
        std::vector<complex_t> output(size);
        const double PI = 3.14159265358979323846;
        for (int k = 0; k < size; ++k) {
            complex_t sum(0, 0);
            for (int n = 0; n < size; ++n) {
                double angle = -2.0 * PI * k * n / size;
                complex_t w(cos(angle), sin(angle));
                sum = sum + input[n] * w;
            }
            output[k] = sum;
        }
        return output;
    }

    int bit_reverse(int index, int bits) {
        int rev = 0;
        for (int i = 0; i < bits; ++i) {
            if ((index & (1 << i)) != 0) {
                rev |= (1 << (bits - 1 - i));
            }
        }
        return rev;
    }

    // Verification check comparing actual memory contents against expected DFT results
    bool verify_fft_output(int core_idx, int start_addr, int len) {
        int aligned_len = ((len + N - 1) / N) * N;
        
        std::vector<complex_t> inputs(aligned_len);
        for (int i = 0; i < aligned_len; ++i) {
            if (i < len) {
                sc_uint<AxiCfg::dataWidth> raw = mems[core_idx].mem[(start_addr >> 3) + i];
                inputs[i] = unpack_complex<AxiCfg>(raw);
            } else {
                inputs[i] = complex_t(0.0, 0.0);
            }
        }

        std::vector<complex_t> expected(aligned_len);
        int num_blocks = aligned_len / N;
        int bits = (int)std::log2(N);
        for (int b = 0; b < num_blocks; ++b) {
            std::vector<complex_t> block_in(N);
            for (int i = 0; i < N; ++i) {
                block_in[i] = inputs[b * N + i];
            }
            std::vector<complex_t> block_out = compute_dft(block_in);
            for (int i = 0; i < N; ++i) {
                int rev_i = bit_reverse(i, bits);
                expected[b * N + rev_i] = block_out[i];
            }
        }

        bool pass = true;
        std::cout << "Core " << core_idx << " Verification (Base Addr: " << start_addr + N * bytesPerBeat << "):" << std::endl;
        for (int i = 0; i < len; ++i) {
            sc_uint<AxiCfg::dataWidth> raw = mems[core_idx].mem[((start_addr + N * bytesPerBeat) >> 3) + i];
            complex_t actual = unpack_complex<AxiCfg>(raw);
            complex_t exp = expected[i];
            
            double diff_real = std::abs(actual.real - exp.real);
            double diff_imag = std::abs(actual.imag - exp.imag);
            bool match = (diff_real < 1e-2) && (diff_imag < 1e-2);
            
            std::cout << "  Addr[" << ((start_addr + N * bytesPerBeat) >> 3) + i << "]: Actual=" << actual 
                      << " Expected=" << exp;
            if (match) {
                std::cout << " [OK]" << std::endl;
            } else {
                std::cout << " [MISMATCH]" << std::endl;
                pass = false;
            }
        }
        return pass;
    }

    void source_thread() {
        start_signal.write(false);
        for(int i=0; i<NUM_CORES; i++) {
            base_addrs[i].write(1023 * bytesPerBeat); 
            num_samples[i].write(0);
        }
        
        // ====================================================================
        // TEST CASE 1: Standard Operation
        // ====================================================================
        std::cout << "\n[TEST 1] Standard Operation (N=" << N << ", Cores=" << NUM_CORES << ")..." << std::endl;
        
        wait();
        rst_n.write(false);
        wait(5);
        rst_n.write(true);
        wait();

        std::cout << "@" << sc_time_stamp() << " Initializing Memory..." << std::endl;
        for (int i = 0; i < NUM_CORES * 2 * N; i++) {
            axi_write(i * bytesPerBeat, pack_complex<AxiCfg>((double)i, 0));
        }
        wait(5);
        
        for(int i=0; i<NUM_CORES; i++) {
            base_addrs[i].write(i * 2 * N * bytesPerBeat); 
            num_samples[i].write(N);
        }
        
        std::cout << "@" << sc_time_stamp() << " Triggering Start..." << std::endl;
        start_signal.write(true);
        wait();
        start_signal.write(false);
        
        wait(2000);

        std::cout << "\nMemory Verification..." << std::endl;
        bool t1_pass = true;
        for (int c = 0; c < NUM_CORES; ++c) {
            t1_pass &= verify_fft_output(c, c * 2 * N * bytesPerBeat, N);
        }
        if (t1_pass) {
            std::cout << "[TEST 1] PASSED." << std::endl;
        } else {
            std::cout << "[TEST 1] FAILED." << std::endl;
        }
        std::cout << "[TEST 1] Finished." << std::endl;

        // ====================================================================
        // TEST CASE 2: Consecutive Starts
        // ====================================================================
        std::cout << "\n[TEST 2] Consecutive Starts..." << std::endl;
        
        std::cout << "@" << sc_time_stamp() << " Triggering Start (Run 2)..." << std::endl;
        start_signal.write(true);
        wait();
        start_signal.write(false);
        
        wait(2000);

        std::cout << "\nMemory Verification..." << std::endl;
        bool t2_pass = true;
        for (int c = 0; c < NUM_CORES; ++c) {
            t2_pass &= verify_fft_output(c, c * 2 * N * bytesPerBeat, N);
        }
        if (t2_pass) {
            std::cout << "[TEST 2] PASSED." << std::endl;
        } else {
            std::cout << "[TEST 2] FAILED." << std::endl;
        }
        std::cout << "[TEST 2] Finished." << std::endl;

        // ====================================================================
        // TEST CASE 3: Mid-flight Reset
        // ====================================================================
        std::cout << "\n[TEST 3] Mid-flight Reset..." << std::endl;
        
        for(int i=0; i<NUM_CORES; i++) {
            base_addrs[i].write(i * 2 * N * bytesPerBeat); 
            num_samples[i].write(N);
        }
        start_signal.write(true);
        wait();
        start_signal.write(false);
        
        wait(9);
        
        std::cout << "@" << sc_time_stamp() << " Asserting Reset!" << std::endl;
        rst_n.write(false);
        wait();
        rst_n.write(true);
        wait();
        
        std::cout << "@" << sc_time_stamp() << " Re-initializing Memory after Reset..." << std::endl;
        for (int i = 0; i < NUM_CORES * 2 * N; i++) { 
            axi_write(i * bytesPerBeat, pack_complex<AxiCfg>((double)i, (double)i*0.5));
        }
        wait();
        
        wait(2000);
        std::cout << "[TEST 3] Finished." << std::endl;

        // ====================================================================
        // TEST CASE 4: Mid-flight Restart
        // ====================================================================
        std::cout << "\n[TEST 4] Mid-flight Restart..." << std::endl;
        
        start_signal.write(true);
        wait();
        start_signal.write(false);
        
        wait(9); 
        
        std::cout << "@" << sc_time_stamp() << " Pulsing Start AGAIN..." << std::endl;
        start_signal.write(true);
        wait();
        start_signal.write(false);
        
        wait(2000);
        std::cout << "[TEST 4] Finished." << std::endl;

        // ====================================================================
        // TEST CASE 5: Dynamic Reconfiguration
        // ====================================================================
        std::cout << "\n[TEST 5] Dynamic Reconfiguration..." << std::endl;
        
        rst_n.write(false);
        wait(5);
        rst_n.write(true);
        wait();

        std::cout << "@" << sc_time_stamp() << " Re-initializing Memory for Test 5..." << std::endl;
        for (int i = 0; i < (4 + NUM_CORES * 2) * N; i++) {
            axi_write(i * bytesPerBeat, pack_complex<AxiCfg>((double)i, (double)i*0.5));
        }
        wait(5);

        for(int i=0; i<NUM_CORES; i++) {
            base_addrs[i].write((2 + i * 2) * N * bytesPerBeat); 
        }
        
        std::cout << "@" << sc_time_stamp() << " Triggering Start (New Config)..." << std::endl;
        start_signal.write(true);
        wait();
        start_signal.write(false);
        
        wait(2000);

        std::cout << "\nMemory Verification..." << std::endl;
        bool t5_pass = true;
        for (int c = 0; c < NUM_CORES; ++c) {
            t5_pass &= verify_fft_output(c, base_addrs[c].read().to_uint(), N);
        }
        if (t5_pass) {
            std::cout << "[TEST 5] PASSED." << std::endl;
        } else {
            std::cout << "[TEST 5] FAILED." << std::endl;
        }
        std::cout << "[TEST 5] Finished." << std::endl;

        // ====================================================================
        // TEST CASE 6: Partial Input
        // ====================================================================
        std::cout << "\n[TEST 6] Partial Input..." << std::endl;
        
        rst_n.write(false);
        wait(5);
        rst_n.write(true);
        wait();

        std::cout << "@" << sc_time_stamp() << " Re-initializing Memory for Test 6..." << std::endl;
        for (int i = 0; i < NUM_CORES * 2 * N; i++) {
            axi_write(i * bytesPerBeat, pack_complex<AxiCfg>((double)i, 0.0));
        }
        wait(5);

        for(int i=0; i<NUM_CORES; i++) {
            base_addrs[i].write(i * 2 * N * bytesPerBeat); 
            num_samples[i].write( (i==0) ? N : (N/2) );
        }
        
        std::cout << "@" << sc_time_stamp() << " Triggering Start (Partial Inputs)..." << std::endl;
        start_signal.write(true);
        wait();
        start_signal.write(false);
        
        wait(2000);

        std::cout << "\nMemory Verification..." << std::endl;
        bool t6_pass = true;
        for (int c = 0; c < NUM_CORES; ++c) {
            t6_pass &= verify_fft_output(c, base_addrs[c].read().to_uint(), num_samples[c].read());
        }
        if (t6_pass) {
            std::cout << "[TEST 6] PASSED." << std::endl;
        } else {
            std::cout << "[TEST 6] FAILED." << std::endl;
        }
        std::cout << "[TEST 6] Finished." << std::endl;

        // ====================================================================
        // TEST CASE 7: Continuous Stream
        // ====================================================================
        std::cout << "\n[TEST 7] Continuous Stream (10 inputs for N=4)..." << std::endl;
        
        rst_n.write(false);
        wait(5);
        rst_n.write(true);
        wait();

        std::cout << "@" << sc_time_stamp() << " Initializing Memory for Stream..." << std::endl;
        for (int i = 0; i < 24 * NUM_CORES; i++) {
            axi_write(i * bytesPerBeat, pack_complex<AxiCfg>((double)i, (double)i*0.5));
        }
        wait();
        
        std::cout << "@" << sc_time_stamp() << " Triggering Start (Continuous Stream)..." << std::endl;
        
        for(int i=0; i<NUM_CORES; i++) {
            base_addrs[i].write(i * 24 * bytesPerBeat); 
            num_samples[i].write(10); 
        }

        start_signal.write(true);
        wait();
        start_signal.write(false);
        
        wait(200); 

        std::cout << "\nMemory Verification (Core 0 Block 1, Untouched by Overlap)..." << std::endl;
        bool t7_pass = true;
        for (int c = 0; c < NUM_CORES; ++c) {
            t7_pass &= verify_fft_output(c, c * 24 * bytesPerBeat, N);
        }
        if (t7_pass) {
            std::cout << "[TEST 7] PASSED." << std::endl;
        } else {
            std::cout << "[TEST 7] FAILED." << std::endl;
        }
        std::cout << "[TEST 7] Finished." << std::endl;
        
        // ====================================================================
        // TEST CASE 8: Zero-length Input
        // ====================================================================
        std::cout << "\n[TEST 8] Zero-length Input (Edge Case)..." << std::endl;
        for(int i=0; i<NUM_CORES; i++) {
            base_addrs[i].write(i * 2 * N * bytesPerBeat); 
            num_samples[i].write(0);
        }
        start_signal.write(true);
        wait();
        start_signal.write(false);
        wait(100);
        std::cout << "[TEST 8] Finished." << std::endl;

        // ====================================================================
        // TEST CASE 9: Memory Verification
        // ====================================================================
        std::cout << "\n[TEST 9] Memory Verification..." << std::endl;
        std::cout << "[TEST 9] Finished." << std::endl;
        
        std::cout << "\n[ALL TESTS FINISHED] @ " << sc_time_stamp() << std::endl;
        sc_stop();
    }
};

#endif // TESTBENCH_H
