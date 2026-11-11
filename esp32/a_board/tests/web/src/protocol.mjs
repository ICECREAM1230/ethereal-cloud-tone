// Frozen HTTP ABI: handoff/reference/.../web_control.c, 2026-10-05.
export const CC_ALLOWED = Object.freeze([7,10,14,15,20,21,22,23,24,25,26,27,28,29,30,31,71,72,73,74,75,76,77,79,85,86,87,102,103,104,105,106,107,109,110,111,112,113]);
export const KEY_MASK = 33554431;
export const HEARTBEAT_MS = 250;
export const LEASE_MS = 1200;
export const STALE_KEY_MS = 300;
export const UINT32_MAX = 4294967295;
export function uint32(value) {
  return Number.isInteger(value) && value >= 0 && value <= UINT32_MAX;
}
export function encodeCommand(token, sequence, op, a, b) {
  const fields = [token, sequence, op, a, b];
  if (!fields.every(uint32) || !token || !sequence) throw new RangeError('invalid uint32/token/sequence');
  const valid = op === 2 ? CC_ALLOWED.includes(a) && b <= 127
    : op === 3 ? a <= KEY_MASK && b >= 1 && b <= 127
    : op === 4 ? a === 0 && b === 0
    : op === 5 ? a <= 1 && b === 0
    : op === 6 ? a >= 200 && a <= 8000 && b === 0 : false;
  if (!valid) throw new RangeError('command outside frozen ABI');
  return fields.join(' ');
}
export function decodeCommand(body) {
  if (typeof body !== 'string' || body.length < 1 || body.length > 95 || !/^[0-9]+ [0-9]+ [0-9]+ [0-9]+ [0-9]+$/.test(body)) throw new RangeError('invalid command text');
  const values = body.split(' ').map(Number);
  encodeCommand(...values);
  return { token: values[0], sequence: values[1], op: values[2], a: values[3], b: values[4] };
}
export function postOptions(body, keepalive = false) {
  return { method: 'POST', headers: { 'X-FPGA-Control': '1', 'Content-Type': 'text/plain' }, body, keepalive };
}
export function validateState(state) {
  if (!state || typeof state.ready !== 'boolean' || !uint32(state.revision) || !uint32(state.panic)) throw new Error('invalid device state');
  for (const key of ['tokens','processed','parameter_processed','stop_processed','keys']) {
    if (!Array.isArray(state[key]) || state[key].length !== 4 || !state[key].every(uint32)) throw new Error('invalid device state: ' + key);
  }
  if (!state.keys.every(value => value <= KEY_MASK)) throw new Error('invalid device key mask');
  // Legacy DDS firmware may omit the engine capability block.
  if (state.engine && (!['requested','applied'].every(key => state.engine[key] === 0 || state.engine[key] === 1))) throw new Error('invalid engine state');
  if (state.engine) for (const key of ['available','valid','switching']) if (typeof state.engine[key] !== 'boolean') throw new Error('invalid engine capability: ' + key);
  if (state.engine) for (const key of ['recovering','busy']) if (state.engine[key] !== undefined && typeof state.engine[key] !== 'boolean') throw new Error('invalid engine status: ' + key);
  const arrays = { osc: [2,4], filter: [5], amp: [4], mod: [4], lfo: [2,4], velocity: [2] };
  for (const [key, dimensions] of Object.entries(arrays)) {
    const value = state[key];
    if (!Array.isArray(value) || value.length !== dimensions[0]) throw new Error('missing patch: ' + key);
    if (dimensions.length === 2 && !value.every(row => Array.isArray(row) && row.length === dimensions[1] && row.every(Number.isFinite))) throw new Error('invalid patch: ' + key);
    if (dimensions.length === 1 && !value.every(Number.isFinite)) throw new Error('invalid patch: ' + key);
  }
  for (const key of ['voice','mix','detune','spread','volume','pan','cutoff_offset','resonance_offset','mod_depth']) if (!Number.isFinite(state[key])) throw new Error('missing patch: ' + key);
  return state;
}
export function engineBlocked(state) {
  const engine = state?.engine;
  if (!state?.ready) return true;
  if (!engine) return false;
  return !!(engine.switching || engine.recovering || engine.requested !== engine.applied || (engine.available && !engine.valid) || (!engine.available && engine.requested === 1));
}
export class HttpError extends Error {
  constructor(status) { super('HTTP ' + status); this.status = status; }
}
export function createTransport(fetcher = globalThis.fetch.bind(globalThis), timeoutMs = 900) {
  return {
    async request(path, options = {}) {
      const abort = new AbortController();
      const timer = setTimeout(() => abort.abort(), timeoutMs);
      try {
        const response = await fetcher(path, { ...options, cache: 'no-store', signal: abort.signal });
        if (!response.ok) throw new HttpError(response.status);
        return await response.json();
      } finally { clearTimeout(timer); }
    },
    release(body) {
      // A custom header is required, so sendBeacon cannot implement this ABI.
      try { return Promise.resolve(fetcher('/api/command', postOptions(body, true))).catch(() => {}); }
      catch { return Promise.resolve(); }
    }
  };
}
