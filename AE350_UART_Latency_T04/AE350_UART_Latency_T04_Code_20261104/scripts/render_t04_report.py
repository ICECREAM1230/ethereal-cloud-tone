#!/usr/bin/env python3
"""Render saved T04 UART bytes and decoder snapshots as offline HTML.

No device, browser, network or decoding calls are made here. The three pages
refresh every two seconds when served locally; run this renderer again after
updating capture.bin/decoded.json. raw.bin is an exact copy of capture.bin.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import html
import json
from pathlib import Path
import tempfile


ROOT = Path(__file__).resolve().parents[1]
PHASES = (
    ("receive_queue_and_ae350", "T1 − T0", "排队与 AE350 解析、分配、计算"),
    ("command_transfer_and_commit", "T2 − T1", "分包传输、ACK 往返与 COMMIT 完成"),
    ("synthesis_and_output_wait", "T3 − T2", "合成起音与播放输出等待"),
    ("usb_note_on_to_pcm", "T3 − T0", "USB NoteOn 接纳至非零 PCM 总延迟"),
)
CSS = """
:root{color-scheme:light;font-family:system-ui,'Noto Sans CJK SC','Microsoft YaHei',sans-serif;color:#17232f;background:#f1f4f6}
*{box-sizing:border-box}body{margin:0}main{max-width:1280px;margin:auto;padding:20px 24px 28px}
header{display:flex;align-items:baseline;justify-content:space-between;gap:18px;border-bottom:2px solid #233e50;padding:0 0 12px;margin-bottom:14px}
h1{font-size:24px;margin:0;font-weight:700}h2{font-size:18px;margin:22px 0 10px}p{margin:7px 0;line-height:1.5}
nav{display:flex;gap:14px;font-size:14px;white-space:nowrap}a{color:#145983;text-decoration:underline}
.meta{display:flex;flex-wrap:wrap;gap:8px 16px;font-size:14px;margin:10px 0}.tag{font-weight:700;border-radius:4px;background:#e4ebef;padding:4px 8px}
.PASS,.SUCCESS{color:#12593c;background:#e2f1e9}.FAIL,.INVALID,.TIMEOUT{color:#8e2820;background:#fae6e2}.INCOMPLETE,.UNKNOWN{color:#775100;background:#fff1c9}
.notice{border-left:4px solid #bd790d;background:#fff6df;padding:10px 13px;font-size:13px;margin:12px 0}
.panel{background:#fff;border:1px solid #ccd6dc;border-radius:5px;padding:12px 14px;margin:12px 0}
.muted{color:#61717d}.small{font-size:12px}.mono,code,pre,td.count{font-family:'DejaVu Sans Mono','Noto Sans Mono',monospace;font-variant-numeric:tabular-nums}
code{font-size:12px;overflow-wrap:anywhere}table{width:100%;border-collapse:collapse;background:#fff;font-size:13px}
th{background:#e5edf1;text-align:left;font-weight:650;white-space:nowrap}th,td{border:1px solid #d4dde2;padding:7px 8px;vertical-align:top}
td.count{text-align:right;white-space:nowrap;font-size:12px}tbody tr:nth-child(even){background:#f6f9fa}tr.missing{color:#9aa6af}tr.bad{background:#fff0eb!important}
.raw-counts th,.raw-counts td{padding:5px 7px}.definitions{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;margin:12px 0;font-size:13px}
.definitions div{background:#e8eef2;padding:8px 10px}.definitions strong{display:block;margin-bottom:2px}
pre.serial{background:#101a22;color:#eaf2f6;padding:14px 16px;white-space:pre-wrap;overflow-wrap:anywhere;word-break:break-all;font-size:13px;line-height:1.65;border:1px solid #394b58;margin:12px 0}
pre.errors{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px;line-height:1.5}.source{overflow-wrap:anywhere}
.empty{padding:25px;text-align:center;color:#61717d;background:#fff;border:1px solid #ccd6dc}.foot{font-size:12px;color:#61717d;margin-top:18px}
@media(max-width:850px){header{display:block}nav{margin-top:10px}.definitions{grid-template-columns:repeat(2,1fr)}main{padding:14px}.table-scroll{overflow-x:auto}}
"""


def esc(value):
    return html.escape(str(value), quote=True)


def number(value, decimals=6):
    if value is None:
        return "—"
    if isinstance(value, int):
        return str(value)
    return f"{value:.{decimals}f}"


def atomic_write(path, data):
    """Replace only this renderer's named outputs, never an input file."""
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".t04-", delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(data)
    try:
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def origin_label(transport):
    origin = str(transport.get("origin", "UNKNOWN"))
    kind = origin.upper()
    if kind in {"PHYSICAL_BOARD", "PHYSICAL_UART", "BOARD_UART", "BOARD"}:
        return origin, "实板 UART 采集（来源由采集记录明确标注）"
    if kind in {"SIMULATION", "SIMULATION_FIXTURE", "SIMULATION_UART"}:
        return origin, "仿真夹具 · 非实板测量"
    return origin, "来源未确认 · 不认定为实板测量"


def common(title, body, summary, transport, capture_dir, raw, generated):
    origin, label = origin_label(transport)
    status = str(summary.get("status", "UNKNOWN"))
    stream = str(summary.get("stream_status", "UNKNOWN"))
    header = f"""<header><h1>{esc(title)}</h1><nav>
<a href="index.html">均值与状态</a><a href="raw-counts.html">原始计数</a><a href="raw-uart.html">串口原文</a></nav></header>
<div class="meta"><span class="tag {esc(status)}">批次 {esc(status)}</span>
<span class="tag {esc(stream)}">整流 {esc(stream)}</span><span class="tag">{esc(label)}</span>
<span>有效 {esc(summary.get('successful_records', 0))} / 20</span>
<span>已收到 {esc(summary.get('unique_records', 0))} / 20</span><span>原始字节 {len(raw)}</span></div>
<p class="small muted">T04 · control48 = 48,000,000 Hz · UART 200,000 baud / 8N1 · 源 USB cable0 · 原始来源标记 <code>{esc(origin)}</code></p>
<div class="notice"><strong>STA 尚未闭合。</strong>本轮官方摘要：887 个 setup / 8 个 hold violated endpoints；普通 setup WNS −2.333 ns，USB → capture 路径 −1.717 ns。
8 个摘要 hold 与普通数据 hold 表口径不同，removal 表有 8 条负值。此处原始计数是功能观察，不能作为硬件精度或时序签核结果。</div>"""
    foot = f"""<div class="foot"><p class="source">采集目录：<code>{esc(capture_dir)}</code></p>
<p>报告生成：{esc(generated)} · 页面每 2 秒刷新；内容随报告重新生成而更新。</p>
<p>四时刻包含系统排队与跨域观察延迟；UART 接收时间、电脑时间、模拟 DAC 与 USB3 PCM 上传不计入这些差值，AE350 纯指令 WCET 未测。</p></div>"""
    return f"""<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="refresh" content="2"><title>{esc(title)}</title><style>{CSS}</style></head><body><main>{header}{body}{foot}</main></body></html>"""


def errors_panel(summary, transport):
    errors = summary.get("errors", [])
    batch = summary.get("batch_errors", [])
    io = {key: transport[key] for key in ("error", "close_error", "status") if key in transport}
    if not errors and not batch and not io:
        return ""
    return '<h2>原样保留的错误与诊断</h2><pre class="errors panel">' + esc(json.dumps(
        {"serial_errors": errors, "batch_errors": batch, "transport": io}, ensure_ascii=False, indent=2)) + "</pre>"


def index_body(summary, transport):
    definitions = """<div class="definitions"><div><strong>T0</strong>完整 USB MIDI NoteOn 被接纳</div>
<div><strong>T1</strong>首个 NEW BEGIN 成功入队</div><div><strong>T2</strong>同组 COMMIT 执行成功返回</div>
<div><strong>T3</strong>首个完整 PCM24 非零样本装载到播放通路</div></div>"""
    rows = []
    for engine in ("SUB", "KS", "FM"):
        group = summary.get("engines", {}).get(engine, {})
        for key, short, meaning in PHASES:
            value = group.get("intervals", {}).get(key, {})
            rows.append(f"<tr><td>{engine}</td><td>{esc(short)}</td><td>{esc(meaning)}</td>"
                        f"<td class=\"count\">{esc(value.get('count', 0))}</td>"
                        f"<td class=\"count\">{esc(number(value.get('mean_cycles')))}</td>"
                        f"<td class=\"count\">{esc(number(value.get('mean_microseconds')))}</td>"
                        f"<td class=\"count\">{esc(number(value.get('min_cycles')))}</td>"
                        f"<td class=\"count\">{esc(number(value.get('max_cycles')))}</td>"
                        f"<td class=\"count\">{esc(value.get('negative_count', 0))}</td></tr>")
    missing = ", ".join(map(str, summary.get("missing_ids", []))) or "无"
    return definitions + """<h2>有效记录的分段均值</h2><div class="table-scroll"><table><thead><tr>
<th>合成器</th><th>区间</th><th>含义</th><th>有效数</th><th>平均计数</th><th>平均 µs</th><th>最小计数</th><th>最大计数</th><th>负值数</th>
</tr></thead><tbody>""" + "".join(rows) + f"""</tbody></table></div>
<p class="small">只统计解码器标为 successful 的记录；批次或整流 FAIL 时，表中均值仅作故障参考。T3 &lt; T2 保留负差值；T3 &lt; T1 的记录无效。</p>
<div class="panel small"><p>缺失 ID：{esc(missing)}</p><p>完全相同的重放记录：{esc(summary.get('identical_repeats', 0))}；重放不增加样本数。无效、超时、CRC 错误、混批和冲突原文均保留。</p></div>""" + errors_panel(summary, transport)


def counts_body(summary, transport):
    records = {int(row["id"]): row for row in summary.get("records", [])}
    counts, deltas = [], []
    for ident in range(1, 21):
        row = records.get(ident)
        if row is None:
            counts.append(f'<tr class="missing"><td>{ident}</td><td colspan="3">未收到</td>' + '<td class="count">—</td>' * 4 + '</tr>')
            continue
        ticks = row.get("raw_timestamps_cycles", {})
        css = "" if row.get("successful") else "bad"
        flags = int(row.get("flags", 0))
        midi = f"{row.get('note', '—')} / {row.get('velocity', '—')}"
        counts.append(f'<tr class="{css}"><td>{ident}</td><td>{esc(row.get("engine_name", "UNKNOWN"))}</td>'
                      f'<td>{esc(midi)}</td><td class="mono">0x{flags:08x}<br>{esc(row.get("status", "UNKNOWN"))}</td>' +
                      ''.join(f'<td class="count">{esc(number(ticks.get(t)))}</td>' for t in ("T0", "T1", "T2", "T3")) + '</tr>')
        values = row.get("signed_deltas_cycles", {})
        identity = row.get("command_identity", {})
        key_text = f"{identity.get('generation', '—')} / {identity.get('revision', '—')} / {identity.get('voice', '—')}"
        deltas.append(f'<tr class="{css}"><td>{ident}</td><td>{esc(key_text)}</td>' +
                      ''.join(f'<td class="count">{esc(number(values.get(k)))}</td>' for k, _, _ in PHASES) + '</tr>')
    return """<h2>原始 64 位计数 · 十进制</h2><p class="small muted">所有收到的记录按原 ID 展示；缺失位置保持空缺。0 是合法原始计数，是否已捕获以 flags 的 presence 位为准。</p>
<div class="table-scroll"><table class="raw-counts"><thead><tr><th>ID</th><th>合成器</th><th>Note / Vel</th><th>原始 flags / 状态</th><th>T0</th><th>T1</th><th>T2</th><th>T3</th></tr></thead><tbody>""" + ''.join(counts) + """</tbody></table></div>
<h2>AE350 差值的有符号解释 · 原始计数单位</h2><div class="table-scroll"><table><thead><tr><th>ID</th><th>generation / revision / voice</th>""" + ''.join(
        f'<th>{esc(short)}</th>' for _, short, _ in PHASES) + '</tr></thead><tbody>' + ''.join(deltas) + '</tbody></table></div>' + errors_panel(summary, transport)


def uart_body(raw, summary, transport):
    checksum = hashlib.sha256(raw).hexdigest()
    literal = raw.decode("ascii", "backslashreplace")
    description = """<p class="small">以下保留全部收到字节的文本顺序，包括错误、诊断、重复和未完成行。ASCII 原文直接显示；非 ASCII 字节使用反斜杠转义。浏览器把 CRLF 显示为换行，视觉折行不增加原始字节。</p>"""
    data = f"""<div class="panel"><p><a href="raw.bin" download>下载逐字节原始串口 raw.bin</a> · {len(raw)} bytes</p>
<p class="small">SHA256：<code>{checksum}</code></p><p class="small">AT04 正常帧 220 B / CRLF；AT4E 诊断 20 B / CRLF。此处没有过滤或修补原串口。</p></div>"""
    serial = '<pre class="serial">' + esc(literal) + '</pre>' if raw else '<div class="empty">尚未收到 UART 字节；不生成测量值。</div>'
    return data + description + serial + errors_panel(summary, transport)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    capture_dir, out = args.capture_dir.resolve(), args.out.resolve()
    if not out.is_relative_to(ROOT) or out == capture_dir:
        parser.error("--out must be a separate report directory inside this experiment")
    # The live collector flushes raw bytes before publishing a decoded snapshot.
    # Read that snapshot first, then raw, and hash these exact in-memory inputs.
    decoded_bytes = (capture_dir / "decoded.json").read_bytes()
    summary = json.loads(decoded_bytes)
    raw = (capture_dir / "capture.bin").read_bytes()
    transport_path = capture_dir / "transport.json"
    transport = json.loads(transport_path.read_text()) if transport_path.exists() else {}
    # A stale/mismatched decoder snapshot must not invent a row absent from raw.
    for row in summary.get("records", []):
        line = bytes.fromhex(row["raw_line_hex"])
        if not line or line not in raw:
            parser.error("decoded record is absent from exact capture.bin")
    out.mkdir(parents=True, exist_ok=True)
    generated = datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=9))).strftime("%Y-%m-%d %H:%M:%S JST")
    pages = {"index.html": ("AE350 四时刻延迟 · 均值与状态", index_body(summary, transport)),
             "raw-counts.html": ("AE350 四时刻延迟 · 原始计数", counts_body(summary, transport)),
             "raw-uart.html": ("AE350 四时刻延迟 · 原始串口", uart_body(raw, summary, transport))}
    atomic_write(out / "raw.bin", raw)
    for filename, (title, body) in pages.items():
        atomic_write(out / filename, common(title, body, summary, transport, capture_dir, raw, generated).encode("utf-8"))
    manifest = {"origin": str(transport.get("origin", "UNKNOWN")), "generated_jst": generated,
                "capture_dir": str(capture_dir), "raw_bytes": len(raw), "raw_sha256": hashlib.sha256(raw).hexdigest(),
                "status": summary.get("status", "UNKNOWN"), "unique_records": summary.get("unique_records", 0),
                "successful_records": summary.get("successful_records", 0), "pages": list(pages),
                "input_sha256": {"capture.bin": hashlib.sha256(raw).hexdigest(),
                                 "decoded.json": hashlib.sha256(decoded_bytes).hexdigest()},
                "scope": "Offline rendering only; exact raw.bin copy; no device, browser, network or precision-signoff claim"}
    atomic_write(out / "report.json", (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode())
    print(json.dumps(manifest, ensure_ascii=False))


if __name__ == "__main__":
    main()
