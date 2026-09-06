//===- SynthesizeRz.cpp - Rz(theta) -> Clifford+T synthesis pass ----------===//
//
// Approximates each `ftqc.logical_rz {angle}` by a Clifford+T gate sequence
// using the Solovay-Kitaev algorithm, then emits the sequence as FTQC logical
// ops and reports the resulting T-count.  Dyadic multiples of pi/4 are emitted
// exactly (0 or 1 T).  Arbitrary angles are approximated to a target precision.
//
// The synthesis core is plain C++ (SU(2) matrices); only the final emission
// touches the IR.  Correctness is checkable by recomputing the emitted
// sequence's unitary (see test/Transforms/synthesize-rz.mlir and the paper's
// verification script).
//
//===----------------------------------------------------------------------===//

#include "FTQC/Dialect/FTQCAttrs.h"
#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <unordered_map>
#include <vector>

using namespace mlir;
using namespace mlir::ftqc;

namespace {

using cd = std::complex<double>;
using Mat = std::array<cd, 4>; // row-major: [00,01,10,11]

static const double PI = 3.14159265358979323846;

// Generator indices.
enum Gen { H = 0, S = 1, T = 2, X = 3, Y = 4, Z = 5, Sdg = 6, Tdg = 7, NGEN = 8 };

static Mat matmul(const Mat &A, const Mat &B) {
  return {A[0] * B[0] + A[1] * B[2], A[0] * B[1] + A[1] * B[3],
          A[2] * B[0] + A[3] * B[2], A[2] * B[1] + A[3] * B[3]};
}
static Mat dagger(const Mat &A) {
  return {std::conj(A[0]), std::conj(A[2]), std::conj(A[1]), std::conj(A[3])};
}
static const Mat I2 = {cd(1, 0), cd(0, 0), cd(0, 0), cd(1, 0)};

static Mat genMatrix(int g) {
  double r2 = 1.0 / std::sqrt(2.0);
  cd i(0, 1);
  switch (g) {
  case H:   return {cd(r2, 0), cd(r2, 0), cd(r2, 0), cd(-r2, 0)};
  case S:   return {cd(1, 0), cd(0, 0), cd(0, 0), i};
  case T:   return {cd(1, 0), cd(0, 0), cd(0, 0), std::exp(i * (PI / 4))};
  case X:   return {cd(0, 0), cd(1, 0), cd(1, 0), cd(0, 0)};
  case Y:   return {cd(0, 0), -i, i, cd(0, 0)};
  case Z:   return {cd(1, 0), cd(0, 0), cd(0, 0), cd(-1, 0)};
  case Sdg: return {cd(1, 0), cd(0, 0), cd(0, 0), -i};
  case Tdg: return {cd(1, 0), cd(0, 0), cd(0, 0), std::exp(-i * (PI / 4))};
  }
  return I2;
}
static int invGen(int g) {
  switch (g) {
  case H: return H; case S: return Sdg; case T: return Tdg;
  case X: return X; case Y: return Y; case Z: return Z;
  case Sdg: return S; case Tdg: return T;
  }
  return g;
}

using Word = std::vector<uint8_t>;

static Mat wordMatrix(const Word &w) {
  Mat m = I2; // first gate applied first => later gates multiply on the left
  for (uint8_t g : w)
    m = matmul(genMatrix(g), m);
  return m;
}
static Word invWord(const Word &w) {
  Word r;
  r.reserve(w.size());
  for (auto it = w.rbegin(); it != w.rend(); ++it)
    r.push_back((uint8_t)invGen(*it));
  return r;
}

// PSU(2) distance: 0 when U ~ V up to global phase.
static double dist(const Mat &U, const Mat &V) {
  Mat p = matmul(dagger(U), V);
  cd tr = p[0] + p[3];
  double f = std::abs(tr) / 2.0;
  if (f > 1.0) f = 1.0;
  return std::sqrt(std::max(0.0, 1.0 - f));
}

// Phase-canonicalize a (unitary) matrix to det=1 SU(2), sign fixed.
static Mat toSU2(const Mat &M) {
  cd det = M[0] * M[3] - M[1] * M[2];
  cd s = std::sqrt(det); // one square root
  Mat U = {M[0] / s, M[1] / s, M[2] / s, M[3] / s};
  // Fix global sign (U and -U are the same rotation): make Re(U[0]) >= 0.
  double k = std::real(U[0]);
  if (k < -1e-12 || (std::abs(k) <= 1e-12 && std::imag(U[0]) < 0))
    for (auto &e : U) e = -e;
  return U;
}

// Rotation angle theta and unit axis n of a det=1 SU(2) matrix.
static double rotAngle(const Mat &U) {
  double c = std::real(U[0] + U[3]) / 2.0; // cos(theta/2)
  c = std::max(-1.0, std::min(1.0, c));
  return 2.0 * std::acos(c);
}
static void rotAxis(const Mat &U, double theta, double n[3]) {
  double s = std::sin(theta / 2.0);
  if (std::abs(s) < 1e-12) { n[0] = 0; n[1] = 0; n[2] = 1; return; }
  cd i(0, 1);
  // B = (cos(theta/2) I - U) / (i sin) = n . sigma
  double c = std::cos(theta / 2.0);
  Mat B = {(cd(c, 0) - U[0]) / (i * s), (cd(0, 0) - U[1]) / (i * s),
           (cd(0, 0) - U[2]) / (i * s), (cd(c, 0) - U[3]) / (i * s)};
  n[0] = std::real(B[2]); // nx = Re(A[1,0])
  n[1] = std::imag(B[2]); // ny = Im(A[1,0])
  n[2] = std::real(B[0]); // nz = Re(A[0,0])
  double nn = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
  if (nn > 1e-12) { n[0] /= nn; n[1] /= nn; n[2] /= nn; }
}
// SU(2) rotation by angle about unit axis n.
static Mat rotMat(const double n[3], double angle) {
  double c = std::cos(angle / 2.0), s = std::sin(angle / 2.0);
  cd i(0, 1);
  return {cd(c, 0) - i * s * n[2], -i * s * cd(n[0], 0) - s * cd(n[1], 0),
          -i * s * cd(n[0], 0) + s * cd(n[1], 0), cd(c, 0) + i * s * n[2]};
}

// Group-commutator decompose: find V,W (rotations) with V W V^-1 W^-1 = U.
static void gcDecompose(const Mat &U, Mat &V, Mat &W) {
  double theta = rotAngle(U);
  if (theta < 1e-9) { V = I2; W = I2; return; }
  double ex[3] = {1, 0, 0}, ey[3] = {0, 1, 0};
  auto commAngle = [&](double phi) {
    Mat Vx = rotMat(ex, phi), Wy = rotMat(ey, phi);
    Mat C = matmul(matmul(Vx, Wy), matmul(dagger(Vx), dagger(Wy)));
    return rotAngle(toSU2(C));
  };
  // Bisection for phi in (0, pi/2] so commAngle(phi) == theta (monotone there).
  double lo = 1e-7, hi = PI / 2.0;
  double amax = commAngle(hi);
  double target = std::min(theta, amax);
  for (int it = 0; it < 60; ++it) {
    double mid = 0.5 * (lo + hi);
    if (commAngle(mid) < target) lo = mid; else hi = mid;
  }
  double phi = 0.5 * (lo + hi);
  Mat Vx = rotMat(ex, phi), Wy = rotMat(ey, phi);
  Mat C = toSU2(matmul(matmul(Vx, Wy), matmul(dagger(Vx), dagger(Wy))));
  // Rotate the commutator's axis onto U's axis.
  double nC[3], nU[3];
  rotAxis(C, rotAngle(C), nC);
  rotAxis(toSU2(U), theta, nU);
  double axis[3] = {nC[1] * nU[2] - nC[2] * nU[1],
                    nC[2] * nU[0] - nC[0] * nU[2],
                    nC[0] * nU[1] - nC[1] * nU[0]};
  double an = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
  double dot = nC[0] * nU[0] + nC[1] * nU[1] + nC[2] * nU[2];
  Mat Srot = I2;
  if (an > 1e-9) {
    double a[3] = {axis[0] / an, axis[1] / an, axis[2] / an};
    Srot = rotMat(a, std::acos(std::max(-1.0, std::min(1.0, dot))));
  } else if (dot < 0) {
    double a[3] = {0, 0, 1};
    Srot = rotMat(a, PI);
  }
  Mat Sd = dagger(Srot);
  V = matmul(matmul(Srot, Vx), Sd);
  W = matmul(matmul(Srot, Wy), Sd);
}

//===----------------------------------------------------------------------===//
// Base approximation net (built once).
//===----------------------------------------------------------------------===//

struct BaseNet {
  std::vector<Mat> mats;
  std::vector<Word> words;

