#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
jf8-tool — command-line tool for the JF8Call WebSocket API.

Usage:
  jf8-tool.py [--host HOST] [--port PORT] <command> [args...]

Examples:
  jf8-tool.py status
  jf8-tool.py config get
  jf8-tool.py config set callsign=W5XYZ grid=DM79AA
  jf8-tool.py send "W4ABC HELLO"
  jf8-tool.py send "W4ABC HELLO" --submode fast
  jf8-tool.py tx hb
  jf8-tool.py tx snr W4ABC
  jf8-tool.py tx info W4ABC
  jf8-tool.py tx grid W4ABC
  jf8-tool.py tx status W4ABC
  jf8-tool.py tx hearing W4ABC
  jf8-tool.py tx queue
  jf8-tool.py tx clear
  jf8-tool.py radio get
  jf8-tool.py radio connect --model 3073 --port /dev/ttyUSB0 --baud 9600 --ptt 1
  jf8-tool.py radio disconnect
  jf8-tool.py radio freq 14078.0
  jf8-tool.py radio ptt on
  jf8-tool.py radio ptt off
  jf8-tool.py radio tune
  jf8-tool.py audio devices
  jf8-tool.py audio restart
  jf8-tool.py messages [--offset N] [--limit N]
  jf8-tool.py messages clear
  jf8-tool.py spectrum
  jf8-tool.py monitor              # stream all events until Ctrl-C
  jf8-tool.py monitor --filter message.decoded
  jf8-tool.py stream                          # stream decoded messages to stdout
  jf8-tool.py stream --frames                 # also show partial frame-by-frame updates
  jf8-tool.py stream --json                   # raw JSON, one object per line
  jf8-tool.py stream --output /tmp/rx.log     # write to file instead of stdout
