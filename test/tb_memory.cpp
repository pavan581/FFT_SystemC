#include "tb_memory.h"
#include <iostream>

Testbench::Testbench(sc_module_name name)
    : sc_module(name),
      clk("clk", 10, SC_NS),
      read_chan("read_chan"),
      write_chan("write_chan")
{
    // Instantiate sub-modules
    master = new Master<AxiCfg, MyMasterCfg>("master");
    mem = new Memory<1024, AxiCfg>("mem");

    Connections::set_sim_clk(&clk);

    // Bind clock and reset
    master->clk(clk);
    master->reset_bar(rst_n);

    mem->clk(clk);
    mem->rst_n(rst_n);

    master->if_rd(read_chan);
    mem->read_port(read_chan);

    master->if_wr(write_chan);
    mem->write_port(write_chan);

    master->done(done);

    // VCD setup
    tf = sc_create_vcd_trace_file("./out/vcd/memory_matchlib_trace");
    tf->set_time_unit(1, SC_PS);
    sc_trace(tf, clk, "clk");
    sc_trace(tf, rst_n, "rst_n");
    sc_trace(tf, done, "done");

    SC_THREAD(stimuli);
}

Testbench::~Testbench() {
    delete master;
    delete mem;
    sc_close_vcd_trace_file(tf);
}

void Testbench::stimuli() {
    // Assert reset
    std::cout << "[MEM TB] Asserting Reset..." << std::endl;
    rst_n.write(false);
    wait(20, SC_NS);
    
    rst_n.write(true);
    std::cout << "[MEM TB] Reset released. Starting Matchlib AXI Master verification..." << std::endl;
    
    // Wait for done
    while (true) {
        wait(10, SC_NS);
        if (done.read()) {
            std::cout << "[MEM TB] Matchlib AXI Master completed all checks successfully!" << std::endl;
            std::cout << "[MEM TB] ALL TESTS PASSED." << std::endl;
            sc_stop();
            return;
        }
    }
}

int sc_main(int argc, char* argv[]) {
    Testbench tb("tb_memory");
    sc_start();
    return 0;
}
