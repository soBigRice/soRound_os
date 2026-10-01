import assert from 'node:assert/strict';
import { Quaternion } from 'three/src/math/Quaternion.js';
import { Vector3 } from 'three/src/math/Vector3.js';
import { smoothingAlpha } from '../src/motion';
import { parseTwinFrame, connectTwin, type TwinFrame } from '../src/bluetooth';
import { computeOrientation, makeDefaultCalibration, makeOrientationRuntime, calibrateCurrentPose, deviceFrameTiming } from '../src/imu';

const base: TwinFrame = { flags: 0, soc: 60, seq: 1, ax: 0, ay: 0, az: 1, gx: 0, gy: 0, gz: 0, uptimeMs: 100 };
let cases = 0;
function check(name: string, fn: () => void) { fn(); cases++; console.log(`PASS ${name}`); }

check('20-byte little-endian frame and version/length rejection', () => {
  const view = new DataView(new ArrayBuffer(20));
  view.setUint8(0, 1); view.setUint8(1, 3); view.setUint8(2, 85); view.setUint8(3, 255);
  view.setInt16(4, -1000, true); view.setInt16(6, 500, true); view.setInt16(8, 2000, true);
  view.setInt16(10, -120, true); view.setInt16(12, 320, true); view.setInt16(14, -5120, true);
  view.setUint32(16, 0xfffffff0, true);
  assert.deepEqual(parseTwinFrame(view), { flags: 3, soc: 85, seq: 255, ax: -1, ay: 0.5, az: 2,
    gx: -12, gy: 32, gz: -512, uptimeMs: 0xfffffff0 });
  assert.equal(parseTwinFrame(new DataView(new ArrayBuffer(19))), null);
  view.setUint8(0, 2); assert.equal(parseTwinFrame(view), null);
});
check('uint32 uptime wrap and device restart/gap handling', () => {
  assert.deepEqual(deviceFrameTiming(17, 0xfffffff0), { dtSeconds: 0.033, restarted: false });
  assert.deepEqual(deviceFrameTiming(20, 10000), { dtSeconds: 0, restarted: true });
  assert.deepEqual(deviceFrameTiming(4000, 100), { dtSeconds: 0, restarted: true });
  assert.deepEqual(deviceFrameTiming(100, null), { dtSeconds: 0, restarted: false });
});
check('screen axis mapping and one-time calibration', () => {
  const frame = { ...base, ax: 0.3, ay: 0.4, az: Math.sqrt(0.75) };
  const runtime = makeOrientationRuntime();
  const pose = computeOrientation(frame, 0, makeDefaultCalibration(), runtime);
  const expected = new Quaternion().setFromUnitVectors(new Vector3(-0.4, 0.3, frame.az).normalize(), new Vector3(0, 1, 0));
  assert(pose.raw.angleTo(expected) < 1e-6);
  const zero = computeOrientation(frame, 0.033, calibrateCurrentPose(pose.raw), runtime);
  assert(zero.calibrated.angleTo(new Quaternion()) < 1e-6);
});
check('yaw deadband, bounded gap and no invalid accelerometer NaN', () => {
  const runtime = makeOrientationRuntime(); const calibration = makeDefaultCalibration();
  computeOrientation({ ...base, gz: 1 }, 0.033, calibration, runtime); assert.equal(runtime.yaw, 0);
  const pose = computeOrientation({ ...base, ax: 0, ay: 0, az: 0, gz: 90 }, 100, calibration, runtime);
  assert(Math.abs(pose.yaw - Math.PI / 2 * 0.06) < 1e-9);
  assert(Number.isFinite(pose.raw.w));
});
for (const hz of [30, 60, 120]) check(`animation convergence at ${hz}Hz`, () => {
  const q = new Quaternion(); const target = new Quaternion().setFromAxisAngle(new Vector3(0, 0, 1), Math.PI / 2);
  for (let i = 0; i < hz / 5; i++) q.slerp(target, smoothingAlpha(1 / hz, 0.05));
  assert(Math.abs(q.angleTo(target) - Math.PI / 2 * Math.exp(-0.2 / 0.05)) < 1e-9);
});
check('sensor filter similar response at 30/60/120Hz', () => {
  const results = [30, 60, 120].map((hz) => {
    const runtime = makeOrientationRuntime(); const calibration = makeDefaultCalibration();
    computeOrientation(base, 0, calibration, runtime);
    for (let i = 0; i < hz; i++) computeOrientation({ ...base, ay: 0.5, az: Math.sqrt(0.75) }, 1 / hz, calibration, runtime);
    return runtime.filteredUp;
  });
  assert(results[0].angleTo(results[2]) < 0.002);
});

class CountedTarget extends EventTarget {
  listeners = new Map<string, Set<EventListenerOrEventListenerObject>>();
  override addEventListener(type: string, listener: EventListenerOrEventListenerObject | null) {
    if (listener) { const set = this.listeners.get(type) ?? new Set(); set.add(listener); this.listeners.set(type, set); }
    super.addEventListener(type, listener);
  }
  override removeEventListener(type: string, listener: EventListenerOrEventListenerObject | null) {
    if (listener) this.listeners.get(type)?.delete(listener); super.removeEventListener(type, listener);
  }
  count() { return [...this.listeners.values()].reduce((n, set) => n + set.size, 0); }
}
async function connectionCase(kind: 'natural' | 'manual' | 'failure') {
  const tx = new CountedTarget() as CountedTarget & { startNotifications: () => Promise<unknown> };
  tx.startNotifications = async () => { if (kind === 'failure') throw new Error('subscribe failed'); return tx; };
  const rx = { writeValueWithoutResponse: async () => undefined };
  const device = new CountedTarget() as CountedTarget & { gatt: { connected: boolean; connect: () => Promise<unknown>; disconnect: () => void } };
  const service = { getCharacteristic: async (uuid: string) => uuid.includes('0002') ? tx : rx };
  const server = { getPrimaryService: async () => service };
  device.gatt = { connected: true, connect: async () => server, disconnect: () => {
    if (device.gatt.connected) { device.gatt.connected = false; device.dispatchEvent(new Event('gattserverdisconnected')); }
  } };
  Object.defineProperty(globalThis, 'navigator', { value: { bluetooth: { requestDevice: async () => device } }, configurable: true });
  let disconnects = 0;
  if (kind === 'failure') await assert.rejects(connectTwin({ onFrame: () => {}, onDisconnect: () => disconnects++ }), /subscribe failed/);
  else {
    const connection = await connectTwin({ onFrame: () => {}, onDisconnect: () => disconnects++ });
    if (kind === 'natural') device.gatt.disconnect(); else { connection.disconnect(); connection.disconnect(); }
    assert.equal(disconnects, kind === 'natural' ? 1 : 0);
  }
  assert.equal(tx.count(), 0); assert.equal(device.count(), 0); assert.equal(device.gatt.connected, false);
  cases++; console.log(`PASS Bluetooth ${kind} disconnect listener cleanup`);
}
for (const kind of ['natural', 'manual', 'failure'] as const) await connectionCase(kind);
console.log(`${cases} regression groups passed`);
