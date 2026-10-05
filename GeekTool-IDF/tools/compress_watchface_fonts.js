// Offline wrapper for the pinned MIT-licensed LVGL font compressor.
'use strict';
const fs = require('fs');
const compress = require('./vendor/lv_font_conv/compress.js');
const glyphs = JSON.parse(fs.readFileSync(0, 'utf8'));
const result = glyphs.map(pixels => {
  const bytes = [];
  let byte = 0, used = 0;
  const stream = { writeBits(value, count) {
    for (let bit = count - 1; bit >= 0; bit--) {
      byte = (byte << 1) | ((value >> bit) & 1);
      if (++used === 8) { bytes.push(byte); byte = used = 0; }
    }
  } };
  compress(stream, pixels, { bpp: 4 });
  if (used) bytes.push(byte << (8 - used));
  // LVGL's bit reader may read the following byte at a byte boundary.
  bytes.push(0, 0);
  return bytes;
});
process.stdout.write(JSON.stringify(result));
