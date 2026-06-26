#include <systemc.h>
#include <vector>
#include <connections/connections.h>
#include "fft.h"

using namespace std;
using namespace Connections;

// Standalone FFT Core Testbench
template<int N, int NUM_MULT=4, int NUM_ADD=6>
SC_MODULE(FFT_TB) {
    sc_clock                clk;
    sc_signal<bool>         rst_n;

    Combinational<complex_t> in_chan;
    Combinational<complex_t> out_chan;

    Out<complex_t> tb_in_port;
    In<complex_t> tb_out_port;

    FFT<N, NUM_MULT, NUM_ADD>* fft;
    sc_trace_file* tf;

    sc_signal<double> trace_in_real;
    sc_signal<double> trace_in_imag;
    sc_signal<double> trace_out_real;
    sc_signal<double> trace_out_imag;

    SC_CTOR(FFT_TB) : 
        clk("clk", 1, SC_NS),
        in_chan("in_chan"),
        out_chan("out_chan"),
        tb_in_port("tb_in_port"),
        tb_out_port("tb_out_port") 
    {
        fft = new FFT<N, NUM_MULT, NUM_ADD>("fft");
        fft->clk(clk);
        fft->rst_n(rst_n);
        fft->in_data(in_chan);
        fft->out_data(out_chan);

        tb_in_port(in_chan);
        tb_out_port(out_chan);

        string trace_name = "./out/vcd/FFT_N" + to_string(N);
        tf = sc_create_vcd_trace_file(trace_name.c_str());
        tf->set_time_unit(1, SC_PS);
        sc_trace(tf, clk,       "clk");
        sc_trace(tf, rst_n,     "rst_n");
        sc_trace(tf, trace_in_real, "in_data_real");
        sc_trace(tf, trace_in_imag, "in_data_imag");
        sc_trace(tf, trace_out_real, "out_data_real");
        sc_trace(tf, trace_out_imag, "out_data_imag");
 
        SC_THREAD(control);
        sensitive << clk.posedge_event();
 
        SC_THREAD(monitor);
        sensitive << clk.posedge_event();
    }
 
    ~FFT_TB() {
        sc_close_vcd_trace_file(tf);
        delete fft;
    }
 
    // Generates input sequences to feed the FFT core
    void control() {
        tb_in_port.Reset();
        trace_in_real.write(0.0);
        trace_in_imag.write(0.0);
        rst_n.write(false);
        wait(5);
        rst_n.write(true);
        wait();
 
        // ====================================================================
        // TEST CASE 1: Impulse at index 0
        // ====================================================================
        std::cout << "\n[FFT TB] TEST 1: Impulse at index 0..." << std::endl;
        for (int i=0; i<N; i++) {
            complex_t val(i==0 ? 1 : 0, 0);
            trace_in_real.write(val.real);
            trace_in_imag.write(val.imag);
            tb_in_port.Push(val);
        }
        for (int i=0; i<N; i++) {
            complex_t val(0, 0);
            trace_in_real.write(val.real);
            trace_in_imag.write(val.imag);
            tb_in_port.Push(val);
        }
        wait(N * 5);
 
        // ====================================================================
        // TEST CASE 2: DC Signal (All 1s)
        // ====================================================================
        std::cout << "\n[FFT TB] TEST 2: DC Signal..." << std::endl;
        for (int i=0; i<N; i++) {
            complex_t val(1, 0);
            trace_in_real.write(val.real);
            trace_in_imag.write(val.imag);
            tb_in_port.Push(val);
        }
        for (int i=0; i<N; i++) {
            complex_t val(0, 0);
            trace_in_real.write(val.real);
            trace_in_imag.write(val.imag);
            tb_in_port.Push(val);
        }
        wait(N * 5);
        
        // ====================================================================
        // TEST CASE 3: Alternating Signal [1, -1, 1, -1]
        // ====================================================================
        std::cout << "\n[FFT TB] TEST 3: Alternating Signal..." << std::endl;
        for (int i=0; i<N; i++) {
            complex_t val(i%2 == 0 ? 1 : -1, 0);
            trace_in_real.write(val.real);
            trace_in_imag.write(val.imag);
            tb_in_port.Push(val);
        }
        for (int i=0; i<N; i++) {
            complex_t val(0, 0);
            trace_in_real.write(val.real);
            trace_in_imag.write(val.imag);
            tb_in_port.Push(val);
        }
        wait(N * 5);
 
        std::cout << "[FFT TB] All tests finished." << std::endl;
        sc_stop();
    }
 
    // Logs outputs from the FFT core
    void monitor() {
        tb_out_port.Reset();
        trace_out_real.write(0.0);
        trace_out_imag.write(0.0);
        wait();
 
        for (int t = 1; t <= 3; ++t) {
            std::cout << "\n[FFT TB] FFT Results for TEST " << t << ":" << std::endl;
            for (int i = 0; i < N; ++i) {
                complex_t out_val = tb_out_port.Pop();
                trace_out_real.write(out_val.real);
                trace_out_imag.write(out_val.imag);
                std::cout << "@" << sc_time_stamp() << " FFT Out[" << i << "] = " << out_val << std::endl;
            }
            for (int i = 0; i < N; ++i) {
                tb_out_port.Pop();
            }
        }
    }
};

int sc_main(int argc, char* argv[]) {
    const int N = 8;
    FFT_TB<N> tb("fft_tb");
    sc_start();
    cout << "FFT Module Simulation Finished." << endl;
    return 0;
}