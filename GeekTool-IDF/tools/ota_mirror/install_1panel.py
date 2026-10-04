#!/usr/bin/env python3
"""Install the mirror on the existing 1Panel host without changing other sites."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import pwd
import re
import subprocess
import tempfile
import time

HERE = Path(__file__).resolve().parent
PUBLIC = Path('/opt/1panel/www/sites/ota.miaozong.cc/index')
VHOST = Path('/opt/1panel/www/conf.d/ota.miaozong.cc.conf')
PROGRAM = Path('/opt/geektool-ota')


def run(*args):
    subprocess.run(args, check=True)


def write_atomic(path, content, mode=0o644):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=path.parent, prefix='.ota-install-')
    try:
        with os.fdopen(fd, 'w') as output:
            output.write(content)
            output.flush()
            os.fsync(output.fileno())
        os.chmod(temporary, mode)
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


def install():
    if not Path('/opt/1panel/www').is_dir():
        raise RuntimeError('expected existing 1Panel www mount')
    # Only the dedicated service can write mirror files; it gets no login shell.
    try:
        account = pwd.getpwnam('geektool-ota')
    except KeyError:
        run('useradd', '--system', '--user-group', '--home-dir',
            '/var/lib/geektool-ota', '--no-create-home', '--shell',
            '/usr/sbin/nologin', 'geektool-ota')
        account = pwd.getpwnam('geektool-ota')
    PUBLIC.mkdir(parents=True, exist_ok=True)
    PUBLIC.chmod(0o755)
    os.chown(PUBLIC, account.pw_uid, account.pw_gid)
    PROGRAM.mkdir(parents=True, exist_ok=True)
    for name in ('sync_firmware.py', 'source.env'):
        destination = (PROGRAM / name if name.endswith('.py')
                       else Path('/etc/geektool-ota') / name)
        write_atomic(destination, (HERE / name).read_text())
    unit = (HERE / 'geektool-ota-sync.service').read_text()
    unit = unit.replace('--source-base ${OTA_R2_SOURCE}',
                        '--source-base ${OTA_R2_SOURCE} --public-dir ' + str(PUBLIC))
    unit = unit.replace('/srv/geektool-ota/public', str(PUBLIC))
    write_atomic(Path('/etc/systemd/system/geektool-ota-sync.service'), unit)
    write_atomic(Path('/etc/systemd/system/geektool-ota-sync.timer'),
                 (HERE / 'geektool-ota-sync.timer').read_text())
    run('systemd-analyze', 'verify', '/etc/systemd/system/geektool-ota-sync.service',
        '/etc/systemd/system/geektool-ota-sync.timer')
    run('systemctl', 'daemon-reload')
    run('systemctl', 'start', 'geektool-ota-sync.service')
    for channel, name in (('stable', 'GeekTool.bin'), ('beta', 'GeekTool-beta.bin')):
        state = json.loads((Path('/var/lib/geektool-ota') / (channel + '.json')).read_text())
        data = (PUBLIC / name).read_bytes()
        if len(data) != state['size'] or hashlib.sha256(data).hexdigest() != state['sha256']:
            raise RuntimeError('initial mirror verification failed: ' + channel)
        print('VERIFIED', channel, state['version'], len(data), state['sha256'])
    print('INSTALLED; configure HTTPS and verify it before enabling the timer/DNS cutover')


def configure_web():
    # Create the dedicated static site and enable its certificate in 1Panel first.
    old = VHOST.read_text()
    if 'server_name ota.miaozong.cc;' not in old:
        raise RuntimeError('unexpected dedicated vhost identity')
    cert = re.search(r'^\s*ssl_certificate\s+([^;]+);', old, re.M)
    key = re.search(r'^\s*ssl_certificate_key\s+([^;]+);', old, re.M)
    if not cert or not key or not all((PUBLIC / name).is_file()
                                     for name in ('GeekTool.bin', 'GeekTool-beta.bin')):
        raise RuntimeError('1Panel certificate or verified mirror files are missing')
    for value in (cert[1], key[1]):
        if not re.fullmatch(r'/www/sites/ota\.miaozong\.cc/ssl/[\w.]+', value):
            raise RuntimeError('unexpected certificate path')
    locations = (HERE / 'nginx-locations.conf').read_text().replace(
        '/srv/geektool-ota/public', '/www/sites/ota.miaozong.cc/index')
    new = '''# Dedicated OTA static mirror; maintained by tools/ota_mirror.
# 1Panel manages the certificate files and their renewal.
server {
    listen 80;
    server_name ota.miaozong.cc;
    location ^~ /.well-known/acme-challenge/ {
        root /usr/share/nginx/html;
        allow all;
    }
    location / { return 301 https://$host$request_uri; }
}
server {
    listen 443 ssl;
    server_name ota.miaozong.cc;
    ssl_certificate CERT;
    ssl_certificate_key KEY;
    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_session_cache shared:SSL:10m;
    ssl_session_timeout 10m;
    access_log /www/sites/ota.miaozong.cc/log/access.log;
    error_log /www/sites/ota.miaozong.cc/log/error.log;
LOCATIONS
}
'''.replace('CERT', cert[1]).replace('KEY', key[1]).replace('LOCATIONS', locations)
    backup = PROGRAM / ('ota-vhost-before-' + str(time.time_ns()) + '.conf')
    write_atomic(backup, old, 0o600)
    cid = subprocess.check_output(
        ['docker', 'ps', '--filter', 'name=1Panel-openresty', '-q'], text=True).strip()
    if not cid or '\n' in cid:
        raise RuntimeError('expected exactly one existing OpenResty container')
    write_atomic(VHOST, new)
    try:
        run('docker', 'exec', cid, 'nginx', '-t')
    except subprocess.CalledProcessError:
        write_atomic(VHOST, old)
        raise
    run('docker', 'exec', cid, 'nginx', '-s', 'reload')
    print('WEB_CONFIGURED', VHOST, 'backup', backup)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('install', 'configure-web', 'enable-timer'))
    args = parser.parse_args()
    if os.geteuid() != 0:
        parser.error('requires root on the authorized 1Panel server')
    if args.action == 'install':
        install()
    elif args.action == 'configure-web':
        configure_web()
    else:
        run('systemctl', 'enable', '--now', 'geektool-ota-sync.timer')
        run('systemctl', 'list-timers', 'geektool-ota-sync.timer', '--no-pager')


if __name__ == '__main__':
    main()
