// SPDX-License-Identifier: GPL-3.0-only
// Scenariotest: FM-1-BRIS och blonda-motors BRIS får exakt samma händelser (toner, pedal, böj, ratt, tryck,
// parameterändringar och ljudbyten mitt i spelet), och utsignalerna jämförs sampel för sampel.
//   c++ -std=c++17 -O2 -I$BLONDA_MOTOR/include -I$BLONDA_MOTOR/motorer -Isrc test/scenarier.cpp src/bris_fm1.c
//   build/bris-scenarier [--wav mapp]
// Ett scenario underkänns om skillnaden (RMS av differensen mot RMS av originalet) är större än gränsen.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "blonda/motorer.h"
#include "blonda/wav.h"
extern "C" {
#include "bris_fm1.h"
}

static const int SR = BRIS_SR;
static const double MAX_SKILLNAD_DB = -30.0;    // differensen ska ligga minst 30 dB under signalen

struct Ev { double t; std::function<void(blonda::Motor&)> ref; std::function<void()> our; };

static int pidx(const char* id) {
    for (int i = 0; i < BP_N; ++i) if (!std::strcmp(BRIS_PARAMS[i].id, id)) return i;
    std::fprintf(stderr, "okänd parameter %s\n", id);
    std::exit(1);
}

struct Scenario {
    std::string name;
    int preset = 0;
    double len = 2.0;
    std::vector<Ev> ev;
    Scenario& on(double t, int n, int v = 100) { ev.push_back({t, [=](blonda::Motor& m) { m.noteOn(n, v); }, [=] { bris_note_on(n, v); }}); return *this; }
    Scenario& off(double t, int n) { ev.push_back({t, [=](blonda::Motor& m) { m.noteOff(n); }, [=] { bris_note_off(n); }}); return *this; }
    Scenario& set(double t, const char* id, double v) {
        int p = pidx(id);
        ev.push_back({t, [=](blonda::Motor& m) { m.setParam(m.findParam(id), v); }, [=] { bris_set(p, (float)v); }});
        return *this;
    }
    Scenario& sus(double t, bool d) { ev.push_back({t, [=](blonda::Motor& m) { m.sustain(d); }, [=] { bris_sustain(d); }}); return *this; }
    Scenario& bend(double t, double v) { ev.push_back({t, [=](blonda::Motor& m) { m.pitchBend(v); }, [=] { bris_bend((float)v); }}); return *this; }
    Scenario& mw(double t, double v) { ev.push_back({t, [=](blonda::Motor& m) { m.modWheel(v); }, [=] { bris_modwheel((float)v); }}); return *this; }
    Scenario& at(double t, double v) { ev.push_back({t, [=](blonda::Motor& m) { m.aftertouch(v); }, [=] { bris_aftertouch((float)v); }}); return *this; }
    Scenario& preset_(double t, int p) { ev.push_back({t, [=](blonda::Motor& m) { m.loadPreset(p); }, [=] { bris_load_preset(p); }}); return *this; }
    Scenario& alloff(double t) { ev.push_back({t, [=](blonda::Motor& m) { m.allNotesOff(); }, [=] { bris_all_notes_off(); }}); return *this; }
};

// Händelser läggs mellan block om 256 sampel, som fm1-dock gör (note() i huvudloopen, render() i avbrottet).
static void run(const Scenario& sc, std::vector<float>& aL, std::vector<float>& aR, std::vector<float>& bL, std::vector<float>& bR) {
    auto ref = blonda::createMotor("bris");
    ref->prepare(SR);
    ref->loadPreset(sc.preset);
    bris_init();
    bris_load_preset(sc.preset);
    int total = (int)(sc.len * SR);
    total -= total % 256;
    aL.assign(total, 0); aR.assign(total, 0); bL.assign(total, 0); bR.assign(total, 0);
    size_t k = 0;
    std::vector<Ev> ev = sc.ev;
    std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
    for (int pos = 0; pos < total; pos += 256) {
        while (k < ev.size() && ev[k].t * SR <= pos) { ev[k].ref(*ref); ev[k].our(); ++k; }
        ref->process(&aL[pos], &aR[pos], 256);
        bris_render(&bL[pos], &bR[pos], 256);
    }
}

