#!/usr/bin/env python3
"""Build flash-only location index from the pinned AreaCity level3/geo CSVs.

Usage: python3 tools/gen_weather_locations.py /path/to/extracted/csv-directory
Expected level3.csv and geo.csv; source/license/version in artwork/locations.
GCJ02 inverse adapted from wandergis/coordtransform (MIT, notice alongside data).
"""
import csv
import hashlib
import json
import math
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]

def wgs84(lon, lat):
    if not (73.66 < lon < 135.05 and 3.86 < lat < 53.55):
        return lon, lat
    x, y = lon - 105, lat - 35
    pi = math.pi
    dl = -100 + 2*x + 3*y + .2*y*y + .1*x*y + .2*math.sqrt(abs(x))
    dn = 300 + x + 2*y + .1*x*x + .1*x*y + .1*math.sqrt(abs(x))
    common = (20*math.sin(6*x*pi) + 20*math.sin(2*x*pi))*2/3
    dl += common + (20*math.sin(y*pi)+40*math.sin(y*pi/3))*2/3
    dl += (160*math.sin(y*pi/12)+320*math.sin(y*pi/30))*2/3
    dn += common + (20*math.sin(x*pi)+40*math.sin(x*pi/3))*2/3
    dn += (150*math.sin(x*pi/12)+300*math.sin(x*pi/30))*2/3
    rad = lat*pi/180
    magic = 1 - .00669342162296594323*math.sin(rad)**2
    dl = dl*180/((6378245*(1-.00669342162296594323))/(magic*math.sqrt(magic))*pi)
    dn = dn*180/(6378245/math.sqrt(magic)*math.cos(rad)*pi)
    return lon-dn, lat-dl

def build(directory):
    csv.field_size_limit(sys.maxsize)
    rows = list(csv.DictReader(open(directory/'level3.csv', encoding='utf-8-sig')))
    geo = {r['id']:r['geo'] for r in csv.DictReader(open(directory/'geo.csv', encoding='utf-8-sig'))}
    excluded = [r for r in rows if r['id'].startswith(('91','71'))]
    # This release has no Taiwan city/district coordinates; do not substitute
    # the province center for hundreds of distinct places.
    rows = [r for r in rows if r not in excluded]
    indices = {r['id']:i+1 for i,r in enumerate(rows)}
    pool = bytearray(b'\0'); offsets = {'':0}
    def intern(s):
        if s not in offsets:
            offsets[s] = len(pool); pool.extend(s.encode()+b'\0')
        return offsets[s]
    entries = ['{0,0,0,0,0,0}']
    for r in rows:
        assert geo.get(r['id']), r
        lon,lat = wgs84(*map(float,geo[r['id']].split()))
        assert -90 <= lat <= 90 and -180 <= lon <= 180
        entries.append('{%s,%d,%d,%d,%d,%d}' % (r['id'],round(lat*100000),round(lon*100000),
                        indices.get(r['pid'],0),intern(r['name']),intern(r['pinyin'].title())))
    assert len(pool)<65536 and len(rows)<65535
    output = ['/* Generated: tools/gen_weather_locations.py; MIT source notices in artwork/locations. */',
              '#include "weather_locations.h"', 'const char wx_location_names[] = {']
    for i in range(0,len(pool),24): output.append(','.join(str(v) for v in pool[i:i+24])+',')
    output += ['};','const wx_location_t wx_locations[] = {', ',\n'.join(entries), '};',
               f'const uint16_t wx_location_count = {len(entries)};']
    (ROOT/'main/weather_locations_data.c').write_text('\n'.join(output)+'\n')
    art = ROOT/'artwork/locations'; art.mkdir(exist_ok=True)
    # Small derived input for font generation and review, never ships polygons.
    (art/'names.txt').write_text('\n'.join(r['name'] for r in rows)+'\n')
    (art/'province-names.txt').write_text('\n'.join(r['name'] for r in rows if r['deep']=='0')+'\n')
    manifest = {'source':'https://github.com/xiangyuecn/AreaCity-JsSpider-StatsGov',
                'release':'2025.251231.260403','coordinate_system':'WGS84, inverse GCJ02 at build time',
                'regions':len(rows),'levels':[sum(r['deep']==str(i) for r in rows) for i in range(3)],
                'pool_bytes':len(pool), 'excluded':['overseas placeholder; Taiwan subtree lacks city/district coordinates'],
                'inputs':{n:hashlib.sha256((directory/n).read_bytes()).hexdigest() for n in ('level3.csv','geo.csv')}}
    (art/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
    assert abs(wgs84(116.404,39.915)[0]-116.39775550083061)<1e-9
    print(manifest['regions'],manifest['levels'],len(pool),'string bytes')

if __name__=='__main__': build(Path(sys.argv[1]))
