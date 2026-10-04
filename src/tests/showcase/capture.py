#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Render and audit the OSKey product gallery with deterministic LVGL fixtures."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

from PIL import Image, ImageDraw, ImageFont

SOURCE = Path(__file__).resolve().parent
ROOT = SOURCE.parents[2]
CATALOG = json.loads((SOURCE / 'catalog.json').read_text())


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def audit_coverage():
    ids = [scene['id'] for scene in CATALOG['scenes']]
    require(len(ids) == len(set(ids)), 'Duplicate scene identifiers')
    pages = set(re.findall(r'\bUI_PAGE_[A-Z_]+\b', (ROOT / 'src/display/ui.h').read_text()))
    pages.discard('UI_PAGE_NONE')
    covered = {scene['page'] for scene in CATALOG['scenes']}
    require(pages <= covered, f'Pages requiring gallery coverage: {sorted(pages - covered)}')
    sections = {section['id'] for section in CATALOG['sections']}
    require(all(scene['section'] in sections for scene in CATALOG['scenes']), 'Unknown section')
    return len(pages)


def render(firmware, build, frames):
    env = {**os.environ, 'SDL_VIDEODRIVER': 'dummy', 'SDL_RENDER_DRIVER': 'software',
           'OSKEY_SHOWCASE_OUTPUT': str(frames)}
    log_path = build / 'capture.log'
    with log_path.open('w') as log:
        result = subprocess.run([str(firmware), '-rt'], cwd=build, env=env,
                                stdout=log, stderr=subprocess.STDOUT, timeout=90)
    log = log_path.read_text()
    require(result.returncode == 0, f'Renderer exited with {result.returncode}:\n{log[-5000:]}')
    expected = {scene['id'] for scene in CATALOG['scenes']}
    actual = {path.stem for path in frames.glob('*.ppm')}
    require(actual == expected, f'Frame mismatch: missing {expected - actual}, extra {actual - expected}')
    require(f'Showcase complete: {len(expected)} frames' in log, 'Renderer completion record missing')


def convert(frames, output):
    image_dir = output / 'images'
    image_dir.mkdir(parents=True, exist_ok=True)
    records = []
    errors = []
    for scene in CATALOG['scenes']:
        text = (frames / (scene['id'] + '.txt')).read_text()
        if scene['expected_text'] not in text:
            errors.append(f"{scene['id']}: expected {scene['expected_text']!r}, rendered:\n{text}")
        with Image.open(frames / (scene['id'] + '.ppm')) as image:
            image.load()
            require(image.size == (480, 800), f"Unexpected screenshot size: {scene['id']}")
            require(len(image.getcolors(image.width * image.height) or []) > 25,
                    f"Blank or incomplete screenshot: {scene['id']}")
            image.save(image_dir / (scene['id'] + '.png'), optimize=True)
        records.append({**scene, 'image': 'images/' + scene['id'] + '.png',
                        'visible_text': text.splitlines(),
                        'sha256': hashlib.sha256((image_dir / (scene['id'] + '.png')).read_bytes()).hexdigest()})
    require(not errors, '\n'.join(errors))
    return records


