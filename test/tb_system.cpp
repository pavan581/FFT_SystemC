#define CONNECTIONS_NAMING_ORIGINAL
#define BOOST_NULLPTR nullptr

#include <systemc.h>
#include <ac_reset_signal_is.h>
#include <axi/axi4.h>
#include <mc_scverify.h>
#include <axi/testbench/SlaveFromFile.h>
#include <axi/testbench/Slave.h>
#include <testbench/nvhls_rand.h>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int_distribution.hpp>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <top.h>
#include <filesystem>
#include <fft_types.h>

using namespace sc_core;
using namespace std;

// ============================================================================
// Simulation Parameters & Configurations
// ============================================================================
#ifndef FFT_SAMPLES
#define FFT_SAMPLES 256
#endif

#ifndef FFT_N
#define FFT_N 8
#endif

#ifndef FFT_NUM_CORES
#define FFT_NUM_CORES 2
#endif

#ifndef FFT_HOP
#define FFT_HOP 1
#endif

#ifndef FFT_NUM_MULT
#define FFT_NUM_MULT 4
#endif

#ifndef FFT_NUM_ADD
#define FFT_NUM_ADD 6
#endif

const int samples = FFT_SAMPLES;
const int N = FFT_N;
const int NUM_CORES = FFT_NUM_CORES;
const int HOP = FFT_HOP;
const int NUM_MULT = FFT_NUM_MULT;
const int NUM_ADD = FFT_NUM_ADD;

const unsigned int seed = 0;

typedef axi::cfg::standard AxiCfg;

// ============================================================================
// Helper Functions for DFT Verification
// ============================================================================
inline int bit_reverse(int index, int bits) {
    int rev = 0;
    for (int i = 0; i < bits; ++i) {
        if ((index & (1 << i)) != 0) {
            rev |= (1 << (bits - 1 - i));
        }
    }
    return rev;
}

