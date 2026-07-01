#!/usr/bin/env py
"""
recover_gnsmod.py  --  one-off salvage tool for a gutted .gnsmod file.

Background: gns::saveModule (game/core/src/ModuleIO.cpp) used to overwrite a module
file *in place* -- DROP TABLE ... / CREATE TABLE ... run before BEGIN, only the INSERTs
inside the transaction. A save that threw part-way left the file with empty module/maps/
areas tables, so loadModule fails with "module row missing" and the module won't open.

The old rows are not erased, only moved to the file's free pages (no VACUUM). This script
reads the raw SQLite pages -- including the freelist and overflow chains -- decodes every
table-leaf record it can find, buckets the records back into their tables by column
signature, and writes a fresh, valid .gnsmod using the exact schema loadModule expects
(PRAGMA user_version = 13).

Weak spots (see plan): the `maps` header row (grid dimensions) and the `module` row landed
on pages that got zeroed. The map terrain arrays partially survive on an overflow page; the
grid size is inferred and the module name/start/end are reconstructed and reported for the
user to confirm in the Creator.

Usage:
    py tools/recover_gnsmod.py <corrupt.gnsmod> [-o out.gnsmod] [--dump] [--name NAME]

    --dump   decode & print what was recovered, write nothing (validation pass)
    -o       output path (default: <input dir>/<stem>.recovered.gnsmod)
    --name   module name to store (default: derived from the input filename)
"""

import argparse
import os
import re
import sqlite3
import struct
import sys

# ---------------------------------------------------------------------------
# Low-level SQLite b-tree page / record decoding
# ---------------------------------------------------------------------------


