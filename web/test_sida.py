#!/usr/bin/env python3
"""Provkör webbemulatorn i en riktig Chrome (headless): skärmen visas direkt, ljudet startar, tangenter, rattar,
knappar, oktav, datorns tangentbord och SAVE fungerar, och ljudet når högtalarna (mäts med en AnalyserNode).
Sparar skärmbilder av sidan i build/webb-*.png.
  python3 web/test_sida.py [web/index.html]
"""
import http.server
import os
import sys
import threading

from playwright.sync_api import sync_playwright

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PAGE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "index.html")
OUT = os.path.join(ROOT, "build")
os.makedirs(OUT, exist_ok=True)

html = open(PAGE, encoding="utf-8").read()
SKELETON = ('<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, '
            'initial-scale=1, viewport-fit=cover"><style>:root{padding-top:env(safe-area-inset-top);'
            'padding-bottom:env(safe-area-inset-bottom)}body{margin:0;font:14px system-ui}[hidden]{display:none!important}'
            '</style></head><body>' + html + '</body></html>')


class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.end_headers()
        self.wfile.write(SKELETON.encode())

    def log_message(self, *a):
        pass


srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), H)
threading.Thread(target=srv.serve_forever, daemon=True).start()
url = f"http://127.0.0.1:{srv.server_port}/"

fails = []


def check(ok, msg):
    print(("ok: " if ok else "FEL: ") + msg)
    if not ok:
        fails.append(msg)


# mäter nivån på det som går till högtalarna: kopplar en AnalyserNode efter modulen
TAP = """() => new Promise(res => {
  const n = window.__tap || (window.__tap = (() => {
    const c = window.__ctx(); if (!c) return null;
    const a = c.createAnalyser(); a.fftSize = 2048; window.__node().connect(a); return a; })());
  if (!n) return res(-1);
  const d = new Float32Array(n.fftSize); let pk = 0, k = 0;
  const t = setInterval(() => { n.getFloatTimeDomainData(d); for (const v of d) pk = Math.max(pk, Math.abs(v)); if (++k > 8) { clearInterval(t); res(pk); } }, 40);
})"""

