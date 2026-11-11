#!/usr/bin/env python3
"""Inject/replay MIDI through the existing ESP32 Web raw owner. No PCM capture."""
import argparse
import datetime
import hashlib
import http.client
import json
import sys
import time
from pathlib import Path
from urllib.parse import urlsplit


class DebugError(RuntimeError):
    pass


def midi_bytes(value):
    if (not isinstance(value, list) or not 1 <= len(value) <= 256 or
            any(type(x) is not int or not 0 <= x <= 255 for x in value)):
        raise ValueError("bytes must contain 1..256 integers in 0..255")
    return value


def load_events(path):
    raw = Path(path).read_bytes()
    if len(raw) > 2_000_000:
        raise ValueError("replay file exceeds 2 MB")
    events = json.loads(raw)
    if not isinstance(events, list) or not 1 <= len(events) <= 4096:
        raise ValueError("replay requires 1..4096 events")
    previous = -1
    for event in events:
        if not isinstance(event, dict) or set(event) != {"at_ms", "bytes"}:
            raise ValueError("each event requires only at_ms and bytes")
        at = event["at_ms"]
        if type(at) is not int or not previous <= at <= 600_000 or at < 0:
            raise ValueError("at_ms must be ordered integers in 0..600000")
        midi_bytes(event["bytes"])
        previous = at
    return events, hashlib.sha256(raw).hexdigest()


