// SPDX-License-Identifier: GPL-3.0-only
// Scenariotest: FM-1-ONE och blonda-motors ONE får exakt samma händelser (toner, pedal, böj, ratt, tryck,
// parameterändringar och ljudbyten mitt i spelet), och utsignalerna jämförs sampel för sampel.
//   c++ -std=c++17 -O2 -I$BLONDA_MOTOR/include -I$BLONDA_MOTOR/motorer -Isrc test/scenarier.cpp src/one_fm1.c
//   build/one-scenarier [--wav mapp]
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
#include "one_fm1.h"
}

static const int SR = ONE_SR;
static const double MAX_SKILLNAD_DB = -30.0;    // differensen ska ligga minst 30 dB under signalen

struct Ev { double t; std::function<void(blonda::Motor&)> ref; std::function<void()> our; };

static int pidx(const char* id) {
    for (int i = 0; i < OP_N; ++i) if (!std::strcmp(ONE_PARAMS[i].id, id)) return i;
    std::fprintf(stderr, "okänd parameter %s\n", id);
    std::exit(1);
}

struct Scenario {
    std::string name;
    int preset = 0;
    double len = 2.0;
    std::vector<Ev> ev;
    Scenario& on(double t, int n, int v = 100) { ev.push_back({t, [=](blonda::Motor& m) { m.noteOn(n, v); }, [=] { one_note_on(n, v); }}); return *this; }
    Scenario& off(double t, int n) { ev.push_back({t, [=](blonda::Motor& m) { m.noteOff(n); }, [=] { one_note_off(n); }}); return *this; }
    Scenario& set(double t, const char* id, double v) {
        int p = pidx(id);
        ev.push_back({t, [=](blonda::Motor& m) { m.setParam(m.findParam(id), v); }, [=] { one_set(p, (float)v); }});
        return *this;
    }
    Scenario& sus(double t, bool d) { ev.push_back({t, [=](blonda::Motor& m) { m.sustain(d); }, [=] { one_sustain(d); }}); return *this; }
    Scenario& bend(double t, double v) { ev.push_back({t, [=](blonda::Motor& m) { m.pitchBend(v); }, [=] { one_bend((float)v); }}); return *this; }
    Scenario& mw(double t, double v) { ev.push_back({t, [=](blonda::Motor& m) { m.modWheel(v); }, [=] { one_modwheel((float)v); }}); return *this; }
    Scenario& at(double t, double v) { ev.push_back({t, [=](blonda::Motor& m) { m.aftertouch(v); }, [=] { one_aftertouch((float)v); }}); return *this; }
    Scenario& preset_(double t, int p) { ev.push_back({t, [=](blonda::Motor& m) { m.loadPreset(p); }, [=] { one_load_preset(p); }}); return *this; }
    Scenario& alloff(double t) { ev.push_back({t, [=](blonda::Motor& m) { m.allNotesOff(); }, [=] { one_all_notes_off(); }}); return *this; }
};

// Händelser läggs mellan block om 256 sampel, som fm1-dock gör (note() i huvudloopen, render() i avbrottet).
static void run(const Scenario& sc, std::vector<float>& aL, std::vector<float>& aR, std::vector<float>& bL, std::vector<float>& bR) {
    auto ref = blonda::createMotor("one");
    ref->prepare(SR);
    ref->loadPreset(sc.preset);
    one_init();
    one_load_preset(sc.preset);
    int total = (int)(sc.len * SR);
    total -= total % 256;
    aL.assign(total, 0); aR.assign(total, 0); bL.assign(total, 0); bR.assign(total, 0);
    size_t k = 0;
    std::vector<Ev> ev = sc.ev;
    std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
    for (int pos = 0; pos < total; pos += 256) {
        while (k < ev.size() && ev[k].t * SR <= pos) { ev[k].ref(*ref); ev[k].our(); ++k; }
        ref->process(&aL[pos], &aR[pos], 256);
        one_render(&bL[pos], &bR[pos], 256);
    }
}

