"""Main-thread model: session-safe RPC, ACK-only apply, and independent draft."""
from __future__ import annotations

import copy
import threading
import time
from collections import deque
from dataclasses import dataclass

from .config import DEFAULT_CONFIG, ConfigError, apply_arguments, validate_config
from .events import EventLog
from .protocol import encode_request
from .transport import Inbox, SerialWorker


@dataclass
class Pending:
    verb: str
    deadline: float
    snapshot: dict | None = None


class Controller:
    def __init__(self, worker_factory=SerialWorker, now=time.monotonic):
        self.inbox = Inbox()
        self.worker_factory, self.now = worker_factory, now
        self.worker = None
        self.epoch = 0
        self._next_id = 1
        self._owner = threading.get_ident()
        self.pending = {}
        self.config = None
        self.revision = None
        self.remote_revision = None
        self.draft = copy.deepcopy(DEFAULT_CONFIG)
        self.draft_base = copy.deepcopy(DEFAULT_CONFIG)
        self.draft_revision = None
        self.draft_version = 0
        self.state = {}
        self.last_state_at = None
        self.ready = False
        self.connected = False
        self.connecting = False
        self.demo = False
        self.conflict = False
        self.uncertain = False
        self.message = '离线 · 选择串口后连接，或启动离线演示'
        self.messages = deque(maxlen=500)
        self.events = EventLog()
        self._bootstrap = None
        self._forced_dirty = False
        self._draft_held = False
        self._keep_reboot_draft = False
        self._last_device_uptime = None
        self._event_reset_at = None

    def _main_thread(self):
        if threading.get_ident() != self._owner:
            raise RuntimeError('Controller must be used on its owning GUI thread')

    @property
    def dirty(self):
        return self._forced_dirty or self.draft != self.draft_base

    @property
    def writing(self):
        return any(p.verb in ('apply', 'save') for p in self.pending.values())

    def notify(self, message):
        self.message = message
        self.messages.append((time.strftime('%H:%M:%S'), message))

    def connect(self, port='', *, demo=False):
        self._main_thread()
        self.disconnect(quiet=True)
        self.epoch += 1
        self.inbox.activate(self.epoch)
        self.demo = demo
        self.connecting = True
        self.config = self.revision = self.remote_revision = None
        self.state = {}
        self.last_state_at = None
        self._last_device_uptime = None
        self._event_reset_at = None
        self.conflict = self.uncertain = False
        self._bootstrap = 'ping'
        self.events.new_connection(self.epoch, demo)
        self.worker = self.worker_factory(self.epoch, self.inbox, port, demo=demo)
        self.notify('正在连接演示设备…' if demo else f'正在打开 {port}…')
        self.worker.start()

    def disconnect(self, quiet=False):
        self._main_thread()
        if self.worker:
            self.worker.stop()
        self.worker = None
        self.epoch += 1
        self.inbox.activate(self.epoch)
        self.pending.clear()
        self.ready = self.connected = self.connecting = False
        self._bootstrap = None
        self._last_device_uptime = None
        self._event_reset_at = None
        if not quiet:
            self.notify('已断开 · 演奏是否继续由设备决定；未重发任何命令')

    def _send(self, verb, arguments='', expected=-1, snapshot=None):
        if not self.worker or not self.connected:
            self.notify('设备尚未连接')
            return None
        if len(self.pending) >= 16:
            self.notify('待确认命令过多，请等待回复')
            return None
        request_id = self._next_id
        self._next_id = self._next_id % 0xffffffff + 1
        line = encode_request(request_id, expected, verb, arguments)
        if not self.worker.send(line):
            self.notify('发送队列已满或连接已关闭；命令未排队')
            return None
        self.pending[request_id] = Pending(verb, self.now() + (8 if verb == 'save' else 3), snapshot)
        return request_id

    def edit(self, patch):
        self._main_thread()
        proposed = copy.deepcopy(self.draft)
        proposed.update(patch)
        self.draft = validate_config(proposed)
        self._draft_held = False
        self.draft_version += 1

    def hold_draft(self):
        """Protect the baseline while an editor contains temporarily invalid text."""
        self._main_thread()
        self._draft_held = True

    def import_draft(self, config):
        self._main_thread()
        self.draft = validate_config(config)
        self._draft_held = False
        self._forced_dirty = True
        self.draft_version += 1
        self.notify('已导入草稿；尚未发送到设备')

    def discard_draft(self):
        self._main_thread()
        if self.config is None:
            self.draft = copy.deepcopy(DEFAULT_CONFIG)
            self.draft_base = copy.deepcopy(DEFAULT_CONFIG)
            self.draft_revision = None
        else:
            self.draft = copy.deepcopy(self.config)
            self.draft_base = copy.deepcopy(self.config)
            self.draft_revision = self.revision
        self._forced_dirty = self.conflict = False
        self._draft_held = False
        self.draft_version += 1
        self.notify('草稿已还原至最近回读的设备配置')

    def rebase_draft(self):
        self._main_thread()
        if self.config is None:
            raise ConfigError('尚未读取设备配置')
        edits = {key: copy.deepcopy(value) for key, value in self.draft.items()
                 if self._forced_dirty or value != self.draft_base[key]}
        proposed = copy.deepcopy(self.config)
        proposed.update(edits)
        self.draft = validate_config(proposed)
        self.draft_base = copy.deepcopy(self.config)
        self.draft_revision = self.revision
        self.conflict = False
        self.draft_version += 1
        self.notify(f'保留已编辑字段，以设备 r{self.revision} 为新基准；尚未应用')

    def apply(self):
        self._main_thread()
        if not self.ready or self.config is None or self.draft_revision is None:
            self.notify('连接握手完成后才能应用配置')
            return None
        if self.writing:
            self.notify('上一项应用或保存仍在等待设备确认')
            return None
        validate_config(self.draft)
        keys = [key for key in DEFAULT_CONFIG
                if self._forced_dirty or self.draft[key] != self.draft_base[key]]
        if not keys:
            self.notify('草稿没有改动')
            return None
        request_id = self._send('apply', apply_arguments(self.draft, keys),
                                self.draft_revision, copy.deepcopy(self.draft))
        if request_id:
            self.notify(f'应用 #{request_id} 等待实际 ACK；草稿尚未生效')
        return request_id

    def read_config(self):
        self._main_thread()
        if not any(p.verb == 'get' for p in self.pending.values()):
            return self._send('get')
        return None

    def save(self):
        self._main_thread()
        if not self.ready or self.writing:
            self.notify('连接未就绪或设备仍有待确认写入')
            return None
        request_id = self._send('save', expected=self.revision if self.revision is not None else -1)
        if request_id:
            self.notify('设备将暂停演奏、清音并写入 Flash；等待保存完成 ACK（只保存已应用配置）')
        return request_id

    def strike(self, channel, duration_ms):
        self._main_thread()
        if type(channel) is not int or not 1 <= channel <= 12:
            raise ValueError('试奏通道须为 1..12')
        if type(duration_ms) is not int or not 1 <= duration_ms <= 60000:
            raise ValueError('试奏 duration 须为 1..60000 ms')
        if not self.ready:
            self.notify('连接握手完成后才能试奏')
            return None
        return self._send('strike', f'{channel} {duration_ms}')

    def panic(self):
        self._main_thread()
        return self._send('panic')

    def _set_config(self, config, revision, pending=None):
        config = validate_config(config)
        had_draft = self.dirty or self._draft_held or self._keep_reboot_draft
        self.config, self.revision = config, revision
        self.remote_revision = revision
        self.uncertain = False
        if pending and pending.verb == 'apply':
            if self.draft == pending.snapshot:
                self.draft = copy.deepcopy(config)
                self._forced_dirty = False
            # An edit made while awaiting ACK remains a draft against that ACK.
            self.draft_base = copy.deepcopy(config)
            self.draft_revision = revision
            self.conflict = False
            self.draft_version += 1
        elif not had_draft:
            self.draft = copy.deepcopy(config)
            self.draft_base = copy.deepcopy(config)
            self.draft_revision = revision
            self.draft_version += 1
        elif self.draft_revision is None:
            # An offline draft is an explicit desired config, preserved on connection.
            self.draft_base = copy.deepcopy(config)
            self.draft_revision = revision
        self._keep_reboot_draft = False

    def _device_reboot(self, uptime):
        # CH343 can remain open across an MCU reset. Connection epoch alone is
        # therefore insufficient to invalidate old RPCs and applied revisions.
        self.pending.clear()
        self.ready = False
        self.connecting = True
        self.uncertain = True
        self.config = self.revision = self.remote_revision = None
        self.draft_revision = None
        self._keep_reboot_draft = True
        self._bootstrap = 'get'
        self._event_reset_at = uptime
        self.notify('检测到 B 板 uptime 回退：旧应用状态与待确认请求已失效，保留草稿并 get 回读')
        self.read_config()

    def _state(self, state):
        if not isinstance(state, dict):
            return
        uptime = state.get('uptime_ms')
        if type(uptime) is int and 0 <= uptime <= 0xffffffff:
            previous = self._last_device_uptime
            if previous is not None and uptime < previous and ((uptime - previous) & 0xffffffff) >= 0x80000000:
                self._device_reboot(uptime)
            self._last_device_uptime = uptime
            if self._event_reset_at is not None:
                self._event_reset_at = uptime
        self.state = state
        self.last_state_at = self.now()
        revision = state.get('revision')
        if type(revision) is int and 0 <= revision <= 0xffffffff:
            self.remote_revision = revision
            if self.ready and revision != self.revision:
                self.read_config()

    def _invalid_reply(self, pending, detail):
        self.uncertain = True
        if self._bootstrap is not None:
            self._bootstrap = None
            self.connecting = self.ready = False
            self.notify(f'握手失败：{detail}；请断开后重新连接')
        else:
            self.notify(f'{pending.verb} 回复无效：{detail}；需要回读')
            if pending.verb != 'get':
                self.read_config()

    def _reply(self, frame):
        request_id = frame.get('id')
        if type(request_id) is not int or request_id not in self.pending:
            return  # stale, uncorrelated, or late replies have no authority
        pending = self.pending.pop(request_id)
        revision = frame.get('revision')
        if type(frame.get('ok')) is not bool or type(revision) is not int or not 0 <= revision <= 0xffffffff:
            self._invalid_reply(pending, 'ok / revision 格式错误')
            return
        ok = frame['ok']
        code = frame.get('code', 'unknown')
        try:
            if 'config' in frame:
                self._set_config(frame['config'], revision, pending if ok else None)
            elif ok and pending.verb in ('get', 'ping', 'apply'):
                raise ConfigError('配置 ACK 缺少 config')
            if ok and pending.verb == 'status' and not isinstance(frame.get('state'), dict):
                raise ConfigError('status ACK 缺少 state')
        except ConfigError as exc:
            self._invalid_reply(pending, str(exc))
            return
        if isinstance(frame.get('state'), dict):
            self._state(frame['state'])
        if not ok:
            self.conflict = code == 'revision_conflict'
            self.notify(f'{pending.verb} #{request_id} 被拒绝：{code}；草稿保留')
            if self.conflict:
                self.read_config()
            if self._bootstrap == pending.verb:
                self.ready = False
                self._bootstrap = None
                self.connecting = False
            return
        if self._bootstrap == pending.verb:
            steps = {'ping': 'get', 'get': 'status', 'status': 'stream', 'stream': None}
            self._bootstrap = steps[pending.verb]
            if self._bootstrap:
                self._send(self._bootstrap, '200' if self._bootstrap == 'stream' else '')
            else:
                self.connecting = False
                self.ready = True
                self.notify(('演示模式 · ' if self.demo else '') + f'已连接 · 5 Hz 遥测 · 设备 r{self.revision}')
        elif pending.verb == 'apply':
            self.notify(f'应用 #{request_id} 已由设备 ACK · r{revision}'
                        + ('；仍有后续草稿改动' if self.dirty else ''))
        elif pending.verb == 'save':
            self.notify(f'设备已 ACK 保存完成 · r{revision}；演奏重新武装')
        elif pending.verb in ('panic', 'strike'):
            self.notify(f'{pending.verb} #{request_id} 已由设备 ACK')
        elif pending.verb == 'get':
            self.notify(f'已 get 回读设备 r{revision}；草稿保留，写入没有自动重发')

    def poll(self):
        self._main_thread()
        batch = self.inbox.drain()
        for epoch, kind, payload in batch.controls:
            if epoch != self.epoch:
                continue
            if kind == 'opened':
                self.connected = True
                self._send('ping')
            elif kind == 'frame':
                self._reply(payload)
            elif kind in ('error', 'closed'):
                message = payload.get('message', '串口已关闭')
                had_pending = bool(self.pending)
                self.disconnect(quiet=True)
                self.uncertain = had_pending
                self.notify(f'连接中断：{message}；待确认写入不重发，重新连接后先读取配置')
        if batch.fatal and batch.fatal[0] == self.epoch:
            self.disconnect(quiet=True)
            self.uncertain = True
            self.notify(batch.fatal[1])
        if batch.state and batch.state[0] == self.epoch:
            self._state(batch.state[1])
        for epoch, event, uptime in batch.events:
            if epoch == self.epoch:
                # Preserve pre-reset events already in this batch in their old
                # session, then begin the new session before its first low seq.
                if self._event_reset_at is not None and type(uptime) is int and uptime <= self._event_reset_at:
                    self.events.new_device_boot()
                    self._event_reset_at = None
                try:
                    self.events.add(event, uptime)
                except OSError as exc:
                    self.events.stop_capture()
                    self.notify(f'CSV 写入失败，记录已停止：{exc}')
        if self._event_reset_at is not None:
            # A reboot snapshot need not carry any events.
            self.events.new_device_boot()
            self._event_reset_at = None
        now = self.now()
        expired = [(request_id, pending) for request_id, pending in self.pending.items()
                   if now >= pending.deadline]
        need_readback = False
        for request_id, pending in expired:
            self.pending.pop(request_id, None)
            if pending.verb in ('apply', 'save', 'strike', 'panic'):
                self.uncertain = True
                need_readback = True
            if self._bootstrap == pending.verb:
                self._bootstrap = None
                self.connecting = False
                self.ready = False
            self.notify(f'{pending.verb} #{request_id} 超时；不自动重发'
                        + ('，正在 get 回读现状' if need_readback else ''))
        if need_readback:
            self.read_config()
