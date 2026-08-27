//===- QASM3Importer.h - OpenQASM 2/3 → FTQC MLIR text importer ----------===//
//
// Shared header used by qasm3-import (standalone tool) and ftqc-opt (native
// .qasm input integration).  No MLIR/LLVM dependencies — pure C++17 stdlib.
//
// Public API:
//   std::string importQASM3(
//       const std::string &src,        // QASM source text
//       const std::string &eccKind,    // "steane" | "surface" | "color_code"
//       int                eccDist,    // code distance
//       const std::string &funcName);  // MLIR function name
//
//===----------------------------------------------------------------------===//

#ifndef FTQC_SRC_QASM3IMPORTER_H
#define FTQC_SRC_QASM3IMPORTER_H

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace qasm3import {

// ─── Tokenizer ───────────────────────────────────────────────────────────────

struct Token {
  enum Kind {
    IDENT, INT, FLOAT, LBRACKET, RBRACKET, LPAREN, RPAREN,
    COMMA, SEMICOLON, ASSIGN, ARROW, STRING, END
  };
  Kind        kind = END;
  std::string text;
  int         ival = 0;
};

inline std::vector<Token> tokenize(const std::string &line) {
  std::vector<Token> out;
  size_t i = 0;
  auto skipWS = [&] {
    while (i < line.size() && std::isspace((unsigned char)line[i])) ++i;
  };
  while (i < line.size()) {
    skipWS();
    if (i >= line.size()) break;
    if (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/') break;
    char c = line[i];
    if      (c == '[') { out.push_back({Token::LBRACKET,  "["}); ++i; }
    else if (c == ']') { out.push_back({Token::RBRACKET,  "]"}); ++i; }
    else if (c == '(') { out.push_back({Token::LPAREN,    "("}); ++i; }
    else if (c == ')') { out.push_back({Token::RPAREN,    ")"}); ++i; }
    else if (c == ',') { out.push_back({Token::COMMA,     ","}); ++i; }
    else if (c == ';') { out.push_back({Token::SEMICOLON, ";"}); ++i; }
    else if (c == '=') { out.push_back({Token::ASSIGN,    "="}); ++i; }
    else if (c == '-' && i + 1 < line.size() && line[i + 1] == '>') {
      out.push_back({Token::ARROW, "->"}); i += 2;
    }
    else if (c == '"') {
      ++i; std::string s;
      while (i < line.size() && line[i] != '"') s += line[i++];
      if (i < line.size()) ++i;
      out.push_back({Token::STRING, s});
    }
    else if (std::isdigit((unsigned char)c)) {
      std::string n; bool hasDot = false;
      while (i < line.size() &&
             (std::isdigit((unsigned char)line[i]) || line[i] == '.')) {
        if (line[i] == '.') hasDot = true; n += line[i++];
      }
      if (hasDot) out.push_back({Token::FLOAT, n});
      else { Token t; t.kind=Token::INT; t.text=n; t.ival=std::stoi(n); out.push_back(t); }
    }
    else if (std::isalpha((unsigned char)c) || c == '_') {
      std::string id;
      while (i < line.size() &&
             (std::isalnum((unsigned char)line[i]) || line[i] == '_'))
        id += line[i++];
      out.push_back({Token::IDENT, id});
    }
    else { ++i; }
  }
  out.push_back({Token::END, ""});
  return out;
}

// ─── Circuit IR ──────────────────────────────────────────────────────────────

struct QRef { std::string reg; int idx = -1; };

struct RegDecl { bool isQubit; std::string name; int size = 1; };

enum class GK { H, X, Y, Z, S, Sdg, T, Tdg, CX, CZ, CCX, SKIP };

struct GateInst { GK gate; std::vector<QRef> args; };
struct MeasInst { QRef src, dst; };

struct Instr {
  enum { GATE, MEAS } kind;
  GateInst g;
  MeasInst m;
};

struct Circuit { std::vector<RegDecl> regs; std::vector<Instr> instrs; };

// ─── Parser ──────────────────────────────────────────────────────────────────

class Parser {
  std::vector<Token> tok;
  size_t p = 0;

  Token &cur()               { return tok[p]; }
  bool   at(Token::Kind k)   { return cur().kind == k; }
  bool   atId(const char *s) { return at(Token::IDENT) && cur().text == s; }
  Token  consume()           { return tok[p++]; }
  void   skipTo(Token::Kind stop) {
    while (!at(stop) && !at(Token::END)) consume();
    if (at(stop)) consume();
  }
  void expect(Token::Kind k) {
    if (!at(k)) throw std::runtime_error("unexpected '" + cur().text + "'");
    consume();
  }
  QRef qref() {
    QRef q; q.reg = consume().text;
    if (at(Token::LBRACKET)) { consume(); q.idx=cur().ival; consume(); expect(Token::RBRACKET); }
    return q;
  }
  std::vector<QRef> qrefList() {
    std::vector<QRef> v; v.push_back(qref());
    while (at(Token::COMMA)) { consume(); v.push_back(qref()); }
    return v;
  }
  static GK gateKind(const std::string &n) {
    if (n=="h")   return GK::H;   if (n=="x")   return GK::X;
    if (n=="y")   return GK::Y;   if (n=="z")   return GK::Z;
    if (n=="s")   return GK::S;   if (n=="sdg") return GK::Sdg;
    if (n=="t")   return GK::T;   if (n=="tdg") return GK::Tdg;
    if (n=="cx"||n=="cnot") return GK::CX;
    if (n=="cz")  return GK::CZ;
    if (n=="ccx"||n=="ccX") return GK::CCX;
    return GK::SKIP;
  }

public:
  explicit Parser(std::vector<Token> t) : tok(std::move(t)) {}

  void parseLine(Circuit &c) {
    if (at(Token::END)) return;
    if (atId("OPENQASM")||atId("include")||atId("barrier")||
        atId("reset")||atId("gate")) { skipTo(Token::SEMICOLON); return; }

    if (atId("qreg")||atId("creg")||atId("qubit")||atId("bit")) {
      bool isQ = (cur().text=="qreg"||cur().text=="qubit"); consume();
      RegDecl d; d.isQubit=isQ;
      if (at(Token::LBRACKET)) {
        consume(); d.size=cur().ival; consume(); expect(Token::RBRACKET);
        if (at(Token::IDENT)) d.name=consume().text;
      } else {
        if (at(Token::IDENT)) d.name=consume().text;
        if (at(Token::LBRACKET)) { consume(); d.size=cur().ival; consume(); consume(); }
      }
      skipTo(Token::SEMICOLON);
      if (!d.name.empty()) c.regs.push_back(d);
      return;
    }

    if (atId("measure")) {
      consume(); Instr inst; inst.kind=Instr::MEAS;
      inst.m.src=qref();
      if (at(Token::ARROW)) { consume(); inst.m.dst=qref(); }
      skipTo(Token::SEMICOLON); c.instrs.push_back(inst); return;
    }

    if (at(Token::IDENT)) {
      size_t saved=p; QRef dst=qref();
      if (at(Token::ASSIGN)) {
        consume();
        if (atId("measure")) {
          consume(); Instr inst; inst.kind=Instr::MEAS;
          inst.m.src=qref(); inst.m.dst=dst;
          skipTo(Token::SEMICOLON); c.instrs.push_back(inst); return;
        }
        skipTo(Token::SEMICOLON); return;
      }
      p=saved;
    }

    if (at(Token::IDENT)) {
      std::string name=consume().text;
      if (at(Token::LPAREN)) {
        consume(); int d=1;
        while (!at(Token::END)&&d>0) {
          if (at(Token::LPAREN)) ++d; if (at(Token::RPAREN)) --d; consume();
        }
      }
      GK gk=gateKind(name);
      if (gk==GK::SKIP) { skipTo(Token::SEMICOLON); return; }
      Instr inst; inst.kind=Instr::GATE; inst.g.gate=gk;
      if (!at(Token::SEMICOLON)&&!at(Token::END)) inst.g.args=qrefList();
      skipTo(Token::SEMICOLON); c.instrs.push_back(inst); return;
    }
    skipTo(Token::SEMICOLON);
  }
};

inline Circuit parseQASM(const std::string &src) {
  Circuit c;
  std::istringstream ss(src); std::string line;
  while (std::getline(ss, line)) {
    while (true) {
      auto s=line.find("/*"); if (s==std::string::npos) break;
      auto e=line.find("*/",s);
      if (e==std::string::npos) { line=line.substr(0,s); break; }
      line=line.substr(0,s)+" "+line.substr(e+2);
    }
    try { auto toks=tokenize(line); Parser p(std::move(toks)); p.parseLine(c); }
    catch (...) {}
  }
  return c;
}

// ─── MLIR Emitter ────────────────────────────────────────────────────────────

class Emitter {
  std::string eccKind;
  int         eccDist;
  std::string funcName;

  std::unordered_map<std::string,int> regBase, regSz;
  int nQubits=0;
  std::vector<std::string> live;
  int valIdx=0, magicIdx=0, bitIdx=0;

  std::string fv() { return "%v"     + std::to_string(valIdx++);   }
  std::string fm() { return "%magic" + std::to_string(magicIdx++); }
  std::string fb() { return "%b"     + std::to_string(bitIdx++);   }

  std::string qt() const {
    return "!ftqc.logical_qubit<#ftqc.ecc<" + eccKind +
           ", dist = " + std::to_string(eccDist) + ">>";
  }

  std::vector<int> expand(const QRef &q) const {
    auto it=regBase.find(q.reg); if (it==regBase.end()) return {};
    int base=it->second, sz=regSz.at(q.reg);
    if (q.idx==-1) { std::vector<int> v; for (int i=0;i<sz;++i) v.push_back(base+i); return v; }
    if (q.idx<0||q.idx>=sz) return {};
    return {base+q.idx};
  }

  void emitS(int qi, std::ostream &os) {
    std::string out=fv();
    os<<"    "<<out<<" = ftqc.logical_s "<<live[qi]<<" : "<<qt()<<"\n";
    live[qi]=out;
  }
  void emitT(int qi, std::ostream &os) {
    std::string magic=fm();
    os<<"    "<<magic<<" = ftqc.prepare_magic_t : !ftqc.magic_state\n";
    std::string out=fv();
    os<<"    "<<out<<" = ftqc.logical_t "<<live[qi]<<", "<<magic<<"\n"
      <<"        : ("<<qt()<<", !ftqc.magic_state)\n"
      <<"        -> "<<qt()<<"\n";
    live[qi]=out;
  }

public:
  Emitter(std::string ecc, int dist, std::string fn)
      : eccKind(std::move(ecc)), eccDist(dist), funcName(std::move(fn)) {}

  std::string emit(const Circuit &c) {
    for (auto &r : c.regs) {
      regSz[r.name]=r.size;
      if (r.isQubit) { regBase[r.name]=nQubits; nQubits+=r.size; }
    }
    live.resize(nQubits);

    std::ostringstream body;
    for (int qi=0;qi<nQubits;++qi) { live[qi]=fv(); body<<"    "<<live[qi]<<" = ftqc.init_zero : "<<qt()<<"\n"; }

    std::vector<std::string> bits;

    for (auto &inst : c.instrs) {
      if (inst.kind==Instr::GATE) {
        auto &g=inst.g;
        auto arg=[&](int i)->std::vector<int>{
          return (g.args.size()>(size_t)i)?expand(g.args[i]):std::vector<int>{};
        };
        if (g.gate==GK::H||g.gate==GK::X||g.gate==GK::Y||g.gate==GK::Z||
            g.gate==GK::S||g.gate==GK::Sdg||g.gate==GK::T||g.gate==GK::Tdg) {
          const char *op=nullptr;
          switch(g.gate){case GK::H:op="ftqc.logical_h";break;case GK::X:op="ftqc.logical_x";break;
            case GK::Y:op="ftqc.logical_y";break;case GK::Z:op="ftqc.logical_z";break;
            case GK::S:op="ftqc.logical_s";break;default:break;}
          for (int qi:arg(0)) {
            switch(g.gate){
              case GK::H:case GK::X:case GK::Y:case GK::Z:case GK::S:{
                std::string out=fv();
                body<<"    "<<out<<" = "<<op<<" "<<live[qi]<<" : "<<qt()<<"\n";
                live[qi]=out; break;}
              case GK::T:   emitT(qi,body); break;
              case GK::Sdg: emitS(qi,body);emitS(qi,body);emitS(qi,body); break;
              case GK::Tdg: emitS(qi,body);emitS(qi,body);emitS(qi,body);emitT(qi,body); break;
              default:break;
            }
          }
        } else if (g.gate==GK::CX) {
          auto cs=arg(0),ts=arg(1); int n=(int)std::min(cs.size(),ts.size());
          for (int i=0;i<n;++i) {
            int ci=cs[i],ti=ts[i]; std::string co=fv(),to_=fv();
            body<<"    "<<co<<", "<<to_<<" = ftqc.logical_cnot "<<live[ci]<<", "<<live[ti]<<"\n"
                <<"        : ("<<qt()<<", "<<qt()<<")\n"
                <<"        -> ("<<qt()<<", "<<qt()<<")\n";
            live[ci]=co; live[ti]=to_;
          }
        } else if (g.gate==GK::CZ) {
          auto a0s=arg(0),a1s=arg(1); int n=(int)std::min(a0s.size(),a1s.size());
          for (int i=0;i<n;++i) {
            int ai=a0s[i],bi=a1s[i]; std::string ao=fv(),bo=fv();
            body<<"    "<<ao<<", "<<bo<<" = ftqc.logical_cz "<<live[ai]<<", "<<live[bi]<<"\n"
                <<"        : ("<<qt()<<", "<<qt()<<")\n"
                <<"        -> ("<<qt()<<", "<<qt()<<")\n";
            live[ai]=ao; live[bi]=bo;
          }
        } else if (g.gate==GK::CCX) {
          auto c0s=arg(0),c1s=arg(1),tts=arg(2);
          if (!c0s.empty()&&!c1s.empty()&&!tts.empty()) {
            int c0=c0s[0],c1=c1s[0],ti=tts[0];
            std::string mg=fm(),c0o=fv(),c1o=fv(),to_=fv();
            body<<"    "<<mg<<" = ftqc.prepare_magic_ccz : !ftqc.magic_state<CCZ>\n";
            body<<"    "<<c0o<<", "<<c1o<<", "<<to_
                <<" = ftqc.logical_toffoli "<<live[c0]<<", "<<live[c1]<<", "<<live[ti]<<", "<<mg<<"\n"
                <<"        : ("<<qt()<<", "<<qt()<<", "<<qt()<<", !ftqc.magic_state<CCZ>)\n"
                <<"        -> ("<<qt()<<", "<<qt()<<", "<<qt()<<")\n";
            live[c0]=c0o; live[c1]=c1o; live[ti]=to_;
          }
        }
      } else {
        for (int qi:expand(inst.m.src)) {
          std::string bit=fb();
          body<<"    "<<bit<<" = ftqc.logical_measure "<<live[qi]<<"\n"
              <<"        : ("<<qt()<<") -> i1\n";
          bits.push_back(bit);
        }
      }
    }

    bool hasMeas=!bits.empty();
    std::string retTy, retArgs;
    if (hasMeas) {
      for (size_t i=0;i<bits.size();++i) {
        if (i) { retTy+=", "; retArgs+=", "; }
        retTy+="i1"; retArgs+=bits[i];
      }
    } else {
      std::string q=qt();
      for (int i=0;i<nQubits;++i) {
        if (i) { retTy+=", "; retArgs+=", "; }
        retTy+=q; retArgs+=live[i];
      }
    }

    if (nQubits==0&&!hasMeas) {
      std::ostringstream out;
      out<<"// Generated by qasm3-import\n// ECC: "<<eccKind<<" d="<<eccDist<<"\n\n"
         <<"func.func @"<<funcName<<"() {\n    func.return\n}\n";
      return out.str();
    }

    if (!retArgs.empty())
      body<<"    func.return "<<retArgs<<" : "<<retTy<<"\n";

    std::ostringstream out;
    out<<"// Generated by qasm3-import\n// ECC: "<<eccKind<<" d="<<eccDist<<"\n\n"
       <<"func.func @"<<funcName<<"() -> ("<<retTy<<") {\n"
       <<body.str()<<"}\n";
    return out.str();
  }
};

// ─── Public API ──────────────────────────────────────────────────────────────

/// Return the filename stem of path (e.g. "/a/b/foo.qasm" → "foo").
/// Non-identifier characters are replaced with '_'.
inline std::string stemOf(const std::string &path) {
  auto s=path.rfind('/');
  std::string base=(s==std::string::npos)?path:path.substr(s+1);
  auto d=base.rfind('.'); if (d!=std::string::npos) base=base.substr(0,d);
  for (auto &ch:base) if (!std::isalnum((unsigned char)ch)) ch='_';
  return base.empty()?"circuit":base;
}

/// Parse qasmSrc and emit FTQC MLIR text.
/// eccKind: "steane" | "surface" | "color_code"
/// funcName: if empty, defaults to "circuit"
inline std::string importQASM3(const std::string &qasmSrc,
                                const std::string &eccKind = "steane",
                                int                eccDist = 3,
                                const std::string &funcName = "circuit") {
  Circuit circ = parseQASM(qasmSrc);
  Emitter emitter(eccKind, eccDist, funcName.empty() ? "circuit" : funcName);
  return emitter.emit(circ);
}

} // namespace qasm3import

#endif // FTQC_SRC_QASM3IMPORTER_H