int main(int argc, char** argv) {
    const char* wavdir = (argc > 2 && !std::strcmp(argv[1], "--wav")) ? argv[2] : nullptr;
    std::vector<Scenario> S;
    { Scenario s; s.name = "ackord, släpp, nytt ackord"; s.preset = 1; s.len = 3;
      s.on(0, 60).on(0, 64).on(0, 67).off(1, 60).off(1, 64).off(1, 67).on(1.5, 62).on(1.5, 65).on(1.5, 69).off(2.5, 62).off(2.5, 65).off(2.5, 69); S.push_back(s); }
    { Scenario s; s.name = "röststöld: 9 toner på 6 röster"; s.preset = 4; s.len = 3;
      for (int i = 0; i < 9; ++i) s.on(i * 0.1, 48 + i * 3, 60 + i * 7);
      for (int i = 0; i < 9; ++i) s.off(1.5 + i * 0.05, 48 + i * 3);
      S.push_back(s); }
    { Scenario s; s.name = "samma ton igen innan den klingat ut"; s.preset = 77; s.len = 2;
      s.on(0, 60).off(0.2, 60).on(0.3, 60).off(0.5, 60).on(0.55, 60, 30).off(1.0, 60); S.push_back(s); }
    { Scenario s; s.name = "mono legato med portamento"; s.preset = 74; s.len = 3;
      s.on(0, 48).on(0.4, 55).on(0.8, 60).off(0.8, 55).off(1.2, 60).on(1.4, 43).off(2.2, 43).off(2.3, 48); S.push_back(s); }
    { Scenario s; s.name = "unison, intervall ändras under spel"; s.preset = 5; s.len = 2.5;
      s.on(0, 41).set(0.5, "uniint", 7).set(1.0, "uniint", -12).set(1.5, "uniint", 0).off(2.0, 41); S.push_back(s); }
    { Scenario s; s.name = "sustainpedal"; s.preset = 79; s.len = 3;
      s.sus(0, true).on(0, 60).off(0.2, 60).on(0.4, 64).off(0.6, 64).on(0.8, 67).off(1.0, 67).sus(1.8, false); S.push_back(s); }
    { Scenario s; s.name = "pitch bend fram och tillbaka"; s.preset = 13; s.len = 2.5;
      s.on(0, 60); for (int i = 0; i <= 40; ++i) s.bend(0.3 + i * 0.04, std::sin(i * 0.4)); s.bend(2.0, 0).off(2.2, 60); S.push_back(s); }
    { Scenario s; s.name = "modhjul och aftertouch"; s.preset = 64; s.len = 3;
      s.on(0, 60); for (int i = 0; i <= 20; ++i) { s.mw(0.2 + i * 0.05, i / 20.0); s.at(1.3 + i * 0.05, i / 20.0); } s.off(2.6, 60); S.push_back(s); }
    { Scenario s; s.name = "filter vrids under spel"; s.preset = 0; s.len = 3;
      s.on(0, 48).on(0, 55).on(0, 60);
      for (int i = 0; i <= 50; ++i) { s.set(0.1 + i * 0.05, "cut", 0.1 + 0.8 * (i / 50.0)); s.set(0.1 + i * 0.05, "res", i / 50.0); }
      s.off(2.7, 48).off(2.7, 55).off(2.7, 60); S.push_back(s); }
    { Scenario s; s.name = "självsvängande filter (res 1)"; s.preset = 84; s.len = 2;
      s.set(0, "res", 1).on(0, 72).off(1.5, 72); S.push_back(s); }
    { Scenario s; s.name = "alla LFO-former, snabb LFO"; s.preset = 0; s.len = 3;
      s.set(0, "lfocut", 0.6).set(0, "lfoosc", 0.3).set(0, "lfopwm", 0.8).set(0, "vform", 1).set(0, "lrate", 0.8).on(0, 57);
      for (int w = 1; w <= 4; ++w) s.set(w * 0.5, "lwave", w);
      s.set(2.5, "lrate", 1).off(2.8, 57); S.push_back(s); }
    { Scenario s; s.name = "byt ljud mitt i spelet"; s.preset = 1; s.len = 3;
      s.on(0, 60).on(0, 64).preset_(0.8, 63).off(1.2, 60).off(1.2, 64).on(1.4, 52).preset_(2.0, 5).on(2.2, 40).off(2.8, 52).off(2.8, 40); S.push_back(s); }
    { Scenario s; s.name = "röstläge byts (poly, mono, unison)"; s.preset = 0; s.len = 3;
      s.on(0, 60).set(0.5, "lage", 2).on(0.6, 62).off(1.0, 62).set(1.2, "lage", 3).on(1.3, 50).off(2.0, 50).set(2.2, "lage", 1).on(2.3, 67).off(2.8, 67); S.push_back(s); }
    { Scenario s; s.name = "eko: tiden ändras under spel"; s.preset = 78; s.len = 4;
      s.set(0, "dmix", 0.8).on(0, 60).off(0.1, 60).set(0.6, "dtime", 0.8).on(0.7, 64).off(0.8, 64).set(1.6, "dtime", 0.1).on(1.7, 67).off(1.8, 67).set(2.5, "dfb", 0.95); S.push_back(s); }
    { Scenario s; s.name = "ytterlägen: lägsta/högsta ton, velocity 1 och 127"; s.preset = 0; s.len = 2;
      s.on(0, 12, 1).on(0, 115, 127).on(0, 127, 64).on(0.3, 0, 127).off(1.2, 12).off(1.2, 115).off(1.2, 127).off(1.2, 0); S.push_back(s); }
    { Scenario s; s.name = "alla noter av"; s.preset = 4; s.len = 2;
      s.on(0, 60).on(0, 63).on(0, 67).alloff(0.7).on(1.0, 72).off(1.6, 72); S.push_back(s); }
    { Scenario s; s.name = "full volym, drive"; s.preset = 12; s.len = 2;
      s.set(0, "vol", 1).on(0, 36).on(0, 48).on(0, 55).on(0, 60).on(0, 64).on(0, 67).off(1.5, 36).off(1.5, 48).off(1.5, 55).off(1.5, 60).off(1.5, 64).off(1.5, 67); S.push_back(s); }

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
