/* 浏览器圆屏交互稿：真实地图、触摸圆弧，无定位或设备遥测。
 * Browser circular UI: real map and touch arc, without geolocation or device telemetry. */
(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const places = [
    { name: '上海 · 外滩', city: '上海', detail: '外滩', center: [121.4892, 31.239], zoom: 14.2 },
    { name: '杭州 · 西湖', city: '杭州', detail: '西湖', center: [120.149, 30.249], zoom: 13.5 },
    { name: '北京 · 景山', city: '北京', detail: '景山', center: [116.391, 39.923], zoom: 14.5 },
    { name: '成都 · 太古里', city: '成都', detail: '太古里', center: [104.081, 30.652], zoom: 14.5 }
  ];
  const minZoom = 4, maxZoom = 18, arcRadius = 212, arcHalfAngle = 26;
  let selected = 0, map, disposed = false, mapReady = false, sourceFailed = false, loadTimer, activePointer = null;
  const observer = new ResizeObserver(resize);
  const clamp = (value, min, max) => Math.min(max, Math.max(min, value));

  function resize() {
    const diameter = Math.min(492, document.querySelector('.device-figure').clientWidth);
    $('device-shell').style.width = `${diameter}px`;
    $('device-shell').style.height = `${diameter}px`;
    $('screen').style.transform = `translate(-50%, -50%) scale(${diameter / 492})`;
    if (map && !disposed) map.resize();
  }
  function notice(text, retry = false) {
    $('notice-text').textContent = text; $('notice').hidden = !text; $('retry').hidden = !retry;
  }
  // 默认浏览比例位于中点，上下半轨分别保留完整远景与街道范围。
  // Center the initial view on the arc while retaining the full overview/street range.
  function zoomToProgress(zoom) {
    const middle = places[selected].zoom;
    return zoom <= middle ? .5 * (zoom - minZoom) / (middle - minZoom) : .5 + .5 * (zoom - middle) / (maxZoom - middle);
  }
  function progressToZoom(progress) {
    const middle = places[selected].zoom;
    return progress <= .5 ? minZoom + progress * 2 * (middle - minZoom) : middle + (progress - .5) * 2 * (maxZoom - middle);
  }
  function updateView() {
    if (!map || disposed) return;
    const zoom = map.getZoom();
    const angle = (arcHalfAngle - zoomToProgress(zoom) * arcHalfAngle * 2) * Math.PI / 180;
    $('zoom-thumb').setAttribute('cx', String(233 + arcRadius * Math.cos(angle)));
    $('zoom-thumb').setAttribute('cy', String(233 + arcRadius * Math.sin(angle)));
    $('zoom-arc').setAttribute('aria-valuenow', zoom.toFixed(1));
    $('zoom-arc').setAttribute('aria-valuetext', `缩放级别 ${zoom.toFixed(1)}`);
    $('zoom-in').disabled = zoom >= maxZoom - .01; $('zoom-out').disabled = zoom <= minZoom + .01;
    const center = map.getCenter();
    map.getCanvas().setAttribute('aria-label', `地图浏览区域，中心纬度 ${center.lat.toFixed(5)}，经度 ${center.lng.toFixed(5)}，缩放 ${zoom.toFixed(1)}`);
  }
  function setZoom(zoom) {
    if (!map || !mapReady || disposed) return;
    map.stop(); map.setZoom(clamp(zoom, minZoom, maxZoom));
  }
  function home() {
    if (!map || !mapReady || disposed) return;
    map.stop(); map.jumpTo({ center: places[selected].center, zoom: places[selected].zoom, bearing: 0, pitch: 0 });
  }
  function picker(open) {
    activePointer = null;
    $('screen').dataset.picking = String(open); $('places').hidden = !open;
    $('place').setAttribute('aria-expanded', String(open));
    $('place-name').textContent = open ? '选择地点' : places[selected].name;
    $('back').setAttribute('aria-label', open ? '返回地图' : '返回地点选择');
    if (map) { open ? map.keyboard.disable() : map.keyboard.enable(); map.getCanvas().tabIndex = open ? -1 : 0; }
    if (open) $('place-list').querySelector('[aria-pressed="true"]').focus();
    else $('place').focus({ preventScroll: true });
  }
  places.forEach((place, index) => {
    const button = document.createElement('button'); button.className = 'place-option';
    button.setAttribute('aria-label', place.name); button.setAttribute('aria-pressed', String(index === selected));
    const title = document.createElement('span'), detail = document.createElement('small');
    title.textContent = place.city; detail.textContent = place.detail; title.append(detail); button.append(title);
    button.addEventListener('click', () => {
      selected = index; $('place').setAttribute('aria-label', place.name);
      [...$('place-list').children].forEach((item, i) => item.setAttribute('aria-pressed', String(i === selected)));
      picker(false); home();
    });
    $('place-list').append(button);
  });
  $('place').addEventListener('click', () => picker($('places').hidden));
  $('back').addEventListener('click', () => picker($('places').hidden));
  $('cancel').addEventListener('click', () => picker(false)); $('reset').addEventListener('click', home);
  $('zoom-in').addEventListener('click', () => map && setZoom(map.getZoom() + .5));
  $('zoom-out').addEventListener('click', () => map && setZoom(map.getZoom() - .5));
  document.addEventListener('keydown', event => {
    if (event.key === 'Escape' && !$('places').hidden) { event.preventDefault(); picker(false); }
    if (event.key !== 'Tab' || $('places').hidden) return;
    const controls = [...$('places').querySelectorAll('button')];
    if (event.shiftKey && document.activeElement === controls[0]) { event.preventDefault(); controls.at(-1).focus(); }
    else if (!event.shiftKey && document.activeElement === controls.at(-1)) { event.preventDefault(); controls[0].focus(); }
  });
  function zoomAtPointer(event) {
    const rect = $('screen').getBoundingClientRect();
    // 还原逻辑坐标，窄屏等比缩放后保持触摸位置正确。
    // Recover logical coordinates so touch remains accurate on a scaled circle.
    const x = (event.clientX - rect.left) * 466 / rect.width - 233;
    const y = (event.clientY - rect.top) * 466 / rect.height - 233;
    const degrees = Math.atan2(y, x) * 180 / Math.PI;
    setZoom(progressToZoom(clamp((arcHalfAngle - degrees) / (arcHalfAngle * 2), 0, 1)));
  }
  $('zoom-arc').addEventListener('pointerdown', event => {
    if (!mapReady || activePointer !== null) return;
    event.preventDefault(); event.stopPropagation(); activePointer = event.pointerId;
    $('zoom-arc').setPointerCapture(event.pointerId); $('zoom-arc').focus({ preventScroll: true }); zoomAtPointer(event);
  });
  $('zoom-arc').addEventListener('pointermove', event => {
    if (event.pointerId !== activePointer) return;
    event.preventDefault(); event.stopPropagation(); zoomAtPointer(event);
  });
  function finishArc(event) {
    if (event.pointerId !== activePointer) return;
    if ($('zoom-arc').hasPointerCapture(event.pointerId)) $('zoom-arc').releasePointerCapture(event.pointerId);
    activePointer = null;
  }
  ['pointerup', 'pointercancel', 'lostpointercapture'].forEach(type => $('zoom-arc').addEventListener(type, finishArc));
  $('zoom-arc').addEventListener('keydown', event => {
    if (!map) return;
    const changes = { ArrowUp: .5, ArrowRight: .5, ArrowDown: -.5, ArrowLeft: -.5, PageUp: 1, PageDown: -1 };
    if (Object.hasOwn(changes, event.key)) { event.preventDefault(); event.stopPropagation(); setZoom(map.getZoom() + changes[event.key]); }
    else if (event.key === 'Home' || event.key === 'End') { event.preventDefault(); event.stopPropagation(); setZoom(event.key === 'Home' ? minZoom : maxZoom); }
  });
  resize(); observer.observe(document.querySelector('.device-figure'));
  if (!window.maplibregl) {
    notice('当前浏览器无法启动地图，请使用支持 WebGL 的浏览器'); $('view-status').textContent = '地图未就绪'; return;
  }
  // 固定版本不导出 supported()，由构造器报告真实 WebGL 初始化失败。
  // The pinned SDK has no supported() export; construction reports actual WebGL failures.
  try {
    map = new maplibregl.Map({
    container: 'map', style: './map-style.json', center: places[0].center, zoom: places[0].zoom, minZoom, maxZoom,
    bearing: 0, pitch: 0, maxPitch: 0, attributionControl: false, dragRotate: false, touchPitch: false,
    doubleClickZoom: false, scrollZoom: false, renderWorldCopies: false, fadeDuration: 0,
    localIdeographFontFamily: 'sans-serif', canvasContextAttributes: { antialias: true, preserveDrawingBuffer: true }
    });
  } catch (error) {
    observer.disconnect();
    notice('当前浏览器无法启动地图，请使用支持 WebGL 的浏览器');
    $('view-status').textContent = '地图未就绪';
    console.error('Map initialization failed:', error.message);
    return;
  }
  map.touchZoomRotate.disableRotation();
  map.on('dataloading', () => {
    if (disposed) return;
    clearTimeout(loadTimer); loadTimer = setTimeout(() => { if (!sourceFailed) notice('正在载入地图'); }, 800);
  });
  map.on('load', () => { mapReady = true; updateView(); });
  map.on('idle', () => { clearTimeout(loadTimer); if (!sourceFailed) { notice(''); $('view-status').textContent = '真实地图 · 已就绪'; } });
  map.on('error', event => {
    clearTimeout(loadTimer); sourceFailed = true;
    notice(navigator.onLine ? '地图载入失败，请检查网络后重试' : '网络已断开，恢复连接后可重试', true);
    $('view-status').textContent = '地图载入失败'; console.warn('Map request failed:', event.error?.message || 'unknown map error');
  });
  map.on('moveend', updateView); map.on('zoom', updateView);
  $('retry').addEventListener('click', () => {
    sourceFailed = false; mapReady = false; notice('正在重新载入地图');
    // 用户重试时才重建样式请求，不添加随机参数或后台重试循环。
    // Rebuild style only on explicit retry, without cache-busting or a background retry loop.
    map.setStyle('./map-style.json', { diff: false });
    map.once('idle', () => { mapReady = !sourceFailed; updateView(); });
  });
  window.addEventListener('offline', () => {
    sourceFailed = true; clearTimeout(loadTimer); notice('网络已断开，恢复连接后可重试', true); $('view-status').textContent = '离线浏览 · 已载入区域';
  });
  // 页面退出销毁 WebGL、worker、观察器和计时器，无后台定位或轮询。
  // Tear down WebGL, workers, observer and timer on exit; no location or background polling.
  window.addEventListener('pagehide', () => { disposed = true; activePointer = null; clearTimeout(loadTimer); observer.disconnect(); map.remove(); }, { once: true });
})();
