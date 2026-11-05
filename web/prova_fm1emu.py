#!/usr/bin/env python3
"""Provar Pages-sidan (fm1-emu med fm1-dock och BRIS) i Chrome innan den publiceras: startar en lokal server för
sida/, öppnar try-outen, väntar tills BRIS har startat och tar en skärmbild. Skärmbilden skrivs till loggen som
base64 (rad som börjar med "BILD sida.png") och ut/sida.png.
  python3 web/prova_fm1emu.py SIDA_MAPP UT_MAPP
"""
import base64
import functools
import http.server
import os
import sys
import threading

from playwright.sync_api import sync_playwright

root, ut = sys.argv[1], sys.argv[2]
srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(http.server.SimpleHTTPRequestHandler, directory=root))
threading.Thread(target=srv.serve_forever, daemon=True).start()
url = f"http://127.0.0.1:{srv.server_port}/"

with sync_playwright() as p:
    b = p.chromium.launch(args=["--autoplay-policy=no-user-gesture-required"])
    pg = b.new_page(viewport={"width": 1280, "height": 900})
    errs, logs = [], []
    pg.on("console", lambda m: (errs if m.type == "error" else logs).append(m.text))
    pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.goto(url)                                 # index.html skickar vidare till try-outen
    pg.wait_for_timeout(3000)
    print("adress:", pg.url)
    # starta om sidan väntar på ett klick (ljud kräver det i vissa webbläsare)
    for sel in ["text=Start", "button:has-text('Start')", "#start"]:
        try:
            if pg.is_visible(sel, timeout=500):
                pg.click(sel)
                print("klickade", sel)
                break
        except Exception:
            pass
    pg.wait_for_timeout(15000)                   # fm1-dock startar, BRIS väljs (SELECT vid 1,5 s emulerad tid)
    png = os.path.join(ut, "sida.png")
    pg.screenshot(path=png)
    print("text på sidan:", " ".join(pg.inner_text("body").split())[:400])
    print("konsolfel:", errs[:5])
    print("BILD sida.png", base64.b64encode(open(png, "rb").read()).decode())
    b.close()
srv.shutdown()
