// SPDX-License-Identifier: GPL-3.0-only
// Scenariotest: FM-1-PULS och blonda-motors PULS får exakt samma händelser (slag, rattar som vrids, trumset som
// byts mitt i spelet, strypning, pukor i olika höjd, ytterlägen) och utsignalerna jämförs sampel för sampel.
//   c++ -std=c++17 -O2 -I$BLONDA_MOTOR/include -I$BLONDA_MOTOR/motorer -Isrc -I../../gemensamt test/scenarier.cpp build/puls_fm1.o
//   build/puls-scenarier [--wav mapp]
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "blonda/motorer.h"
#include "blonda/wav.h"
extern "C" {
#include "puls_fm1.h"
}

static const int SR = PULS_SR;
static const double MAX_SKILLNAD_DB = -30.0;

struct Ev { double t; std::function<void(blonda::Motor&)> ref; std::function<void()> our; };

static int pidx(const char* id) {
    for (int i = 0; i < PP_N; ++i) if (!std::strcmp(PULS_PARAMS[i].id, id)) return i;
    std::fprintf(stderr, "okänd parameter %s\n", id);
    std::exit(1);
}

struct Scenario {
    std::string name;
    int preset = 0;
    double len = 2.0;
    std::vector<Ev> ev;
    Scenario& on(double t, int n, int v = 100) { ev.push_back({t, [=](blonda::Motor& m) { m.noteOn(n, v); }, [=] { puls_note_on(n, v); }}); return *this; }
    Scenario& set(double t, const char* id, double v) {
        int p = pidx(id);
        ev.push_back({t, [=](blonda::Motor& m) { m.setParam(m.findParam(id), v); }, [=] { puls_set(p, (float)v); }});
        return *this;
    }
    Scenario& preset_(double t, int p) { ev.push_back({t, [=](blonda::Motor& m) { m.loadPreset(p); }, [=] { puls_load_preset(p); }}); return *this; }
    // ett trummönster: "36:0,4,8,12;38:4,12" i sextondelar från t0, takter gånger
    Scenario& beat(double t0, double bpm, int takter, const char* m) {
        const double per = 60.0 / bpm / 4.0;
        std::string s = m;
        size_t a = 0;
        while (a < s.size()) {
            size_t e = s.find(';', a); if (e == std::string::npos) e = s.size();
            std::string part = s.substr(a, e - a);
            int note = std::atoi(part.c_str());
            size_t c = part.find(':');
            for (size_t q = c + 1; q < part.size();) {
                int st = std::atoi(part.c_str() + q);
                for (int t = 0; t < takter; ++t) on(t0 + (t * 16 + st) * per, note, st % 4 == 0 ? 120 : 90);
                size_t k = part.find(',', q); if (k == std::string::npos) break; q = k + 1;
            }
            a = e + 1;
        }
        return *this;
    }
};

// händelser mellan block om 256 sampel, som fm1-dock (note() i huvudloopen, render() i avbrottet)
static void run(const Scenario& sc, std::vector<float>& aL, std::vector<float>& aR, std::vector<float>& bL, std::vector<float>& bR) {
    auto ref = blonda::createMotor("puls");
    ref->prepare(SR);
    ref->loadPreset(sc.preset);
    puls_init();
    puls_load_preset(sc.preset);
    int total = (int)(sc.len * SR);
    total -= total % 256;
    aL.assign(total, 0); aR.assign(total, 0); bL.assign(total, 0); bR.assign(total, 0);
    size_t k = 0;
    std::vector<Ev> ev = sc.ev;
    std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
    for (int pos = 0; pos < total; pos += 256) {
        while (k < ev.size() && ev[k].t * SR <= pos) { ev[k].ref(*ref); ev[k].our(); ++k; }
        ref->process(&aL[pos], &aR[pos], 256);
        puls_render(&bL[pos], &bR[pos], 256);
    }
}