  static int64_t key(const Mat &su2) {
    // Hash phase-canonicalized SU(2) on a coarse 4D grid.
    auto q = [](double v) { return (int64_t)std::llround(v / 0.06); };
    int64_t k = q(std::real(su2[0]));
    k = k * 131 + q(std::imag(su2[0]));
    k = k * 131 + q(std::real(su2[2]));
    k = k * 131 + q(std::imag(su2[2]));
    return k;
  }

  void build(int maxLen) {
    std::unordered_map<int64_t, int> seen;
    std::vector<Word> frontier;
    auto add = [&](const Word &w) {
      Mat su2 = toSU2(wordMatrix(w));
      int64_t k = key(su2);
      if (seen.count(k)) return false;
      seen[k] = (int)mats.size();
      mats.push_back(su2);
      words.push_back(w);
      return true;
    };
    add(Word{});
    frontier.push_back(Word{});
    for (int len = 1; len <= maxLen && !frontier.empty(); ++len) {
      std::vector<Word> next;
      for (const Word &w : frontier)
        for (int g = 0; g < NGEN; ++g) {
          if (!w.empty() && invGen(w.back()) == g) continue; // trivial cancel
          Word nw = w;
          nw.push_back((uint8_t)g);
          if (add(nw)) next.push_back(nw);
        }
      frontier.swap(next);
      if (mats.size() > 400000) break;
    }
  }

