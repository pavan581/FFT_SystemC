// ============================================================================
// TB_INTERLEAVED_FFT.H - Top-Level System Testbench (AXI4 version)
// ============================================================================
// This testbench verifies the functionality of the complete Multi-Core 
// Interleaved FFT system. It instantiates the shared memory, the AXI
// channels, the DMA controllers, and the FFT processing cores.
// ============================================================================

#ifndef TESTBENCH_H
#define TESTBENCH_H

#include <systemc.h>
#include "interleaved_fft.h"
#include "memory.h"
#include "monitor.h"
#include "matchlib_axi.h"
#include <vector>
#include <iostream>
#include <iomanip>
#include <cmath>

// ============================================================================
// AXI4 Configuration Struct
// ============================================================================
struct AxiCfg {
    static const int addrWidth = 12;
    static const int dataWidth = 64;
    static const int idWidth = 4;
};

// ============================================================================
// Testbench Module Definition
// ============================================================================
template<int N, int NUM_CORES, int HOP, int NUM_MULT=4, int NUM_ADD=6>
SC_MODULE(Testbench) {
    sc_clock clk;
    sc_signal<int> cycle_count;
    sc_signal<bool> rst;

    static const int MEM_DEPTH   = 2048;
    static const int ADDR_WIDTH  = 12;
    static const int DATA_WIDTH  = 64;
    
    // Multi-port AXI Memory
    Memory<NUM_CORES + 1, NUM_CORES, MEM_DEPTH, AxiCfg>* mem;
    
    // AXI read/write channels
    sc_vector<axi::axi4_read_channel<AxiCfg>> mem_read_chans;
    sc_vector<axi::axi4_write_channel<AxiCfg>> mem_write_chans;

    InterleavedFFT<N, NUM_CORES, HOP, AxiCfg, NUM_MULT, NUM_ADD>* fft_sys;
    
    sc_signal<bool> start_signal;
    
    sc_vector<sc_signal<sc_uint<ADDR_WIDTH>>> base_addrs;
    sc_vector<sc_signal<int>> num_samples;
    
    sc_vector<sc_signal<complex_t>> out_data;
    sc_vector<sc_signal<bool>> out_valids;
    sc_vector<sc_signal<int>> out_indices;
    
    // Memory read address signals for Monitor
    sc_vector<sc_signal<sc_uint<ADDR_WIDTH>>> tb_mem_raddrs;

    Monitor<NUM_CORES, ADDR_WIDTH>* mon;

    sc_trace_file* tf;

    SC_CTOR(Testbench) :
        clk("clk", 1, SC_NS),
        mem_read_chans("mem_read_chans", NUM_CORES),
        mem_write_chans("mem_write_chans", NUM_CORES + 1),
        base_addrs("base_addr", NUM_CORES),
        num_samples("num_samples", NUM_CORES),
        out_data("out_data", NUM_CORES),
        out_valids("out_valid", NUM_CORES),
        out_indices("out_index", NUM_CORES),
        tb_mem_raddrs("tb_mem_raddrs", NUM_CORES)
    {
        // 1. Create and Connect Memory
        mem = new Memory<NUM_CORES + 1, NUM_CORES, MEM_DEPTH, AxiCfg>("shared_mem");
        mem->clk(clk);
        mem->rst(rst);
        
        mem->read_ports(mem_read_chans);
        mem->write_ports(mem_write_chans);

        // 2. Create and Connect FFT System
        fft_sys = new InterleavedFFT<N, NUM_CORES, HOP, AxiCfg, NUM_MULT, NUM_ADD>("fft_sys");
        fft_sys->clk(clk);
        fft_sys->rst(rst);
        fft_sys->start(start_signal);
        
        fft_sys->mem_read_ports(mem_read_chans);
        for(int i=0; i<NUM_CORES; i++) {
            fft_sys->mem_write_ports[i](mem_write_chans[i]);
        }
        
        fft_sys->base_addrs(base_addrs);
        fft_sys->num_samples(num_samples);
        
        fft_sys->out_data(out_data);
        fft_sys->out_valids(out_valids);
        fft_sys->out_indices(out_indices);

        SC_THREAD(source_thread);
        sensitive << clk.posedge_event();
        
        // 3. Create and Connect Monitor
        mon = new Monitor<NUM_CORES, ADDR_WIDTH>("monitor");
        mon->clk(clk);
        for(int i=0; i<NUM_CORES; i++) {
            mon->out_data[i](out_data[i]);
            mon->out_valids[i](out_valids[i]);
            mon->out_indices[i](out_indices[i]);
            mon->mem_raddrs[i](tb_mem_raddrs[i]);
        }

        // Combinational update of Monitor's addresses
        SC_METHOD(update_monitor_addrs);
        for (int i = 0; i < NUM_CORES; i++) {
            sensitive << mem_read_chans[i].ar.msg << mem_read_chans[i].ar.vld << mem_read_chans[i].ar.rdy;
        }

        SC_METHOD(cycle_counter);
        sensitive << clk.posedge_event();

        // 4. Trace Setup
        std::string trace_name = "./out/vcd/InterleavedFFT-DMA_N" + std::to_string(N) + "_C" + std::to_string(NUM_CORES) + "_H" + std::to_string(HOP) + "_M" + std::to_string(NUM_MULT) + "_A" + std::to_string(NUM_ADD) + "_axi";
        tf = sc_create_vcd_trace_file(trace_name.c_str());
        tf->set_time_unit(1, SC_PS);
        
        sc_trace(tf, clk, "clk");
        sc_trace(tf, cycle_count, "cycle_count");
        sc_trace(tf, rst, "rst");
        sc_trace(tf, start_signal, "start");
        
        for(int i=0; i<NUM_CORES; i++) {
            std::string d_name = "out_data_" + std::to_string(i);
            std::string v_name = "out_valid_" + std::to_string(i);
            std::string idx_name = "out_idx_" + std::to_string(i);
            sc_trace(tf, out_data[i], d_name.c_str());
            sc_trace(tf, out_valids[i], v_name.c_str());
            sc_trace(tf, out_indices[i], idx_name.c_str());
        }
    }

    ~Testbench() {
        sc_close_vcd_trace_file(tf);
        delete fft_sys;
        delete mem;
        delete mon;
    }

    int cycle_cnt = 0;
    void cycle_counter() {
        cycle_count.write(cycle_cnt++);
    }

    // Combinational method to map AXI read handshakes to Monitor inputs
    void update_monitor_addrs() {
        for (int i = 0; i < NUM_CORES; i++) {
            if (mem_read_chans[i].ar.vld.read() && mem_read_chans[i].ar.rdy.read()) {
                tb_mem_raddrs[i].write(mem_read_chans[i].ar.msg.read().addr);
            }
        }
    }

    // Helper to perform an AXI Write Handshake
    void axi_write(unsigned int addr, sc_uint<64> data) {
        // Drive Address
        axi::AddrPayload<AxiCfg> aw_pay;
        aw_pay.addr = addr;
        aw_pay.id = 0;
        aw_pay.len = 0;
        aw_pay.size = 0;
        aw_pay.burst = 0;
        mem_write_chans[NUM_CORES].aw.msg.write(aw_pay);
        mem_write_chans[NUM_CORES].aw.vld.write(true);

        // Drive Data
        axi::WritePayload<AxiCfg> w_pay;
        w_pay.data = data;
        w_pay.strb = 0xFF;
        w_pay.last = true;
        mem_write_chans[NUM_CORES].w.msg.write(w_pay);
        mem_write_chans[NUM_CORES].w.vld.write(true);

        mem_write_chans[NUM_CORES].b.rdy.write(true);

        wait(); // Advance 1 cycle to let handshakes evaluate
        while (!(mem_write_chans[NUM_CORES].aw.rdy.read() && mem_write_chans[NUM_CORES].w.rdy.read())) {
            wait();
        }
        
        // Deassert valids
        mem_write_chans[NUM_CORES].aw.vld.write(false);
        mem_write_chans[NUM_CORES].w.vld.write(false);
    }

    // Source thread for driving test stimulus
    void source_thread() {
        mem_write_chans[NUM_CORES].aw.vld.write(false);
        mem_write_chans[NUM_CORES].w.vld.write(false);
        mem_write_chans[NUM_CORES].b.rdy.write(true);
        start_signal.write(false);
        
        for(int i=0; i<NUM_CORES; i++) {
            base_addrs[i].write(1023); 
            num_samples[i].write(0);
        }

        // ====================================================================
        // TEST CASE 1: Standard Operation (Baseline)
        // ====================================================================
        std::cout << "\n[TEST 1] Standard Operation (N=" << N << ", Cores=" << NUM_CORES << ")..." << std::endl;
        
        wait();
        rst.write(true);
        wait(5);
        rst.write(false);
        wait();

        std::cout << "@" << sc_time_stamp() << " Initializing Memory..." << std::endl;
        
        for (int i = 0; i < 4 * N; i++) {
            axi_write(i, pack_complex((double)i, 0));
        }
        wait(5);
        
        for(int i=0; i<NUM_CORES; i++) {
            base_addrs[i].write(i * N); 
            num_samples[i].write(N);
        }
        
        std::cout << "@" << sc_time_stamp() << " Triggering Start..." << std::endl;
        start_signal.write(true);
        wait();
        start_signal.write(false);
        
        wait(2000);

        std::cout << "\nMemory Verification..." << std::endl;
        for (int c = 0; c < NUM_CORES; ++c) {
            int base = c * N;
            std::cout << "Core " << c << " Memory (Addresses " << base + N << " to " << base + N + N - 1 << "):" << std::endl;
            for (int k = 0; k < N; ++k) {
                sc_uint<DATA_WIDTH> raw = mem->mem[base + N + k];
                double r, i;
                if (DATA_WIDTH == 64) {
                    int r_int = raw.range(63, 32).to_int();
                    int i_int = raw.range(31, 0).to_int();
                    r = (double)r_int;
                    i = (double)i_int;
                } else {
                    int r_int = raw.range(31, 0).to_int();
                    r = (double)r_int;
                    i = 0.0;
                }
                std::cout << "Mem[" << base + N + k << "] = (" << r << " + " << i << "j)" << std::endl;
            }
        }
        std::cout << "[TEST 1] Finished." << std::endl;

        // ====================================================================
        // TEST CASE 2: Consecutive Starts (Restart after completion)
        // ====================================================================
        std::cout << "\n[TEST 2] Consecutive Starts..." << std::endl;
        
        std::cout << "@" << sc_time_stamp() << " Triggering Start (Run 2)..." << std::endl;
        start_signal.write(true);
        wait();
        start_signal.write(false);
        
        wait(2000);

        std::cout << "\nMemory Verification..." << std::endl;
        for (int c = 0; c < NUM_CORES; ++c) {
            int base = c * N;
            std::cout << "Core " << c << " Memory (Addresses " << base + N << " to " << base + N + N - 1 << "):" << std::endl;
            for (int k = 0; k < N; ++k) {
                sc_uint<DATA_WIDTH> raw = mem->mem[base + N + k];
                double r, i;
                if (DATA_WIDTH == 64) {
                    int r_int = raw.range(63, 32).to_int();
                    int i_int = raw.range(31, 0).to_int();
                    r = (double)r_int;
                    i = (double)i_int;
                } else {
                    int r_int = raw.range(31, 0).to_int();
                    r = (double)r_int;
                    i = 0.0;
                }
                std::cout << "Mem[" << base + N + k << "] = (" << r << " + " << i << "j)" << std::endl;
            }
        }
        std::cout << "[TEST 2] Finished." << std::endl;

        // ====================================================================
        // TEST CASE 3: Mid-flight Reset
        // ====================================================================
        std::cout << "\n[TEST 3] Mid-flight Reset..." << std::endl;
        
        for(int i=0; i<NUM_CORES; i++) {
            base_addrs[i].write(i * N); 
            num_samples[i].write(N);
        }
        start_signal.write(true);
        wait();
        start_signal.write(false);
        
        wait(9);
        
        std::cout << "@" << sc_time_stamp() << " Asserting Reset!" << std::endl;
        rst.write(true);
        wait();
        rst.write(false);
        wait();
        
        std::cout << "@" << sc_time_stamp() << " Re-initializing Memory after Reset..." << std::endl;
        for (int i = 0; i < 4 * N; i++) { 
            axi_write(i, pack_complex((double)i, (double)i*0.5));
        }
        wait();
        
        wait(2000);
        std::cout << "[TEST 3] Finished." << std::endl;

        // ====================================================================
        // TEST CASE 4: Mid-flight Restart (Stress Test)
        // ====================================================================
        std::cout << "\n[TEST 4] Mid-flight Restart..." << std::endl;
        
        start_signal.write(true);
        wait();
        start_signal.write(false);
        
        wait(9); 
        
        std::cout << "@" << sc_time_stamp() << " Pulsing Start AGAIN (Intrusion)..." << std::endl;
        start_signal.write(true);
        wait();
        start_signal.write(false);
        
        wait(2000);
        std::cout << "[TEST 4] Finished." << std::endl;

        // ====================================================================
        // TEST CASE 5: Dynamic Reconfiguration
        // ====================================================================
        std::cout << "\n[TEST 5] Dynamic Reconfiguration..." << std::endl;
        
        for(int i=0; i<NUM_CORES; i++) {
            base_addrs[i].write((2 + i) * N); 
        }
        
        std::cout << "@" << sc_time_stamp() << " Triggering Start (New Config)..." << std::endl;
        start_signal.write(true);
        wait();
        start_signal.write(false);
        
        wait(2000);
        std::cout << "[TEST 5] Finished." << std::endl;

        // ====================================================================
        // TEST CASE 6: Partial Input
        // ====================================================================
        std::cout << "\n[TEST 6] Partial Input..." << std::endl;
        
        for(int i=0; i<NUM_CORES; i++) {
            base_addrs[i].write(i * N); 
            num_samples[i].write( (i==0) ? N : (N/2) );
        }
        
        std::cout << "@" << sc_time_stamp() << " Triggering Start (Partial Inputs)..." << std::endl;
        start_signal.write(true);
        wait();
        start_signal.write(false);
        
        wait(2000);
        std::cout << "[TEST 6] Finished." << std::endl;

        // ====================================================================
        // TEST CASE 7: Continuous Stream (Overflow Buffer)
        // ====================================================================
        std::cout << "\n[TEST 7] Continuous Stream (10 inputs for N=4)..." << std::endl;
        
        rst.write(true);
        wait(5);
        rst.write(false);
        wait();

        std::cout << "@" << sc_time_stamp() << " Initializing Memory for Stream..." << std::endl;
        for (int i = 0; i < 20 * N; i++) {
            axi_write(i, pack_complex((double)i, (double)i*0.5));
        }
        wait();
        
        std::cout << "@" << sc_time_stamp() << " Triggering Start (Continuous Stream)..." << std::endl;
        
        for(int i=0; i<NUM_CORES; i++) {
            base_addrs[i].write(i * N); 
            num_samples[i].write(10); 
        }

        start_signal.write(true);
        wait();
        start_signal.write(false);
        
        wait(200); 
        
        std::cout << "[TEST 7] Finished." << std::endl;
        
        // ====================================================================
        // TEST CASE 8: Zero-length Input
        // ====================================================================
        std::cout << "\n[TEST 8] Zero-length Input (Edge Case)..." << std::endl;
        for(int i=0; i<NUM_CORES; i++) {
            base_addrs[i].write(i * N); 
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
        std::cout << "\n[TEST 9] Memory Verification (Checking Final Output Data)..." << std::endl;
        for (int c = 0; c < NUM_CORES; ++c) {
            int base = c * N;
            std::cout << "Core " << c << " Expected Output in Memory (Addresses " << base + N << " to " << base + N + N - 1 << "):" << std::endl;
            for (int k = 0; k < N; ++k) {
                sc_uint<DATA_WIDTH> raw = mem->mem[base + N + k];
                double r, i;
                if (DATA_WIDTH == 64) {
                    int r_int = raw.range(63, 32).to_int();
                    int i_int = raw.range(31, 0).to_int();
                    r = (double)r_int;
                    i = (double)i_int;
                } else {
                    int r_int = raw.range(31, 0).to_int();
                    r = (double)r_int;
                    i = 0.0;
                }
                std::cout << "Mem[" << base + N + k << "] = (" << r << " + " << i << "j)" << std::endl;
            }
        }
        std::cout << "[TEST 9] Finished." << std::endl;
        
        std::cout << "\n[ALL TESTS FINISHED] @ " << sc_time_stamp() << std::endl;
        sc_stop();
    }

    // Helper method to pack real and imaginary parts into a 64-bit word
    sc_uint<64> pack_complex(double r, double i) {
        int r_int = (int)std::round(r);
        int i_int = (int)std::round(i);
        sc_uint<64> res;
        res.range(63, 32) = r_int;
        res.range(31, 0) = i_int;
        return res;
    }
};

#endif // TESTBENCH_H
