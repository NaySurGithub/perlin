#!/usr/bin/env python3
"""Builds resources/block_models.bin: per-Bedrock-block-state baked Java models.

Inputs (not shipped in the repo; see README):
  JAVA_ASSETS   extracted assets/minecraft of the Java client jar (models/block, blockstates)
  BEDROCK_STATES  bedrock_states.json  (name, state) per record, in network order
  PHYSICS       physics.json            (seq, hash, flags, ..., boxes) from the Bedrock block registry
  texmap.json   Java texture -> Bedrock texture path (texmap.py)
"""
import itertools
import json
import os
import struct
import sys
from collections import Counter, OrderedDict

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from javamodels import Models, DIRS, block_rotation  # noqa: E402
from blockstates import load_all  # noqa: E402
from translate import translate  # noqa: E402

JAVA_ASSETS = os.environ.get("JAVA_ASSETS", "/tmp/claude-0/jm/assets/minecraft")
OUT = os.environ.get("OUT", os.path.join(HERE, "..", "..", "resources", "block_models.bin"))

# Blocks whose rendering the client implements specially; they keep the legacy path.
KEEP_LEGACY = {
    "grass_block", "mycelium", "podzol", "water", "lava", "bubble_column", "fire", "soul_fire", "redstone_wire",
    "air", "cave_air", "void_air", "light", "barrier", "structure_void", "moving_piston", "piston_head",
}

# Java OffsetType: XZ (0.25) = 1, XYZ = 2, dripstone (0.125) = 3
OFFSET_XZ = {
    "poppy", "dandelion", "blue_orchid", "allium", "azure_bluet", "red_tulip", "orange_tulip", "white_tulip",
    "pink_tulip", "oxeye_daisy", "cornflower", "lily_of_the_valley", "torchflower", "wither_rose", "sunflower",
    "lilac", "rose_bush", "peony", "tall_grass", "large_fern", "bamboo", "bamboo_sapling", "dead_bush",
    "seagrass", "tall_seagrass", "small_dripleaf", "hanging_roots", "crimson_roots", "warped_roots",
    "nether_sprouts", "golden_dandelion", "open_eyeblossom", "closed_eyeblossom", "short_dry_grass",
    "tall_dry_grass", "firefly_bush", "bush",
}
OFFSET_XYZ = {"short_grass", "fern"}
OFFSET_DRIPSTONE = {"pointed_dripstone", "sulfur_spike"}

DIR_INDEX = {d: i for i, d in enumerate(DIRS)}


def load_records():
    states = json.load(open(os.path.join(HERE, "bedrock_states.json")))
    phys = json.load(open(os.path.join(HERE, "physics.json")))
    assert len(states) == len(phys), (len(states), len(phys))
    recs = []
    for (name, st), p in zip(states, phys):
        seq, h, flags, surf, fr, hs, vs, fh, boxes = p
        recs.append({"name": name, "state": st, "hash": h, "flags": flags,
                     "boxes": [[c / 1e8 for c in b] for b in boxes]})
    return recs


def is_full_cube(model):
    els = model["elements"] or []
    if len(els) != 1:
        return False
    e = els[0]
    return (e["from"] == [0, 0, 0] and e["to"] == [16, 16, 16] and not e.get("rotation") and len(e["faces"]) == 6)