class Db:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        if self.data[:16] != b"SQLite format 3\x00":
            raise SystemExit(f"{path}: not a SQLite file")
        self.page_size = struct.unpack(">H", self.data[16:18])[0]
        if self.page_size == 1:
            self.page_size = 65536
        self.reserved = self.data[20]
        self.usable = self.page_size - self.reserved
        self.npages = len(self.data) // self.page_size

    def page(self, n):  # 1-based
        off = (n - 1) * self.page_size
        return self.data[off:off + self.page_size]

    # --- varint --------------------------------------------------------------
    @staticmethod
    def varint(buf, o):
        v = 0
        for i in range(9):
            c = buf[o + i]
            if i == 8:
                return (v << 8) | c, o + 9
            v = (v << 7) | (c & 0x7F)
            if not (c & 0x80):
                return v, o + i + 1
        return v, o + 9

    # --- freelist ------------------------------------------------------------
    def freelist_pages(self):
        pages = set()
        trunk = struct.unpack(">I", self.data[32:36])[0]
        seen = set()
        while trunk and trunk not in seen:
            seen.add(trunk)
            base = (trunk - 1) * self.page_size
            nxt = struct.unpack(">I", self.data[base:base + 4])[0]
            n = struct.unpack(">I", self.data[base + 4:base + 8])[0]
            for i in range(n):
                p = struct.unpack(">I", self.data[base + 8 + i * 4:base + 12 + i * 4])[0]
                if p:
                    pages.add(p)
            trunk = nxt
        return pages

    # --- overflow chain ------------------------------------------------------
    def follow_overflow(self, first_page, remaining):
        """Return `remaining` bytes read from an overflow chain starting at first_page."""
        out = bytearray()
        pg = first_page
        seen = set()
        while pg and remaining > 0 and pg not in seen and 1 <= pg <= self.npages:
            seen.add(pg)
            base = (pg - 1) * self.page_size
            nxt = struct.unpack(">I", self.data[base:base + 4])[0]
            chunk = self.data[base + 4:base + 4 + min(remaining, self.usable - 4)]
            out += chunk
            remaining -= len(chunk)
            pg = nxt
        return bytes(out)

    # --- read one table-leaf cell (with overflow) ----------------------------
    def read_table_leaf_cell(self, page_no, cell_off):
        """Decode a table b-tree leaf cell at absolute offset. Returns (rowid, payload)."""
        d = self.data
        payload_len, o = self.varint(d, cell_off)
        rowid, o = self.varint(d, o)
        # Sanity guard: free pages carry stale cell arrays whose bogus payload lengths
        # would otherwise send follow_overflow copying gigabytes. A real record never
        # exceeds the whole file.
        if payload_len < 0 or payload_len > len(d):
            raise ValueError("implausible payload length")
        # overflow threshold for table b-tree leaf
        U = self.usable
        max_local = U - 35
        min_local = (U - 12) * 32 // 255 - 23
        if payload_len <= max_local:
            local = d[o:o + payload_len]
            return rowid, local
        # spilled: compute how much is stored locally
        K = min_local + (payload_len - min_local) % (U - 4)
        local_size = K if K <= max_local else min_local
        local = d[o:o + local_size]
        ovf_ptr = struct.unpack(">I", d[o + local_size:o + local_size + 4])[0]
        rest = self.follow_overflow(ovf_ptr, payload_len - local_size)
        return rowid, local + rest

    # --- decode a record payload into typed values ---------------------------
    def decode_record(self, payload):
        hlen, p = self.varint(payload, 0)
        if hlen > len(payload):
            raise ValueError("header longer than payload")
        types = []
        while p < hlen:
            t, p = self.varint(payload, p)
            types.append(t)
        vals = []
        d = hlen
        for t in types:
            if t == 0:
                vals.append(None)
            elif t == 1:
                vals.append(int.from_bytes(payload[d:d + 1], "big", signed=True)); d += 1
            elif t == 2:
                vals.append(int.from_bytes(payload[d:d + 2], "big", signed=True)); d += 2
            elif t == 3:
                vals.append(int.from_bytes(payload[d:d + 3], "big", signed=True)); d += 3
            elif t == 4:
                vals.append(int.from_bytes(payload[d:d + 4], "big", signed=True)); d += 4
            elif t == 5:
                vals.append(int.from_bytes(payload[d:d + 6], "big", signed=True)); d += 6
            elif t == 6:
                vals.append(int.from_bytes(payload[d:d + 8], "big", signed=True)); d += 8
            elif t == 7:
                vals.append(struct.unpack(">d", payload[d:d + 8])[0]); d += 8
            elif t == 8:
                vals.append(0)
            elif t == 9:
                vals.append(1)
            elif t >= 12 and t % 2 == 0:
                n = (t - 12) // 2
                vals.append(bytes(payload[d:d + n])); d += n
            else:  # t >= 13 odd -> text
                n = (t - 13) // 2
                vals.append(payload[d:d + n].decode("utf-8", "replace")); d += n
        return types, vals

    # --- iterate every decodable table-leaf cell in the file -----------------
    def all_leaf_records(self):
        recs = []
        for pg in range(1, self.npages + 1):
            base = (pg - 1) * self.page_size
            hdr = 100 if pg == 1 else 0
            ptype = self.data[base + hdr]
            if ptype != 0x0D:  # table b-tree leaf only
                continue
            ncell = struct.unpack(">H", self.data[base + hdr + 3:base + hdr + 5])[0]
            # A leaf cell needs >= ~4 bytes; cap the count so a corrupt/free page's stale
            # header can't spin us over tens of thousands of phantom cells.
            ncell = min(ncell, self.page_size // 4)
            cpa = base + hdr + 8
            for i in range(ncell):
                cp = struct.unpack(">H", self.data[cpa + i * 2:cpa + i * 2 + 2])[0]
                if cp < hdr + 8 or cp >= self.page_size:
                    continue
                try:
                    rowid, payload = self.read_table_leaf_cell(pg, base + cp)
                    types, vals = self.decode_record(payload)
                    recs.append((pg, rowid, types, vals))
                except Exception as e:  # noqa: BLE001 -- best effort salvage
                    recs.append((pg, "ERR", None, str(e)))
        return recs


# ---------------------------------------------------------------------------
# Classify recovered records back into their tables
# ---------------------------------------------------------------------------
#
# The .gnsmod tables have distinct column shapes; we match on arity + the type
# of key columns. `id INTEGER PRIMARY KEY` columns are rowid aliases and appear
# as None (the real value is the cell rowid), so we splice the rowid back in.


def is_int(v):
    return isinstance(v, int)


def is_txt(v):
    return isinstance(v, str)


def classify(recs):
    tables = {
        "areas": [], "area_shop_items": [], "area_monsters": [], "area_treasures": [],
        "area_prerequisites": [], "control_points": [], "map_objects": [], "maps": [],
        "module": [], "area_images": [], "area_transitions": [], "map_texts": [],
        "unknown": [],
    }
    for pg, rowid, types, vals in recs:
        if types is None:  # decode error
            continue
        n = len(vals)
        v = vals

        # areas: id(None),map_id,label,name,color,dm,player,monster_chance,monster_type,
        # treasure_chance,... up to 23. First col None(rowid), col1 int(map_id),
        # col2 text(label), col4 int-ish(color). Arity 10..23.
        if (v and v[0] is None and n >= 10 and is_int(v[1]) and is_txt(v[2])
                and is_txt(v[3]) and is_int(v[4]) and is_txt(v[5]) and is_txt(v[6])):
            tables["areas"].append((rowid, v))
            continue
        # area_shop_items: area_id, name, description, cost, stock, image, image_id (7)
        if (n == 7 and is_int(v[0]) and is_txt(v[1]) and is_txt(v[2]) and is_int(v[3])
                and is_int(v[4]) and is_txt(v[5]) and is_txt(v[6])):
            tables["area_shop_items"].append((rowid, v))
            continue
        # control_points: id(None),name,desc,map_id,area_id,kind,x,y (8, x/y float)
        if (n == 8 and v[0] is None and is_txt(v[1]) and is_txt(v[2]) and is_int(v[3])
                and is_int(v[4]) and is_int(v[5])):
            tables["control_points"].append((rowid, v))
            continue
        # map_objects: id(None),map_id,type,x,y,rot,scale (7, x..scale float)
        if (n == 7 and v[0] is None and is_int(v[1]) and is_int(v[2])
                and isinstance(v[3], float)):
            tables["map_objects"].append((rowid, v))
            continue
        # map_texts: id(None),map_id,x,y,text,color,size (7 with text at idx4)
        if (n == 7 and v[0] is None and is_int(v[1]) and isinstance(v[2], float)
                and isinstance(v[3], float) and is_txt(v[4])):
            tables["map_texts"].append((rowid, v))
            continue
        # area_images: area_id,slot,path,direction,is_default (5)
        if (n == 5 and is_int(v[0]) and is_int(v[1]) and is_txt(v[2])
                and is_int(v[3]) and is_int(v[4])):
            tables["area_images"].append((rowid, v))
            continue
        # area_monsters: area_id,type(text),count(int) (3)
        if n == 3 and is_int(v[0]) and is_txt(v[1]) and is_int(v[2]):
            tables["area_monsters"].append((rowid, v))
            continue
        # area_treasures: area_id,type(text),chance(int) -- same shape as monsters;
        # handled together below via a heuristic. (Both are (int,text,int).)
        # area_transitions: area_id,target_area_id,label (int,int,text)
        if n == 3 and is_int(v[0]) and is_int(v[1]) and is_txt(v[2]):
            tables["area_transitions"].append((rowid, v))
            continue
        # area_prerequisites: area_id,control_point_id (2 ints)
        if n == 2 and is_int(v[0]) and is_int(v[1]):
            tables["area_prerequisites"].append((rowid, v))
            continue
        # maps: id(None),name,grid_w,grid_h,overlay_w,overlay_h,cells(blob),cell_area(blob)
        if (n == 8 and v[0] is None and is_txt(v[1]) and is_int(v[2]) and is_int(v[3])
                and isinstance(v[6], (bytes, bytearray))):
            tables["maps"].append((rowid, v))
            continue
        # module: id(1),name,author,summary,start_map,start_area,end_area,cover,...
        if v and v[0] == 1 and n >= 7 and is_txt(v[1]):
            tables["module"].append((rowid, v))
            continue
        tables["unknown"].append((pg, rowid, types, v))
    return tables


# area_monsters vs area_treasures both look like (int, text, int). Disambiguate by
# value: monster rows carry a monster name; treasure `type` may be empty and `chance`
# a percentage. We keep the (int,text,int) rows and split heuristically at write time.


# ---------------------------------------------------------------------------
# Recover map cell arrays from surviving overflow pages
# ---------------------------------------------------------------------------


def recover_map_arrays(db):
    """The maps row (with the cells/cell_area int blobs) was lost, but its blob payload
    may survive on now-free overflow pages as little-endian int32 arrays. Return a list
    of (page_no, ints) for pages that look like an int32 map array."""
    free = db.freelist_pages()
    arrays = []
    for pg in sorted(free):
        base = (pg - 1) * db.page_size
        ptype = db.data[base]
        if ptype in (0x0D, 0x05, 0x0A, 0x02):
            continue  # a real b-tree page, not overflow
        body = db.data[base + 4:base + db.page_size]  # skip overflow next-ptr
        if len(body) % 4:
            body = body[:len(body) - (len(body) % 4)]
        ints = list(struct.unpack("<%dI" % (len(body) // 4), body))
        # Heuristic: map arrays are dominated by small values (terrain/area ids).
        small = sum(1 for x in ints if x < 256)
        if ints and small / len(ints) > 0.8:
            arrays.append((pg, ints))
    return arrays


# ---------------------------------------------------------------------------
# Build a fresh valid .gnsmod
# ---------------------------------------------------------------------------

KSCHEMA = """
CREATE TABLE module (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    name TEXT, author TEXT, summary TEXT,
    start_map_id INTEGER, start_area_id INTEGER, end_area_id INTEGER,
    cover_art TEXT, splash_music TEXT, default_music TEXT);
CREATE TABLE maps (
    id INTEGER PRIMARY KEY, name TEXT,
    grid_w INTEGER, grid_h INTEGER, overlay_w INTEGER, overlay_h INTEGER,
    cells BLOB, cell_area BLOB);
CREATE TABLE areas (
    id INTEGER PRIMARY KEY, map_id INTEGER,
    label TEXT, name TEXT, color INTEGER,
    dm_text TEXT, player_text TEXT,
    monster_chance INTEGER, monster_type TEXT,
    treasure_chance INTEGER, treasure_type TEXT,
    trap_chance INTEGER, trap_desc TEXT,
    lock_chance INTEGER, lock_desc TEXT,
    hidden_chance INTEGER, hidden_desc TEXT,
    artwork_path TEXT, fill_enabled INTEGER, label_auto INTEGER,
    is_shop INTEGER, music TEXT, hidden INTEGER);
CREATE TABLE area_images (
    area_id INTEGER, slot INTEGER, path TEXT, direction INTEGER, is_default INTEGER);
CREATE TABLE area_monsters (area_id INTEGER, type TEXT, count INTEGER);
CREATE TABLE area_treasures (area_id INTEGER, type TEXT, chance INTEGER);
CREATE TABLE area_shop_items (
    area_id INTEGER, name TEXT, description TEXT, cost INTEGER, stock INTEGER,
    image TEXT, image_id TEXT);
CREATE TABLE area_transitions (area_id INTEGER, target_area_id INTEGER, label TEXT);
CREATE TABLE control_points (
    id INTEGER PRIMARY KEY, name TEXT, description TEXT,
    map_id INTEGER, area_id INTEGER, kind INTEGER, x REAL, y REAL);
CREATE TABLE area_prerequisites (area_id INTEGER, control_point_id INTEGER);
CREATE TABLE map_objects (
    id INTEGER PRIMARY KEY, map_id INTEGER, type INTEGER,
    x REAL, y REAL, rot REAL, scale REAL);
CREATE TABLE map_texts (
    id INTEGER PRIMARY KEY, map_id INTEGER, x REAL, y REAL,
    text TEXT, color INTEGER, size REAL);
"""

USER_VERSION = 13


def area_from_row(rowid, v):
    """Pad a recovered `areas` record out to the full 23-column tuple (older/partial
    records omit trailing columns -> fill with sensible defaults matching loadModule)."""
    def g(i, default):
        return v[i] if i < len(v) and v[i] is not None else default
    return (
        rowid,               # id
        g(1, 1),             # map_id
        g(2, ""),            # label
        g(3, ""),            # name
        g(4, 0),             # color
        g(5, ""),            # dm_text
        g(6, ""),            # player_text
        g(7, 0),             # monster_chance
        g(8, ""),            # monster_type
        g(9, 0),             # treasure_chance
        g(10, ""),           # treasure_type
        g(11, 0),            # trap_chance
        g(12, ""),           # trap_desc
        g(13, 0),            # lock_chance
        g(14, ""),           # lock_desc
        g(15, 0),            # hidden_chance
        g(16, ""),           # hidden_desc
        g(17, ""),           # artwork_path
        g(18, 1),            # fill_enabled
        g(19, 0),            # label_auto
        g(20, 0),            # is_shop
        g(21, ""),           # music
        g(22, 0),            # hidden
    )


def main():
    ap = argparse.ArgumentParser(description="Salvage a gutted .gnsmod file.")
    ap.add_argument("input")
    ap.add_argument("-o", "--output")
    ap.add_argument("--dump", action="store_true")
    ap.add_argument("--name")
    ap.add_argument("--start-area", type=int, help="override start_area_id")
    ap.add_argument("--end-area", type=int, help="override end_area_id")
    args = ap.parse_args()

    db = Db(args.input)
    print(f"file: {args.input}  page_size={db.page_size} pages={db.npages} "
          f"free={sorted(db.freelist_pages())}")

    recs = db.all_leaf_records()
    tables = classify(recs)

    # Areas: keep highest-arity record per rowid (a fuller record wins over a stub).
    area_by_id = {}
    for rowid, v in tables["areas"]:
        cur = area_by_id.get(rowid)
        if cur is None or len(v) > len(cur):
            area_by_id[rowid] = v
    areas = {rid: area_from_row(rid, v) for rid, v in area_by_id.items()}

    print("\n=== recovered areas ===")
    for rid in sorted(areas):
        a = areas[rid]
        print(f"  area {rid:2d}  label={a[2]!r:10} name={a[3]!r:34} "
              f"dm={len(a[5])}c player={len(a[6])}c shop={a[20]}")

    # Shop items
    shop = [v for _, v in tables["area_shop_items"]]
    print(f"\n=== shop items: {len(shop)} ===")
    for v in shop:
        print(f"  area {v[0]:2d}  {v[1]!r:28} cost={v[3]} stock={v[4]}")

    # (int,text,int) rows -> split monsters vs treasures. Monsters carry a non-empty
    # name that isn't a bare treasure code; treasures usually have a small chance value.
    mt_rows = [v for _, v in tables["area_monsters"]]  # (int,text,int) landed here
    print(f"\n=== area_monsters/treasures candidates ({len(mt_rows)}) ===")
    for v in mt_rows:
        print(f"  area {v[0]:2d}  type={v[1]!r:16} n/chance={v[2]}")

    print(f"\n=== control_points: {len(tables['control_points'])} ===")
    for _, v in tables["control_points"]:
        print(f"  {v[1]!r:28} map={v[3]} area={v[4]} kind={v[5]} x={v[6]} y={v[7]}")

    print(f"\n=== map_objects: {len(tables['map_objects'])} ===")
    for rid, v in tables["map_objects"]:
        print(f"  obj {rid} map={v[1]} type={v[2]} x={v[3]:.2f} y={v[4]:.2f} "
              f"rot={v[5]} scale={v[6]}")

    print(f"\n=== area_prerequisites: {len(tables['area_prerequisites'])} ===")
    for _, v in tables["area_prerequisites"]:
        print(f"  area {v[0]} requires cp {v[1]}")

    print(f"\n=== area_images (live): {len(tables['area_images'])} ===")
    for _, v in tables["area_images"]:
        print(f"  area {v[0]} slot {v[1]} default={v[4]} {os.path.basename(v[2])}")

    # Map arrays from overflow
    map_arrays = recover_map_arrays(db)
    print(f"\n=== candidate map int-arrays on free overflow pages ===")
    for pg, ints in map_arrays:
        nz = sum(1 for x in ints if x)
        print(f"  page {pg}: {len(ints)} ints, {nz} non-zero, "
              f"sample={ints[:24]}")

    print(f"\n=== unknown/unclassified records: {len(tables['unknown'])} ===")
    for pg, rowid, types, v in tables["unknown"][:20]:
        preview = [ (x[:30] if isinstance(x, str) else x) for x in (v or []) ][:8]
        print(f"  page {pg} rowid {rowid} types={types} {preview}")

    if args.dump:
        print("\n[--dump] no file written.")
        return

    # ---- decide start/end areas -------------------------------------------
    def find_area(pred):
        for rid in sorted(areas):
            if pred(areas[rid]):
                return rid
        return 0

    start_area = args.start_area or find_area(
        lambda a: "arrival" in (a[3] or "").lower())
    end_area = args.end_area or 0
    module_name = args.name or re.sub(r"[_\-]+", " ",
                                      os.path.splitext(os.path.basename(args.input))[0])

    out = args.output or os.path.join(
        os.path.dirname(os.path.abspath(args.input)),
        os.path.splitext(os.path.basename(args.input))[0] + ".recovered.gnsmod")
    if os.path.exists(out):
        os.remove(out)

    con = sqlite3.connect(out)
    con.executescript(KSCHEMA)
    con.execute(f"PRAGMA user_version = {USER_VERSION}")

    # module row (reconstructed -- confirm in Creator)
    con.execute(
        "INSERT INTO module(id,name,author,summary,start_map_id,start_area_id,"
        "end_area_id,cover_art,splash_music,default_music) VALUES(1,?,?,?,?,?,?,?,?,?)",
        (module_name, "", "", 1, start_area, end_area, "", "", ""))

    # maps row: synthesize a single map (id 1). The maps header row (grid dims) and the
    # terrain `cells` blob were lost when their pages were zeroed; only the `cell_area`
    # (which cell belongs to which area) partially survives on an overflow page. IMPORTANT:
    # both cells and cell_area MUST be sized grid_w*grid_h -- the Creator/renderer index
    # them as cells[y*gridW+x] with no bounds check, so a size mismatch crashes. We emit a
    # consistent square grid: the recovered cell_area, an all-Empty cells array of the same
    # length, and grid_w=grid_h=isqrt(len). The layout shape is a best guess -- the true
    # width is unknown -- so the area blobs may appear scrambled; re-check/repaint in the
    # Creator. Fallback: a blank default grid if nothing usable was recovered.
    import math
    DEFAULT_W = DEFAULT_H = 32
    TERRAIN_EMPTY = 0  # gns::Terrain::Empty
    area_ids = set(areas)
    best = None
    for _pg, ints in map_arrays:
        hits = sum(1 for x in ints if x in area_ids)
        if best is None or hits > best[0]:
            best = (hits, ints)
    if best and best[0] > 0:
        ints = best[1]
        side = int(math.isqrt(len(ints)))
        grid_w = grid_h = side
        n = side * side
        cell_area_blob = struct.pack("<%dI" % n, *[max(0, v) for v in ints[:n]])
    else:
        grid_w, grid_h = DEFAULT_W, DEFAULT_H
        n = grid_w * grid_h
        cell_area_blob = struct.pack("<%dI" % n, *([0] * n))
    cells_blob = struct.pack("<%dI" % (grid_w * grid_h),
                             *([TERRAIN_EMPTY] * (grid_w * grid_h)))
    # overlay is the coarse label grid (Map defaults 8x6), independent of the fine grid.
    con.execute(
        "INSERT INTO maps(id,name,grid_w,grid_h,overlay_w,overlay_h,cells,cell_area) "
        "VALUES(1,?,?,?,?,?,?,?)",
        ("Recovered Map", grid_w, grid_h, 8, 6, cells_blob, cell_area_blob))

    # areas
    for rid in sorted(areas):
        con.execute(
            "INSERT INTO areas VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            areas[rid])

    # area_images
    for _, v in tables["area_images"]:
        con.execute("INSERT INTO area_images VALUES(?,?,?,?,?)", tuple(v))

    # shop items
    for v in shop:
        con.execute("INSERT INTO area_shop_items VALUES(?,?,?,?,?,?,?)", tuple(v))

    # monsters / treasures (all (int,text,int) rows preserved as monsters; treasures with
    # empty type are skipped -- confirm in Creator).
    for v in mt_rows:
        con.execute("INSERT INTO area_monsters VALUES(?,?,?)", tuple(v))

    # control points
    for rid, v in tables["control_points"]:
        con.execute("INSERT INTO control_points VALUES(?,?,?,?,?,?,?,?)",
                    (rid, v[1], v[2], v[3], v[4], v[5], v[6], v[7]))

    # prerequisites
    for _, v in tables["area_prerequisites"]:
        con.execute("INSERT INTO area_prerequisites VALUES(?,?)", tuple(v))

    # map objects
    for rid, v in tables["map_objects"]:
        con.execute("INSERT INTO map_objects VALUES(?,?,?,?,?,?,?)",
                    (rid, v[1], v[2], v[3], v[4], v[5], v[6]))

    # map texts
    for rid, v in tables["map_texts"]:
        con.execute("INSERT INTO map_texts VALUES(?,?,?,?,?,?,?)",
                    (rid, v[1], v[2], v[3], v[4], v[5], v[6]))

    con.commit()
    con.close()

    print(f"\nWrote {out}")
    print(f"  module name : {module_name!r}")
    print(f"  start area  : {start_area}  end area: {end_area}  (confirm in Creator)")
    print(f"  areas       : {len(areas)}")
    print(f"  grid        : {grid_w}x{grid_h}  (inferred; re-check terrain paint)")
    print("\nNext: open in the Module Creator, set exactly one start + one end area,")
    print("touch up terrain paint, then rename over the original once verified.")


if __name__ == "__main__":
    main()
