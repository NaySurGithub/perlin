"""Maps Java block texture ids to Bedrock resource pack texture paths by pixel content."""
import json, os, sys
import numpy as np
from PIL import Image

JAVA = '/tmp/claude-0/jb/assets/minecraft/textures'
BEDROCK = '/tmp/claude-0/cl/r/resource_pack'


def load(path):
    try:
        im = Image.open(path).convert('RGBA')
    except Exception:
        return None
    a = np.asarray(im, dtype=np.float32)
    s = min(a.shape[0], a.shape[1])
    a = a[:s, :s]  # first frame of a vertical strip
    if s != 16:
        idx = (np.arange(16) * s // 16)
        a = a[idx][:, idx]
    return a


def sig(a):
    # premultiplied colour so fully transparent pixels compare equal
    p = a.copy()
    p[..., :3] *= (p[..., 3:4] / 255.0)
    return p


def main():
    bed = {}
    bdir = os.path.join(BEDROCK, 'textures', 'blocks')
    files = []
    for dp, dn, fn in os.walk(bdir):
        for f in fn:
            files.append(os.path.relpath(os.path.join(dp, f), bdir).replace(os.sep, '/'))
    for f in sorted(files):
        stem, ext = os.path.splitext(f)
        if ext not in ('.png', '.tga'):
            continue
        a = load(os.path.join(bdir, f))
        if a is None:
            continue
        if stem.endswith(('_mers', '_normal', '_heightmap', '_mer', '_n')):
            continue
        key = 'textures/blocks/' + stem
        if key in bed and ext == '.png':
            continue  # .tga preferred, as the runtime does
        bed[key] = sig(a)
    keys = list(bed)
    stack = np.stack([bed[k] for k in keys])  # N,16,16,4
    out = {}
    jdir = os.path.join(JAVA, 'block')
    unmatched = []
    for f in sorted(os.listdir(jdir)):
        if not f.endswith('.png'):
            continue
        jid = 'block/' + f[:-4]
        a = load(os.path.join(jdir, f))
        if a is None:
            continue
        s = sig(a)
        d = np.abs(stack - s[None]).mean(axis=(1, 2, 3))
        order = np.argsort(d)
        best = order[0]
        # prefer a same-named bedrock file when it is almost as good
        same = 'textures/blocks/' + f[:-4]
        if same in bed and d[keys.index(same)] <= d[best] + 1.0:
            best = keys.index(same)
        out[jid] = {'path': keys[best], 'diff': float(d[best])}
        if d[best] > 5:
            unmatched.append((jid, keys[best], float(d[best])))
    json.dump(out, open(os.path.join(os.path.dirname(__file__), 'texmap.json'), 'w'), indent=1, sort_keys=True)
    print(len(out), 'mapped;', len(unmatched), 'poor')
    for u in unmatched:
        print(u)


main()
