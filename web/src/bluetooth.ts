export const TWIN_SERVICE_UUID = 'c0de0001-feed-face-cafe-0123456789ab';
export const TWIN_TX_UUID = 'c0de0002-feed-face-cafe-0123456789ab';
export const TWIN_RX_UUID = 'c0de0003-feed-face-cafe-0123456789ab';

export type TwinFrame = {
  flags: number;
  soc: number;
  seq: number;
  ax: number;
  ay: number;
  az: number;
  gx: number;
  gy: number;
  gz: number;
  uptimeMs: number;
};

export type TwinConnection = {
  device: BluetoothDevice;
  rxChar: BluetoothRemoteGATTCharacteristic;
  disconnect: () => void;
  ping: () => Promise<void>;
};

// 解析固件 app_twin.c 发出的 20 字节小端数据帧;字段顺序必须和固件保持一致。
export function parseTwinFrame(value: DataView): TwinFrame | null {
  if (value.byteLength < 20 || value.getUint8(0) !== 0x01) return null;
  return {
    flags: value.getUint8(1),
    soc: value.getUint8(2),
    seq: value.getUint8(3),
    ax: value.getInt16(4, true) / 1000,
    ay: value.getInt16(6, true) / 1000,
    az: value.getInt16(8, true) / 1000,
    gx: value.getInt16(10, true) / 10,
    gy: value.getInt16(12, true) / 10,
    gz: value.getInt16(14, true) / 10,
    uptimeMs: value.getUint32(16, true),
  };
}

export function supportsWebBluetooth(): boolean {
  return Boolean(navigator.bluetooth);
}

type ConnectOptions = {
  onFrame: (frame: TwinFrame) => void;
  onDisconnect: () => void;
};

// 建立 Web Bluetooth 连接并订阅 TX notify;RX 只用于向设备发送一字节 ping。
export async function connectTwin({ onFrame, onDisconnect }: ConnectOptions): Promise<TwinConnection> {
  if (!navigator.bluetooth) {
    throw new Error('当前浏览器不支持 Web Bluetooth');
  }

  const device = await navigator.bluetooth.requestDevice({
    filters: [{ services: [TWIN_SERVICE_UUID] }],
    optionalServices: [TWIN_SERVICE_UUID],
  });

  device.addEventListener('gattserverdisconnected', onDisconnect);
  const server = await device.gatt?.connect();
  if (!server) throw new Error('GATT 连接失败');

  const service = await server.getPrimaryService(TWIN_SERVICE_UUID);
  const txChar = await service.getCharacteristic(TWIN_TX_UUID);
  const rxChar = await service.getCharacteristic(TWIN_RX_UUID);

  const handleFrame = (event: Event) => {
    const target = event.target as BluetoothRemoteGATTCharacteristic;
    if (!target.value) return;
    const frame = parseTwinFrame(target.value);
    if (frame) onFrame(frame);
  };

  await txChar.startNotifications();
  txChar.addEventListener('characteristicvaluechanged', handleFrame);

  return {
    device,
    rxChar,
    disconnect: () => {
      txChar.removeEventListener('characteristicvaluechanged', handleFrame);
      device.removeEventListener('gattserverdisconnected', onDisconnect);
      device.gatt?.disconnect();
    },
    ping: async () => {
      const data = new Uint8Array([0x01]);
      if ('writeValueWithoutResponse' in rxChar && rxChar.writeValueWithoutResponse) {
        await rxChar.writeValueWithoutResponse(data);
      } else {
        await rxChar.writeValue(data);
      }
    },
  };
}
