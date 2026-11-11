// 仅用于打包器测试；不是正式 FM 契约，不进入产品 src。
export const PREFIX = 0xffff0000;
export function packPair(low) { return (PREFIX + low) >>> 0; }
export function label(value) { return `fixture:${value}`; }
export class TargetView {
  constructor(value) { this.value = value; }
}
