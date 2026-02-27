// ============================================================================
// TB_FFT.CPP - Standalone FFT Core Testbench
// ============================================================================
// This testbench verifies the functionality of a single, standalone FFT core.
// It directly feeds complex numbers to the FFT pipeline and checks the output.
//
// Verification Scenarios:
// - TEST 1: Impulse at index 0
// - TEST 2: DC Signal (All 1s)
// - TEST 3: Alternating Signal [1, -1, 1, -1]
// - TEST 4: Back-to-Back Blocks
// - TEST 5: Values with Imaginary components
// ============================================================================

#include <systemc.h>
#include <vector>

#include "fft.h"

using namespace std;

template<int N>
SC_MODULE(FFT_TB) {
    // Signals
    sc_clock                clk;
    sc_signal<bool>         rst;

    sc_signal<bool>         in_valid, out_valid;
    sc_signal<complex_t>    in_data, out_data;
    sc_signal<int>          in_index, out_index;
    sc_signal<bool>         status;

    FFT* fft;

    sc_trace_file* tf;

    SC_CTOR(FFT_TB) : clk("clk", 1, SC_NS) {
        fft = new FFT("fft", N);
        fft->clk(clk);
        fft->rst(rst);
        fft->status(status);
        fft->in_valid(in_valid);
        fft->in_index(in_index);
        fft->in_data(in_data);
        fft->out_valid(out_valid);
        fft->out_index(out_index);
        fft->out_data(out_data);

        string trace_name = "./out/vcd/FFT_N" + to_string(N);
        tf = sc_create_vcd_trace_file(trace_name.c_str());
        tf->set_time_unit(1, SC_PS);
        sc_trace(tf, clk,       "clk");
        sc_trace(tf, rst,       "rst");
        sc_trace(tf, in_valid,  "in_valid");
        sc_trace(tf, in_index,  "in_index");
        sc_trace(tf, in_data,   "in_data");
        sc_trace(tf, out_valid, "out_valid");
        sc_trace(tf, out_index, "out_index");
        sc_trace(tf, out_data,  "out_data");


        SC_THREAD(control);
        sensitive << clk.posedge_event();
    }

    ~FFT_TB() {
        sc_close_vcd_trace_file(tf);
        delete fft;
    }

    // Control thread that generates input stimuli.
    // Drives the input signals through various standard DSP test cases.
    void control() {
        int wait_for = N*5;
        rst.write(true);
        in_valid.write(false);
        wait(5);
        rst.write(false);

        // ====================================================================
        // TEST CASE 1: Impulse at index 0
        // ====================================================================
        for (int i=0; i<N; i++) {
            in_data.write(complex_t(i==0 ? 1 : 0, 0));
            in_valid.write(true);
            wait();
        }
        in_valid.write(false);

        wait(wait_for);

        // ====================================================================
        // TEST CASE 2: DC Signal (All 1s)
        // ====================================================================
        for (int i=0; i<N; i++) {
            in_data.write(complex_t(1, 0));
            in_valid.write(true);
            wait();
        }
        in_valid.write(false);

        wait(wait_for);
        
        // ====================================================================
        // TEST CASE 3: Alternating Signal [1, -1, 1, -1]
        // ====================================================================
        for (int i=0; i<N; i++) {
            in_data.write(complex_t(i%2 == 0 ? 1 : -1, 0));
            in_valid.write(true);
            wait();
        }
        in_valid.write(false);

        wait(wait_for);

        // ====================================================================
        // TEST CASE 4: Back-to-Back Blocks
        // ====================================================================
        for (int i=0; i<N; i++) {
            in_data.write(complex_t(i==0 ? 2 : 0, 0));
            in_valid.write(true);
            wait();
        }
        for (int i=0; i<N; i++) {
            in_data.write(complex_t(i+1, 0));
            in_valid.write(true);
            wait();
        }
        in_valid.write(false);

        wait(wait_for);

        // ====================================================================
        // TEST CASE 5: Values with Imaginary
        // ====================================================================
        for (int i=0; i<N; i++) {
            in_data.write(complex_t(i*7, i*3));
            in_valid.write(true);
            wait();
        }
        in_valid.write(false);

        wait(wait_for);

        sc_stop();
    }
};

int sc_main(int argc, char* argv[]) {
    // Parameters
    const int N = 4;      // FFT Size

    FFT_TB<N> tb("fft_tb");

    sc_start();

    cout << "FFT Module Simulation Finished." << endl;
    return 0;
}