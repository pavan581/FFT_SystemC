// ============================================================================
// FFT_TYPES.H - Complex Number Type Definition for FFT Implementation
// ============================================================================
// This file defines a custom complex number type (complex_t) for use in
// SystemC modules.
// ============================================================================

#ifndef FFT_TYPES_H
#define FFT_TYPES_H

#include <systemc.h>
#include <complex>
#include <auto_gen_fields.h>

using namespace sc_core;

// ============================================================================
// Complex Number Structure
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

    // Reflection macro for tracing, streaming, and equality comparison.
    // V2 is used because double fields do not have a synthesizable HLS bit width.
    AUTO_GEN_FIELD_METHODS_V2(complex_t, (real, imag))
};

// Input operator: Reads real and imaginary parts from stream
inline std::istream& operator>>(std::istream& is, complex_t& c) {
    is >> c.real >> c.imag;
    return is;
}

#endif  // FFT_TYPES_H