class MidiDebugClient:
    def __init__(self, url, *, exchange=None, clock=time.monotonic, sleep=time.sleep):
        parsed = urlsplit(url)
        if (parsed.scheme != "http" or not parsed.hostname or parsed.username or
                parsed.password or parsed.query or parsed.fragment or parsed.path not in ("", "/")):
            raise ValueError("url must be an http://host[:port] origin")
        self.host, self.port = parsed.hostname, parsed.port or 80
        self.url = url.rstrip("/")
        self.clock, self.sleep = clock, sleep
        self.exchange = exchange or self._exchange
        self.started = clock()
        self.token = self.context = self.sequence = 0
        self.client = None
        self.last_lease = self.started
        self.heartbeat_s = 0.25
        self.reset_count = None
        self.report = {"version": 1, "url": self.url, "pcm_capture": False,
                       "sample_accurate": False, "ack_scope": "fpga_raw_byte",
                       "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                       "http": [], "events": []}

    def _exchange(self, method, path, body, headers):
        connection = http.client.HTTPConnection(self.host, self.port, timeout=0.4)
        try:
            connection.request(method, path, body=body, headers=headers)
            response = connection.getresponse()
            payload = response.read(65537)
            if len(payload) > 65536:
                raise DebugError("response exceeds 64 KiB")
            return response.status, json.loads(payload)
        finally:
            connection.close()

    def request(self, method, path, body=b"", content_type="application/json"):
        at = self.clock()
        record = {"method": method, "path": path, "at_ms": (at-self.started)*1000}
        self.report["http"].append(record)
        try:
            code, result = self.exchange(method, path, body, {
                "X-FPGA-Control": "1", "Content-Type": content_type})
            record.update(status=code, response=result)
            if not 200 <= code < 300:
                raise DebugError(f"HTTP {code}: {result}")
            if not isinstance(result, dict):
                raise DebugError("expected a JSON object")
            return result
        except (OSError, ValueError, http.client.HTTPException) as exc:
            record["error"] = str(exc)
            raise DebugError(str(exc)) from exc
        finally:
            record["elapsed_ms"] = (self.clock()-at)*1000

    def status(self):
        state = self.request("GET", "/api/debug/midi")
        if state.get("version") != 1 or state.get("ack_scope") != "fpga_raw_byte":
            raise DebugError("unsupported debug interface; install the matching firmware")
        return state

    def _check(self, state):
        if state.get("context_epoch") != self.context:
            raise DebugError("engine context changed; no automatic replay/reconnect")
        if not state.get("ready") or state.get("reset_count") != self.reset_count:
            raise DebugError("raw owner not ready or FPGA link reset")
        if state["web"]["session"] not in (0, self.token):
            raise DebugError("Web MIDI source belongs to another client")

    def _await_owner(self, sequence):
        deadline = self.clock() + 0.8
        while self.clock() < deadline:
            state = self.request("GET", "/api/v2/state")
            if state.get("context_epoch") != self.context:
                raise DebugError("engine context changed")
            owner = state["clients"][self.client]
            if owner.get("token") == self.token and owner.get("result_sequence") == sequence:
                if owner.get("result_code") != 0 or owner.get("result_context_epoch") != self.context:
                    raise DebugError(f"owner rejected heartbeat: {owner}")
                return
            self.sleep(0.02)
        raise DebugError("owner heartbeat acknowledgement timed out")

    def renew(self):
        if self.sequence == 0xffffffff:
            raise DebugError("sequence exhausted")
        self.sequence += 1
        # Empty key snapshot renews this same-token raw stream without
        # injecting notes or claiming an idle stream from another producer.
        body = f"{self.token} {self.sequence} {self.context} 3 0 100 0".encode()
        self.request("POST", "/api/v2/command", body, "text/plain")
        self.last_lease = self.clock()
        self._await_owner(self.sequence)

    def open(self):
        before = self.status()
        if not before.get("ready") or before["web"]["session"]:
            raise DebugError("MIDI not ready or Web source busy; release the browser keyboard first")
        self.report["initial_state"] = self.request("GET", "/api/v2/state")
        session = self.request("POST", "/api/v2/session")
        self.token, self.context, self.client = (session["token"], session["context_epoch"], session["client"])
        if (session.get("api_major") != 2 or type(self.token) is not int or not 0 < self.token <= 0xffffffff or
                type(self.context) is not int or not 0 < self.context <= 0xffffffff or
                type(self.client) is not int or not 0 <= self.client < 4 or session.get("lease_ms", 0) < 1000):
            raise DebugError("invalid or unsupported session")
        self.reset_count = before["reset_count"]
        self.report["session"] = session
        self.renew()

    def send(self, data, *, planned_ms=None):
        midi_bytes(data)
        before = self.status()
        self._check(before)
        if self.sequence == 0xffffffff:
            raise DebugError("sequence exhausted")
        self.sequence += 1
        event = {"sequence": self.sequence, "bytes": list(data), "planned_ms": planned_ms,
                 "dispatch_ms": (self.clock()-self.started)*1000, "stage": "sending"}
        self.report["events"].append(event)
        body = json.dumps({"source": "web", "bytes": data, "token": self.token,
                           "sequence": self.sequence, "context_epoch": self.context}).encode()
        result = self.request("POST", "/api/debug/midi", body)
        if result.get("accepted") is not True or result.get("queued_bytes") != len(data):
            raise DebugError("unexpected MIDI queue receipt")
        self.last_lease = self.clock()
        event["stage"] = "http_queued"
        deadline = self.clock()+0.8
        while self.clock() < deadline:
            state = self.status()
            self._check(state)
            web = state["web"]
            if web["dropped_bytes"] != before["web"]["dropped_bytes"] or web["fault"] != before["web"]["fault"]:
                raise DebugError("raw MIDI bytes were dropped or faulted")
            delta = (web["sent_bytes"]-before["web"]["sent_bytes"]) & 0xffffffff
            if web["session"] == self.token and delta == len(data) and not web["queued_bytes"]:
                event.update(stage="fpga_byte_ack", acknowledged_ms=(self.clock()-self.started)*1000)
                return
            if delta > len(data):
                raise DebugError("unexpected Web byte counter movement")
            self.sleep(0.02)
        raise DebugError("FPGA raw-byte acknowledgement timed out; request was not retried")

    def wait_until(self, deadline):
        while True:
            now = self.clock()
            if now >= deadline:
                return
            until_heartbeat = self.heartbeat_s-(now-self.last_lease)
            if until_heartbeat <= 0:
                self.renew()
                self._check(self.status())
            else:
                self.sleep(min(deadline-now, until_heartbeat))

    def replay(self, events):
        start = self.clock()
        self.report["replay_start_ms"] = (start-self.started)*1000
        for event in events:
            self.wait_until(start+event["at_ms"]/1000)
            self.send(event["bytes"], planned_ms=event["at_ms"])


def bounded_number(low, high):
    def parse(text):
        value = int(text)
        if not low <= value <= high:
            raise argparse.ArgumentTypeError(f"expected {low}..{high}")
        return value
    return parse


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://192.168.4.1")
    parser.add_argument("--report", type=Path)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("status", help="read RAM status without opening a MIDI session")
    note = sub.add_parser("note", help="send NoteOn, hold with heartbeat, then NoteOff")
    note.add_argument("note", type=bounded_number(0, 127))
    note.add_argument("--channel", type=bounded_number(1, 16), default=1)
    note.add_argument("--velocity", type=bounded_number(1, 127), default=80)
    note.add_argument("--duration-ms", type=bounded_number(0, 600000), default=1000)
    raw = sub.add_parser("raw", help="send 1..256 hexadecimal MIDI bytes")
    raw.add_argument("hex_bytes", help='e.g. "B0 07 40" or "90 3C 50"')
    raw.add_argument("--hold-ms", type=bounded_number(0, 600000), default=500)
    replay = sub.add_parser("replay", help="replay ordered JSON at_ms/bytes events")
    replay.add_argument("file", type=Path)
    replay.add_argument("--tail-ms", type=bounded_number(0, 600000), default=500)
    args = parser.parse_args(argv)
    try:
        events, digest = None, None
        if args.command == "raw":
            data = midi_bytes(list(bytes.fromhex(args.hex_bytes)))
        elif args.command == "replay":
            events, digest = load_events(args.file)
        client = MidiDebugClient(args.url)
    except (ValueError, OSError) as exc:
        parser.error(str(exc))
    output = args.report or Path(f"midi-debug-{time.time_ns()}.json")
    # Reserve the record before sending any MIDI; never overwrite a prior run.
    with output.open("x", encoding="utf-8") as report:
        status, note_attempted = 0, False
        try:
            if args.command == "status":
                client.report["state"] = client.status()
            else:
                client.open()
                if args.command == "note":
                    note_attempted = True
                    client.send([0x90+args.channel-1, args.note, args.velocity])
                    client.wait_until(client.clock()+args.duration_ms/1000)
                elif args.command == "raw":
                    client.send(data)
                    client.wait_until(client.clock()+args.hold_ms/1000)
                else:
                    client.report["replay_sha256"] = digest
                    client.replay(events)
                    client.wait_until(client.clock()+args.tail_ms/1000)
            client.report["status"] = "PASS"
        except (DebugError, KeyError, IndexError, TypeError, KeyboardInterrupt) as exc:
            status = 1
            client.report.update(status="FAIL", error=str(exc) or "interrupted")
        finally:
            if note_attempted:
                try:
                    client.send([0x80+args.channel-1, args.note, 0])
                except (DebugError, KeyError, IndexError, TypeError, KeyboardInterrupt) as exc:
                    status = 1
                    client.report.update(status="FAIL", note_off_error=str(exc) or "interrupted")
            client.report["release"] = "heartbeats stopped; Web source releases on its existing lease timeout"
            json.dump(client.report, report, ensure_ascii=False, indent=2)
            report.write("\n")
        print(json.dumps({"status": client.report["status"], "report": str(output),
                          "events": len(client.report["events"]), "pcm_capture": False}))
        return status


if __name__ == "__main__":
    sys.exit(main())
