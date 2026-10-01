// 按时间计算指数平滑,30/60/120Hz 的收敛时间一致。
export function smoothingAlpha(dtSeconds: number, timeConstant: number): number {
  return -Math.expm1(-Math.max(dtSeconds, 0) / timeConstant);
}