int main(int argc, char** argv) {
    const char* wavdir = (argc > 2 && !std::strcmp(argv[1], "--wav")) ? argv[2] : nullptr;
    static const char* BEAT = "36:0,6,8;38:4,12;42:0,2,4,6,8,10,14;46:14;39:12;37:10;45:15;56:7";
    std::vector<Scenario> S;
    { Scenario s; s.name = "trummönster, Klassisk, 120 bpm"; s.preset = 1; s.len = 4; s.beat(0, 120, 2, BEAT); S.push_back(s); }
    { Scenario s; s.name = "trummönster, Hård, 150 bpm"; s.preset = 2; s.len = 3.5; s.beat(0, 150, 2, BEAT); S.push_back(s); }
    { Scenario s; s.name = "varje trumljud för sig, styrka 1 och 127"; s.preset = 0; s.len = 5;
      const int n[8] = {36, 38, 37, 39, 42, 46, 41, 56};
      for (int i = 0; i < 8; ++i) { s.on(i * 0.6, n[i], 1); s.on(i * 0.6 + 0.3, n[i], 127); } S.push_back(s); }
    { Scenario s; s.name = "stängd hihat stryper den öppna"; s.preset = 6; s.len = 2.5;
      s.on(0, 46).on(0.25, 42).on(0.5, 46).on(1.2, 46).on(1.3, 46).on(1.31, 42).on(1.8, 46, 60); S.push_back(s); }
    { Scenario s; s.name = "pukor i fyra höjder, snabbt"; s.preset = 5; s.len = 2;
      for (int i = 0; i < 16; ++i) s.on(i * 0.07, (const int[]){41, 43, 45, 47}[i % 4], 70 + i * 3); S.push_back(s); }
    { Scenario s; s.name = "snabba omslag (32-delar) på BD, SD, CP"; s.preset = 3; s.len = 2;
      for (int i = 0; i < 24; ++i) { s.on(i * 0.0625, 36, 100); if (i % 3 == 0) s.on(i * 0.0625, 38, 90); if (i % 5 == 0) s.on(i * 0.0625, 39, 110); } S.push_back(s); }
    { Scenario s; s.name = "alla rattar vrids under spel"; s.preset = 1; s.len = 4; s.beat(0, 120, 2, BEAT);
      for (int i = 0; i <= 40; ++i) {
          double x = i / 40.0, t = 0.05 + i * 0.09;
          for (const char* id : {"bdtun", "sdton", "rsdec", "cptun", "chton", "ohdec", "tmtun", "cbton"}) s.set(t, id, x);
          s.set(t, "bdton", 1 - x); s.set(t, "sddec", 1 - x); s.set(t, "drv", x * 0.6);
      }
      S.push_back(s); }
    { Scenario s; s.name = "byt trumset mitt i mönstret"; s.preset = 1; s.len = 4; s.beat(0, 120, 2, BEAT);
      s.preset_(0.9, 7).preset_(1.9, 4).preset_(2.6, 9).preset_(3.3, 8); S.push_back(s); }
    { Scenario s; s.name = "full volym och drivning, allt på en gång"; s.preset = 7; s.len = 2;
      s.set(0, "vol", 1).set(0, "drv", 1);
      for (int n : {36, 38, 37, 39, 42, 41, 56}) s.on(0, n, 127);
      for (int n : {36, 38, 37, 39, 46, 47, 56}) s.on(1.0, n, 127);
      S.push_back(s); }
    { Scenario s; s.name = "längsta klangen på allt"; s.preset = 0; s.len = 4;
      for (const char* id : {"bddec", "sddec", "rsdec", "cpdec", "chdec", "ohdec", "tmdec", "cbdec"}) s.set(0, id, 1);
      for (int n : {36, 38, 37, 39, 46, 45, 56}) s.on(0, n, 110);
      S.push_back(s); }
    { Scenario s; s.name = "ytterlägen: stämning 0 och 1, karaktär 0 och 1"; s.preset = 0; s.len = 4;
      for (int k = 0; k < 2; ++k) {
          double t = k * 2.0;
          for (const char* id : {"bdtun", "sdtun", "rstun", "cptun", "chtun", "ohtun", "tmtun", "cbtun"}) s.set(t, id, k);
          for (const char* id : {"bdton", "sdton", "rston", "cpton", "chton", "ohton", "tmton", "cbton"}) s.set(t, id, 1 - k);
          for (int i = 0; i < 8; ++i) s.on(t + i * 0.22, (const int[]){36, 38, 37, 39, 42, 46, 41, 56}[i], 120);
      }
      S.push_back(s); }

    int bad = 0;
    std::printf("%-50s %12s %10s\n", "scenario", "skillnad dB", "topp dBFS");
    for (auto& sc : S) {
        std::vector<float> aL, aR, bL, bR;
        run(sc, aL, aR, bL, bR);
        double e = 0, d = 0, pk = 0;
        for (size_t i = 0; i < aL.size(); ++i) {
            e += (double)aL[i] * aL[i] + (double)aR[i] * aR[i];
            double x = (double)aL[i] - bL[i], y = (double)aR[i] - bR[i];
            d += x * x + y * y;
            pk = std::max(pk, (double)std::max(std::fabs(bL[i]), std::fabs(bR[i])));
            if (!std::isfinite(bL[i]) || !std::isfinite(bR[i])) { d = 1e30; break; }
        }
        double db = 10 * std::log10((d + 1e-30) / (e + 1e-30));
        bool ok = db <= MAX_SKILLNAD_DB && pk <= 1.0;
        if (!ok) ++bad;
        std::printf("%-50s %12.1f %10.1f  %s\n", sc.name.c_str(), db, 20 * std::log10(pk + 1e-12), ok ? "ok" : "AVVIKER");
        if (wavdir) {
            std::string base = std::string(wavdir) + "/" + std::to_string(&sc - &S[0]);
            blonda::writeWavFloat((base + "-blonda-motor.wav").c_str(), aL, aR, SR);
            blonda::writeWavFloat((base + "-fm1.wav").c_str(), bL, bR, SR);
        }
    }
    std::printf("\ngräns: skillnaden minst %.0f dB under signalen\n", -MAX_SKILLNAD_DB);
    if (bad) { std::printf("%d av %zu scenarier avviker\n", bad, S.size()); return 1; }
    std::printf("alla %zu scenarier ok\n", S.size());
    return 0;
}
