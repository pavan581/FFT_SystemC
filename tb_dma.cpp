// ============================================================================
// TB_DMA.CPP - DMA Module Testbench Implementation
// ============================================================================
// Implements the stimuli and response checking for the DMA testbench.
// ============================================================================

#include "tb_dma.h"
#include <iostream>
#include <iomanip>

using namespace std;

Testbench::Testbench(sc_module_name name) : sc_module(name), clk("clk", 10, SC_NS) {
    // Instantiate Modules
    mem_inst = new Memory<MEM_SIZE, DATA_WIDTH, ADDR_WIDTH>("Memory");
    mem_inst->clk(clk);
    mem_inst->rst(rst);
    mem_inst->wrt_en(mem_wr_en_sig);
    mem_inst->raddr(mem_addr);
    mem_inst->waddr(mem_waddr_sig);
    mem_inst->data_in(mem_wdata_sig);
    mem_inst->data_out(mem_data);

    dma_inst = new DMA<ADDR_WIDTH, DATA_WIDTH>("DMA");
    dma_inst->clk(clk);
    dma_inst->rst(rst);
    dma_inst->start(start);
    dma_inst->base_addr(base_addr);
    dma_inst->num_samples(num_samples);
    dma_inst->busy(busy);
    dma_inst->mem_addr(mem_addr);
    dma_inst->mem_data(mem_data);
    dma_inst->fft_data(fft_data);
    dma_inst->fft_valid(fft_valid);

    // Tracing
    tf = sc_create_vcd_trace_file("./out/vcd/dma_trace_210126");
    tf->set_time_unit(1, SC_NS);
    sc_trace(tf, clk, "clk");
    sc_trace(tf, rst, "rst");
    sc_trace(tf, start, "start");
    sc_trace(tf, busy, "busy");
    sc_trace(tf, base_addr, "base_addr");
    sc_trace(tf, mem_addr, "mem_addr");
    sc_trace(tf, mem_data, "mem_data");
    sc_trace(tf, fft_valid, "fft_valid");
    sc_trace(tf, fft_data, "fft_data");

    SC_THREAD(stimuli);
}

Testbench::~Testbench() {
    sc_close_vcd_trace_file(tf);
    delete mem_inst;
    delete dma_inst;
}

void Testbench::stimuli() {
    // Reset
    rst.write(true);
    start.write(false);
    mem_wr_en_sig.write(false); // Init other signals
    wait(20, SC_NS);
    rst.write(false);
    wait(20, SC_NS);

    // 1. Initialize Memory backdoor with known test patterns.
    // This simulates pre-existing data in the shared memory that the DMA will fetch.
    cout << "Initializing Memory..." << endl;
    for (int i = 0; i < 16; i++) {
        // High 32: i, Low 32: i*2
        sc_uint<64> val = 0;
        val.range(63, 32) = i+5;
        val.range(31, 0) = i * 2;
        mem_inst->mem[i] = val;
    }

    // Test Sequence
    // 2. Perform first DMA transfer block.
    // Configure base address and transfer length, then pulse start.
    cout << "Starting DMA Transfer 01.." << endl;
    base_addr.write(1);
    num_samples.write(8);
    start.write(true);
    wait(10, SC_NS); // Start pulse
    start.write(false);

    // Wait for completion
    for (int i = 0; i < 30; i++) {
        wait(10, SC_NS);
        if (fft_valid.read()) {
            cout << "Time " << std::setw(5) << sc_time_stamp() 
                 << " Output: " << fft_data.read() << endl;
        }
        if (!busy.read() && i > 10) break;
    }

    cout << "Starting DMA Transfer 02.." << endl;
    base_addr.write(5);
    num_samples.write(10);
    start.write(true);
    wait(10, SC_NS); // Start pulse
    start.write(false);

    // Wait for completion
    for (int i = 0; i < 30; i++) {
        wait(10, SC_NS);
        if (fft_valid.read()) {
            cout << "Time " << std::setw(5) << sc_time_stamp() 
                 << " Output: " << fft_data.read() << endl;
        }
        if (!busy.read() && i > 10) break;
    }
    
    sc_stop();
}