inline std::vector<complex_t> compute_dft(const std::vector<complex_t>& input) {
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

// ============================================================================
// Testbench Module Definition
// ============================================================================
SC_MODULE(testbench) {
    // Clock and Control Signals
    sc_clock clk;
    sc_signal<bool> rst_n;
    sc_signal<bool> start_signal;
    
    // Core Configuration Ports
    sc_vector<sc_signal<sc_uint<AxiCfg::addrWidth>>> base_addrs;
    sc_vector<sc_signal<int>> num_samples;
    
    // AXI4 Transaction Channels
    sc_vector<typename axi4<AxiCfg>::read::template chan<>> mem_read_chans;
    sc_vector<typename axi4<AxiCfg>::write::template chan<>> mem_write_chans;

    // Dedicated Slave Memories for each core
#ifdef USE_SLAVE_FROM_FILE
    sc_vector<SlaveFromFile<AxiCfg>> slaves;
#else
    sc_vector<Slave<AxiCfg>> slaves;
#endif

    // Design Under Test (DUT)
    Top<N, NUM_CORES, HOP, AxiCfg, NUM_MULT, NUM_ADD> fft_sys;

    // CSV Logging & Verification Tracking
    std::ofstream r_csv_files[NUM_CORES];
    std::ofstream w_csv_files[NUM_CORES];
    vector<vector<complex_t>> inputs{NUM_CORES, vector<complex_t>(samples)};
    int read_count[NUM_CORES];

    SC_CTOR(testbench)
        : clk("clk", 1.0, SC_NS, 0.5, 0, SC_NS, true),
          rst_n("rst_n"),
          start_signal("start_signal"),
          base_addrs("base_addrs", NUM_CORES),
          num_samples("num_samples", NUM_CORES),
          mem_read_chans("mem_read_chans", NUM_CORES),
          mem_write_chans("mem_write_chans", NUM_CORES),
#ifdef USE_SLAVE_FROM_FILE
          slaves("slaves"),
#else
          slaves("slaves", NUM_CORES),
#endif
          fft_sys("fft_sys") 
    {
        Connections::set_sim_clk(&clk);

        fft_sys.clk(clk);
        fft_sys.rst_n(rst_n);
        fft_sys.start(start_signal);

        // Determine output directory
        const char* out_dir_env = std::getenv("SIM_OUT_DIR");
        std::string out_dir = (out_dir_env != nullptr) ? out_dir_env : "out";
        
        // Create output directory and its data subfolder
        std::filesystem::create_directories(out_dir + "/data");

#ifdef USE_SLAVE_FROM_FILE
        // Determine filenames for each core
        const char* stim_file_env = std::getenv("STIMULUS_FILE");
        std::vector<std::string> filenames(NUM_CORES);
        
        if (stim_file_env == nullptr) {
            std::cerr << "Error: USE_SLAVE_FROM_FILE is defined but STIMULUS_FILE is not set!" << std::endl;
            sc_report_handler::report(SC_ERROR, "Config error", "STIMULUS_FILE not set", __FILE__, __LINE__);
        } else {
            std::string stim_file_template(stim_file_env);
            for (int c = 0; c < NUM_CORES; ++c) {
                std::string filename = stim_file_template;
                size_t pos = filename.find("%d");
                if (pos != std::string::npos) {
                    std::string filename_1 = filename;
                    filename_1.replace(pos, 2, std::to_string(c + 1));
                    std::ifstream f_test(filename_1);
                    if (f_test.good()) {
                        filename = filename_1;
                    } else {
                        filename.replace(pos, 2, std::to_string(c));
                    }
                }
                filenames[c] = filename;
            }
        }

        // Initialize slaves vector using custom filenames
        slaves.init(NUM_CORES, [&](const char* name, int i) {
            return new SlaveFromFile<AxiCfg>(name, filenames[i]);
        });
#endif

        // Connect Cores, Channels, and Slaves
        for (int i = 0; i < NUM_CORES; ++i) {
            fft_sys.mem_read_ports[i](mem_read_chans[i]);
            fft_sys.mem_write_ports[i](mem_write_chans[i]);
            fft_sys.base_addrs[i](base_addrs[i]);
            fft_sys.num_samples[i](num_samples[i]);
            
            slaves[i].clk(clk);
            slaves[i].reset_bar(rst_n);
            slaves[i].if_rd(mem_read_chans[i]);
            slaves[i].if_wr(mem_write_chans[i]);
            
            std::string r_filename = out_dir + "/data/core" + std::to_string(i) + "_input.csv";
            r_csv_files[i].open(r_filename);
            r_csv_files[i] << "Timestamp,Real,Imaginary\n";
            
            std::string w_filename = out_dir + "/data/core" + std::to_string(i) + "_output.csv";
            w_csv_files[i].open(w_filename);
            w_csv_files[i] << "Timestamp,Real,Imaginary\n";

            read_count[i] = 0;
        }

        SC_THREAD(run);
        
        SC_METHOD(monitor_transfers);
        sensitive << clk.posedge_event();
    }
    
    ~testbench() {
        for (int i = 0; i < NUM_CORES; ++i) {
            if (r_csv_files[i].is_open()) {
                r_csv_files[i].close();
            }
            if (w_csv_files[i].is_open()) {
                w_csv_files[i].close();
            }
        }
    }

    // Spies on transaction channels to log CSV files and capture read inputs
    void monitor_transfers() {
        if (!rst_n.read()) {
            return;
        }
        for (int c = 0; c < NUM_CORES; ++c) {
            // Monitor Read data channel (from Slave to DMA)
            if (mem_read_chans[c].r.in_val.read() && mem_read_chans[c].r.in_rdy.read()) {
                auto r_pay = mem_read_chans[c].r.in_msg.read();
                complex_t val = unpack_complex<AxiCfg>(r_pay.data);
                r_csv_files[c] << sc_time_stamp().to_string() << "," << val.real << "," << val.imag << "\n";
                if (read_count[c] < samples) {
                    inputs[c][read_count[c]] = val;
                    read_count[c]++;
                }
            }
            // Monitor Write data channel (from DMA to Slave)
            if (mem_write_chans[c].w.in_val.read() && mem_write_chans[c].w.in_rdy.read()) {
                auto w_pay = mem_write_chans[c].w.in_msg.read();
                complex_t val = unpack_complex<AxiCfg>(w_pay.data);
                w_csv_files[c] << sc_time_stamp().to_string() << "," << val.real << "," << val.imag << "\n";
            }
        }
    }

    // Simulates the main execution flow and performs memory verification
    void init_slave_memories() {
#ifdef USE_SLAVE_FROM_FILE
        std::cout << "@" << sc_time_stamp() << " Slave memories were initialized from files using SlaveFromFile." << std::endl;
#else
        std::cout << "@" << sc_time_stamp() << " Initializing Slave memories with random traffic pattern..." << std::endl;
        boost::random::mt19937 gen(seed);
        boost::random::uniform_int_distribution<> uniform_rand;
        for (int c = 0; c < NUM_CORES; ++c) {
            for (int i = 0; i < samples; ++i) {
                uint64_t byte_addr = i * (AxiCfg::dataWidth / 8);
                // Generate two independent 16-bit random integers
                uint16_t rand_real = uniform_rand(gen) & 0xFFFF;
                uint16_t rand_imag = uniform_rand(gen) & 0xFFFF;
                sc_uint<AxiCfg::dataWidth> wr_data = ((uint64_t)rand_real << 32) | rand_imag;
                
                slaves[c].localMem[byte_addr] = wr_data;
                for (int j = 0; j < (AxiCfg::dataWidth / 8); j++) {
                    slaves[c].localMem_wstrb[byte_addr + j] = nvhls::get_slc<8>(wr_data, 8 * j);
                }
                slaves[c].validReadAddresses.push_back(byte_addr);
            }
        }
#endif
    }

    bool verify_slave_memories() {
        std::cout << "@" << sc_time_stamp() << " Simulation complete. Verifying Slave memory..." << std::endl;

        bool all_pass = true;
        for (int c = 0; c < NUM_CORES; ++c) {
            int len = samples;
            int aligned_len = ((len + N - 1) / N) * N;

            std::vector<complex_t> padded_inputs = inputs[c];
            while (padded_inputs.size() < aligned_len) {
                padded_inputs.push_back(complex_t(0.0, 0.0));
            }

            std::vector<complex_t> expected(aligned_len);
            int num_blocks = aligned_len / N;
            int bits = (int)std::log2(N);
            for (int b = 0; b < num_blocks; ++b) {
                std::vector<complex_t> block_in(N);
                for (int i = 0; i < N; ++i) {
                    block_in[i] = padded_inputs[b * N + i];
                }
                std::vector<complex_t> block_out = compute_dft(block_in);
                for (int i = 0; i < N; ++i) {
                    int rev_i = bit_reverse(i, bits);
                    expected[b * N + rev_i] = block_out[i];
                }
            }

            for (int i = 0; i < len; ++i) {
                uint64_t byte_addr = (N + i) * (AxiCfg::dataWidth / 8);
                sc_uint<AxiCfg::dataWidth> raw = 0;
                for (int j = 0; j < (AxiCfg::dataWidth / 8); j++) {
                    raw = nvhls::set_slc(raw, slaves[c].localMem_wstrb[byte_addr + j], 8 * j);
                }
                complex_t actual = unpack_complex<AxiCfg>(raw);
                complex_t exp = expected[i];

                double diff_real = std::abs(actual.real - std::round(exp.real));
                double diff_imag = std::abs(actual.imag - std::round(exp.imag));
                bool match = (diff_real < 1e-2) && (diff_imag < 1e-2);
                
                if (match) {
                    continue;
                } else {
                    std::cout << "Core " << c << " index " << i << " [MISMATCH] Expected: (" 
                              << exp.real << ", " << exp.imag << "), Actual: (" 
                              << actual.real << ", " << actual.imag << ")" << std::endl;
                    all_pass = false;
                    break;
                }
            }
            if (all_pass) std::cout << "Core " << c << " [OK]" << std::endl;
        }

        if (all_pass) {
            std::cout << "Verification Successful!" << std::endl;
        } else {
            std::cout << "Verification Failed!" << std::endl;
        }
        return all_pass;
    }

    void run() {
        rst_n.write(false);
        start_signal.write(false);
        for (int i = 0; i < NUM_CORES; ++i) {
            base_addrs[i].write(0);
            num_samples[i].write(0);
        }
        wait(5, SC_NS);

        rst_n.write(true);
        wait(5, SC_NS);
        
        init_slave_memories();
        wait(5, SC_NS);

        for (int c = 0; c < NUM_CORES; ++c) {
            base_addrs[c].write(0);
            num_samples[c].write(samples);
        }

        std::cout << "@" << sc_time_stamp() << " Starting FFT system..." << std::endl;
        start_signal.write(true);
        wait(1, SC_NS);
        start_signal.write(false);

        wait(samples * 10, SC_NS);

        bool all_pass = verify_slave_memories();
        if (!all_pass) {
            sc_report_handler::report(SC_ERROR, "Verification failed", "Some outputs mismatch", __FILE__, __LINE__);
        }
        sc_stop();
    }
};

// ============================================================================
// Simulation Main Entry Point
// ============================================================================
int sc_main(int argc, char *argv[]) {
    sc_report_handler::set_actions( "/IEEE_Std_1666/deprecated", SC_DO_NOTHING );
    sc_set_default_time_unit(1.0, SC_NS); 

    nvhls::set_random_seed();

    testbench tb("tb");

    // Enable VCD tracing
    const char* out_dir_env = std::getenv("SIM_OUT_DIR");
    std::string out_dir = (out_dir_env != nullptr) ? out_dir_env : "out";
    std::string vcd_path = out_dir + "/trace";
    sc_trace_file *tf = sc_create_vcd_trace_file(vcd_path.c_str());
    if (tf) {
        // Trace top-level testbench signals grouped under Control
        sc_trace(tf, tb.clk, "Control.clk");
        sc_trace(tf, tb.rst_n, "Control.rst_n");
        sc_trace(tf, tb.start_signal, "Control.start_signal");
        
        // Trace Top module stagger status signals grouped under Stagger
        sc_trace(tf, tb.fft_sys.active_stagger, "Stagger.active");
        sc_trace(tf, tb.fft_sys.stagger_counter, "Stagger.counter");
        
        for (int i = 0; i < NUM_CORES; ++i) {
            std::string core_prefix = "Core" + std::to_string(i) + ".";
            sc_trace(tf, tb.base_addrs[i], (core_prefix + "base_addr").c_str());
            sc_trace(tf, tb.num_samples[i], (core_prefix + "num_samples").c_str());
            sc_trace(tf, tb.fft_sys.core_starts[i], (core_prefix + "start").c_str());
            sc_trace(tf, tb.fft_sys.core_busy[i], (core_prefix + "busy").c_str());

            // Trace AXI Read Channels grouped under Core<i>.AXI.Read
            sc_trace(tf, tb.mem_read_chans[i].ar.in_val, (core_prefix + "AXI_AR.ar_val").c_str());
            sc_trace(tf, tb.mem_read_chans[i].ar.in_rdy, (core_prefix + "AXI_AR.ar_rdy").c_str());
            sc_trace(tf, tb.mem_read_chans[i].ar.in_msg, (core_prefix + "AXI_AR.ar_msg").c_str());

            sc_trace(tf, tb.mem_read_chans[i].r.in_val, (core_prefix + "AXI_R.r_val").c_str());
            sc_trace(tf, tb.mem_read_chans[i].r.in_rdy, (core_prefix + "AXI_R.r_rdy").c_str());
            sc_trace(tf, tb.mem_read_chans[i].r.in_msg, (core_prefix + "AXI_R.r_msg").c_str());

            // Trace AXI Write Channels grouped under Core<i>.AXI.Write
            sc_trace(tf, tb.mem_write_chans[i].aw.in_val, (core_prefix + "AXI_AW.aw_val").c_str());
            sc_trace(tf, tb.mem_write_chans[i].aw.in_rdy, (core_prefix + "AXI_AW.aw_rdy").c_str());
            sc_trace(tf, tb.mem_write_chans[i].aw.in_msg, (core_prefix + "AXI_AW.aw_msg").c_str());

            sc_trace(tf, tb.mem_write_chans[i].w.in_val, (core_prefix + "AXI_W.w_val").c_str());
            sc_trace(tf, tb.mem_write_chans[i].w.in_rdy, (core_prefix + "AXI_W.w_rdy").c_str());
            sc_trace(tf, tb.mem_write_chans[i].w.in_msg, (core_prefix + "AXI_W.w_msg").c_str());

            // Trace AXI Write Response Channel grouped under Core<i>.AXI.Write
            sc_trace(tf, tb.mem_write_chans[i].b.in_val, (core_prefix + "AXI_B.b_val").c_str());
            sc_trace(tf, tb.mem_write_chans[i].b.in_rdy, (core_prefix + "AXI_B.b_rdy").c_str());
            sc_trace(tf, tb.mem_write_chans[i].b.in_msg, (core_prefix + "AXI_B.b_msg").c_str());

            // Trace Core Internal Handshake Channels (DMA <-> FFT) grouped under Core<i>.Internal
            sc_trace(tf, tb.fft_sys.cores[i].dma_to_fft_chan.in_val, (core_prefix + "Internal.dma_to_fft_val").c_str());
            sc_trace(tf, tb.fft_sys.cores[i].dma_to_fft_chan.in_rdy, (core_prefix + "Internal.dma_to_fft_rdy").c_str());
            sc_trace(tf, tb.fft_sys.cores[i].dma_to_fft_chan.in_msg, (core_prefix + "Internal.dma_to_fft_msg").c_str());

            sc_trace(tf, tb.fft_sys.cores[i].fft_to_dma_chan.in_val, (core_prefix + "Internal.fft_to_dma_val").c_str());
            sc_trace(tf, tb.fft_sys.cores[i].fft_to_dma_chan.in_rdy, (core_prefix + "Internal.fft_to_dma_rdy").c_str());
            sc_trace(tf, tb.fft_sys.cores[i].fft_to_dma_chan.in_msg, (core_prefix + "Internal.fft_to_dma_msg").c_str());
        }
    }

    sc_report_handler::set_actions(SC_ERROR, SC_DISPLAY);

    sc_start();

    // Close VCD tracing
    if (tf) {
        sc_close_vcd_trace_file(tf);
    }

    bool rc = (sc_report_handler::get_count(SC_ERROR) > 0);
    if (rc) 
        DCOUT("TESTBENCH FAIL" << endl);
    else 
        DCOUT("TESTBENCH PASS" << endl);
    return rc;
}