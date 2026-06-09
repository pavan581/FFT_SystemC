// ============================================================================
// MATCHLIB_AXI.H - MatchLib & AXI4 Emulation Library for SystemC
// ============================================================================
// This header emulates NVIDIA's MatchLib library components, specifically:
// 1. Connections - Latency-insensitive ready/valid handshaking ports & channels
// 2. AXI4 Payloads & Ports - Synthesizable AXI4 interface representations
//
// This enables clean, HLS-compatible SystemC coding without requiring a
// Catapult HLS installation or licenses to build/simulate locally.
// ============================================================================

#ifndef MATCHLIB_AXI_H
#define MATCHLIB_AXI_H

#include <systemc.h>
#include <string>

// ============================================================================
// Connections Namespace
// ============================================================================
namespace Connections {

// Combinational Channel - Holds the ready/valid/data signals
template <typename T>
struct Combinational {
    sc_signal<T> msg;
    sc_signal<bool> vld;
    sc_signal<bool> rdy;

    Combinational() : msg("msg"), vld("vld"), rdy("rdy") {}
    Combinational(const char* name) 
        : msg((std::string(name) + "_msg").c_str()), 
          vld((std::string(name) + "_vld").c_str()), 
          rdy((std::string(name) + "_rdy").c_str()) {}
};

// Input Port - Reads data with handshake
template <typename T>
struct In {
    sc_in<T> msg;
    sc_in<bool> vld;
    sc_out<bool> rdy;

    In() : msg("msg"), vld("vld"), rdy("rdy") {}
    In(const char* name) 
        : msg((std::string(name) + "_msg").c_str()), 
          vld((std::string(name) + "_vld").c_str()), 
          rdy((std::string(name) + "_rdy").c_str()) {}

    template <typename Chan>
    void operator()(Chan& chan) {
        msg(chan.msg);
        vld(chan.vld);
        rdy(chan.rdy);
    }
};

// Output Port - Writes data with handshake
template <typename T>
struct Out {
    sc_out<T> msg;
    sc_out<bool> vld;
    sc_in<bool> rdy;

    Out() : msg("msg"), vld("vld"), rdy("rdy") {}
    Out(const char* name) 
        : msg((std::string(name) + "_msg").c_str()), 
          vld((std::string(name) + "_vld").c_str()), 
          rdy((std::string(name) + "_rdy").c_str()) {}

    template <typename Chan>
    void operator()(Chan& chan) {
        msg(chan.msg);
        vld(chan.vld);
        rdy(chan.rdy);
    }
};

} // namespace Connections

// ============================================================================
// AXI Namespace
// ============================================================================
namespace axi {

// 1. AXI4 Payloads (Configurable via template Cfg)
template <typename Cfg>
struct AddrPayload {
    sc_uint<Cfg::addrWidth> addr;
    sc_uint<Cfg::idWidth> id;
    sc_uint<8> len;
    sc_uint<3> size;
    sc_uint<2> burst;

    inline bool operator==(const AddrPayload& rhs) const {
        return (addr == rhs.addr && id == rhs.id && len == rhs.len && size == rhs.size && burst == rhs.burst);
    }

    friend ostream& operator<<(ostream& os, const AddrPayload& p) {
        os << "(addr=" << p.addr << ", id=" << p.id << ")";
        return os;
    }
};

template <typename Cfg>
struct ReadPayload {
    sc_uint<Cfg::dataWidth> data;
    sc_uint<Cfg::idWidth> id;
    sc_uint<2> resp;
    bool last;

    inline bool operator==(const ReadPayload& rhs) const {
        return (data == rhs.data && id == rhs.id && resp == rhs.resp && last == rhs.last);
    }

    friend ostream& operator<<(ostream& os, const ReadPayload& p) {
        os << "(data=" << p.data << ", id=" << p.id << ", last=" << p.last << ")";
        return os;
    }
};

template <typename Cfg>
struct WritePayload {
    sc_uint<Cfg::dataWidth> data;
    sc_uint<Cfg::dataWidth / 8> strb;
    bool last;

    inline bool operator==(const WritePayload& rhs) const {
        return (data == rhs.data && strb == rhs.strb && last == rhs.last);
    }

    friend ostream& operator<<(ostream& os, const WritePayload& p) {
        os << "(data=" << p.data << ", last=" << p.last << ")";
        return os;
    }
};

template <typename Cfg>
struct WRespPayload {
    sc_uint<Cfg::idWidth> id;
    sc_uint<2> resp;

    inline bool operator==(const WRespPayload& rhs) const {
        return (id == rhs.id && resp == rhs.resp);
    }

