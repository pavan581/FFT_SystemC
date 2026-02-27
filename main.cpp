// ============================================================================
// MAIN.CPP - SystemC Simulation Entry Point
// ============================================================================
// This file serves as the main entry point for the SystemC simulation of the
// Interleaved FFT architecture. It instantiates the top-level testbench
// with specific parameters and starts the simulation kernel.
// ============================================================================

#include "tb_interleaved_fft.h"
#include <iostream>

int sc_main(int argc, char* argv[]) {
    std::cout << "Starting Interleaved FFT Simulation (Refactored)" << std::endl;

    // Parameters: N=8, Cores=2, Hop=1
    Testbench<4, 2, 1> tb("tb");
    
    // Run Simulation
    sc_start();
    
    std::cout << "Simulation Finished." << std::endl;
    return 0;
}
