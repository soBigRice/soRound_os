import { lazy, Suspense, useCallback, useMemo, useRef, useState } from 'react';
import { Bluetooth, Box, Gauge, PlugZap, RefreshCcw, RotateCcw, Target } from 'lucide-react';
import { Quaternion } from 'three/src/math/Quaternion.js';
import { connectTwin, supportsWebBluetooth, type TwinConnection, type TwinFrame } from './bluetooth';
import { calibrateCurrentPose, computeOrientation, makeDefaultCalibration, makeOrientationRuntime } from './imu';

// WebGL/Three.js 独立成异步块,先让状态面板和连接按钮可交互,再加载较重的 3D 场景。
const CubeScene = lazy(async () => {
  const module = await import('./CubeScene');
  return { default: module.CubeScene };
});

type Status = 'idle' | 'connecting' | 'connected' | 'error';

const identity = new Quaternion();

export function App() {
  const [status, setStatus] = useState<Status>(supportsWebBluetooth() ? 'idle' : 'error');
  const [message, setMessage] = useState(supportsWebBluetooth() ? '未连接' : '当前浏览器不支持 Web Bluetooth');
  const [frame, setFrame] = useState<TwinFrame | null>(null);
  const [orientation, setOrientation] = useState(identity);
  const [calibrated, setCalibrated] = useState(false);
  const [frameRate, setFrameRate] = useState(0);

  const connectionRef = useRef<TwinConnection | null>(null);
  const calibrationRef = useRef(makeDefaultCalibration());
  const orientationRuntimeRef = useRef(makeOrientationRuntime());
  const rawOrientationRef = useRef(identity.clone());
  const lastDeviceUptimeRef = useRef<number | null>(null);
  const frameCounterRef = useRef({ count: 0, startedAt: performance.now() });

  const connected = status === 'connected';

  const handleFrame = useCallback((next: TwinFrame) => {
    const now = performance.now();
    const lastUptime = lastDeviceUptimeRef.current;
    const dtSeconds = lastUptime === null ? 0 : (next.uptimeMs - lastUptime) / 1000;
    lastDeviceUptimeRef.current = next.uptimeMs;

    const pose = computeOrientation(next, dtSeconds, calibrationRef.current, orientationRuntimeRef.current);
    rawOrientationRef.current = pose.raw;
    setOrientation(pose.calibrated.clone());
    setFrame(next);

    const counter = frameCounterRef.current;
    counter.count += 1;
    if (now - counter.startedAt >= 1000) {
      setFrameRate(Math.round((counter.count * 1000) / (now - counter.startedAt)));
      frameCounterRef.current = { count: 0, startedAt: now };
    }
  }, []);

  const handleDisconnect = useCallback(() => {
    connectionRef.current = null;
    lastDeviceUptimeRef.current = null;
    orientationRuntimeRef.current = makeOrientationRuntime();
    setStatus('idle');
    setMessage('已断开');
  }, []);

  const connect = async () => {
    if (connectionRef.current) {
      connectionRef.current.disconnect();
      handleDisconnect();
      return;
    }

    try {
      setStatus('connecting');
      setMessage('正在连接 GeekTwin');
      connectionRef.current = await connectTwin({ onFrame: handleFrame, onDisconnect: handleDisconnect });
      setStatus('connected');
      setMessage('已连接');
    } catch (error) {
      setStatus('error');
      setMessage(error instanceof Error ? error.message : String(error));
    }
  };

  const ping = async () => {
    if (!connectionRef.current) return;
    try {
      await connectionRef.current.ping();
      setMessage('Ping 已发送');
    } catch (error) {
      setStatus('error');
      setMessage(error instanceof Error ? error.message : String(error));
    }
  };

  const calibrate = () => {
    calibrationRef.current = calibrateCurrentPose(rawOrientationRef.current);
    setOrientation(new Quaternion());
    setCalibrated(true);
    setMessage('当前姿态已设为零位');
  };

  const resetCalibration = () => {
    calibrationRef.current = makeDefaultCalibration();
    orientationRuntimeRef.current = makeOrientationRuntime();
    lastDeviceUptimeRef.current = null;
    rawOrientationRef.current = identity.clone();
    setOrientation(identity.clone());
    setCalibrated(false);
    setMessage('校准已重置');
  };

  const batteryLabel = useMemo(() => {
    if (!frame) return '--';
    const charging = Boolean(frame.flags & 0x01);
    const full = Boolean(frame.flags & 0x02);
    return `${frame.soc}%${full ? ' 满' : charging ? ' 充电' : ''}`;
  }, [frame]);

  return (
    <main className="shell">
      <Suspense fallback={<div className="scene" aria-label="正在加载 3D 场景" />}>
        <CubeScene orientation={orientation} connected={connected} />
      </Suspense>

      <section className="panel status-panel" aria-label="设备状态">
        <div className="brand">
          <Box size={18} />
          <span>GeekTwin Calibrator</span>
        </div>
        <div className={`connection ${status}`}>
          <span className="dot" />
          <span>{message}</span>
        </div>

        <div className="metrics">
          <Metric icon={<PlugZap size={16} />} label="电量" value={batteryLabel} />
          <Metric icon={<Gauge size={16} />} label="帧率" value={`${frameRate || '--'} Hz`} />
          <Metric icon={<Target size={16} />} label="校准" value={calibrated ? '已设零位' : '原始姿态'} />
        </div>

        <div className="sensor-grid">
          <Sensor label="AX" value={frame?.ax} />
          <Sensor label="AY" value={frame?.ay} />
          <Sensor label="AZ" value={frame?.az} />
          <Sensor label="GZ" value={frame?.gz} unit="°/s" />
        </div>
      </section>

      <section className="toolbar" aria-label="控制">
        <button className="primary" onClick={connect} disabled={status === 'connecting'} title="连接或断开 GeekTwin">
          <Bluetooth size={18} />
          <span>{connected ? '断开' : status === 'connecting' ? '连接中' : '连接'}</span>
        </button>
        <button onClick={calibrate} disabled={!frame} title="把当前方块姿态设为零位">
          <Target size={18} />
          <span>设零位</span>
        </button>
        <button onClick={resetCalibration} title="清除姿态校准">
          <RotateCcw size={18} />
          <span>重置</span>
        </button>
        <button onClick={ping} disabled={!connected} title="向设备发送 Ping">
          <RefreshCcw size={18} />
          <span>Ping</span>
        </button>
      </section>
    </main>
  );
}

function Metric({ icon, label, value }: { icon: React.ReactNode; label: string; value: string }) {
  return (
    <div className="metric">
      <div className="metric-label">
        {icon}
        <span>{label}</span>
      </div>
      <strong>{value}</strong>
    </div>
  );
}

function Sensor({ label, value, unit = 'g' }: { label: string; value?: number; unit?: string }) {
  return (
    <div className="sensor">
      <span>{label}</span>
      <strong>{typeof value === 'number' ? `${value.toFixed(unit === 'g' ? 2 : 0)} ${unit}` : '--'}</strong>
    </div>
  );
}
