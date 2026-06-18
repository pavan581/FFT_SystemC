#include "tb_memory.h"
#include <iostream>
#include <iomanip>

Testbench::Testbench(sc_module_name name)
    : sc_module(name),
      clk("clk", 10, SC_NS),
      read_chans("read_chans", 1),
      write_chans("write_chans", 1),
      tb_read_master("tb_read_master"),
      tb_write_master("tb_write_master")
{
    mem = new Memory<1, 1, 1024, AxiCfg>("mem");
    mem->clk(clk);
    mem->rst(rst);
    
    // Bind memory ports to channels
    mem->read_ports(read_chans);
    mem->write_ports(write_chans);

    // Bind testbench master ports to channels
    tb_read_master(read_chans[0]);
    tb_write_master(write_chans[0]);

    tf = sc_create_vcd_trace_file("./out/vcd/memory_trace");
    tf->set_time_unit(1, SC_NS);
    sc_trace(tf, clk, "clk");
    sc_trace(tf, rst, "rst");

    SC_THREAD(stimuli);
    sensitive << clk.posedge_event();
}

Testbench::~Testbench() {
    delete mem;
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

sc_uint<AxiCfg::dataWidth> Testbench::axi_read(unsigned int addr) {
    typename axi4<AxiCfg>::AddrPayload ar_pay;
    ar_pay.addr = addr;
    ar_pay.id = 0;
    ar_pay.len = 0;
    ar_pay.size = (AxiCfg::dataWidth == 64) ? 3 : 2;
    ar_pay.burst = 1;

    tb_read_master.ar.Push(ar_pay);
    typename axi4<AxiCfg>::ReadPayload r_pay = tb_read_master.r.Pop();
    return r_pay.data;
}

void Testbench::stimuli() {
    // Reset AXI masters
    tb_write_master.reset();
    tb_read_master.reset();

    // Assert reset
    rst.write(true);
    wait(20, SC_NS);
    rst.write(false);
    wait(20, SC_NS);

    std::cout << "[MEM TB] Starting AXI Write operations..." << std::endl;
    for (int i = 0; i < 16; i++) {
        axi_write(i, i + 0xA0);
    }

    std::cout << "[MEM TB] Starting AXI Read operations and verifying data..." << std::endl;
    bool all_passed = true;
    for (int i = 0; i < 16; i++) {
        sc_uint<AxiCfg::dataWidth> val = axi_read(i);
        std::cout << "  Addr[" << i << "] = " << std::hex << val << std::dec;
        if (val == (i + 0xA0)) {
            std::cout << " [OK]" << std::endl;
        } else {
            std::cout << " [ERROR: expected " << std::hex << (i + 0xA0) << "]" << std::dec << std::endl;
            all_passed = false;
        }
    }

    if (all_passed) {
        std::cout << "[MEM TB] ALL TESTS PASSED." << std::endl;
    } else {
        std::cout << "[MEM TB] TESTS FAILED." << std::endl;
    }

    sc_stop();
}

int sc_main(int argc, char* argv[]) {
    Testbench tb("tb_memory");
    sc_start();
    return 0;
}
