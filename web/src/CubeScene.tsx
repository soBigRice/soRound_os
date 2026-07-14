import { useEffect, useRef } from 'react';
import * as THREE from 'three';

type CubeSceneProps = {
  orientation: THREE.Quaternion;
  connected: boolean;
};

export function CubeScene({ orientation, connected }: CubeSceneProps) {
  const mountRef = useRef<HTMLDivElement | null>(null);
  const orientationRef = useRef(orientation);
  const connectedRef = useRef(connected);

  useEffect(() => {
    orientationRef.current = orientation;
  }, [orientation]);

  useEffect(() => {
    connectedRef.current = connected;
  }, [connected]);

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
    const target = new THREE.Quaternion();

    const resize = () => {
      const width = mount.clientWidth || 1;
      const height = mount.clientHeight || 1;
      camera.aspect = width / height;
      camera.updateProjectionMatrix();
      renderer.setSize(width, height);
    };

    const render = () => {
      target.copy(orientationRef.current);
      cube.quaternion.slerp(target, 0.16);
      body.material.color.setHex(connectedRef.current ? 0x20242b : 0x141418);
      renderer.render(scene, camera);
      raf = requestAnimationFrame(render);
    };

    window.addEventListener('resize', resize);
    resize();
    render();

    return () => {
      cancelAnimationFrame(raf);
      window.removeEventListener('resize', resize);
      mount.removeChild(renderer.domElement);
      renderer.dispose();
      body.geometry.dispose();
      body.material.dispose();
      faceMark.geometry.dispose();
      faceMark.material.dispose();
      grid.geometry.dispose();
      grid.material.dispose();
    };
  }, []);

  return <div className="scene" ref={mountRef} />;
}