class Gen:
    def __init__(self):
        self.models = Models(JAVA_ASSETS)
        self.jb = load_all(JAVA_ASSETS)
        self.texmap = json.load(open(os.path.join(HERE, "texmap.json")))
        manual = os.path.join(HERE, "texalias.json")
        self.alias = json.load(open(manual)) if os.path.exists(manual) else {}
        self.tex_ids = OrderedDict()
        self.variant_ids = OrderedDict()
        self.variants = []
        self.entry_ids = OrderedDict()
        self.entries = []
        self.unresolved_tex = Counter()
        self.include_cache = {}
        self.vox_cache = {}

    # ----------------------------------------------------------------------------------------------
    def include(self, jname):
        """True when this Java block must be rendered through the model path."""
        if jname in self.include_cache:
            return self.include_cache[jname]
        res = False
        jb = self.jb.get(jname)
        if jb and jname not in KEEP_LEGACY:
            refs = jb.all_groups()
            visible = False
            noncube = False
            for mid, x, y in refs:
                m = self.models.resolve(mid)
                if m and m["elements"]:
                    visible = True
                    if not is_full_cube(m):
                        noncube = True
            distinct = len({(mid, x, y) for mid, x, y in refs})
            res = visible and (noncube or distinct > 1)
        self.include_cache[jname] = res
        return res

    def tex_index(self, jtex):
        if jtex in ("missingno",):
            return 0xFFFF
        path = None
        if jtex in self.alias:
            path = self.alias[jtex]
        elif jtex in self.texmap and (self.texmap[jtex]["diff"] <= 14
                                      or os.path.basename(self.texmap[jtex]["path"]) == os.path.basename(jtex)):
            path = self.texmap[jtex]["path"]
        elif jtex in self.texmap:
            path = self.texmap[jtex]["path"]
            self.unresolved_tex[jtex + " -> " + path] += 1
        else:
            self.unresolved_tex[jtex] += 1
            return 0xFFFF
        if path not in self.tex_ids:
            self.tex_ids[path] = len(self.tex_ids)
        return self.tex_ids[path]

    def bake_variant(self, mid, x, y, uvlock):
        key = (mid, x % 360, y % 360, bool(uvlock))
        if key in self.variant_ids:
            return self.variant_ids[key]
        quads = self.models.bake(mid, x, y, uvlock)
        out = []
        for q in quads:
            t = self.tex_index(q["tex"])
            if t == 0xFFFF:
                continue
            out.append((DIR_INDEX[q["dir"]], DIR_INDEX[q["shade_dir"]], DIR_INDEX[q["cull"]] if q["cull"] else -1,
                        q["tint"], 1 if q["shade"] else 0, min(15, int(q.get("light", 0))), t, q["pos"], q["uv"]))
        vid = len(self.variants)
        self.variants.append(out)
        self.variant_ids[key] = vid
        return vid

    # ---- voxel oracle ------------------------------------------------------------------------------
    def boxes_of(self, mid, x, y):
        key = (mid, x % 360, y % 360)
        if key in self.vox_cache:
            return self.vox_cache[key]
        m = self.models.resolve(mid)
        rot = block_rotation(x, y)
        boxes = []
        if m and m["elements"]:
            for e in m["elements"]:
                fr = np.array(e["from"], dtype=float) / 16
                to = np.array(e["to"], dtype=float) / 16
                corners = np.array([[c[0], c[1], c[2]] for c in itertools.product(*zip(fr, to))])
                r = e.get("rotation")
                if r:
                    # approximate: ignore element rotation (only the bounding box matters for the fit)
                    pass
                corners = 0.5 + (rot @ (corners - 0.5).T).T
                boxes.append((corners.min(axis=0), corners.max(axis=0)))
        self.vox_cache[key] = boxes
        return boxes

    @staticmethod
    def voxelize(boxes):
        g = (np.arange(16) + 0.5) / 16
        X, Y, Z = np.meshgrid(g, g, g, indexing="ij")
        v = np.zeros((16, 16, 16), bool)
        for lo, hi in boxes:
            v |= ((X > lo[0] - 1e-6) & (X < hi[0] + 1e-6) & (Y > lo[1] - 1e-6) & (Y < hi[1] + 1e-6)
                  & (Z > lo[2] - 1e-6) & (Z < hi[2] + 1e-6))
        return v

    def candidate_boxes(self, jb, props):
        groups = jb.select(props)
        boxes = []
        for g in groups or []:
            mid, x, y, uvl, w = g[0]
            boxes.extend(self.boxes_of(mid, x, y))
        return boxes

    def fit(self, rec, jb, props, search):
        keys = list(search)
        combos = list(itertools.product(*[search[k] for k in keys]))
        if len(combos) == 1:
            p = dict(props)
            p.update(dict(zip(keys, combos[0])))
            return p, None
        target = self.voxelize([(np.array(b[:3]), np.minimum(np.array(b[3:]), 1.0)) for b in rec["boxes"]]) if rec["boxes"] else None
        best, bestp, bests = -1, None, None
        for c in combos:
            p = dict(props)
            p.update(dict(zip(keys, c)))
            if target is None:
                return p, None
            cand = self.voxelize(self.candidate_boxes(jb, p))
            inter = np.logical_and(cand, target).sum()
            union = np.logical_or(cand, target).sum()
            s = inter / union if union else 0.0
            if s > best + 1e-9:
                best, bestp, bests = s, p, s
        return bestp, bests

    # ---- emission ----------------------------------------------------------------------------------
    def entry_for(self, jname, props, offset):
        jb = self.jb[jname]
        groups = jb.select(props)
        if groups is None:
            return None
        gl = []
        total = 0
        for g in groups:
            choices = []
            for (mid, x, y, uvl, w) in g:
                vid = self.bake_variant(mid, x, y, uvl)
                choices.append((vid, min(255, w)))
                total += len(self.variants[vid])
            gl.append(tuple(choices))
        if total == 0:
            return None
        key = (offset, tuple(gl))
        if key not in self.entry_ids:
            self.entry_ids[key] = len(self.entries)
            self.entries.append(key)
        return self.entry_ids[key]

    def run(self):
        recs = load_records()
        blocks = []
        stats = Counter()
        fitlog = Counter()
        poor = []
        missing_props = Counter()
        for rec in recs:
            tr = translate(rec["name"], rec["state"], self.jb)
            if tr is None:
                stats["no-translation"] += 1
                continue
            jname, props, search = tr
            if jname not in self.jb:
                stats["no-java-block:" + jname] += 1
                continue
            if not self.include(jname):
                stats["legacy-path"] += 1
                continue
            jb = self.jb[jname]
            for p in list(props):
                if p not in jb.values:
                    missing_props[(jname, p)] += 1
                    del props[p]
            for p in list(search):
                if p not in jb.values:
                    del search[p]
            if search:
                props, score = self.fit(rec, jb, props, search)
                if score is not None:
                    fitlog[round(score, 1)] += 1
                    if score < 0.6:
                        poor.append((rec["name"], rec["state"], props, round(score, 2)))
            offset = 0
            if jname in OFFSET_XZ:
                offset = 1
            elif jname in OFFSET_XYZ:
                offset = 2
            elif jname in OFFSET_DRIPSTONE:
                offset = 3
            eid = self.entry_for(jname, props, offset)
            if eid is None:
                stats["empty:" + jname] += 1
                continue
            blocks.append((rec["hash"], eid))
            stats["emitted"] += 1
        self.write(blocks)
        print("emitted", len(blocks), "block states;", len(self.variants), "variants;", len(self.entries),
              "entries;", len(self.tex_ids), "textures")
        for k, v in sorted(stats.items(), key=lambda kv: -kv[1])[:60]:
            print("  ", k, v)
        print("fit score histogram:", sorted(fitlog.items()))
        print("poor fits:", len(poor))
        for p in poor[:40]:
            print("   ", p)
        print("props not on java block:", dict(missing_props.most_common(40)))
        print("unresolved/poor textures:", dict(self.unresolved_tex.most_common(80)))

    def write(self, blocks):
        out = bytearray()
        out += b"PBM1"
        out += struct.pack("<I", len(self.tex_ids))
        for path in self.tex_ids:
            bts = path.encode()
            out += struct.pack("<H", len(bts)) + bts
        out += struct.pack("<I", len(self.variants))
        for quads in self.variants:
            out += struct.pack("<H", len(quads))
            for (d, sd, cull, tint, shade, light, tex, pos, uv) in quads:
                out += struct.pack("<BBbbBBH", d, sd, cull, tint, shade, light, tex)
                for p in pos:
                    out += struct.pack("<3f", *p)
                for uvp in uv:
                    out += struct.pack("<2f", *uvp)
        out += struct.pack("<I", len(self.entries))
        for (offset, groups) in self.entries:
            out += struct.pack("<BB", offset, len(groups))
            for g in groups:
                out += struct.pack("<B", len(g))
                for vid, w in g:
                    out += struct.pack("<IB", vid, w)
        out += struct.pack("<I", len(blocks))
        for h, eid in blocks:
            out += struct.pack("<iI", h if h < 2 ** 31 else h - 2 ** 32, eid)
        os.makedirs(os.path.dirname(os.path.abspath(OUT)), exist_ok=True)
        open(OUT, "wb").write(out)
        print("wrote", OUT, len(out), "bytes")


if __name__ == "__main__":
    Gen().run()