  const Word &nearest(const Mat &U) const {
    Mat su2 = toSU2(U);
    int best = 0;
    double bd = 1e9;
    for (size_t i = 0; i < mats.size(); ++i) {
      double d = dist(su2, mats[i]);
      if (d < bd) { bd = d; best = (int)i; }
    }
    return words[best];
  }
};

static const BaseNet &baseNet() {
  static BaseNet net = [] {
    BaseNet n;
    n.build(9);
    return n;
  }();
  return net;
}

// Solovay-Kitaev recursion returning a Clifford+T word approximating U.
static Word solovayKitaev(const Mat &U, int depth) {
  if (depth == 0)
    return baseNet().nearest(U);
  Word u = solovayKitaev(U, depth - 1);
  Mat Um = toSU2(U);
  Mat um = toSU2(wordMatrix(u));
  Mat delta = toSU2(matmul(Um, dagger(um)));
  Mat V, W;
  gcDecompose(delta, V, W);
  Word wv = solovayKitaev(toSU2(V), depth - 1);
  Word ww = solovayKitaev(toSU2(W), depth - 1);
  Word res;
  res.insert(res.end(), u.begin(), u.end());
  Word iww = invWord(ww), iwv = invWord(wv);
  res.insert(res.end(), iww.begin(), iww.end());
  res.insert(res.end(), iwv.begin(), iwv.end());
  res.insert(res.end(), ww.begin(), ww.end());
  res.insert(res.end(), wv.begin(), wv.end());
  return res;
}

// Exact word for Rz(k * pi/4) up to global phase: T^k reduced via Z,S,T.
static bool exactPiOver4(double angle, Word &out) {
  double q = angle / (PI / 4.0);
  long k = std::lround(q);
  if (std::abs(q - (double)k) > 1e-9)
    return false;
  k = ((k % 8) + 8) % 8; // T^k, k in [0,8)
  out.clear();
  if (k >= 4) { out.push_back(Z); k -= 4; }
  if (k >= 2) { out.push_back(S); k -= 2; }
  if (k >= 1) { out.push_back(T); k -= 1; }
  return true;
}

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

struct SynthesizeRz : public OpRewritePattern<LogicalRzOp> {
  SynthesizeRz(MLIRContext *ctx, int depth)
      : OpRewritePattern<LogicalRzOp>(ctx), depth_(depth) {}