    friend ostream& operator<<(ostream& os, const WRespPayload& p) {
        os << "(id=" << p.id << ", resp=" << p.resp << ")";
        return os;
    }
};

// 2. AXI4 Read Interfaces
template <typename Cfg>
struct axi4_read_master {
    Connections::Out<AddrPayload<Cfg>> ar;
    Connections::In<ReadPayload<Cfg>> r;

    axi4_read_master() {}
    axi4_read_master(const char* name) 
        : ar((std::string(name) + "_ar").c_str()), 
          r((std::string(name) + "_r").c_str()) {}

    template <typename Chan>
    void operator()(Chan& chan) {
        ar(chan.ar);
        r(chan.r);
    }
};

template <typename Cfg>
struct axi4_read_slave {
    Connections::In<AddrPayload<Cfg>> ar;
    Connections::Out<ReadPayload<Cfg>> r;

    axi4_read_slave() {}
    axi4_read_slave(const char* name) 
        : ar((std::string(name) + "_ar").c_str()), 
          r((std::string(name) + "_r").c_str()) {}

    template <typename Chan>
    void operator()(Chan& chan) {
        ar(chan.ar);
        r(chan.r);
    }
};

// 3. AXI4 Write Interfaces
template <typename Cfg>
struct axi4_write_master {
    Connections::Out<AddrPayload<Cfg>> aw;
    Connections::Out<WritePayload<Cfg>> w;
    Connections::In<WRespPayload<Cfg>> b;

    axi4_write_master() {}
    axi4_write_master(const char* name) 
        : aw((std::string(name) + "_aw").c_str()), 
          w((std::string(name) + "_w").c_str()), 
          b((std::string(name) + "_b").c_str()) {}

    template <typename Chan>
    void operator()(Chan& chan) {
        aw(chan.aw);
        w(chan.w);
        b(chan.b);
    }
};

template <typename Cfg>
struct axi4_write_slave {
    Connections::In<AddrPayload<Cfg>> aw;
    Connections::In<WritePayload<Cfg>> w;
    Connections::Out<WRespPayload<Cfg>> b;

    axi4_write_slave() {}
    axi4_write_slave(const char* name) 
        : aw((std::string(name) + "_aw").c_str()), 
          w((std::string(name) + "_w").c_str()), 
          b((std::string(name) + "_b").c_str()) {}

    template <typename Chan>
    void operator()(Chan& chan) {
        aw(chan.aw);
        w(chan.w);
        b(chan.b);
    }
};

// 4. AXI4 Physical Channels (for Testbench & System Connections)
template <typename Cfg>
struct axi4_read_channel {
    Connections::Combinational<AddrPayload<Cfg>> ar;
    Connections::Combinational<ReadPayload<Cfg>> r;

    axi4_read_channel() {}
    axi4_read_channel(const char* name)
        : ar((std::string(name) + "_ar").c_str()),
          r((std::string(name) + "_r").c_str()) {}
};

template <typename Cfg>
struct axi4_write_channel {
    Connections::Combinational<AddrPayload<Cfg>> aw;
    Connections::Combinational<WritePayload<Cfg>> w;
    Connections::Combinational<WRespPayload<Cfg>> b;

    axi4_write_channel() {}
    axi4_write_channel(const char* name)
        : aw((std::string(name) + "_aw").c_str()),
          w((std::string(name) + "_w").c_str()),
          b((std::string(name) + "_b").c_str()) {}
};

} // namespace axi

// ============================================================================
// SystemC Trace Overloads for AXI Payloads (Enables VCD Dumping)
// ============================================================================
template <typename Cfg>
inline void sc_trace(sc_trace_file* tf, const axi::AddrPayload<Cfg>& p, const std::string& name) {
    sc_trace(tf, p.addr, name + ".addr");
    sc_trace(tf, p.id, name + ".id");
    sc_trace(tf, p.len, name + ".len");
    sc_trace(tf, p.size, name + ".size");
    sc_trace(tf, p.burst, name + ".burst");
}

template <typename Cfg>
inline void sc_trace(sc_trace_file* tf, const axi::ReadPayload<Cfg>& p, const std::string& name) {
    sc_trace(tf, p.data, name + ".data");
    sc_trace(tf, p.id, name + ".id");
    sc_trace(tf, p.resp, name + ".resp");
    sc_trace(tf, p.last, name + ".last");
}

template <typename Cfg>
inline void sc_trace(sc_trace_file* tf, const axi::WritePayload<Cfg>& p, const std::string& name) {
    sc_trace(tf, p.data, name + ".data");
    sc_trace(tf, p.strb, name + ".strb");
    sc_trace(tf, p.last, name + ".last");
}

template <typename Cfg>
inline void sc_trace(sc_trace_file* tf, const axi::WRespPayload<Cfg>& p, const std::string& name) {
    sc_trace(tf, p.id, name + ".id");
    sc_trace(tf, p.resp, name + ".resp");
}

#endif // MATCHLIB_AXI_H
