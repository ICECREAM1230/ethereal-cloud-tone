import { packPair as pack } from '../generated/fm-contract.mjs';

export function readTarget() { return pack(0xffff); }
