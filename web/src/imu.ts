import * as THREE from 'three';
import type { TwinFrame } from './bluetooth';

export type CalibrationState = {
  offset: THREE.Quaternion;
};

export type OrientationState = {
  raw: THREE.Quaternion;
  calibrated: THREE.Quaternion;
  yaw: number;
};

export type OrientationRuntime = {
  yaw: number;
  filteredUp: THREE.Vector3;
  hasFilteredUp: boolean;
};

const WORLD_UP = new THREE.Vector3(0, 1, 0);
const LOCAL_Z = new THREE.Vector3(0, 0, 1);
const DISPLAY_X_SIGN = -1;
const DISPLAY_Y_SIGN = 1;
const DISPLAY_Z_SIGN = 1;
const ACCEL_ALPHA = 0.18;
const YAW_DEADBAND_DPS = 1.2;
const MAX_FRAME_DT_SECONDS = 0.06;

// 轴向映射对齐实机手持方向:固件 imu.c 里右边压低会让 ay 变大、下边压低会让 ax 变大。
// 这些读数表示"哪一侧更低",而模型姿态需要表现屏幕法线的方向,所以屏幕平面两轴要反向:
// 右边压低时方块右边也压低,下边压低时方块下边也压低。
function mappedUpVector(frame: TwinFrame): THREE.Vector3 {
  return new THREE.Vector3(
    DISPLAY_X_SIGN * frame.ay,
    DISPLAY_Y_SIGN * frame.ax,
    DISPLAY_Z_SIGN * frame.az,
  );
}

function normalizeRadians(angle: number): number {
  const twoPi = Math.PI * 2;
  return THREE.MathUtils.euclideanModulo(angle + Math.PI, twoPi) - Math.PI;
}

function filteredUpVector(frame: TwinFrame, runtime: OrientationRuntime): THREE.Vector3 {
  const measured = mappedUpVector(frame);
  if (measured.lengthSq() < 1e-6) {
    return runtime.filteredUp;
  }

  measured.normalize();
  if (!runtime.hasFilteredUp) {
    runtime.filteredUp.copy(measured);
    runtime.hasFilteredUp = true;
  } else {
    runtime.filteredUp.lerp(measured, ACCEL_ALPHA).normalize();
  }
  return runtime.filteredUp;
}

function integrateYaw(frame: TwinFrame, dtSeconds: number, runtime: OrientationRuntime): number {
  const dt = Math.min(Math.max(dtSeconds, 0), MAX_FRAME_DT_SECONDS);
  const gz = Math.abs(frame.gz) < YAW_DEADBAND_DPS ? 0 : frame.gz;
  runtime.yaw = normalizeRadians(runtime.yaw + gz * Math.PI / 180 * dt);
  return runtime.yaw;
}

// 根据低通后的加速度绝对倾斜 + gz 积分偏航生成小方块姿态。
// 注意:没有磁力计时偏航无法长期绝对校准,这里只做死区和角度归一化,避免静止抖动和无限累积。
export function computeOrientation(
  frame: TwinFrame,
  dtSeconds: number,
  calibration: CalibrationState,
  runtime: OrientationRuntime,
): OrientationState {
  const up = filteredUpVector(frame, runtime);
  const tilt = new THREE.Quaternion();
  if (up.lengthSq() > 1e-6) {
    tilt.setFromUnitVectors(up, WORLD_UP);
  }

  const yaw = integrateYaw(frame, dtSeconds, runtime);
  const yawQ = new THREE.Quaternion().setFromAxisAngle(LOCAL_Z, yaw);
  const raw = tilt.multiply(yawQ);
  const calibrated = calibration.offset.clone().multiply(raw);

  return { raw, calibrated, yaw };
}

export function makeDefaultCalibration(): CalibrationState {
  return {
    offset: new THREE.Quaternion(),
  };
}

export function makeOrientationRuntime(): OrientationRuntime {
  return {
    yaw: 0,
    filteredUp: new THREE.Vector3(0, 0, 1),
    hasFilteredUp: false,
  };
}

// 把当前实机姿态设为零位:后续展示只应用 offset * raw,不能再额外减 yaw,否则会出现二次校准导致错位。
export function calibrateCurrentPose(raw: THREE.Quaternion): CalibrationState {
  return {
    offset: raw.clone().invert(),
  };
}
