import { readTarget as read } from './nested/read-target.mjs';
import { TargetView as View, label } from './generated/fm-contract.mjs';

globalThis.result = new View(read()).value;
globalThis.label = label(globalThis.result);