with sync_playwright() as p:
    b = p.chromium.launch(executable_path="/usr/bin/google-chrome", args=["--autoplay-policy=no-user-gesture-required"])
    pg = b.new_page(viewport={"width": 1100, "height": 1000})
    errs = []
    pg.on("console", lambda m: errs.append(m.text) if m.type == "error" else None)
    pg.on("pageerror", lambda e: errs.append(str(e)))
    # testkrokar: ljudkontexten och noden (sidan sätter dem i globala let; läses via en liten patch)
    pg.add_init_script("window.__ctx = () => null; window.__node = () => null;")
    pg.goto(url)
    pg.wait_for_timeout(1200)
    pg.evaluate("() => { window.__ctx = () => ctx; window.__node = () => node || spn; }")
    lit = pg.evaluate("""() => { const g = document.getElementById('lcd').getContext('2d').getImageData(0,0,240,240).data;
      let y = 0; for (let i = 0; i < g.length; i += 4) if (g[i] > 200 && g[i+1] > 150 && g[i+2] < 60) y++; return y; }""")
    check(lit > 2000, f"skärmen visar BRIS innan ljudet startat ({lit} gula punkter)")
    pg.screenshot(path=os.path.join(OUT, "webb-1-start.png"))

    pg.click("#start")
    pg.wait_for_timeout(1500)
    st = pg.inner_text("#st-audio")
    check("LJUD PÅ" in st, f"ljudet startar ({st.strip()}, AudioWorklet: {pg.evaluate('() => !!node')})")
    check(pg.evaluate("() => ctx && ctx.sampleRate") == 44100, "44,1 kHz som på FM-1")

    quiet = pg.evaluate(TAP)
    check(0 <= quiet < 0.01, f"tyst utan toner ({quiet:.4f})")
    # tre tangenter med musen (håll) + en med datorns tangentbord
    keys = pg.query_selector_all(".key")
    box = keys[7].bounding_box()
    pg.mouse.move(box["x"] + box["width"] / 2, box["y"] + box["height"] * 0.7)
    pg.mouse.down()
    pg.keyboard.down("KeyD")
    pg.keyboard.down("KeyG")
    pg.wait_for_timeout(300)
    lvl = pg.evaluate(TAP)
    check(lvl > 0.05, f"ljud när tangenter hålls ({lvl:.3f})")
    on = pg.evaluate("() => document.querySelectorAll('.key.on').length")
    check(on == 3, f"tre tangenter lyser ({on})")
    pg.wait_for_timeout(200)
    pg.screenshot(path=os.path.join(OUT, "webb-2-spelar.png"))
    voices = pg.evaluate("""() => { const g = document.getElementById('lcd').getContext('2d').getImageData(170, 86, 66, 8).data;
      let n = 0; for (let r = 0; r < 6; r++) { const i = (4 * 240 * 0 + (r * 11 + 4) * 4); if (g[i] > 200) n++; } return n; }""")
    check(voices == 3, f"skärmen visar tre röster som låter ({voices})")
    pg.mouse.up(); pg.keyboard.up("KeyD"); pg.keyboard.up("KeyG")
    pg.wait_for_timeout(200)
    check(pg.evaluate("() => document.querySelectorAll('.key.on').length") == 0, "tangenterna släpps")

    def lcd_hash():
        return pg.evaluate("() => { const g = document.getElementById('lcd').getContext('2d').getImageData(0,24,240,216).data; let h = 0; for (let i = 0; i < g.length; i += 7) h = (h * 31 + g[i]) | 0; return h; }")

    # PRESETS-ratten: rulla ett steg -> nytt ljud på skärmen
    h0 = lcd_hash()
    kp = pg.query_selector('.knob[data-enc="6"]').bounding_box()
    pg.mouse.move(kp["x"] + 29, kp["y"] + 29)
    pg.mouse.wheel(0, -100)
    pg.wait_for_timeout(250)
    check(lcd_hash() != h0, "PRESETS byter ljud på skärmen")
    # KNOB1: dra uppåt -> cutoff ändras
    h1 = lcd_hash()
    k1 = pg.query_selector('.knob[data-enc="2"]').bounding_box()
    pg.mouse.move(k1["x"] + 29, k1["y"] + 29); pg.mouse.down(); pg.mouse.move(k1["x"] + 29, k1["y"] - 60, steps=8); pg.mouse.up()
    pg.wait_for_timeout(250)
    check(lcd_hash() != h1, "KNOB1 ändrar värdet (dra)")
    # sidknapp: FX -> lysdioden tänds
    pg.click("text=FX")
    pg.wait_for_timeout(250)
    led = pg.evaluate("() => [...document.querySelectorAll('.btn')].find(b => b.textContent === 'FX').dataset.led")
    check(led == "1", f"FX-knappen tänds och visar FX-sidan (led {led})")
    pg.screenshot(path=os.path.join(OUT, "webb-3-fx.png"))
    # OCT+ -> värdens rad visar OCT +1
    h2 = pg.evaluate("() => { const g = document.getElementById('lcd').getContext('2d').getImageData(160,0,80,24).data; let h=0; for (const v of g) h=(h*31+v)|0; return h; }")
    pg.click("#b-octp"); pg.wait_for_timeout(250)
    h3 = pg.evaluate("() => { const g = document.getElementById('lcd').getContext('2d').getImageData(160,0,80,24).data; let h=0; for (const v of g) h=(h*31+v)|0; return h; }")
    check(h2 != h3, "OCT+ syns överst på skärmen")
    # SAVE -> sparas i webbläsaren
    pg.click("text=SAVE"); pg.wait_for_timeout(400)
    saved = pg.evaluate("() => { try { return !!localStorage.getItem('blonda-fm1-bris'); } catch (_) { return false; } }")
    check(saved, "SAVE sparar ljudet i webbläsaren")

    # mobil bredd: ingen sidledsrullning
    pg.set_viewport_size({"width": 390, "height": 860})
    pg.wait_for_timeout(300)
    sw = pg.evaluate("() => document.documentElement.scrollWidth")
    check(sw <= 390, f"mobilbredd: ingen sidledsrullning ({sw}px)")
    pg.screenshot(path=os.path.join(OUT, "webb-4-mobil.png"), full_page=True)
    # typsnitten från Google Fonts når inte testmiljön: det felet räknas inte
    errs = [e for e in errs if "ERR_TUNNEL_CONNECTION_FAILED" not in e and "fonts.g" not in e]
    check(not errs, "inga fel i konsolen" + ("" if not errs else ": " + " | ".join(errs[:3])))
    b.close()

srv.shutdown()
print("\nalla kontroller ok" if not fails else f"\n{len(fails)} fel")
sys.exit(1 if fails else 0)
