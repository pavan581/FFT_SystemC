#include "tb_dma.h"
#include <iostream>
#include <iomanip>

Testbench::Testbench(sc_module_name name)
    : sc_module(name),
      clk("clk", 10, SC_NS),
      mem_read_chans("mem_read_chans", 1),
      mem_write_chans("mem_write_chans", 2),
      tb_write_master("tb_write_master"),
      fft_out_chan("fft_out_chan"),
      fft_in_chan("fft_in_chan"),
      tb_fft_in("tb_fft_in"),
      tb_fft_out("tb_fft_out")
{
    // Instantiate Memory with 2 write ports and 1 read port
    mem_inst = new Memory<2, 1, 1024, AxiCfg>("Memory");
    mem_inst->clk(clk);
    mem_inst->rst(rst);
    
    // Bind memory AXI ports
    mem_inst->read_ports(mem_read_chans);
    mem_inst->write_ports(mem_write_chans);

    // Instantiate DMA
    dma_inst = new DMA<AxiCfg, 4>("DMA");
    dma_inst->clk(clk);
    dma_inst->rst(rst);
    dma_inst->start(start);
    dma_inst->base_addr(base_addr);
    dma_inst->num_samples(num_samples);
    dma_inst->busy(busy);
    
    // Bind DMA AXI master ports
    dma_inst->mem_read_port(mem_read_chans[0]);
    dma_inst->mem_write_port(mem_write_chans[0]);

    // Bind DMA connections to the FFT channels
    dma_inst->fft_out(fft_out_chan);
    dma_inst->fft_in(fft_in_chan);

    // Bind testbench connections to the FFT channels (reversed role)
    tb_fft_in(fft_out_chan);
    tb_fft_out(fft_in_chan);

    // Bind testbench write master to Memory write port 1 (for initialization)
    tb_write_master(mem_write_chans[1]);

    tf = sc_create_vcd_trace_file("./out/vcd/dma_trace");
    tf->set_time_unit(1, SC_NS);
    sc_trace(tf, clk, "clk");
    sc_trace(tf, rst, "rst");
    sc_trace(tf, start, "start");
    sc_trace(tf, busy, "busy");

    SC_THREAD(stimuli);
    sensitive << clk.posedge_event();

    SC_THREAD(monitor);
    sensitive << clk.posedge_event();
}

Testbench::~Testbench() {
    delete mem_inst;
    delete dma_inst;
    sc_close_vcd_trace_file(tf);
}

void Testbench::axi_write(unsigned int addr, sc_uint<AxiCfg::dataWidth> data) {
    typename axi4<AxiCfg>::AddrPayload aw_pay;
    aw_pay.addr = addr;
    aw_pay.id = 0;
    aw_pay.len = 0;
    aw_pay.size = (AxiCfg::dataWidth == 64) ? 3 : 2;
    aw_pay.burst = 1;
    
    typename axi4<AxiCfg>::WritePayload w_pay;
    w_pay.data = data;
    w_pay.wstrb = ~0;
    w_pay.last = true;
    
    tb_write_master.write(aw_pay, w_pay);
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
    // Reset AXI write master
    tb_write_master.reset();

    rst.write(true);
    start.write(false);
    wait(20, SC_NS);
    rst.write(false);
    wait(20, SC_NS);

    std::cout << "[DMA TB] Initializing source memory slots..." << std::endl;
    for (int i = 0; i < 8; i++) {
        axi_write(i, pack_complex<AxiCfg>((double)i, 0.0));
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
        // Memory read from indices 4 to 7
        sc_uint<AxiCfg::dataWidth> raw = mem_inst->mem[4 + i];
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
