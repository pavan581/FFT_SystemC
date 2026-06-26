/*
 * fft_types.h
 *
 * Defines the custom complex number data structure ('complex_t') used across the
 * system, equipped with basic arithmetic operators, magnitude computation, and
 * auto-generation trace methods.
 *
 * It also defines the centralized templates 'pack_complex' and 'unpack_complex' 
 * to serialize and deserialize complex data over AXI4 channels.
 */

#ifndef FFT_TYPES_H
#define FFT_TYPES_H

#include <systemc.h>
#include <complex>
#include <auto_gen_fields.h>

using namespace sc_core;

// Custom complex number representation for SystemC signals and tracing.
struct complex_t {
    double real;
    double imag;

    complex_t(double x = 0.0, double y = 0.0) : real(x), imag(y) {}

    // Arithmetic operators
    complex_t operator + (const complex_t& b) const {
        return complex_t(real + b.real, imag + b.imag);
    }
    
    complex_t operator - (const complex_t& b) const {
        return complex_t(real - b.real, imag - b.imag);
    }
    
    complex_t operator * (const complex_t& b) const {
        return complex_t(
            real * b.real - imag * b.imag, 
            real * b.imag + imag * b.real
        );
    }

    double magnitude() const {
        return std::sqrt(real * real + imag * imag);
    }

    // Auto-generation macro for stream formatting and tracing.
    // Double fields are marked V2 as they lack a synthesis-specific bit width.
    AUTO_GEN_FIELD_METHODS_V2(complex_t, (real, imag))
};

// Stream extraction helper
inline std::istream& operator>>(std::istream& is, complex_t& c) {
    is >> c.real >> c.imag;
    return is;
}

// Packs a complex number into a raw AXI data word.
template<typename AxiCfg>
inline sc_uint<AxiCfg::dataWidth> pack_complex(double r, double i) {
    static const int HALF_WIDTH = AxiCfg::dataWidth / 2;
    int r_int = (int)std::round(r);
    int i_int = (int)std::round(i);
    sc_uint<AxiCfg::dataWidth> res = 0;
    if (AxiCfg::dataWidth == 64) {
        res.range(AxiCfg::dataWidth - 1, HALF_WIDTH) = r_int;
        res.range(HALF_WIDTH - 1, 0) = i_int;
    } else {
        res.range(AxiCfg::dataWidth - 1, 0) = r_int;
    }
    return res;
}

template<typename AxiCfg>
inline sc_uint<AxiCfg::dataWidth> pack_complex(const complex_t& val) {
    return pack_complex<AxiCfg>(val.real, val.imag);
}

// Unpacks a raw AXI data word into a complex number.
template<typename AxiCfg>
inline complex_t unpack_complex(sc_uint<AxiCfg::dataWidth> raw) {
    static const int HALF_WIDTH = AxiCfg::dataWidth / 2;
    if (AxiCfg::dataWidth == 64) {
        int r_int = raw.range(AxiCfg::dataWidth - 1, HALF_WIDTH).to_int();
        int i_int = raw.range(HALF_WIDTH - 1, 0).to_int();
        return complex_t((double)r_int, (double)i_int);
    } else {
        int r_int = raw.range(AxiCfg::dataWidth - 1, 0).to_int();
        return complex_t((double)r_int, 0.0);
    }
}

#endif  // FFT_TYPES_H
