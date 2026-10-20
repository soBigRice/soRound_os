(function () {
  'use strict';

  const C = 233;
  const TAU = Math.PI * 2;
  const pad = value => String(value).padStart(2, '0');
  const time = state => `${pad(state.h)}:${pad(state.m)}`;
  const point = (radius, angle, cx = C, cy = C) => [
    +(cx + Math.cos(angle) * radius).toFixed(2),
    +(cy + Math.sin(angle) * radius).toFixed(2),
  ];
  const line = (a, b, stroke, width = 1, extra = '') =>
    `<line x1="${a[0]}" y1="${a[1]}" x2="${b[0]}" y2="${b[1]}" stroke="${stroke}" stroke-width="${width}" ${extra}/>`;
  const label = (x, y, value, size, fill, extra = '') =>
    `<text x="${x}" y="${y}" fill="${fill}" font-family="Barlow, sans-serif" font-size="${size}" text-anchor="middle" ${extra}>${value}</text>`;
  const dateLabel = state => {
    const date = state.date instanceof Date ? state.date : new Date();
    const weekday = ['SUN', 'MON', 'TUE', 'WED', 'THU', 'FRI', 'SAT'][date.getDay()];
    return `${weekday}   ${pad(date.getMonth() + 1)} / ${pad(date.getDate())}`;
  };

  function stillTime(state, y = 262) {
    return label(C, y, time(state), 72, '#4b4d4b', 'font-weight="500" letter-spacing="-2"');
  }

  function radar(state, aod) {
    if (aod) {
      return stillTime(state) + [0, 1, 2, 3].map(index => {
        const angle = index * Math.PI / 2;
        return line(point(180, angle), point(190, angle), '#202824');
      }).join('');
    }

    let grid = '';
    for (let offset = -160; offset <= 160; offset += 40) {
      // 网格止于内圆，不依赖卡片外部裁切。 / Trim the grid to the inner circle.
      const reach = Math.sqrt(196 * 196 - offset * offset);
      grid += line([C + offset, C - reach], [C + offset, C + reach], '#142b22');
      grid += line([C - reach, C + offset], [C + reach, C + offset], '#142b22');
    }
    const rings = [64, 128, 196].map(radius =>
      `<circle cx="233" cy="233" r="${radius}" fill="none" stroke="#1d3e2e" stroke-width="1"/>`
    ).join('');
    const ticks = Array.from({ length: 60 }, (_, index) => {
      const angle = index / 60 * TAU - Math.PI / 2;
      return line(point(index % 5 === 0 ? 187 : 193, angle), point(196, angle),
        index % 5 === 0 ? '#4f765b' : '#274531', index % 5 === 0 ? 1.5 : 1);
    }).join('');
    const angle = Number(state.s) / 60 * TAU - Math.PI / 2;
    const trail = Array.from({ length: 12 }, (_, index) => {
      const start = angle - (index + 1) * Math.PI / 90;
      const end = start + Math.PI / 90;
      const p1 = point(195, start);
      const p2 = point(195, end);
      return `<path d="M 233 233 L ${p1.join(' ')} A 195 195 0 0 1 ${p2.join(' ')} Z" fill="#89dc91" opacity="${(0.045 - index * 0.003).toFixed(3)}"/>`;
    }).join('');

    // 扫线只表达真实秒数，不生成虚构目标。 / The sweep represents seconds, never invented targets.
    return `<circle cx="233" cy="233" r="233" fill="#08130f"/>` +
      grid + rings + ticks + trail +
      line([233, 233], point(195, angle), '#9ce7a1', 1.4) +
      `<circle cx="233" cy="233" r="3" fill="#a5e7aa"/>` +
      label(C, 110, 'RADAR', 13, '#7baf88', 'letter-spacing="6" font-weight="500"') +
      `<rect x="73" y="181" width="320" height="102" rx="16" fill="#08130f" opacity="0.91"/>` +
      label(C, 261, time(state), 91, '#d3f3c9', 'font-weight="500" letter-spacing="-2.5"') +
      label(C, 327, dateLabel(state), 13, '#78977d', 'letter-spacing="2"') +
      label(C, 366, `${pad(state.s)} SEC`, 10, '#52735b', 'letter-spacing="3"');
  }

  function reel(cx, cy, rotation) {
    const radius = 65;
    const outline = `<circle cx="${cx}" cy="${cy}" r="${radius}" fill="#12120f" stroke="#7b776a" stroke-width="1.25"/>` +
      `<circle cx="${cx}" cy="${cy}" r="58" fill="none" stroke="#3f3d34" stroke-width="0.7"/>`;
    const spokes = [0, 1, 2].map(index => {
      const angle = index * 120;
      return `<g transform="rotate(${angle} ${cx} ${cy})">` +
        `<path d="M ${cx - 6} ${cy - 17} C ${cx - 11} ${cy - 29}, ${cx - 9} ${cy - 49}, ${cx} ${cy - 51} C ${cx + 14} ${cy - 51}, ${cx + 23} ${cy - 38}, ${cx + 14} ${cy - 24} L ${cx + 7} ${cy - 13}" fill="none" stroke="#c2bba5" stroke-width="1.5" stroke-linecap="round"/>` +
        `</g>`;
    }).join('');
    return outline + `<g transform="rotate(${rotation} ${cx} ${cy})">${spokes}` +
      `<circle cx="${cx}" cy="${cy}" r="16" fill="none" stroke="#c2bba5" stroke-width="1.25"/>` +
      `<path d="M ${cx - 5} ${cy} H ${cx + 5} M ${cx} ${cy - 5} V ${cy + 5}" stroke="#c2bba5" stroke-width="1.1"/>` +
      `</g>`;
  }

  function tape(state, aod) {
    if (aod) {
      return stillTime(state, 303) +
        `<circle cx="156" cy="165" r="16" fill="none" stroke="#252522"/>` +
        `<circle cx="310" cy="165" r="16" fill="none" stroke="#252522"/>`;
    }
    const rotation = (Number(state.s) + Number(state.m) * 60) * 6;
    // 卷盘逐秒旋转，字面时间独立保持可读。 / Reels follow seconds while the time stays fixed.
    return `<circle cx="233" cy="233" r="233" fill="#13130f"/>` +
      label(C, 67, dateLabel(state), 12, '#99957f', 'letter-spacing="2"') +
      `<path d="M 156 100 L 310 100 M 156 230 L 310 230" fill="none" stroke="#6d6755" stroke-width="1"/>` +
      reel(156, 165, rotation) + reel(310, 165, -rotation * 0.75) +
      `<path d="M 110 211 L 149 246 H 317 L 356 211" fill="none" stroke="#8e866d" stroke-width="1"/>` +
      `<path d="M 163 244 V 250 M 303 244 V 250" stroke="#c0b699" stroke-width="2"/>` +
      label(C, 326, time(state), 87, '#f1ead6', 'font-weight="500" letter-spacing="-2"') +
      `<path d="M 170 351 H 296" stroke="#39392e" stroke-width="1"/>` +
      label(C, 378, 'T A P E', 12, '#a49e88', 'font-weight="500" letter-spacing="3"') +
      label(C, 400, 'TIME IN MOTION', 8, '#646450', 'letter-spacing="2.6"');
  }

  function iris(state, aod) {
    if (aod) {
      return stillTime(state) +
        `<path d="M 126 191 L 159 140 H 307 L 340 191 M 340 275 L 307 326 H 159 L 126 275" fill="none" stroke="#292724" stroke-width="1"/>`;
    }
    const blades = Array.from({ length: 8 }, (_, index) => {
      // 叶片是同一几何的旋转副本，可映射为八个缓存矢量块。 / One blade geometry is reused eight times.
      const fill = index % 2 === 0 ? '#24211c' : '#1b1a17';
      return `<g transform="rotate(${index * 45} 233 233)">` +
        `<path d="M 233 35 C 296 35 350 65 382 108 L 340 191 L 307 140 L 159 140 Z" fill="${fill}" stroke="#353027" stroke-width="1"/>` +
        `<path d="M 382 108 L 340 191 L 307 140" fill="none" stroke="#766044" stroke-width="0.8"/>` +
        `</g>`;
    }).join('');
    const ticks = Array.from({ length: 60 }, (_, index) => {
      const angle = index / 60 * TAU - Math.PI / 2;
      return line(point(index % 5 === 0 ? 195 : 200, angle), point(204, angle),
        index % 5 === 0 ? '#92704c' : '#4f4232', 0.8);
    }).join('');
    const secondAngle = Number(state.s) / 60 * TAU - Math.PI / 2;
    const second = point(204, secondAngle);
    return `<circle cx="233" cy="233" r="233" fill="#13120f"/>` +
      blades +
      `<circle cx="233" cy="233" r="211" fill="none" stroke="#4c3b28" stroke-width="1"/>` +
      ticks +
      `<path d="M 159 140 H 307 L 340 191 V 275 L 307 326 H 159 L 126 275 V 191 Z" fill="#11110f" stroke="#af7a42" stroke-width="1.2"/>` +
      label(C, 189, 'IRIS', 11, '#b18b5d', 'font-weight="500" letter-spacing="5"') +
      label(C, 260, time(state), 72, '#eee5d5', 'font-weight="500" letter-spacing="-2"') +
      label(C, 296, dateLabel(state), 10, '#9c8d76', 'letter-spacing="1.5"') +
      `<circle cx="${second[0]}" cy="${second[1]}" r="2.8" fill="#e7a65b"/>` +
      label(C, 385, 'APERTURE OF TIME', 9, '#7c684e', 'letter-spacing="2.6"');
  }

  window.instrumentFaces = { radar, tape, iris };
}());
