# make_logo.py <TitanOne-Regular.ttf> <out.svg>: generates docs/logo.svg.
# Original artwork (no game assets). Lettering: Titan One by Rodrigo Fuenzalida, SIL Open Font License 1.1
# (github.com/google/fonts ofl/titanone), converted to outlines. Needs fontTools.
import sys
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

font = TTFont(sys.argv[1]); out = sys.argv[2]
gs = font.getGlyphSet(); cmap = font.getBestCmap(); upm = font['head'].unitsPerEm

def text_path(s, size, x0, y0, skew=-0.2, track=0):
    sc = size / upm; x = x0; d = []
    for ch in s:
        g = cmap[ord(ch)]; pen = SVGPathPen(gs)
        # flip y, scale, italic shear (x += -skew*y)
        tp = TransformPen(pen, (sc, 0, -skew * sc, -sc, x, y0))
        gs[g].draw(tp); d.append(pen.getCommands())
        x += gs[g].width * sc + track
    return ' '.join(d), x

NAVY = '#16205a'; WHITE = '#ffffff'
word, xend = text_path('wings64', 150, 70, 228, track=-4)
tagx = xend + 14
tag, tagend = text_path('DC', 58, tagx + 18, 222, track=1)

cloud = lambda cx, cy, s: (f'<g transform="translate({cx} {cy}) scale({s})">'
    '<path d="M-60 18 C-78 18 -82 -6 -62 -10 C-62 -32 -30 -38 -20 -20 C-12 -44 30 -44 34 -16 '
    'C52 -24 70 -6 60 10 C70 16 64 22 56 22 Z" fill="#fff" stroke="%s" stroke-width="5" stroke-linejoin="round"/>'
    '<path d="M-52 10 C-40 14 -20 14 -4 8" fill="none" stroke="#c9e3ff" stroke-width="5" stroke-linecap="round"/></g>' % NAVY)

# Hang glider seen from below-front: a swept delta sail with stripes, keel, control bar and a tiny pilot.
glider = f'''
<g transform="translate(700 70) rotate(-9)">
  <path d="M-120 18 L0 -22 L120 18 L0 4 Z" fill="{NAVY}" transform="translate(5 7)" opacity=".35"/>
  <clipPath id="sail"><path d="M-120 18 L0 -22 L120 18 L0 4 Z"/></clipPath>
  <g clip-path="url(#sail)">
    <rect x="-130" y="-30" width="260" height="60" fill="#ffd23f"/>
    <path d="M-130 30 L-60 -30 L-38 -30 L-108 30 Z M-12 -30 L12 -30 L12 30 L-12 30 Z M38 -30 L60 -30 L130 30 L108 30 Z" fill="#e8402e"/>
  </g>
  <path d="M-120 18 L0 -22 L120 18 L0 4 Z" fill="none" stroke="#fff" stroke-width="11" stroke-linejoin="round" opacity=".9"/>
  <clipPath id="sail2"><path d="M-120 18 L0 -22 L120 18 L0 4 Z"/></clipPath>
  <g clip-path="url(#sail2)">
    <rect x="-130" y="-30" width="260" height="60" fill="#ffd23f"/>
    <path d="M-130 30 L-60 -30 L-38 -30 L-108 30 Z M-12 -30 L12 -30 L12 30 L-12 30 Z M38 -30 L60 -30 L130 30 L108 30 Z" fill="#e8402e"/>
  </g>
  <path d="M-120 18 L0 -22 L120 18 L0 4 Z" fill="none" stroke="{NAVY}" stroke-width="5" stroke-linejoin="round"/>
  <path d="M0 4 L0 30 M-22 44 L0 30 L22 44 Z M-24 44 L24 44" fill="none" stroke="#fff" stroke-width="9" stroke-linejoin="round" stroke-linecap="round" opacity=".9"/>
  <path d="M0 4 L0 30 M-22 44 L0 30 L22 44 Z" fill="none" stroke="{NAVY}" stroke-width="4" stroke-linejoin="round" stroke-linecap="round"/>
  <path d="M-24 44 L24 44" stroke="{NAVY}" stroke-width="5" stroke-linecap="round"/>
  <ellipse cx="0" cy="36" rx="6" ry="9" fill="#2f7de1" stroke="{NAVY}" stroke-width="3"/>
  <circle cx="0" cy="25" r="5.5" fill="#ffd9b0" stroke="{NAVY}" stroke-width="3"/>
</g>'''

trail = f'<path d="M150 122 C 300 70, 470 60, 585 84" fill="none" stroke="{WHITE}" stroke-width="7" stroke-linecap="round" stroke-dasharray="2 17" opacity=".95"/>' \
        f'<path d="M150 122 C 300 70, 470 60, 585 84" fill="none" stroke="#7cc4ff" stroke-width="3.5" stroke-linecap="round" stroke-dasharray="2 17" transform="translate(0 1)"/>'

W = int(tagend + 60)
svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} 300" width="{W}" height="300" role="img" aria-label="wings64-dc">
<title>wings64-dc</title>
<defs>
  <linearGradient id="sun" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#fff38a"/><stop offset=".45" stop-color="#ffd23f"/><stop offset="1" stop-color="#ff8a1f"/>
  </linearGradient>
  <linearGradient id="sea" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#4fb3ff"/><stop offset="1" stop-color="#1f6fe0"/>
  </linearGradient>
</defs>
{cloud(110, 70, 0.9)}{cloud(455, 40, 0.5)}
{trail}
<path d="{word}" fill="{NAVY}" transform="translate(7 9)"/>
<path d="{word}" fill="none" stroke="{NAVY}" stroke-width="30" stroke-linejoin="round"/>
<path d="{word}" fill="none" stroke="{WHITE}" stroke-width="14" stroke-linejoin="round"/>
<path d="{word}" fill="url(#sun)"/>
<path d="{word}" fill="none" stroke="#e86a12" stroke-width="2" stroke-linejoin="round" opacity=".55"/>
<g transform="rotate(-6 {tagx + 60} 200)">
  <rect x="{tagx}" y="158" width="{tagend - tagx + 18}" height="78" rx="20" fill="{NAVY}" transform="translate(5 7)"/>
  <rect x="{tagx}" y="158" width="{tagend - tagx + 18}" height="78" rx="20" fill="url(#sea)" stroke="{NAVY}" stroke-width="8"/>
  <path d="{tag}" fill="#fff" stroke="{NAVY}" stroke-width="3" stroke-linejoin="round"/>
</g>
{glider}
</svg>
'''
open(out, 'w').write(svg)
print('width', W)
