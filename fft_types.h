// ============================================================================
// FFT_TYPES.H - Complex Number Type Definition for FFT Implementation
// ============================================================================
// This file defines a custom complex number type (complex_t) for use in
// SystemC modules. The custom struct is used instead of std::complex to
// provide better integration with SystemC signals and trace functionality.
//
// Features:
// - Double precision floating-point representation
// - Overloaded arithmetic operators (+, -, *)
// - SystemC signal compatibility
// - VCD trace support for waveform debugging
// ============================================================================

#ifndef FFT_TYPES_H
#define FFT_TYPES_H

#include <systemc.h>
#include <complex>

using namespace sc_core;

// ============================================================================
// Complex Number Structure
// ============================================================================
// Custom complex number type with explicit real and imaginary components.
// This struct is designed to work seamlessly with SystemC signals and provides
// arithmetic operations needed for FFT butterfly computations.
//
// Note: For hardware synthesis, replace 'double' with 'sc_fixed'
//       to model fixed-point arithmetic with configurable precision.
// ============================================================================
struct complex_t {
    double real;  // Real component
    double imag;  // Imaginary component

    complex_t(double x = 0.0, double y = 0.0) : real(x), imag(y) {}

    // ========================================================================
    // Arithmetic Operators
    // ========================================================================
    
    // Calculates the sum of two complex numbers.
    // Formula: (a + jb) + (c + jd) = (a+c) + j(b+d)
    complex_t operator + (const complex_t& b) const {
        return complex_t(real + b.real, imag + b.imag);
    }
    
    // Calculates the difference between two complex numbers.
    // Formula: (a + jb) - (c + jd) = (a-c) + j(b-d)
    complex_t operator - (const complex_t& b) const {
        return complex_t(real - b.real, imag - b.imag);
    }
    
    // Multiplication: (a + jb) * (c + jd) = (ac-bd) + j(ad+bc)
    complex_t operator * (const complex_t& b) const {
        return complex_t(
            real * b.real - imag * b.imag, 
            real * b.imag + imag * b.real
        );
    }

    // ========================================================================
    // Utility Methods
    // ========================================================================
    
    // Calculate magnitude: |z| = sqrt(real² + imag²)
    // Useful for computing FFT output power spectrum
    double magnitude() const {
        return std::sqrt(real * real + imag * imag);
    }

    // Equality comparison for testing and validation
    bool operator==(const complex_t& other) const {
        return real == other.real && imag == other.imag;
    }
};
// ============================================================================
// Stream I/O Operators
// ============================================================================
// Enable printing and reading complex numbers using standard streams.
// Format: (real + imagj) for output
// ============================================================================
inline std::ostream& operator<<(std::ostream& os, const complex_t& c) {
    os << "(" << c.real << " + " << c.imag << "j)";
    return os;
}

// Input operator: Reads real and imaginary parts from stream
inline std::istream& operator>>(std::istream& is, complex_t& c) {
    is >> c.real >> c.imag;
    return is;
}

// ============================================================================
// SystemC Trace Support
// ============================================================================
// Enables VCD waveform generation for complex signals.
// Automatically splits complex number into separate real and imaginary traces.
//
// Usage in testbench:
//   sc_signal<complex_t> sig;
//   sc_trace(trace_file, sig, "my_signal");
//   
// Creates two traces in VCD:
//   - my_signal_real
//   - my_signal_imag
// ============================================================================
inline void sc_trace(sc_trace_file* tf, const complex_t& v, const std::string& name) {
    sc_trace(tf, v.real, name + "_real");
    sc_trace(tf, v.imag, name + "_imag");
}

#endif  // FFT_TYPES_H
