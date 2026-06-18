#include "tb_top.h"
#include <iostream>

int sc_main(int argc, char* argv[]) {
    std::cout << "Starting Interleaved FFT Simulation" << std::endl;

    // Simulation parameters: N=4, Cores=2, Hop=1, NUM_MULT=4, NUM_ADD=6
    Testbench<4, 2, 1, 2, 2> tb("tb");
    
    sc_start();
    
    std::cout << "Simulation Finished." << std::endl;
    return 0;
}