int main(int argc, char** argv) {
    const char* wavdir = (argc > 2 && !std::strcmp(argv[1], "--wav")) ? argv[2] : nullptr;
    std::vector<Scenario> S;
    // ljudens nummer: 1 one, 2 moogbas, 13 unison bas, 15 prog lead, 22 sträv lead, 24 varm pad, 26 pwm-pad, 28 stor poly,
    // 37 pluck, 39 elpiano, 40 ekopluck, 44 stor unison, 47 s&h-bubbel
    { Scenario s; s.name = "ackord, släpp, nytt ackord"; s.preset = 1; s.len = 3;
      s.on(0, 60).on(0, 64).on(0, 67).off(1, 60).off(1, 64).off(1, 67).on(1.5, 62).on(1.5, 65).on(1.5, 69).off(2.5, 62).off(2.5, 65).off(2.5, 69); S.push_back(s); }
    { Scenario s; s.name = "röststöld: 9 toner på 6 röster"; s.preset = 24; s.len = 3;
      for (int i = 0; i < 9; ++i) s.on(i * 0.1, 48 + i * 3, 60 + i * 7);
      for (int i = 0; i < 9; ++i) s.off(1.5 + i * 0.05, 48 + i * 3);
      S.push_back(s); }
    { Scenario s; s.name = "samma ton igen innan den klingat ut"; s.preset = 37; s.len = 2;
      s.on(0, 60).off(0.2, 60).on(0.3, 60).off(0.5, 60).on(0.55, 60, 30).off(1.0, 60); S.push_back(s); }
    { Scenario s; s.name = "mono legato med portamento"; s.preset = 15; s.len = 3;
      s.on(0, 48).on(0.4, 55).on(0.8, 60).off(0.8, 55).off(1.2, 60).on(1.4, 43).off(2.2, 43).off(2.3, 48); S.push_back(s); }
    { Scenario s; s.name = "unison, intervall ändras under spel"; s.preset = 13; s.len = 2.5;
      s.on(0, 41).set(0.5, "uniint", 7).set(1.0, "uniint", -12).set(1.5, "uniint", 0).off(2.0, 41); S.push_back(s); }
    { Scenario s; s.name = "sustainpedal"; s.preset = 39; s.len = 3;
      s.sus(0, true).on(0, 60).off(0.2, 60).on(0.4, 64).off(0.6, 64).on(0.8, 67).off(1.0, 67).sus(1.8, false); S.push_back(s); }
    { Scenario s; s.name = "pitch bend fram och tillbaka"; s.preset = 28; s.len = 2.5;
      s.on(0, 60); for (int i = 0; i <= 40; ++i) s.bend(0.3 + i * 0.04, std::sin(i * 0.4)); s.bend(2.0, 0).off(2.2, 60); S.push_back(s); }
    { Scenario s; s.name = "modhjul och aftertouch"; s.preset = 22; s.len = 3;
      s.on(0, 60); for (int i = 0; i <= 20; ++i) { s.mw(0.2 + i * 0.05, i / 20.0); s.at(1.3 + i * 0.05, i / 20.0); } s.off(2.6, 60); S.push_back(s); }
    { Scenario s; s.name = "filter vrids under spel"; s.preset = 0; s.len = 3;
      s.on(0, 48).on(0, 55).on(0, 60);
      for (int i = 0; i <= 50; ++i) { s.set(0.1 + i * 0.05, "cut", 0.1 + 0.8 * (i / 50.0)); s.set(0.1 + i * 0.05, "res", i / 50.0); }
      s.off(2.7, 48).off(2.7, 55).off(2.7, 60); S.push_back(s); }
    { Scenario s; s.name = "självsvängande filter (res 1)"; s.preset = 26; s.len = 2;
      s.set(0, "res", 1).on(0, 72).off(1.5, 72); S.push_back(s); }
    { Scenario s; s.name = "alla LFO-former, snabb LFO"; s.preset = 0; s.len = 3;
      s.set(0, "lfocut", 0.6).set(0, "lfoosc", 0.3).set(0, "lfopwm", 0.8).set(0, "o1w", 2).set(0, "lrate", 0.8).on(0, 57);
      for (int w = 1; w <= 4; ++w) s.set(w * 0.5, "lwave", w);
      s.set(2.5, "lrate", 1).off(2.8, 57); S.push_back(s); }
    { Scenario s; s.name = "byt ljud mitt i spelet"; s.preset = 1; s.len = 3;
      s.on(0, 60).on(0, 64).preset_(0.8, 44).off(1.2, 60).off(1.2, 64).on(1.4, 52).preset_(2.0, 2).on(2.2, 40).off(2.8, 52).off(2.8, 40); S.push_back(s); }
    { Scenario s; s.name = "röstläge byts (poly, mono, unison)"; s.preset = 0; s.len = 3;
      s.on(0, 60).set(0.5, "lage", 2).on(0.6, 62).off(1.0, 62).set(1.2, "lage", 3).on(1.3, 50).off(2.0, 50).set(2.2, "lage", 1).on(2.3, 67).off(2.8, 67); S.push_back(s); }
    { Scenario s; s.name = "eko: tiden ändras under spel"; s.preset = 40; s.len = 4;
      s.set(0, "dmix", 0.8).on(0, 60).off(0.1, 60).set(0.6, "dtime", 0.8).on(0.7, 64).off(0.8, 64).set(1.6, "dtime", 0.1).on(1.7, 67).off(1.8, 67).set(2.5, "dfb", 0.95); S.push_back(s); }
    { Scenario s; s.name = "ytterlägen: lägsta/högsta ton, velocity 1 och 127"; s.preset = 0; s.len = 2;
      s.on(0, 12, 1).on(0, 115, 127).on(0, 127, 64).on(0.3, 0, 127).off(1.2, 12).off(1.2, 115).off(1.2, 127).off(1.2, 0); S.push_back(s); }
    { Scenario s; s.name = "alla noter av"; s.preset = 24; s.len = 2;
      s.on(0, 60).on(0, 63).on(0, 67).alloff(0.7).on(1.0, 72).off(1.6, 72); S.push_back(s); }
    { Scenario s; s.name = "full volym och full drivning, sex toner"; s.preset = 28; s.len = 2;
      s.set(0, "vol", 1).set(0, "drv", 1).on(0, 36).on(0, 48).on(0, 55).on(0, 60).on(0, 64).on(0, 67).off(1.5, 36).off(1.5, 48).off(1.5, 55).off(1.5, 60).off(1.5, 64).off(1.5, 67); S.push_back(s); }
    { Scenario s; s.name = "oscillatorerna ändras under spel"; s.preset = 1; s.len = 3;
      s.on(0, 55).on(0, 62).set(0.3, "o1w", 0).set(0.6, "o2w", 2).set(0.9, "o3r", 2).set(1.2, "o2s", -5).set(1.5, "o3f", 1)
       .set(1.8, "o2l", 0).set(2.1, "o1r", -2).set(2.4, "o3w", 2).set(2.4, "o3l", 1).off(2.7, 55).off(2.7, 62); S.push_back(s); }
    { Scenario s; s.name = "brus och drivning vrids upp"; s.preset = 0; s.len = 3;
      s.on(0, 45).on(0, 57);
      for (int i = 0; i <= 20; ++i) { s.set(0.2 + i * 0.05, "noi", i / 20.0); s.set(1.3 + i * 0.05, "drv", i / 20.0); }
      s.off(2.6, 45).off(2.6, 57); S.push_back(s); }
    { Scenario s; s.name = "höga toner i 2' och +7: frekvenstaket"; s.preset = 28; s.len = 2;
      s.set(0, "o3r", 2).set(0, "o2s", 7).set(0, "o2r", 1).on(0, 96).on(0.2, 108).on(0.4, 120).off(1.4, 96).off(1.4, 108).off(1.4, 120); S.push_back(s); }
    { Scenario s; s.name = "pulsbredden vrids, PWM på"; s.preset = 26; s.len = 3;
      s.on(0, 50).on(0, 57).on(0, 65);
      for (int i = 0; i <= 40; ++i) s.set(0.2 + i * 0.05, "pw", i / 40.0);
      s.set(2.3, "lfopwm", 1).set(2.3, "lrate", 0.7).off(2.7, 50).off(2.7, 57).off(2.7, 65); S.push_back(s); }
    { Scenario s; s.name = "S&H-LFO på filtret, takten ändras"; s.preset = 47; s.len = 3;
      s.on(0, 48).on(0, 60).set(0.8, "lrate", 0.75).set(1.6, "lrate", 0.4).set(2.0, "lfoamp", 0.8).off(2.6, 48).off(2.6, 60); S.push_back(s); }

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
