/*
 * fft_types.h
 *
 * Complex number type definitions, math operators, and AXI4 stream packing/unpacking helpers.
 *
 * Defines the complex_t structure with basic arithmetic operators for complex math,
 * and provides template helpers to serialize/deserialize complex data over AXI4 channels.
 */

#ifndef FFT_TYPES_H
#define FFT_TYPES_H

#include <systemc.h>
#include <complex>
#include <auto_gen_fields.h>

using namespace sc_core;

// Complex number representation for SystemC signals and tracing
struct complex_t {
    double real;
    double imag;

    complex_t(double x = 0.0, double y = 0.0) : real(x), imag(y) {}

    // Basic arithmetic operators
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

    // Auto-generation macro for tracing fields (double fields are marked V2)
    AUTO_GEN_FIELD_METHODS_V2(complex_t, (real, imag))
};

// Stream input helper
inline std::istream& operator>>(std::istream& is, complex_t& c) {
    is >> c.real >> c.imag;
    return is;
}

// Pack complex value into AXI data word
template<typename AxiCfg>
inline sc_uint<AxiCfg::dataWidth> pack_complex(double r, double i) {
    sc_uint<AxiCfg::dataWidth> res = 0;
    if (AxiCfg::dataWidth == 64) {
        static const int HALF_WIDTH = AxiCfg::dataWidth / 2;
        int r_int = (int)std::round(r);
        int i_int = (int)std::round(i);
        res.range(AxiCfg::dataWidth - 1, HALF_WIDTH) = r_int;
        res.range(HALF_WIDTH - 1, 0) = i_int;
    } else {
        int r_int = (int)std::round(r);
        res.range(AxiCfg::dataWidth - 1, 0) = r_int;
    }
    return res;
}

template<typename AxiCfg>
inline sc_uint<AxiCfg::dataWidth> pack_complex(const complex_t& val) {
    return pack_complex<AxiCfg>(val.real, val.imag);
}

// Unpack AXI data word into complex number
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

