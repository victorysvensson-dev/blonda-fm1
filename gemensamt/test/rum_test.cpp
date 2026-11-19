// SPDX-License-Identifier: GPL-3.0-only
// RUM för FM-1 mot blonda-motors RUM (samma GenExpr som Max-pluggen): samma testsignal, samma parametrar.
// Skriver differensen och båda signalerna som WAV; rum_test.py bedömer.
//   c++ -std=c++17 -O2 -I$BLONDA_MOTOR/include -I$BLONDA_MOTOR/motorer -Igemensamt -Igemensamt/effekter \
//       gemensamt/test/rum_test.cpp gemensamt/effekter/rum_fm1.c -o build/rum-test
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "blonda/effekter.h"
#include "blonda/wav.h"
extern "C" {
#include "rum_fm1.h"
}

static const int SR = RUM_SR;

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
    const char* dir = argc > 1 ? argv[1] : "build/rum";
    struct Case { const char* name; std::vector<std::pair<const char*, double>> p; std::vector<std::pair<double, std::pair<const char*, double>>> later; };
    std::vector<Case> C = {
        {"standard", {}, {}},
        {"bara klang (mix 1)", {{"wet", 1}}, {}},
        {"stor hall, lång klang", {{"wet", 1}, {"size", 1}, {"decay", 0.85}, {"damp", 0.2}}, {}},
        {"litet rum, kort", {{"wet", 1}, {"size", 0.1}, {"decay", 0.2}, {"damp", 0.7}}, {}},
        {"typ 2 (shape 1)", {{"wet", 1}, {"shape", 1}}, {}},
        {"utan rörelse", {{"wet", 1}, {"modd", 0}}, {}},
        {"full rörelse", {{"wet", 1}, {"modd", 1}, {"decay", 0.7}}, {}},
        {"mörk: höskärning 0.3, dämpad", {{"wet", 1}, {"highc", 0.3}, {"damp", 1}}, {}},
        {"ljus: odämpad, lågskärning 1", {{"wet", 1}, {"damp", 0}, {"lowc", 1}}, {}},
        {"lång predelay, smal", {{"wet", 0.6}, {"predel", 1}, {"width", 0.2}}, {}},
        {"ingen predelay", {{"wet", 1}, {"predel", 0}}, {}},
        {"ducking 1", {{"wet", 0.5}, {"duck", 1}, {"decay", 0.7}}, {}},
        {"mix 0.75, utnivå 1", {{"wet", 0.75}, {"outv", 1}}, {}},
        {"längsta klang, störst", {{"wet", 1}, {"size", 1}, {"decay", 1}, {"damp", 0}, {"lowc", 0}}, {}},
        {"storleken vrids under spel", {{"wet", 1}, {"size", 0.2}}, {{0.5, {"size", 0.9}}, {1.3, {"size", 0.4}}, {1.8, {"decay", 0.8}}}},
    };
    const char* ids[RU_NPARAM] = {"size", "decay", "damp", "predel", "lowc", "modd", "width", "wet", "duck", "outv", "shape", "highc"};
    int n = (int)(4.0 * SR);
    n -= n % 256;
    std::vector<float> inL, inR;
    signal(inL, inR, n);
    std::printf("%-34s %12s\n", "fall", "skillnad dB");
    for (size_t c = 0; c < C.size(); ++c) {
        auto ref = blonda::createEffect("rum");
        ref->prepare(SR);
        static rum_t our;
        rum_init(&our);
        auto set = [&](const char* id, double v) {
            ref->setParam(ref->findParam(id), v);
            for (int k = 0; k < RU_NPARAM; ++k) if (std::string(ids[k]) == id) rum_set(&our, k, (float)v);
        };
        for (auto& kv : C[c].p) set(kv.first, kv.second);
        std::vector<float> aL = inL, aR = inR, bL = inL, bR = inR;
        for (int p = 0; p < n; p += 256) {
            for (auto& ev : C[c].later) { int at = (int)(ev.first * SR); at -= at % 256; if (at == p) set(ev.second.first, ev.second.second); }
            ref->process(&aL[p], &aR[p], 256); rum_process(&our, &bL[p], &bR[p], 256);
        }
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