  LogicalResult matchAndRewrite(LogicalRzOp op,
                                PatternRewriter &rewriter) const override {
    Location loc = op.getLoc();
    auto *ctx = rewriter.getContext();
    Type lq = op.getQubit().getType();
    double angle = op.getAngle().convertToDouble();

    Word word;
    if (!exactPiOver4(angle, word)) {
      // Rz(theta) = diag(e^{-i theta/2}, e^{+i theta/2}).
      cd i(0, 1);
      Mat rz = {std::exp(-i * (angle / 2.0)), cd(0, 0), cd(0, 0),
                std::exp(i * (angle / 2.0))};
      word = solovayKitaev(toSU2(rz), depth_);
    }

    // Emit the word as FTQC ops, threading the qubit value.
    Value q = op.getQubit();
    unsigned tCount = 0;
    auto emitT = [&]() {
      auto ms = rewriter.create<PrepareMagicTOp>(
          loc, MagicStateType::get(ctx, MagicKind::T));
      q = rewriter.create<LogicalTOp>(loc, lq, q, ms.getState()).getResult();
      ++tCount;
    };
    for (uint8_t g : word) {
      switch (g) {
      case H: q = rewriter.create<LogicalHOp>(loc, lq, q); break;
      case S: q = rewriter.create<LogicalSOp>(loc, lq, q); break;
      case Z: q = rewriter.create<LogicalZOp>(loc, lq, q); break;
      case X: q = rewriter.create<LogicalXOp>(loc, lq, q); break;
      case Y: q = rewriter.create<LogicalYOp>(loc, lq, q); break;
      case T: emitT(); break;
      case Sdg: // S^-1 = S . Z
        q = rewriter.create<LogicalSOp>(loc, lq, q);
        q = rewriter.create<LogicalZOp>(loc, lq, q);
        break;
      case Tdg: // T^-1 = T . S . Z
        emitT();
        q = rewriter.create<LogicalSOp>(loc, lq, q);
        q = rewriter.create<LogicalZOp>(loc, lq, q);
        break;
      }
    }
    op->setAttr("ftqc.synth_t_count",
                rewriter.getI64IntegerAttr((int64_t)tCount));
    rewriter.replaceOp(op, q);
    return success();
  }

  int depth_;
};

struct FTQCSynthesizeRzPassImpl
    : public PassWrapper<FTQCSynthesizeRzPassImpl,
                         OperationPass<func::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FTQCSynthesizeRzPassImpl)

  FTQCSynthesizeRzPassImpl() = default;
  FTQCSynthesizeRzPassImpl(const FTQCSynthesizeRzPassImpl &o)
      : PassWrapper(o) { depth = o.depth.getValue(); }
  explicit FTQCSynthesizeRzPassImpl(int d) { depth = d; }

  StringRef getArgument() const override { return "ftqc-synthesize-rz"; }
  StringRef getDescription() const override {
    return "Approximate logical_rz by a Clifford+T sequence (Solovay-Kitaev)";
  }

  Option<unsigned> depth{
      *this, "depth",
      llvm::cl::desc("Solovay-Kitaev recursion depth (higher = more accurate, "
                     "longer sequence)"),
      llvm::cl::init(2)};

  void runOnOperation() override {
    RewritePatternSet patterns(&getContext());
    patterns.add<SynthesizeRz>(&getContext(), (int)depth);
    if (failed(applyPatternsAndFoldGreedily(getOperation(),
                                            std::move(patterns))))
      signalPassFailure();
  }
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createSynthesizeRzPass() {
  return std::make_unique<FTQCSynthesizeRzPassImpl>(2);
}
std::unique_ptr<mlir::Pass> mlir::ftqc::createSynthesizeRzPass(double eps) {
  return std::make_unique<FTQCSynthesizeRzPassImpl>(eps <= 5e-3 ? 3 : 2);
}
