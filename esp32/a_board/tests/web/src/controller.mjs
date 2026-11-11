import { encodeCommand, postOptions, validateState, engineBlocked, HEARTBEAT_MS, STALE_KEY_MS, UINT32_MAX } from './protocol.mjs';

export class SynthController {
  constructor(transport, options = {}) {
    this.transport = transport;
    this.now = options.now || (() => performance.now());
    this.delay = options.delay || (ms => new Promise(resolve => setTimeout(resolve, ms)));
    this.onChange = options.onChange || (() => {});
    this.onClear = options.onClear || (() => {});
    this.ownerTimeout = options.ownerTimeout || 650;
    this.modeTimeout = options.modeTimeout || 2400;
    this.status = 'offline'; this.message = '连接设备后开始演奏。';
    this.token = 0; this.client = -1; this.sequence = 0;
    this.state = null; this.epoch = 0; this.inputVersion = 0;
    this.desiredMask = 0; this.sentMask = 0; this.velocity = 100;
    this.frames = []; this.parameters = new Map(); this.pending = new Map();
    this.modeRequest = null; this.decayRequest = null; this.panicRequest = false;
    this.modeWaiting = false; this.rearming = false; this.active = null; this.running = null;
    this.connecting = false; this.polling = false; this.suspended = false;
    this.lastHeartbeat = -Infinity; this.lastReceived = 0; this.lastAdopted = 0;
    this.retryAt = 0; this.retryDelay = 2000;
  }
  get playable() { return this.status === 'online' && !this.suspended && !this.modeWaiting && !this.rearming && this.modeRequest === null && !this.panicRequest && !engineBlocked(this.state); }
  get editable() { return this.playable; }
  get queued() { return this.frames.length + this.parameters.size + Number(this.decayRequest !== null); }
  notify() { this.onChange(this); }
  resetInput(message) {
    this.inputVersion++; this.desiredMask = 0; this.frames.length = 0;
    this.frames.push({ mask: 0, velocity: this.velocity, at: this.now() });
    this.onClear();
    if (message) this.message = message;
  }
  clearPending() {
    this.parameters.clear(); this.pending.clear(); this.decayRequest = null;
    this.modeRequest = null; this.panicRequest = false;
  }
  observe(raw) {
    const state = validateState(raw);
    if (this.state && ((state.revision - this.state.revision) >>> 0) > 2147483647) return;
    const previous = this.state; this.state = state;
    const reset = previous && (previous.panic !== state.panic || (previous.engine?.requested || 0) !== (state.engine?.requested || 0) || (!engineBlocked(previous) && engineBlocked(state)));
    if (reset) {
      this.rearming = true;
      this.resetInput('演奏已释放，请在引擎就绪后重新按键。');
      this.parameters.clear(); this.pending.clear(); this.decayRequest = null;
    }
    this.notify();
  }
  async connect() {
    if (this.connecting || this.suspended || this.status === 'online') return;
    this.connecting = true; this.status = 'connecting'; this.message = '正在读取设备当前音色…';
    const epoch = ++this.epoch; this.notify();
    try {
      const state = await this.transport.request('/api/state');
      if (epoch !== this.epoch) return;
      this.state = null; this.observe(state);
      if (!state.ready) throw new Error('device not ready');
      const session = await this.transport.request('/api/session', postOptions(''));
      if (epoch !== this.epoch) return;
      if (!Number.isInteger(session.token) || session.token <= 0 || session.token > UINT32_MAX || !Number.isInteger(session.client) || session.client < 0 || session.client > 3 || session.lease_ms !== 1200) throw new Error('invalid session');
      this.token = session.token; this.client = session.client; this.sequence = 0;
      this.sentMask = 0; this.resetInput(); this.clearPending(); this.error = null;
      // Bind owner state and re-arm with an empty bitmap before enabling keys.
      const command = await this.send(3, 0, this.velocity, epoch);
      await this.waitFor(command, epoch, this.ownerTimeout, true);
      if (epoch !== this.epoch) return;
      this.frames.length = 0; this.rearming = false; this.status = 'online'; this.retryDelay = 2000;
      this.message = '已读取当前音色，可以演奏。实际声音请在设备端确认。';
    } catch (error) { if (epoch === this.epoch) this.fail(error); }
    finally { this.connecting = false; this.notify(); }
  }
  nextSequence() {
    if (this.sequence >= UINT32_MAX) throw new Error('sequence exhausted');
    return ++this.sequence;
  }
  async send(op, a, b, epoch) {
    const sequence = this.nextSequence();
    const command = { token: this.token, client: this.client, sequence, op, a, b, inputVersion: this.inputVersion };
    if (op === 3) this.lastHeartbeat = this.now();
    const response = await this.transport.request('/api/command', postOptions(encodeCommand(command.token, sequence, op, a, b)));
    if (epoch !== this.epoch) return command;
    if (response.accepted !== sequence || response.stage !== 'received' || response.fpga !== 'unverified') throw new Error('invalid receipt');
    this.lastReceived = sequence; this.message = op === 3 ? '演奏请求已接收，等待控制任务采用。' : '调整请求已接收，等待控制任务采用。';
    this.notify(); return command;
  }
  async waitFor(command, epoch, timeout, initial = false) {
    const start = this.now();
    while (epoch === this.epoch) {
      const state = await this.transport.request('/api/state');
      if (epoch !== this.epoch) return false;
      this.observe(state);
      const slot = command.client;
      if (state.tokens[slot] !== command.token) {
        if (!initial) throw new Error('session replaced');
      } else {
        if (command.op === 3 && command.a && (command.inputVersion !== this.inputVersion || engineBlocked(state))) return false;
        let adopted = command.op === 3 ? state.processed[slot] >= command.sequence && state.keys[slot] === command.a
          : command.op === 4 ? state.stop_processed[slot] >= command.sequence
          : state.parameter_processed[slot] >= command.sequence;
        if (command.op === 5) {
          const engine = state.engine;
          adopted = adopted && (engine ? engine.requested === command.a && engine.applied === command.a && !engine.switching && !engine.recovering && (engine.valid || command.a === 0 && !engine.available) : command.a === 0);
        }
        if (adopted) { this.lastAdopted = command.sequence; return true; }
      }
      if (this.now() - start >= timeout) throw new Error(command.op === 5 ? 'mode readback timeout' : 'owner adoption timeout');
      await this.delay(25);
    }
    return false;
  }
  keys(mask) {
    if (!this.playable || !Number.isInteger(mask) || mask < 0 || mask > 33554431) return false;
    const tail = this.frames.at(-1)?.mask ?? (this.active?.op === 3 ? this.active.a : this.sentMask);
    this.desiredMask = mask;
    if (mask === tail) return true;
    if (this.frames.length >= 32) { this.resetInput('按键请求积压，已释放，请重新按键。'); this.notify(); }
    else this.frames.push({ mask, velocity: this.velocity, at: this.now() });
    this.kick(); return true;
  }
  setVelocity(value) { this.velocity = Math.max(1, Math.min(127, Math.round(Number(value) || 100))); }
  cc(number, value) {
    if (!this.editable || ((this.state?.engine?.requested || 0) === 1 && number !== 7 && number !== 10)) return false;
    encodeCommand(1, 1, 2, number, value);
    this.parameters.set(number, value); this.pending.set(number, value); this.notify(); this.kick(); return true;
  }
  decay(value) {
    if (!this.editable || !this.state?.engine?.available || this.state.engine.requested !== 1) return false;
    encodeCommand(1, 1, 6, value, 0); this.decayRequest = value; this.pending.set('decay', value); this.notify(); this.kick(); return true;
  }
  mode(value) {
    if (!this.editable || ![0,1].includes(value) || (value === 1 && !this.state?.engine?.available)) return false;
    this.resetInput('正在切换引擎，请稍候…'); this.clearPending(); this.modeRequest = value; this.modeWaiting = true;
    this.notify(); this.kick(); return true;
  }
  releaseOwn() {
    if (this.status !== 'online') return;
    this.resetInput('已释放本网页按键，其他来源继续演奏。'); this.parameters.clear(); this.pending.clear(); this.decayRequest = null;
    this.notify(); this.kick();
  }
  panic() {
    if (this.status !== 'online' || this.suspended) return;
    this.resetInput('正在停止所有来源，包括蓝牙与其他网页。'); this.clearPending(); this.panicRequest = true; this.rearming = true;
    if (this.active?.op !== 5) this.modeWaiting = false;
    this.notify(); this.kick();
  }
  emergencyRelease() {
    if (!this.token || this.sequence >= UINT32_MAX) return;
    const body = encodeCommand(this.token, this.nextSequence(), 3, 0, this.velocity);
    this.transport.release(body);
  }
  suspend(reason = '页面已离开，网页按键已释放。') {
    if (this.suspended) return;
    this.epoch++; this.suspended = true; this.resetInput(); this.clearPending(); this.modeWaiting = false; this.rearming = true;
    this.emergencyRelease(); this.status = 'paused'; this.message = reason; this.notify();
  }
  resume() {
    if (!this.suspended) return;
    this.suspended = false; this.resetInput(); this.clearPending();
    if (this.token) { this.status = 'online'; this.message = '已回到页面，请重新按键。'; this.kick(); }
    else { this.status = 'offline'; this.retryAt = 0; }
    this.notify();
  }
  fail(error) {
    this.epoch++; this.resetInput(); this.clearPending(); this.modeWaiting = false; this.rearming = true;
    this.emergencyRelease(); this.token = 0; this.status = 'offline';
    const messages = { 503: '设备的四个控制席位已满，请关闭闲置页面后重试。', 410: '会话已过期，请重新连接。', 409: '请求顺序已失效，已释放按键，请重新连接。', 403: '设备拒绝控制请求，请确认页面由设备或本地代理提供。', 400: '设备拒绝了请求格式，已释放按键。' };
    this.message = messages[error?.status] || (error?.message === 'device not ready' ? '设备尚未就绪，稍后重试。' : '连接中断或确认超时。按键将由 1.2 秒租约释放，请重新连接并按键。');
    this.error = error?.message || String(error); this.retryAt = this.now() + this.retryDelay;
    this.retryDelay = Math.min(12000, this.retryDelay * 2); this.notify();
  }
  async poll() {
    if (this.status !== 'online' || this.suspended || this.polling || this.running) return;
    this.polling = true; const epoch = this.epoch;
    try {
      const state = await this.transport.request('/api/state');
      if (epoch !== this.epoch) return;
      if (state.tokens[this.client] !== this.token) throw new Error('session replaced');
      this.observe(state);
      if (!state.ready) throw new Error('device not ready');
      // Owner releasing a held mask (e.g. lease expiry) must never be renewed.
      if (this.sentMask && state.keys[this.client] !== this.sentMask) { this.rearming = true; this.resetInput('演奏租约已释放，请重新按键。'); }
      this.kick();
    } catch (error) { if (epoch === this.epoch) this.fail(error); }
    finally { this.polling = false; }
  }
  tick() {
    if (this.suspended) return;
    if (this.status === 'offline' && this.now() >= this.retryAt) this.connect();
    else this.kick();
  }
  kick() {
    if (this.running || this.status !== 'online' || this.suspended) return this.running;
    const epoch = this.epoch;
    this.running = this.drain(epoch).finally(() => { this.running = null; this.active = null; if (this.status === 'online' && !this.suspended && (this.frames.length || this.parameters.size || this.modeRequest !== null || this.decayRequest !== null || this.panicRequest)) queueMicrotask(() => this.kick()); });
    return this.running;
  }
  async drain(epoch) {
    try {
      while (epoch === this.epoch && this.status === 'online' && !this.suspended) {
        let op, a, b, parameter;
        if (this.panicRequest) { this.panicRequest = false; op = 4; a = b = 0; }
        else if (this.modeRequest !== null) { op = 5; a = this.modeRequest; b = 0; this.modeRequest = null; }
        else if (this.frames.length || this.now() - this.lastHeartbeat >= HEARTBEAT_MS) {
          let frame = this.frames.shift() || { mask: this.playable ? this.desiredMask : 0, velocity: this.velocity, at: this.now() };
          if (frame.mask && (this.now() - frame.at > STALE_KEY_MS || !this.playable)) { this.resetInput('触控请求已延迟，已释放，请重新按键。'); frame = this.frames.shift(); }
          op = 3; a = frame.mask; b = frame.velocity;
        } else if (this.decayRequest !== null) { op = 6; a = this.decayRequest; b = 0; this.decayRequest = null; parameter = 'decay'; }
        else if (this.parameters.size) { [a,b] = this.parameters.entries().next().value; this.parameters.delete(a); op = 2; parameter = a; }
        else break;
        this.active = { op, a, b };
        const command = await this.send(op, a, b, epoch);
        if (epoch !== this.epoch) break;
        const adopted = await this.waitFor(command, epoch, op === 5 ? this.modeTimeout : this.ownerTimeout);
        if (epoch !== this.epoch) break;
        if (adopted) {
          if (op === 3) { this.sentMask = a; if (a === 0) this.rearming = false; }
          if (parameter !== undefined && this.pending.get(parameter) === (op === 2 ? b : a)) this.pending.delete(parameter);
          if (op === 5) { this.modeWaiting = false; this.resetInput(); }
          this.message = op === 4 ? '控制任务已采用全来源停止，实际静音请在设备端确认。' : op === 5 ? '引擎模式已读回，请重新按键。' : '控制任务已采用请求。实际声音请在设备端确认。';
        }
        this.active = null; this.notify();
      }
    } catch (error) { if (epoch === this.epoch) this.fail(error); }
  }
}
