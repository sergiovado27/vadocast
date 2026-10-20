"""Render the vadocast logo (no third-party deps) to PNG/ICO/SVG."""
import math, struct, sys, zlib, pathlib

RED = (229, 9, 20); WHITE = (255, 255, 255); BLACK = (11, 11, 11)

def rbox(px, py, x0, y0, x1, y1, r):
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    bx, by = (x1 - x0) / 2 - r, (y1 - y0) / 2 - r
    qx, qy = abs(px - cx) - bx, abs(py - cy) - by
    return math.hypot(max(qx, 0), max(qy, 0)) + min(max(qx, qy), 0) - r

def layers(paused):
    # (sdf, color) painted in order, coordinates in a 256x256 design space
    L = []
    L.append((lambda x, y: rbox(x, y, 8, 8, 248, 248, 52), RED))
    # horizontal 16:9 frame (outline)
    L.append((lambda x, y: max(rbox(x, y, 30, 92, 194, 184, 14), -rbox(x, y, 42, 104, 182, 172, 6)), WHITE))
    # vertical 9:16 frame: black body with white border
    L.append((lambda x, y: rbox(x, y, 146, 56, 222, 196, 12), WHITE))
    L.append((lambda x, y: rbox(x, y, 156, 66, 212, 186, 5), BLACK))
    if paused:
        L.append((lambda x, y: rbox(x, y, 172, 112, 180, 144, 2), WHITE))
        L.append((lambda x, y: rbox(x, y, 188, 112, 196, 144, 2), WHITE))
    else:
        L.append((lambda x, y: math.hypot(x - 184, y - 126) - 15, RED))
    return L

def render(size, paused=False, ss=None):
    ss = ss or (4 if size >= 128 else 8)
    L = layers(paused); scale = 256 / size
    px = bytearray()
    offs = [(i + 0.5) / ss for i in range(ss)]
    for j in range(size):
        px.append(0)  # PNG filter type
        for i in range(size):
            r = g = b = a = 0.0
            for oy in offs:
                for ox in offs:
                    x, y = (i + ox) * scale, (j + oy) * scale
                    col = None
                    for sdf, c in L:
                        if sdf(x, y) <= 0: col = c
                    if col:
                        r += col[0]; g += col[1]; b += col[2]; a += 1
            n = ss * ss
            if a:
                px += bytes((round(r / a), round(g / a), round(b / a), round(255 * a / n)))
            else:
                px += b'\0\0\0\0'
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(bytes(px), 9)) + chunk(b'IEND', b''))

def ico(pngs):
    head = struct.pack('<HHH', 0, 1, len(pngs)); off = 6 + 16 * len(pngs); dirs = b''; data = b''
    for size, png in pngs:
        s = 0 if size >= 256 else size
        dirs += struct.pack('<BBBBHHII', s, s, 0, 0, 1, 32, len(png), off + len(data)); data += png
    return head + dirs + data

SVG = '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">
  <!-- vadocast logo: one horizontal (16:9) and one vertical (9:16) canvas, live dot -->
  <rect x="8" y="8" width="240" height="240" rx="52" fill="#E50914"/>
  <rect x="36" y="98" width="152" height="80" rx="10" fill="none" stroke="#FFFFFF" stroke-width="12"/>
  <rect x="146" y="56" width="76" height="140" rx="12" fill="#FFFFFF"/>
  <rect x="156" y="66" width="56" height="120" rx="5" fill="#0B0B0B"/>
  <circle cx="184" cy="126" r="15" fill="#E50914"/>
</svg>
'''

if __name__ == '__main__':
    out = pathlib.Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
    (out / 'vadocast.svg').write_text(SVG, encoding='utf-8')
    (out / 'vadocast-256.png').write_bytes(render(256))
    (out / 'vadocast-paused-256.png').write_bytes(render(256, paused=True))
    (out / 'vadocast-1024.png').write_bytes(render(1024, ss=2))
    (out / 'vadocast.ico').write_bytes(ico([(s, render(s)) for s in (16, 24, 32, 48, 64, 128, 256)]))
    print('done')
