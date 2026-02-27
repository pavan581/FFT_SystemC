// ============================================================================
// TB_INTERLEAVED_FFT.H - Top-Level System Testbench
// ============================================================================
// This testbench verifies the functionality of the complete Multi-Core 
// Interleaved FFT system. It instantiates the shared memory, the DMA 
// controllers, and the FFT processing cores.
//
// Verification Scenarios:
// - TEST 1: Standard Operation (Baseline functionality)
// - TEST 2: Consecutive Starts (Back-to-back execution runs)
// - TEST 3: Mid-flight Reset (System recovery from sudden reset)
// - TEST 4: Mid-flight Restart (Handling start pulse during active run)
// - TEST 5: Dynamic Reconfiguration (Changing base addresses during runtime)
// - TEST 6: Partial Input (Different length inputs for different cores)
// - TEST 7: Continuous Stream (Extended processing beyond a single block)
// ============================================================================

#ifndef TESTBENCH_H
#define TESTBENCH_H

#include <systemc.h>
#include "interleaved_fft.h"
#include "memory.h"
#include <vector>
#include <iostream>
#include <iomanip>

// ============================================================================
// Testbench Module Definition
// ============================================================================
// Parameters matching the InterleavedFFT module.
// ============================================================================
template<int N, int NUM_CORES, int HOP>
SC_MODULE(Testbench) {
    sc_clock clk;
    sc_signal<int> cycle_count;
    sc_signal<bool> rst;

    static const int MEM_DEPTH   = 2048;
    static const int ADDR_WIDTH  = 12;
    static const int DATA_WIDTH  = 64;
    
    Memory<NUM_CORES, MEM_DEPTH, DATA_WIDTH, ADDR_WIDTH>* mem;
    
    sc_signal<bool> mem_wrt_en;
    sc_signal<sc_uint<ADDR_WIDTH>> mem_waddr;
    sc_signal<sc_uint<DATA_WIDTH>> mem_din;
    
    sc_vector<sc_signal<sc_uint<ADDR_WIDTH>>> mem_raddrs;
    sc_vector<sc_signal<sc_uint<DATA_WIDTH>>> mem_douts;

    InterleavedFFT<N, NUM_CORES, HOP, DATA_WIDTH, ADDR_WIDTH>* fft_sys;
    
    sc_signal<bool> start_signal;
    
    sc_vector<sc_signal<sc_uint<ADDR_WIDTH>>> base_addrs;
    sc_vector<sc_signal<int>> num_samples;
    
    sc_vector<sc_signal<complex_t>> out_data;
    sc_vector<sc_signal<bool>> out_valids;
    sc_vector<sc_signal<int>> out_indices;
    
    sc_trace_file* tf;

    SC_CTOR(Testbench) :
        clk("clk", 1, SC_NS),
        mem_raddrs("mem_raddr", NUM_CORES),
        mem_douts("mem_dout", NUM_CORES),
        base_addrs("base_addr", NUM_CORES),
        num_samples("num_samples", NUM_CORES),
        out_data("out_data", NUM_CORES),
        out_valids("out_valid", NUM_CORES),
        out_indices("out_index", NUM_CORES)
    {
        mem = new Memory<NUM_CORES, MEM_DEPTH, DATA_WIDTH, ADDR_WIDTH>("shared_mem");
        mem->clk(clk);
        mem->rst(rst);
        
        mem->wrt_en(mem_wrt_en);
        mem->waddr(mem_waddr);
        mem->data_in(mem_din);
        
        mem->raddr(mem_raddrs);
        mem->data_out(mem_douts);

        fft_sys = new InterleavedFFT<N, NUM_CORES, HOP, DATA_WIDTH, ADDR_WIDTH>("fft_sys");
        fft_sys->clk(clk);
        fft_sys->rst(rst);
        fft_sys->start(start_signal);
        
        fft_sys->mem_addrs(mem_raddrs);
        fft_sys->mem_data(mem_douts);
        
        fft_sys->base_addrs(base_addrs);
        fft_sys->num_samples(num_samples);
        
        fft_sys->out_data(out_data);
        fft_sys->out_valids(out_valids);
        fft_sys->out_indices(out_indices);

        SC_THREAD(source_thread);
        sensitive << clk.posedge_event();
        
        SC_METHOD(sink_method);
        sensitive << clk.posedge_event();

        SC_METHOD(cycle_counter);
        sensitive << clk.posedge_event();

        std::string trace_name = "./out/vcd/InterleavedFFT-DMA_N" + std::to_string(N) + "_C" + std::to_string(NUM_CORES) + "_H" + std::to_string(HOP);
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
    }

    int cycle_cnt = 0;
    void cycle_counter() {
        cycle_count.write(cycle_cnt++);
    }

    // Source thread for driving test stimulus.
    // Handles memory initialization, test case generation, and start signals.
    void source_thread() {
        mem_wrt_en.write(false);
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
            mem_waddr.write(i);
            mem_din.write(pack_complex((double)i, 0.0));
            mem_wrt_en.write(true);
            wait();
        }
        mem_wrt_en.write(false);
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
        mem_wrt_en.write(true);
        for (int i = 0; i < 4 * N; i++) { 
            mem_waddr.write(i);
            mem_din.write(pack_complex((double)i, 0.0));
            wait();
        }
        mem_wrt_en.write(false);
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
        mem_wrt_en.write(true);
        for (int i = 0; i < 20 * N; i++) {
            mem_waddr.write(i);
            mem_din.write(pack_complex((double)i, 0));
            wait();
        }
        mem_wrt_en.write(false);
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
        
        std::cout << "\n[ALL TESTS FINISHED] @ " << sc_time_stamp() << std::endl;
        sc_stop();
    }

    // Sink method for monitoring output.
    // Prints valid output samples and flags unexpected memory reads.
    void sink_method() {
        for (int i = 0; i < NUM_CORES; i++) {
            if (out_valids[i].read()) {
                std::cout << "@" << std::setw(5) << sc_time_stamp() 
                          << " [Core " << i << "] Out[" << std::setw(2) << out_indices[i].read() << "] = " 
                          << out_data[i].read() << std::endl;
            }
        }
        
        static sc_uint<ADDR_WIDTH> last_addr[NUM_CORES] = {0, 0};
        for(int i=0; i<NUM_CORES; i++) {
            if (mem_raddrs[i].read() != last_addr[i]) {
                 std::cout << "DEBUGL DMA[" << i << "] Read Addr: " << mem_raddrs[i].read() << " @ " << sc_time_stamp() << std::endl;
                 last_addr[i] = mem_raddrs[i].read();
            }
        }
    }
    
    // Helper method to pack real and imaginary parts into a 64-bit word.
    // Upper 32 bits: Real part, Lower 32 bits: Imaginary part.
    sc_uint<64> pack_complex(double r, double i) {
        unsigned int r_int = (unsigned int)r;
        unsigned int i_int = (unsigned int)i;
        sc_uint<64> res;
        res.range(63, 32) = r_int;
        res.range(31, 0) = i_int;
        return res;
    }
};

#endif
