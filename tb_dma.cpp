#include "tb_dma.h"
#include <iostream>
#include <iomanip>

Testbench::Testbench(sc_module_name name)
    : sc_module(name),
      clk("clk", 10, SC_NS),
      read_chan("read_chan"),
      write_chan("write_chan"),
      fft_out_chan("fft_out_chan"),
      fft_in_chan("fft_in_chan"),
      tb_fft_in("tb_fft_in"),
      tb_fft_out("tb_fft_out")
{
    // Instantiate Slave
    slave_inst = new Slave<AxiCfg>("Slave");
    slave_inst->clk(clk);
    slave_inst->reset_bar(rst_n);

    // Instantiate DMA
    dma_inst = new DMA<AxiCfg, 4>("DMA");
    dma_inst->clk(clk);
    dma_inst->rst_n(rst_n);
    dma_inst->start(start);
    dma_inst->base_addr(base_addr);
    dma_inst->num_samples(num_samples);
    dma_inst->busy(busy);
    
    // Connect DMA to Slave directly
    dma_inst->mem_read_port(read_chan);
    slave_inst->if_rd(read_chan);

    dma_inst->mem_write_port(write_chan);
    slave_inst->if_wr(write_chan);

    // Bind DMA connections to the FFT channels
    dma_inst->fft_out(fft_out_chan);
    dma_inst->fft_in(fft_in_chan);

    // Bind testbench connections to the FFT channels (reversed role)
    tb_fft_in(fft_out_chan);
    tb_fft_out(fft_in_chan);

    // VCD Tracing
    tf = sc_create_vcd_trace_file("./out/vcd/dma_matchlib_trace");
    tf->set_time_unit(1, SC_PS);
    sc_trace(tf, clk, "clk");
    sc_trace(tf, rst_n, "rst_n");
    sc_trace(tf, start, "start");
    sc_trace(tf, busy, "busy");

    SC_THREAD(stimuli);
    sensitive << clk.posedge_event();

    SC_THREAD(monitor);
    sensitive << clk.posedge_event();
}

Testbench::~Testbench() {
    delete slave_inst;
    delete dma_inst;
    sc_close_vcd_trace_file(tf);
}

void Testbench::monitor() {
    tb_fft_in.Reset();
    tb_fft_out.Reset();
    wait();

    while (true) {
        // Fetch streamed data from DMA
        complex_t val = tb_fft_in.Pop();
        std::cout << "[DMA TB] Received from DMA: " << val << " @ " << sc_time_stamp() << std::endl;
        
        // Simulating FFT logic: add a bias constant and stream back
        complex_t out_val = val + complex_t(100.0, 0.0);
        tb_fft_out.Push(out_val);
    }
}

void Testbench::stimuli() {
    // Assert reset
    std::cout << "[DMA TB] Asserting Reset..." << std::endl;
    rst_n.write(false);
    start.write(false);
    wait(20, SC_NS);
    
    rst_n.write(true);
    wait(20, SC_NS);

    std::cout << "[DMA TB] Initializing Slave memory..." << std::endl;
    for (int i = 0; i < 8; i++) {
        uint64_t byte_addr = i * (AxiCfg::dataWidth / 8);
        sc_uint<AxiCfg::dataWidth> packed = pack_complex<AxiCfg>((double)i, 0.0);
        slave_inst->localMem[byte_addr] = packed;
        for (int j = 0; j < (AxiCfg::dataWidth / 8); j++) {
            slave_inst->localMem_wstrb[byte_addr + j] = nvhls::get_slc<8>(packed, 8 * j);
        }
        slave_inst->validReadAddresses.push_back(byte_addr);
    }
    wait(10, SC_NS);

    std::cout << "[DMA TB] Launching DMA Transfer (Base: 0, Len: 4)..." << std::endl;
    base_addr.write(0);
    num_samples.write(4);
    start.write(true);
    wait(10, SC_NS);
    start.write(false);

    // Wait for DMA completion
    while (busy.read()) {
        wait(10, SC_NS);
    }
    wait(50, SC_NS);

    std::cout << "[DMA TB] Verifying memory write-back (offset by N=4)..." << std::endl;
    bool pass = true;
    for (int i = 0; i < 4; i++) {
        uint64_t byte_addr = (4 + i) * (AxiCfg::dataWidth / 8);
        sc_uint<AxiCfg::dataWidth> raw = 0;
        for (int j = 0; j < (AxiCfg::dataWidth / 8); j++) {
            raw = nvhls::set_slc(raw, slave_inst->localMem_wstrb[byte_addr + j], 8 * j);
        }
        complex_t val = unpack_complex<AxiCfg>(raw);
        std::cout << "  Addr[" << (4 + i) << "] = " << val;
        
        // Expected value: input + 100
        double expected_real = (double)i + 100.0;
        if (std::abs(val.real - expected_real) < 1e-2 && std::abs(val.imag) < 1e-2) {
            std::cout << " [OK]" << std::endl;
        } else {
            std::cout << " [ERROR: expected " << expected_real << "]" << std::endl;
            pass = false;
        }
    }

    if (pass) {
        std::cout << "[DMA TB] DMA VERIFICATION PASSED." << std::endl;
    } else {
        std::cout << "[DMA TB] DMA VERIFICATION FAILED." << std::endl;
    }

    sc_stop();
}

int sc_main(int argc, char* argv[]) {
    Testbench tb("tb_dma");
    sc_start();
    return 0;
}
