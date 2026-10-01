import { useEffect, useRef, type MutableRefObject } from 'react';
import { smoothingAlpha } from './motion';
import * as THREE from 'three';

type CubeSceneProps = {
  orientationRef: MutableRefObject<THREE.Quaternion>;
  requestRenderRef: MutableRefObject<(() => void) | null>;
  connected: boolean;
};

export function CubeScene({ orientationRef, requestRenderRef, connected }: CubeSceneProps) {
  const mountRef = useRef<HTMLDivElement | null>(null);
  const connectedRef = useRef(connected);

  useEffect(() => {
    connectedRef.current = connected;
    requestRenderRef.current?.();
  }, [connected, requestRenderRef]);

  useEffect(() => {
    if (!mountRef.current) return undefined;

    const mount = mountRef.current;
    const scene = new THREE.Scene();
    const camera = new THREE.PerspectiveCamera(42, mount.clientWidth / mount.clientHeight, 0.1, 100);
    camera.position.set(0, 1.1, 4.2);
    camera.lookAt(0, 0, 0);

    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.setSize(mount.clientWidth, mount.clientHeight);
    mount.appendChild(renderer.domElement);

    scene.add(new THREE.AmbientLight(0xffffff, 0.58));
    const key = new THREE.DirectionalLight(0xffffff, 1.4);
    key.position.set(3, 5, 4);
    scene.add(key);

    const grid = new THREE.GridHelper(4.8, 12, 0x3d3d42, 0x242428);
    grid.position.y = -1.15;
    scene.add(grid);

    const cube = new THREE.Group();
    const body = new THREE.Mesh(
      new THREE.BoxGeometry(1.35, 1.35, 1.35),
      new THREE.MeshStandardMaterial({ color: 0x1c1d22, metalness: 0.18, roughness: 0.52 }),
    );
    cube.add(body);
    cube.add(
      new THREE.LineSegments(
        new THREE.EdgesGeometry(body.geometry),
        new THREE.LineBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.88 }),
      ),
    );

    const faceMark = new THREE.Mesh(
      new THREE.CircleGeometry(0.17, 32),
      new THREE.MeshBasicMaterial({ color: 0xd1283a }),
    );
    faceMark.position.set(0, 0.44, 0.681);
    cube.add(faceMark);

    const xAxis = new THREE.ArrowHelper(new THREE.Vector3(1, 0, 0), new THREE.Vector3(0, 0, 0), 1.15, 0xff5a5f, 0.14, 0.08);
    const yAxis = new THREE.ArrowHelper(new THREE.Vector3(0, 1, 0), new THREE.Vector3(0, 0, 0), 1.15, 0x30d158, 0.14, 0.08);
    const zAxis = new THREE.ArrowHelper(new THREE.Vector3(0, 0, 1), new THREE.Vector3(0, 0, 0), 1.15, 0x64d2ff, 0.14, 0.08);
    cube.add(xAxis, yAxis, zAxis);
    scene.add(cube);

    let raf = 0;
    let lastTime = 0;
    let disposed = false;
    const target = new THREE.Quaternion();

    const render = (now: number) => {
      raf = 0;
      if (disposed || document.hidden) return;
      const dt = lastTime ? Math.min((now - lastTime) / 1000, 0.1) : 1 / 60;
      lastTime = now;
      target.copy(orientationRef.current);
      cube.quaternion.slerp(target, smoothingAlpha(dt, 0.05));
      const moving = cube.quaternion.angleTo(target) > 0.001;
      if (!moving) cube.quaternion.copy(target);
      body.material.color.setHex(connectedRef.current ? 0x20242b : 0x141418);
      renderer.render(scene, camera);
      if (moving) raf = requestAnimationFrame(render);
    };
    const invalidate = () => {
      if (!disposed && !document.hidden && !raf) {
        lastTime = 0; raf = requestAnimationFrame(render);
      }
    };
    requestRenderRef.current = invalidate;
    const visibility = () => {
      if (document.hidden) { cancelAnimationFrame(raf); raf = 0; lastTime = 0; }
      else invalidate();
    };
    const resize = () => {
      const width = mount.clientWidth || 1;
      const height = mount.clientHeight || 1;
      camera.aspect = width / height;
      camera.updateProjectionMatrix();
      renderer.setSize(width, height);
      invalidate();
    };
    const observer = new ResizeObserver(resize);
    observer.observe(mount);
    document.addEventListener('visibilitychange', visibility);

    window.addEventListener('resize', resize);
    resize();
    invalidate();

    return () => {
      disposed = true;
      cancelAnimationFrame(raf);
      if (requestRenderRef.current === invalidate) requestRenderRef.current = null;
      window.removeEventListener('resize', resize);
      document.removeEventListener('visibilitychange', visibility);
      observer.disconnect();
      mount.removeChild(renderer.domElement);
      // 包括边框与 ArrowHelper,共享 geometry/material 只释放一次。
      const geometries = new Set<THREE.BufferGeometry>();
      const materials = new Set<THREE.Material>();
      scene.traverse((object) => {
        if (object instanceof THREE.Mesh || object instanceof THREE.Line) {
          geometries.add(object.geometry);
          const list = Array.isArray(object.material) ? object.material : [object.material];
          list.forEach((material) => materials.add(material));
        }
      });
      geometries.forEach((geometry) => geometry.dispose());
      materials.forEach((material) => material.dispose());
      renderer.dispose();
    };
  }, [orientationRef, requestRenderRef]);

  return <div className="scene" ref={mountRef} />;
}