def cover(output):
    chosen = [('23-wallet-home', 'YOUR WALLET'), ('18-mnemonic-24', 'RECOVERY'),
              ('32-ethereum-transaction', 'SIGNING'), ('42-google-authenticate', 'PASSKEYS'),
              ('38-airgap-signature', 'AIR GAP'), ('50-nxp-unlocked', 'SECURE ELEMENT'),
              ('54-wifi-connected', 'CONNECTIVITY'), ('65-imu-orientation', 'MOTION')]
    canvas = Image.new('RGB', (1280, 1000), '#090b0e')
    draw = ImageDraw.Draw(canvas)
    font_path = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
    font = ImageFont.truetype(font_path, 18) if Path(font_path).exists() else ImageFont.load_default()
    title = ImageFont.truetype(font_path, 46) if Path(font_path).exists() else ImageFont.load_default()
    draw.text((38, 20), 'OSKey', fill='#f2f5f7', font=title)
    draw.text((38, 83), 'Your keys. Your choices. Your hardware.', fill='#929eaa', font=font)
    for index, (identifier, label) in enumerate(chosen):
        x = 40 + (index % 4) * 310
        y = 140 + (index // 4) * 420
        draw.text((x, y), label, fill='#6fd6a4', font=font)
        with Image.open(output / 'images' / (identifier + '.png')) as image:
            thumbnail = image.resize((222, 370), Image.Resampling.LANCZOS)
            canvas.paste(thumbnail, (x, y + 30))
    canvas.save(output / 'images' / 'overview.png', optimize=True)


def heading_id(title):
    return re.sub(r'[^\w\-\s]', '', title.lower()).strip().replace(' ', '-')


def markdown(output):
    sections = CATALOG['sections']
    scenes = CATALOG['scenes']
    text = ['<!-- SPDX-License-Identifier: MPL-2.0 -->', '', '# OSKey 产品展示', '',
            '从一块开发板开始，把钱包、身份认证、连接与交互组合成属于自己的硬件。'
            '这里用完整的设备画面，带你走过 OSKey 的每一项能力。', '',
            '![OSKey 钱包、签名、身份认证、安全芯片与外设总览](images/overview.png)', '',
            '## 一览', '', '| 模块 | 功能 |', '| --- | --- |']
    for section in sections:
        text.append(f"| [{section['title']}](#{heading_id(section['title'])}) | {section['summary']} |")
    for section in sections:
        selected = [scene for scene in scenes if scene['section'] == section['id']]
        text += ['', f"## {section['title']}", '', section['description'], '']
        for offset in range(0, len(selected), 3):
            group = selected[offset:offset + 3]
            text.append('| ' + ' | '.join(scene['title'] for scene in group) + ' |')
            text.append('| ' + ' | '.join('---' for _ in group) + ' |')
            text.append('| ' + ' | '.join(
                f'<a href="images/{scene["id"]}.png"><img src="images/{scene["id"]}.png" alt="{scene["title"]}" width="240"></a>'
                for scene in group) + ' |')
            text.append('')
    text += ['## 自由组合，定义自己的设备', '',
             '每一项能力都是可以按需选择的模块。你可以做一枚随身携带的签名钥匙，'
             '也可以组合出带屏幕、摄像头、声音和运动感知的桌面伙伴。'
             '从功能的取舍，到 PCB 的布局，再到外壳握在掌心的感觉，设备最终的模样，由你决定。', '',
             '## 演示资料', '',
             '查看 [完整截图清单](manifest.json)。', '']
    (output / 'README.md').write_text('\n'.join(text))


def input_digests():
    paths = set(SOURCE.glob('*.c')) | set(SOURCE.glob('*.h'))
    paths |= {SOURCE / name for name in ['CMakeLists.txt', 'prj.conf', 'app.overlay', 'catalog.json', 'capture.py']}
    paths |= set((ROOT / 'src/display').glob('*.c')) | set((ROOT / 'src/display').glob('*.h'))
    paths |= set((ROOT / 'src/display/assets').glob('*.c')) | set((ROOT / 'src/display/assets').glob('*.h'))
    paths |= set((ROOT / 'src/display/assets/generated').glob('*.a8'))
    return {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(paths)}


def decode_qrs(output, build):
    build.mkdir(parents=True, exist_ok=True)
    executable = build / 'qr-decode'
    library = ROOT / 'lib/quirc/lib'
    subprocess.run(['cc', '-std=c17', '-O2', '-DQUIRC_FLOAT_TYPE=float', '-DQUIRC_USE_TGMATH=1',
                    '-I', str(library), str(SOURCE / 'qr_decode.c'),
                    *(str(library / name) for name in ['decode.c', 'identify.c', 'quirc.c', 'version_db.c']),
                    '-lm', '-o', str(executable)], check=True)
    records = {}
    for identifier in ['26-hd-account', '36-airgap-request', '38-airgap-signature',
                       '57-wifi-access-point', '68-qr-scanner']:
        with Image.open(output / 'images' / (identifier + '.png')) as image:
            grayscale = image.convert('L')
            pixels = f'P5\n{image.width} {image.height}\n255\n'.encode() + grayscale.tobytes()
        result = subprocess.run([str(executable)], input=pixels, capture_output=True, timeout=10)
        require(result.returncode == 0, f'QR decode failed: {identifier}')
        payloads = result.stdout.decode().splitlines()
        require(len(payloads) == 1, f'Expected one QR payload: {identifier}')
        records[identifier] = payloads[0]
    address = 'ethereum:0x9858EfFD232B4033E47d90003D41EC34EcaEda94'
    require(records['26-hd-account'] == address, 'Address QR mismatch')
    require(records['68-qr-scanner'] == address, 'Camera preview QR mismatch')
    request = json.loads(records['36-airgap-request'])
    require(request['type'] == 'eth-sign-request' and request['chainId'] == 1
            and request['to'] == '0x2222222222222222222222222222222222222222'
            and request['value'] == '0.025 ETH', 'Air-gap request mismatch')
    result = json.loads(records['38-airgap-signature'])
    signature = '0x' + bytes((index * 5 + 23) % 256 for index in range(64)).hex()
    require(result['type'] == 'eth-signature' and result['signature'] == signature,
            'Air-gap signature mismatch')
    require(records['57-wifi-access-point'] == 'WIFI:T:WPA;S:OSKey-AP;P:12345678;;',
            'Provisioning QR mismatch')
    return records


def verify(output, build):
    page_count = audit_coverage()
    manifest = json.loads((output / 'manifest.json').read_text())
    require(len(manifest['screenshots']) == len(CATALOG['scenes']), 'Manifest count mismatch')
    require([item['id'] for item in manifest['screenshots']] == [item['id'] for item in CATALOG['scenes']],
            'Manifest scene order mismatch')
    require(manifest['inputs_sha256'] == input_digests(), 'Renderer inputs have changed; regenerate the gallery')
    readme = (output / 'README.md').read_text()
    for scene, expected in zip(manifest['screenshots'], CATALOG['scenes']):
        require(all(scene[key] == value for key, value in expected.items()), f'Catalog mismatch: {scene["id"]}')
        path = output / scene['image']
        require(path.is_file(), f'Image missing: {path}')
        require(scene['image'] in readme, f'Image absent from homepage: {path}')
        require(scene['expected_text'] in '\n'.join(scene['visible_text']), f'Visible text mismatch: {path}')
        require(hashlib.sha256(path.read_bytes()).hexdigest() == scene['sha256'], f'Image digest mismatch: {path}')
        with Image.open(path) as image:
            image.verify()
        with Image.open(path) as image:
            require(image.size == (480, 800), f'Image dimensions mismatch: {path}')
            require(len(image.getcolors(image.width * image.height) or []) > 25, f'Blank image: {path}')
    for section in CATALOG['sections']:
        require(f"## {section['title']}" in readme, f"Section missing: {section['id']}")
    for scene in manifest['screenshots']:
        if scene['section'] == 'fido' and scene['page'] == 'UI_PAGE_CONFIRMATION' and 'permission' not in scene['id']:
            require('www.google.com' in '\n'.join(scene['visible_text']), f"Google RP missing: {scene['id']}")
    references = re.findall(r'(?:src|href)="([^"]+)"|\]\(([^)]+)\)', readme)
    for alternatives in references:
        reference = next((item for item in alternatives if item), '')
        if not reference.startswith(('#', 'https://', 'http://')):
            require((output / reference).exists(), f'Local link missing: {reference}')
    anchors = {heading_id(title) for title in re.findall(r'^## (.+)$', readme, re.MULTILINE)}
    require(all(anchor in anchors for anchor in re.findall(r'\]\(#([^)]+)\)', readme)),
            'Navigation anchor mismatch')
    expected_images = {scene['id'] + '.png' for scene in CATALOG['scenes']} | {'overview.png'}
    require({path.name for path in (output / 'images').glob('*.png')} == expected_images,
            'Gallery contains a different set of images from the catalog')
    require((output / 'images/overview.png').is_file(), 'Overview missing')
    require(decode_qrs(output, build) == manifest['qr_payloads'], 'QR manifest mismatch')
    print(f"Verified {len(manifest['screenshots'])} screenshots, {len(CATALOG['sections'])} sections, "
          f"all {page_count} UI pages, Google FIDO examples, five decoded QR codes and local links")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build/showcase')
    parser.add_argument('--output', type=Path, default=ROOT / 'docs/showcase')
    parser.add_argument('--skip-build', action='store_true')
    parser.add_argument('--verify', action='store_true', help='audit the existing gallery')
    args = parser.parse_args()
    output, build = args.output.resolve(), args.build_dir.resolve()
    if args.verify:
        verify(output, build)
        return
    audit_coverage()
    if not args.skip_build:
        require(os.environ.get('ZEPHYR_BASE'), 'Load the Zephyr environments before building')
        subprocess.run(['west', 'build', '-b', 'native_sim/native/64', '-d', str(build), str(SOURCE)],
                       cwd=ROOT, check=True)
    firmware = build / 'zephyr/zephyr.exe'
    require(firmware.is_file(), f'Native executable missing: {firmware}')
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='frames-', dir=build) as directory:
        frames = Path(directory)
        render(firmware, build, frames)
        records = convert(frames, output)
    metadata = {'renderer': 'Zephyr native_sim / OSKey LVGL / fixed presentation data',
                'rp_id': 'www.google.com', 'width': 480, 'height': 800, 'screenshots': records,
                'inputs_sha256': input_digests(), 'qr_payloads': decode_qrs(output, build)}
    (output / 'manifest.json').write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + '\n')
    cover(output)
    markdown(output)
    verify(output, build)


if __name__ == '__main__':
    main()