"""

import argparse
import asyncio
import json
import sys
import uuid

try:
    import websockets
except ImportError:
    sys.exit("websockets library not found — run: pip install websockets")


# ── Helpers ────────────────────────────────────────────────────────────────────

def jprint(obj):
    """Pretty-print a JSON-serialisable object."""
    print(json.dumps(obj, indent=2))


def make_cmd(cmd, data=None):
    msg = {"type": "cmd", "id": str(uuid.uuid4())[:8], "cmd": cmd}
    if data:
        msg["data"] = data
    return msg


async def connect(host, port):
    uri = f"ws://{host}:{port}"
    ws = await websockets.connect(uri)
    # Drain the hello + initial status push
    await ws.recv()  # hello
    await ws.recv()  # status event
    return ws


async def send_cmd(ws, cmd, data=None):
    """Send a command and wait for the matching reply."""
    msg = make_cmd(cmd, data)
    await ws.send(json.dumps(msg))
    # Read until we get a reply with matching id (ignore other events)
    while True:
        raw = await asyncio.wait_for(ws.recv(), timeout=10)
        obj = json.loads(raw)
        if obj.get("type") == "reply" and obj.get("id") == msg["id"]:
            return obj
        # Events that arrive before the reply are printed live
        if obj.get("type") == "event":
            pass  # silently skip; monitor mode handles these


# ── Command handlers ───────────────────────────────────────────────────────────

async def cmd_status(ws, _args):
    r = await send_cmd(ws, "status.get")
    if r["ok"]:
        d = r["data"]
        print(f"Callsign   : {d.get('callsign','—')}")
        print(f"Grid       : {d.get('grid','—')}")
        print(f"Submode    : {d.get('submode_name','?')} ({d.get('submode')})")
        print(f"Dial freq  : {d.get('frequency_khz')} kHz")
        print(f"TX offset  : {d.get('tx_freq_hz')} Hz")
        print(f"Transmit   : {'yes' if d.get('transmitting') else 'no'}")
        print(f"Audio      : {'running' if d.get('audio_running') else 'stopped'}")
        print(f"Radio      : {'connected' if d.get('radio_connected') else 'not connected'}")
        if d.get("radio_connected"):
            print(f"Radio freq : {d.get('radio_freq_khz')} kHz  {d.get('radio_mode','')}")
        print(f"TX queue   : {d.get('tx_queue_size')} frame(s)")
        print(f"Heartbeat  : {'on' if d.get('heartbeat_enabled') else 'off'}"
              f"  every {d.get('heartbeat_interval_periods')} period(s)")
        print(f"Auto-reply : {'on' if d.get('auto_reply') else 'off'}")
        print(f"WS clients : {d.get('ws_clients')}")
    else:
        print(f"Error: {r.get('error')}", file=sys.stderr)


async def cmd_config_get(ws, _args):
    r = await send_cmd(ws, "config.get")
    if r["ok"]:
        jprint(r["data"])
    else:
        print(f"Error: {r.get('error')}", file=sys.stderr)


async def cmd_config_set(ws, args):
    if not args.kv:
        print("Usage: config set key=value [key=value ...]", file=sys.stderr)
        return
    data = {}
    for kv in args.kv:
        if "=" not in kv:
            print(f"Bad key=value pair: {kv!r}", file=sys.stderr)
            return
        k, v = kv.split("=", 1)
        # Try to coerce numbers / booleans
        if v.lower() == "true":
            v = True
        elif v.lower() == "false":
            v = False
        else:
            try:
                v = int(v)
            except ValueError:
                try:
                    v = float(v)
                except ValueError:
                    pass
        data[k] = v
    r = await send_cmd(ws, "config.set", data)
    if r["ok"]:
        print("OK")
    else:
        print(f"Error: {r.get('error')}", file=sys.stderr)


async def cmd_audio_devices(ws, _args):
    r = await send_cmd(ws, "audio.devices")
    if r["ok"]:
        print("Input devices:")
        for d in r["data"].get("input", []):
            print(f"  {d}")
        print("Output devices:")
        for d in r["data"].get("output", []):
            print(f"  {d}")
    else:
        print(f"Error: {r.get('error')}", file=sys.stderr)


async def cmd_audio_restart(ws, _args):
    r = await send_cmd(ws, "audio.restart")
    print("OK" if r["ok"] else f"Error: {r.get('error')}")


async def cmd_radio_get(ws, _args):
    r = await send_cmd(ws, "radio.get")
    if r["ok"]:
        d = r["data"]
        print(f"Connected : {'yes' if d.get('connected') else 'no'}")
        if d.get("connected"):
            print(f"Freq      : {d.get('freq_khz')} kHz")
            print(f"Mode      : {d.get('mode','—')}")
        print(f"Model     : {d.get('rig_model')}")
        print(f"Port      : {d.get('port','—')}")
        print(f"Baud      : {d.get('baud')}")
        ptt_names = {0: "VOX", 1: "CAT", 2: "DTR", 3: "RTS"}
        print(f"PTT type  : {ptt_names.get(d.get('ptt_type'), '?')}")
    else:
        print(f"Error: {r.get('error')}", file=sys.stderr)


async def cmd_radio_connect(ws, args):
    data = {}
    if args.model is not None:
        data["rig_model"] = args.model
    if args.port:
        data["port"] = args.port
    if args.baud is not None:
        data["baud"] = args.baud
    if args.ptt is not None:
        data["ptt_type"] = args.ptt
    r = await send_cmd(ws, "radio.connect", data)
    print("OK" if r["ok"] else f"Error: {r.get('error')}")


async def cmd_radio_disconnect(ws, _args):
    r = await send_cmd(ws, "radio.disconnect")
    print("OK" if r["ok"] else f"Error: {r.get('error')}")


async def cmd_radio_freq(ws, args):
    r = await send_cmd(ws, "radio.frequency.set", {"freq_khz": args.khz})
    print("OK" if r["ok"] else f"Error: {r.get('error')}")


async def cmd_radio_ptt(ws, args):
    ptt = args.state.lower() in ("on", "1", "true", "yes")
    r = await send_cmd(ws, "radio.ptt.set", {"ptt": ptt})
    print("OK" if r["ok"] else f"Error: {r.get('error')}")


async def cmd_radio_tune(ws, _args):
    r = await send_cmd(ws, "radio.tune")
    print("OK" if r["ok"] else f"Error: {r.get('error')}")


async def cmd_messages(ws, args):
    data = {}
    if args.offset:
        data["offset"] = args.offset
    if args.limit:
        data["limit"] = args.limit
    r = await send_cmd(ws, "messages.get", data or None)
    if r["ok"]:
        msgs = r["data"].get("messages", [])
        total = r["data"].get("count", len(msgs))
        print(f"Showing {len(msgs)} of {total} messages:")
        for m in msgs:
            frm  = m.get("from", "?")
            to   = m.get("to", "")
            body = m.get("body") or m.get("raw", "")
            snr  = m.get("snr_db", 0)
            freq = m.get("freq_hz", 0)
            t    = m.get("time", "")
            dest = f" → {to}" if to else ""
            print(f"  [{t}] +{freq:.0f}Hz  SNR{snr:+d}  {frm}{dest}  {body}")
    else:
        print(f"Error: {r.get('error')}", file=sys.stderr)


async def cmd_messages_clear(ws, _args):
    r = await send_cmd(ws, "messages.clear")
    print("OK" if r["ok"] else f"Error: {r.get('error')}")


async def cmd_spectrum(ws, _args):
    r = await send_cmd(ws, "spectrum.get")
    if r["ok"]:
        bins = r["data"].get("bins", [])
        hz   = r["data"].get("hz_per_bin", 0)
        print(f"{len(bins)} bins, {hz:.3f} Hz/bin")
        if bins:
            peak_bin = max(range(len(bins)), key=lambda i: bins[i])
            print(f"Peak: {bins[peak_bin]:.1f} dB at {peak_bin * hz:.1f} Hz (bin {peak_bin})")
    else:
        print(f"Error: {r.get('error')}", file=sys.stderr)


async def cmd_send(ws, args):
    data = {"text": args.text}
    if args.submode:
        data["submode"] = args.submode
    r = await send_cmd(ws, "tx.send", data)
    if r["ok"]:
        print(f"Queued, {r['data'].get('queue_size')} frame(s) now in queue")
    else:
        print(f"Error: {r.get('error')}", file=sys.stderr)


async def cmd_tx_hb(ws, _args):
    r = await send_cmd(ws, "tx.hb")
    print("OK" if r["ok"] else f"Error: {r.get('error')}")


async def cmd_tx_snr(ws, args):
    r = await send_cmd(ws, "tx.snr", {"to": args.callsign.upper()})
    print("OK" if r["ok"] else f"Error: {r.get('error')}")


async def cmd_tx_info(ws, args):
    r = await send_cmd(ws, "tx.info", {"to": args.callsign.upper()})
    print("OK" if r["ok"] else f"Error: {r.get('error')}")


async def cmd_tx_grid(ws, args):
    r = await send_cmd(ws, "tx.send",
        {"text": f"{args.callsign.upper()} @GRID?"})
    print("OK" if r["ok"] else f"Error: {r.get('error')}")


async def cmd_tx_status(ws, args):
    r = await send_cmd(ws, "tx.status", {"to": args.callsign.upper()})
    print("OK" if r["ok"] else f"Error: {r.get('error')}")


async def cmd_tx_hearing(ws, args):
    r = await send_cmd(ws, "tx.send",
        {"text": f"{args.callsign.upper()} @HEARING?"})
    print("OK" if r["ok"] else f"Error: {r.get('error')}")


async def cmd_tx_queue(ws, _args):
    r = await send_cmd(ws, "tx.queue.get")
    if r["ok"]:
        q = r["data"].get("queue", [])
        print(f"{len(q)} frame(s) in TX queue")
        for i, f in enumerate(q):
            print(f"  [{i}] submode={f.get('submode')} frame_type={f.get('frame_type')}"
                  f" payload={f.get('payload','')[:40]}")
    else:
        print(f"Error: {r.get('error')}", file=sys.stderr)


async def cmd_tx_clear(ws, _args):
    r = await send_cmd(ws, "tx.queue.clear")
    print("OK" if r["ok"] else f"Error: {r.get('error')}")


async def cmd_monitor(ws, args):
    filt = args.filter.lower() if args.filter else None
    print(f"Monitoring events{f' (filter: {filt})' if filt else ''}… (Ctrl-C to stop)")
    try:
        async for raw in ws:
            obj = json.loads(raw)
            if obj.get("type") != "event":
                continue
            ev = obj.get("event", "")
            if filt and filt not in ev:
                continue
            if ev == "message.decoded":
                d = obj.get("data", {})
                frm  = d.get("from", "?")
                to   = d.get("to", "")
                body = d.get("body") or d.get("raw", "")
                snr  = d.get("snr_db", 0)
                freq = d.get("freq_hz", 0)
                t    = d.get("time", "")
                dest = f" → {to}" if to else ""
                print(f"[{t}] +{freq:.0f}Hz  SNR{snr:+d}  {frm}{dest}  {body}")
            elif ev == "tx.started":
                print(">>> TX STARTED")
            elif ev == "tx.finished":
                print("<<< TX FINISHED")
            elif ev == "tx.queued":
                print(f"TX queued: {obj['data'].get('queue_size')} frame(s)")
            elif ev == "radio.connected":
                d = obj.get("data", {})
                print(f"Radio connected: {d.get('freq_khz')} kHz {d.get('mode','')}")
            elif ev == "radio.disconnected":
                print("Radio disconnected")
            elif ev == "config.changed":
                print(f"Config changed: {json.dumps(obj.get('data', {}))}")
            elif ev == "error":
                print(f"ERROR: {obj['data'].get('message','')}", file=sys.stderr)
            elif ev != "status" and ev != "spectrum":
                # Print anything else we don't recognise
                jprint(obj)
    except KeyboardInterrupt:
        pass


async def cmd_stream(ws, args):
    """Stream decoded messages (and optionally partial frames) to stdout or a file."""
    import sys as _sys

    outfile = open(args.output, "a") if args.output else _sys.stdout

    def fmt_message(d, label="RX"):
        frm  = d.get("from", "?")
        to   = d.get("to", "")
        body = d.get("body") or d.get("raw") or d.get("assembled_text", "")
        snr  = d.get("snr_db", 0)
        freq = d.get("freq_hz", 0)
        t    = d.get("time", "")
        dest = f" → {to}" if to else ""
        return f"[{t}] +{freq:.0f}Hz  SNR{snr:+d}  {label}  {frm}{dest}  {body}"

    def fmt_frame(d):
        freq  = d.get("freq_hz", 0)
        snr   = d.get("snr_db", 0)
        t     = d.get("time", "")
        ftype = d.get("frame_type", 0)
        text  = d.get("assembled_text", "")
        ftype_name = {0: "MID", 1: "FIRST", 2: "LAST", 3: "SINGLE"}.get(ftype, str(ftype))
        return f"[{t}] +{freq:.0f}Hz  SNR{snr:+d}  [{ftype_name}]  {text}"

    try:
        async for raw in ws:
            obj = json.loads(raw)
            if obj.get("type") != "event":
                continue
            ev = obj.get("event", "")
            data = obj.get("data", {})

            line = None
            if ev == "message.decoded":
                if args.json:
                    line = json.dumps(data)
                else:
                    line = fmt_message(data)
            elif ev == "message.frame" and args.frames:
                if args.json:
                    line = json.dumps(data)
                else:
                    line = fmt_frame(data)

            if line is not None:
                print(line, file=outfile, flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        if args.output:
            outfile.close()


# ── Argument parsing ───────────────────────────────────────────────────────────

def build_parser():
    p = argparse.ArgumentParser(
        prog="jf8-tool.py",
        description="JF8Call WebSocket API command-line tool")
    p.add_argument("--host", default="localhost", help="JF8Call host (default: localhost)")
    p.add_argument("--port", type=int, default=2102, help="WebSocket port (default: 2102)")

    sub = p.add_subparsers(dest="command", metavar="COMMAND")
    sub.required = True

    # status
    sub.add_parser("status", help="Show current status")

    # config
    cfg = sub.add_parser("config", help="Get or set configuration")
    cfg_sub = cfg.add_subparsers(dest="config_cmd", metavar="CMD")
    cfg_sub.required = True
    cfg_sub.add_parser("get", help="Print full configuration")
    cfg_set = cfg_sub.add_parser("set", help="Set configuration fields (key=value ...)")
    cfg_set.add_argument("kv", nargs="+", metavar="key=value")

    # audio
    audio = sub.add_parser("audio", help="Audio device management")
    audio_sub = audio.add_subparsers(dest="audio_cmd", metavar="CMD")
    audio_sub.required = True
    audio_sub.add_parser("devices", help="List available audio devices")
    audio_sub.add_parser("restart", help="Restart audio I/O")

    # radio
    radio = sub.add_parser("radio", help="Radio / Hamlib control")
    radio_sub = radio.add_subparsers(dest="radio_cmd", metavar="CMD")
    radio_sub.required = True
    radio_sub.add_parser("get", help="Show radio status")
    rc = radio_sub.add_parser("connect", help="Connect to rig")
    rc.add_argument("--model", type=int, metavar="N", help="Hamlib model number")
    rc.add_argument("--port", dest="port", metavar="PATH", help="Serial port or host:port")
    rc.add_argument("--baud", type=int, metavar="N", help="Baud rate")
    rc.add_argument("--ptt", type=int, metavar="N",
                    help="PTT type: 0=VOX 1=CAT 2=DTR 3=RTS")
    radio_sub.add_parser("disconnect", help="Disconnect from rig")
    rf = radio_sub.add_parser("freq", help="Set VFO frequency in kHz")
    rf.add_argument("khz", type=float, metavar="KHZ")
    rp = radio_sub.add_parser("ptt", help="Set PTT (on/off)")
    rp.add_argument("state", choices=["on", "off"])
    radio_sub.add_parser("tune", help="Trigger ATU tune cycle (RIG_OP_TUNE via CAT)")

    # messages
    msgs = sub.add_parser("messages", help="View or clear decoded message log")
    msgs.add_argument("--offset", type=int, default=0, help="Start offset")
    msgs.add_argument("--limit", type=int, default=50, help="Max results (default 50)")
    msgs_sub = msgs.add_subparsers(dest="messages_cmd", metavar="CMD")
    msgs_sub.add_parser("clear", help="Clear the message log")

    # spectrum
    sub.add_parser("spectrum", help="Show latest spectrum snapshot summary")

    # send
    snd = sub.add_parser("send", help="Transmit a message")
    snd.add_argument("text", help="Message text")
    snd.add_argument("--submode", metavar="NAME",
                     help="Submode: normal/fast/turbo/slow/ultra (default: active)")

    # tx
    tx = sub.add_parser("tx", help="TX commands (@HB, @SNR?, etc.)")
    tx_sub = tx.add_subparsers(dest="tx_cmd", metavar="CMD")
    tx_sub.required = True
    tx_sub.add_parser("hb", help="Send heartbeat (@HB)")
    ts = tx_sub.add_parser("snr", help="Send @SNR? query to callsign")
    ts.add_argument("callsign")
    ti = tx_sub.add_parser("info", help="Send @INFO? query to callsign")
    ti.add_argument("callsign")
    tg = tx_sub.add_parser("grid", help="Send @GRID? query to callsign")
    tg.add_argument("callsign")
    tst = tx_sub.add_parser("status", help="Send @STATUS? query to callsign")
    tst.add_argument("callsign")
    th = tx_sub.add_parser("hearing", help="Send @HEARING? query to callsign")
    th.add_argument("callsign")
    tx_sub.add_parser("queue", help="Show TX queue")
    tx_sub.add_parser("clear", help="Clear TX queue")

    # monitor
    mon = sub.add_parser("monitor", help="Stream live events (Ctrl-C to stop)")
    mon.add_argument("--filter", metavar="EVENT",
                     help="Only show events containing this string")

    # stream
    strm = sub.add_parser("stream", help="Stream decoded messages to stdout or a file")
    strm.add_argument("--frames", action="store_true",
                      help="Also emit partial frame-by-frame updates (message.frame events)")
    strm.add_argument("--json", action="store_true",
                      help="Output raw JSON (one object per line) instead of human-readable text")
    strm.add_argument("--output", metavar="FILE",
                      help="Write to FILE instead of stdout (appends if file exists)")

    return p


# ── Dispatch ───────────────────────────────────────────────────────────────────

async def main():
    parser = build_parser()
    args = parser.parse_args()

    try:
        ws = await connect(args.host, args.port)
    except Exception as e:
        sys.exit(f"Cannot connect to JF8Call at ws://{args.host}:{args.port} — {e}")

    try:
        cmd = args.command
        if cmd == "status":
            await cmd_status(ws, args)
        elif cmd == "config":
            if args.config_cmd == "get":
                await cmd_config_get(ws, args)
            elif args.config_cmd == "set":
                await cmd_config_set(ws, args)
        elif cmd == "audio":
            if args.audio_cmd == "devices":
                await cmd_audio_devices(ws, args)
            elif args.audio_cmd == "restart":
                await cmd_audio_restart(ws, args)
        elif cmd == "radio":
            if args.radio_cmd == "get":
                await cmd_radio_get(ws, args)
            elif args.radio_cmd == "connect":
                await cmd_radio_connect(ws, args)
            elif args.radio_cmd == "disconnect":
                await cmd_radio_disconnect(ws, args)
            elif args.radio_cmd == "freq":
                await cmd_radio_freq(ws, args)
            elif args.radio_cmd == "ptt":
                await cmd_radio_ptt(ws, args)
            elif args.radio_cmd == "tune":
                await cmd_radio_tune(ws, args)
        elif cmd == "messages":
            if getattr(args, "messages_cmd", None) == "clear":
                await cmd_messages_clear(ws, args)
            else:
                await cmd_messages(ws, args)
        elif cmd == "spectrum":
            await cmd_spectrum(ws, args)
        elif cmd == "send":
            await cmd_send(ws, args)
        elif cmd == "tx":
            if args.tx_cmd == "hb":
                await cmd_tx_hb(ws, args)
            elif args.tx_cmd == "snr":
                await cmd_tx_snr(ws, args)
            elif args.tx_cmd == "info":
                await cmd_tx_info(ws, args)
            elif args.tx_cmd == "grid":
                await cmd_tx_grid(ws, args)
            elif args.tx_cmd == "status":
                await cmd_tx_status(ws, args)
            elif args.tx_cmd == "hearing":
                await cmd_tx_hearing(ws, args)
            elif args.tx_cmd == "queue":
                await cmd_tx_queue(ws, args)
            elif args.tx_cmd == "clear":
                await cmd_tx_clear(ws, args)
        elif cmd == "monitor":
            await cmd_monitor(ws, args)
        elif cmd == "stream":
            await cmd_stream(ws, args)
    finally:
        await ws.close()


if __name__ == "__main__":
    asyncio.run(main())
