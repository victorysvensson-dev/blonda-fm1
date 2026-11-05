// SPDX-License-Identifier: GPL-3.0-only
// SLITEN för FM-1 mot blonda-motors SLITEN (samma GenExpr som Max-pluggen): samma testsignal, samma parametrar,
// alla nio media. Skriver differensen och båda signalerna som WAV; sliten_test.py bedömer.
//   c++ -std=c++17 -O2 -I$BLONDA_MOTOR/include -I$BLONDA_MOTOR/motorer -Igemensamt -Igemensamt/effekter \
//       gemensamt/test/sliten_test.cpp gemensamt/effekter/sliten_fm1.c -o build/sliten-test
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "blonda/effekter.h"
#include "blonda/wav.h"
extern "C" {
#include "sliten_fm1.h"
}

static const int SR = SLITEN_SR;

// testsignal: ett ackord av sågtänder med hölje och en kort tystnad (för brus och avklingning), lite brus
static void signal(std::vector<float>& L, std::vector<float>& R, int n) {
    L.assign(n, 0); R.assign(n, 0);
    const double f[3] = {220.0, 277.18, 329.63};
    double ph[3] = {0, 0.3, 0.6};
    uint32_t s = 12345;
    for (int i = 0; i < n; ++i) {
        double t = (double)i / SR, env = t < 1.6 ? std::min(1.0, t * 20) * (0.6 + 0.4 * std::cos(t * 3)) : std::exp(-(t - 1.6) * 6);
        double x = 0;
        for (int k = 0; k < 3; ++k) { ph[k] += f[k] / SR; ph[k] -= std::floor(ph[k]); x += 2 * ph[k] - 1; }
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        double nz = ((double)s / 4294967296.0 - 0.5) * 0.02;
        L[i] = (float)((x * 0.18 + nz) * env);
        R[i] = (float)((x * 0.15 - nz) * env);
    }
}

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "build/sliten";
    struct Case { const char* name; std::vector<std::pair<const char*, double>> p; };
    std::vector<Case> C = {
        {"kassett (media 0, standard)", {{"media", 0}}},
        {"kassett slitet (age 1, sat 0.8)", {{"media", 0}, {"age", 1}, {"sat", 0.8}, {"brus", 0.6}}},
        {"VHS (1)", {{"media", 1}}},
        {"digital (2), avbrott av", {{"media", 2}, {"avbr", 0}}},
        {"digital (2) lofi, avbrott av", {{"media", 2}, {"age", 1}, {"avbr", 0}}},
        {"vinyl (3)", {{"media", 3}}},
        {"rulle (4)", {{"media", 4}}},
        {"stenkaka (5)", {{"media", 5}}},
        {"diktafon (6)", {{"media", 6}}},
        {"bilradio (7)", {{"media", 7}}},
        {"telefon (8), avbrott av", {{"media", 8}, {"avbr", 0}}},
        {"kassett, mörk ton, smal", {{"media", 0}, {"ton", 0.1}, {"bredd", 0.2}, {"wet", 0.6}}},
    };
    const char* ids[SL_NPARAM] = {"media", "age", "wow", "flut", "sat", "brus", "avbr", "ton", "bredd", "wet", "outv"};
    int n = (int)(2.6 * SR);
    n -= n % 256;
    std::vector<float> inL, inR;
    signal(inL, inR, n);
    std::printf("%-34s %12s\n", "fall", "skillnad dB");
    for (size_t c = 0; c < C.size(); ++c) {
        auto ref = blonda::createEffect("sliten");
        ref->prepare(SR);
        static sliten_t our;
        sliten_init(&our);
        for (auto& kv : C[c].p) {
            ref->setParam(ref->findParam(kv.first), kv.second);
            for (int k = 0; k < SL_NPARAM; ++k) if (std::string(ids[k]) == kv.first) sliten_set(&our, k, (float)kv.second);
        }
        std::vector<float> aL = inL, aR = inR, bL = inL, bR = inR;
        for (int p = 0; p < n; p += 256) { ref->process(&aL[p], &aR[p], 256); sliten_process(&our, &bL[p], &bR[p], 256); }
        double e = 0, d = 0;
        for (int i = 0; i < n; ++i) {
            e += (double)aL[i] * aL[i] + (double)aR[i] * aR[i];
            double x = (double)aL[i] - bL[i], y = (double)aR[i] - bR[i];
            d += x * x + y * y;
            if (!std::isfinite(bL[i]) || !std::isfinite(bR[i])) { d = 1e30; break; }
        }
        std::printf("%-34s %12.1f\n", C[c].name, 10 * std::log10((d + 1e-30) / (e + 1e-30)));
        std::string base = std::string(dir) + "/" + std::to_string(c);
        blonda::writeWavFloat((base + "-blonda-motor.wav").c_str(), aL, aR, SR);
        blonda::writeWavFloat((base + "-fm1.wav").c_str(), bL, bR, SR);
    }
    return 0;
}
