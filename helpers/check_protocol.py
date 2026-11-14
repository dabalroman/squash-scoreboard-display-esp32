#!/usr/bin/env python3
"""Holds the BLE protocol v1 to its one definition, docs/protocol/*.yaml (dialect: docs/protocol/README.md).

  python helpers/check_protocol.py                 check the YAML, its scenarios and the markdown's generated regions
  python helpers/check_protocol.py build           rewrite the generated regions of docs/garmin-protocol.md
  python helpers/check_protocol.py selftest        prove that broken specs fail
  python helpers/check_protocol.py --repo <name>   also hold an implementation to it (firmware, client, watch, android, all)

Exit code 0 = green, 1 = red (every finding printed). Nothing here generates code: the implementations stay
hand-written, this script only compares.
"""

import argparse
import ast
import copy
import hashlib
import hmac
import re
import shutil
import sys
import tempfile
import zlib
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parent.parent
SPEC_DIR = REPO / "docs" / "protocol"
SPEC_MD = REPO / "docs" / "garmin-protocol.md"

INT_TYPES = {"uint8": 1, "uint16": 2, "uint32": 4}
TYPES = set(INT_TYPES) | {"uint8[]", "utf8s", "uuid128"}

# Allowed keys per object; a key outside these sets is a typo and fails the check.
KEYS = {
    "constant": ({"name", "type", "value", "description", "usage", "see"}, {"unit"}),
    "uuid": ({"name", "value", "description", "usage", "see"}, set()),
    "enum": ({"name", "type", "description", "usage", "see", "values"}, set()),
    "enum_value": ({"name", "description"}, {"value", "bit"}),
    "carrier": ({"identifier", "name", "description", "usage", "see", "frames"},
                {"uuid", "properties", "auth_required", "optional"}),
    "frame": ({"frame", "name", "access", "sender", "size", "description", "usage", "structure"}, {"chunked"}),
    "structure": ({"name", "description", "structure"}, {"offset", "usage", "see"}),
    "field": ({"field", "description"},
              {"type", "size", "value", "enum", "range", "present_if", "computed", "padding", "switch", "cases",
               "structure"}),
    "command": ({"id", "name", "description", "usage", "see", "valid_on", "args"}, {"unused"}),
    "scenario": ({"id", "section", "title", "goal", "steps"}, {"notes", "intro"}),
    "step": ({"step", "direction", "access", "characteristic", "frame", "hex", "why"},
             {"fields", "body", "state_seq", "derivation", "instead_of", "label"}),
    "derivation": (set(), {"hmac_sha256", "digest", "field", "crc32", "value"}),
    "hmac_sha256": ({"key", "message"}, set()),
}


class SpecError(Exception):
    pass


def hexbytes(text):
    return bytes.fromhex(str(text).replace(" ", ""))


def hexstr(data):
    return " ".join(f"{b:02x}" for b in data)


# --------------------------------------------------------------------------------------------------- loading


class Spec:
    def __init__(self, spec_dir):
        self.dir = Path(spec_dir)
        self.errors = []
        load = lambda p: yaml.safe_load(p.read_text(encoding="utf-8")) or {}
        consts = load(self.dir / "constants.yaml")
        self.constants = consts.get("constants", [])
        self.uuids = consts.get("uuids", [])
        self.enums = {e["name"]: e for e in consts.get("enums", [])}
        self.carriers, self.structures = {}, {}
        for path in sorted((self.dir / "characteristics").glob("*.yaml")):
            doc = load(path)
            for key in ("characteristic", "advertising"):
                if key in doc:
                    c = doc[key]
                    c["_kind"], c["_file"] = key, path.name
                    self.carriers[c["name"]] = c
            for s in doc.get("structures", []):
                s["_file"] = path.name
                self.structures[s["name"]] = s
        cmd_path = self.dir / "commands.yaml"
        self.commands = load(cmd_path).get("commands", []) if cmd_path.exists() else []
        scen = load(self.dir / "scenarios.yaml")
        self.fixtures = scen.get("fixtures", {})
        self.scenarios = scen.get("scenarios", [])

        self.C = {c["name"]: c["value"] for c in self.constants if "value" in c}
        self.C.update({u["name"]: u["value"] for u in self.uuids})
        self.enum_values = {}
        for name, e in self.enums.items():
            vals = {}
            for v in e.get("values", []):
                vals[v["name"]] = (1 << v["bit"]) if "bit" in v else v.get("value")
            self.enum_values[name] = vals

    def err(self, where, msg):
        self.errors.append(f"{where}: {msg}")

    def is_bits(self, enum_name):
        return any("bit" in v for v in self.enums[enum_name].get("values", []))

    def frame(self, carrier, frame):
        c = self.carriers.get(carrier)
        return next((f for f in c.get("frames", []) if f.get("frame") == frame), None) if c else None

    def command_by_id(self, cid):
        return next((c for c in self.commands if c["id"] == cid), None)


# --------------------------------------------------------------------------------------------------- validation


def check_keys(spec, where, obj, kind, purpose=True):
    required, optional = KEYS[kind]
    if not isinstance(obj, dict):
        spec.err(where, f"expected a mapping, got {type(obj).__name__}")
        return False
    for k in sorted(required - set(obj)):
        spec.err(where, f"missing `{k}`")
    for k in sorted(set(obj) - required - optional - {"_kind", "_file"}):
        spec.err(where, f"unknown key `{k}`")
    for k in ("description", "usage", "why", "goal"):
        if k in obj and not str(obj[k] or "").strip():
            spec.err(where, f"empty `{k}`")
    return True


def validate(spec):
    names = set()
    for c in spec.constants:
        where = f"constants.yaml {c.get('name')}"
        check_keys(spec, where, c, "constant")
        if c.get("name") in names:
            spec.err(where, "duplicate name")
        names.add(c.get("name"))
    for u in spec.uuids:
        where = f"constants.yaml {u.get('name')}"
        check_keys(spec, where, u, "uuid")
        if not re.fullmatch(r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}", str(u.get("value"))):
            spec.err(where, "not a lowercase 128-bit UUID")
    for name, e in spec.enums.items():
        where = f"constants.yaml enum {name}"
        check_keys(spec, where, e, "enum")
        seen = set()
        for v in e.get("values", []):
            check_keys(spec, f"{where}.{v.get('name')}", v, "enum_value")
            if ("value" in v) == ("bit" in v):
                spec.err(f"{where}.{v.get('name')}", "needs exactly one of `value`, `bit`")
            key = spec.enum_values[name].get(v.get("name"))
            if key in seen:
                spec.err(f"{where}.{v.get('name')}", "duplicate value")
            seen.add(key)
    for name, c in spec.carriers.items():
        where = f"{c['_file']} {name}"
        check_keys(spec, where, c, "carrier")
        if c["_kind"] == "characteristic":
            for k in ("uuid", "properties", "auth_required"):
                if k not in c:
                    spec.err(where, f"missing `{k}`")
            if c.get("uuid") not in spec.C:
                spec.err(where, f"uuid `{c.get('uuid')}` is not in constants.yaml uuids")
        for f in c.get("frames", []):
            fw = f"{where} frame {f.get('frame')}"
            check_keys(spec, fw, f, "frame")
            if f.get("access") not in ("read", "write", "notify", "advertise", "scan_response"):
                spec.err(fw, f"bad access `{f.get('access')}`")
            validate_structure(spec, fw, f.get("structure", []))
            if "chunked" in f and f["chunked"].get("body") not in spec.structures:
                spec.err(fw, f"chunked body `{f['chunked'].get('body')}` is not a structure")
            check_frame_size(spec, fw, f)
    for name, s in spec.structures.items():
        where = f"{s['_file']} structure {name}"
        check_keys(spec, where, s, "structure")
        validate_structure(spec, where, s.get("structure", []))
    ids = set()
    for cmd in spec.commands:
        where = f"commands.yaml {cmd.get('name')}"
        check_keys(spec, where, cmd, "command")
        if cmd.get("id") in ids:
            spec.err(where, "duplicate id")
        ids.add(cmd.get("id"))
        for screen in cmd.get("valid_on", []):
            if screen not in spec.enum_values.get("screen", {}) and screen != "ALL":
                spec.err(where, f"valid_on `{screen}` is not in enum screen")
        validate_structure(spec, where, cmd.get("args", []))
    validate_scenarios(spec)


def validate_structure(spec, where, structure):
    for fld in structure:
        fw = f"{where}.{fld.get('field')}"
        check_keys(spec, fw, fld, "field")
        if "switch" in fld:
            cases = fld.get("cases")
            if cases != "commands":
                for case, target in (cases or {}).items():
                    if target is not None and target not in spec.structures:
                        spec.err(fw, f"case {case} -> `{target}` is not a structure")
            continue
        if "structure" in fld:
            if fld["structure"] not in spec.structures:
                spec.err(fw, f"`{fld['structure']}` is not a structure")
            continue
        if fld.get("type") not in TYPES:
            spec.err(fw, f"bad type `{fld.get('type')}`")
        if fld.get("type") in ("uint8[]", "utf8s") and "size" not in fld:
            spec.err(fw, "needs `size`")
        if "enum" in fld and fld["enum"] not in spec.enums:
            spec.err(fw, f"enum `{fld['enum']}` is not in constants.yaml")


def field_size_bounds(spec, fld):
    """(min, max) bytes this field can take; max None = rest of the frame."""
    if "switch" in fld or "structure" in fld:
        return None
    size = fld.get("size", INT_TYPES.get(fld.get("type")))
    lo = 0 if "present_if" in fld else None
    if isinstance(size, int):
        n = size
    elif isinstance(size, str) and size in spec.C:
        n = spec.C[size]
    else:
        return (0, None)
    return (n if lo is None else 0, n)


def check_frame_size(spec, where, frame):
    declared = frame.get("size")
    lo = hi = 0
    branches = {}  # `x < y` / `x >= y` fields exclude each other: one branch counts, not both
    for fld in frame.get("structure", []):
        b = field_size_bounds(spec, fld)
        if b is None or b[1] is None:
            return  # variable part: scenarios check the length
        cond = fld.get("present_if")
        m = re.fullmatch(r"\s*(\w+)\s*(<|>=)\s*(\w+)\s*", str(cond)) if cond else None
        if m:
            key = (m[1], m[3])
            branches.setdefault(key, [0, 0])[m[2] == "<"] += b[1]
        else:
            lo += b[0]
            hi += b[1]
    for a, b in branches.values():
        if a and b:
            lo += min(a, b)
            hi += max(a, b)
        else:
            hi += a + b
    want = (declared, declared) if isinstance(declared, int) else tuple(declared or (None, None))
    if (lo, hi) != want:
        spec.err(where, f"declared size {declared} but the fields add up to {lo if lo == hi else [lo, hi]}")


# --------------------------------------------------------------------------------------------------- encoding


class Encoder:
    """Encodes a scenario step's decoded fields into bytes, the way the YAML says."""

    def __init__(self, spec, where, values, vector):
        self.spec, self.where, self.values, self.vector = spec, where, dict(values or {}), vector
        self.ctx, self.field_enum, self.used, self.computed = {}, {}, set(), {}

    def env(self):
        env = {"__builtins__": {}}
        env.update({k: v for k, v in self.spec.C.items() if isinstance(v, int)})
        for name, vals in self.spec.enum_values.items():
            env[name] = type(name, (), dict(vals))
        for fname, ename in self.field_enum.items():
            env.update(self.spec.enum_values[ename])
        env.update(self.ctx)
        return env

    def eval(self, expr):
        if isinstance(expr, int):
            return expr
        try:
            return eval(str(expr), self.env())
        except Exception as e:  # noqa: BLE001 - report any bad expression as a spec error
            raise SpecError(f"cannot evaluate `{expr}`: {e}")

    def resolve(self, value, enum=None):
        """A YAML value -> int: int, constant, enum.NAME, NAME of the field's enum, or a list of bit names."""
        if isinstance(value, bool):
            raise SpecError(f"boolean value {value}")
        if isinstance(value, int):
            return value
        if isinstance(value, list):
            out = 0
            for v in value:
                out |= self.resolve(v, enum)
            return out
        if enum and value in self.spec.enum_values[enum]:
            return self.spec.enum_values[enum][value]
        if "." in str(value):
            e, n = str(value).split(".", 1)
            if n in self.spec.enum_values.get(e, {}):
                return self.spec.enum_values[e][n]
        if value in self.spec.C:
            return self.spec.C[value]
        raise SpecError(f"unknown value `{value}`" + (f" (enum {enum})" if enum else ""))

    def take(self, name):
        if name not in self.values:
            raise SpecError(f"no value for field `{name}`")
        self.used.add(name)
        return self.values[name]

    def structure(self, structure, out):
        for fld in structure:
            if "present_if" in fld and not self.eval(fld["present_if"]):
                continue
            if "switch" in fld:
                key = self.ctx.get(fld["switch"])
                if fld["cases"] == "commands":
                    cmd = self.spec.command_by_id(key)
                    if cmd is None:
                        raise SpecError(f"no command with id {key}")
                    self.structure(cmd.get("args", []), out)
                    continue
                enum = self.field_enum.get(fld["switch"])
                names = [n for n, v in self.spec.enum_values.get(enum, {}).items() if v == key]
                target = next((fld["cases"][n] for n in names if n in fld["cases"]), fld["cases"].get("default"))
                if target:
                    self.structure(self.spec.structures[target]["structure"], out)
                continue
            if "structure" in fld:
                self.structure(self.spec.structures[fld["structure"]]["structure"], out)
                continue
            out += self.field(fld, len(out))

    def field(self, fld, pos):
        name, ftype, enum = fld["field"], fld["type"], fld.get("enum")
        size = fld.get("size", INT_TYPES.get(ftype))
        if "computed" in fld:
            n = self.eval(size)
            data = self.vector[pos:pos + n]
            if len(data) != n:
                raise SpecError(f"`{name}` is computed but the vector is too short")
            self.computed[name] = data
            self.used.add(name)
            return data
        if "value" in fld:
            raw = fld["value"]
            if name in self.values:  # a scenario may restate a fixed value; it must agree
                if self.resolve(self.take(name), enum) != self.resolve(raw, enum):
                    raise SpecError(f"`{name}` is fixed to {raw}")
        else:
            raw = self.take(name)
        if ftype in INT_TYPES:
            v = self.resolve(raw, enum)
            n = INT_TYPES[ftype]
            if not 0 <= v < (1 << (8 * n)):
                raise SpecError(f"`{name}` = {v} does not fit {ftype}")
            if "range" in fld:
                lo, hi = (self.eval(x) for x in fld["range"])
                if not lo <= v <= hi:
                    raise SpecError(f"`{name}` = {v} outside {fld['range']}")
            self.ctx[name] = v
            if enum:
                self.field_enum[name] = enum
            return v.to_bytes(n, "little")
        if ftype == "uuid128":
            data = bytes(reversed(hexbytes(str(self.resolve(raw)).replace("-", ""))))
        elif ftype == "utf8s":
            data = str(raw).encode("ascii")
        elif enum:
            data = bytes(self.resolve(v, enum) for v in raw)
        else:
            data = hexbytes(raw)
        if size == "rest":
            n = len(data)
        else:
            n = self.eval(size)
        if fld.get("padding") == "nul" and len(data) < n:
            data += bytes(n - len(data))
        if len(data) != n:
            raise SpecError(f"`{name}` is {len(data)} B, the layout says {n}")
        self.ctx[name] = n if ftype != "uint8[]" or enum else data
        return data


def encode_step(spec, where, step):
    """Returns (list of encoded frames, computed fields) for a scenario step."""
    frame = spec.frame(step["characteristic"], step["frame"])
    vectors = step["hex"] if isinstance(step["hex"], list) else [step["hex"]]
    if "chunked" in frame:
        body_enc = Encoder(spec, where, step.get("body"), b"")
        body = bytearray()
        body_enc.structure(spec.structures[frame["chunked"]["body"]]["structure"], body)
        unused = set(body_enc.values) - body_enc.used
        if unused:
            raise SpecError(f"body fields not in the layout: {sorted(unused)}")
        per = spec.C["CHUNK_BODY"]
        count = max(1, -(-len(body) // per))
        frames = []
        for i in range(count):
            vals = {"state_seq": step["state_seq"], "index_count": (i << 4) | count,
                    "body": hexstr(body[i * per:(i + 1) * per])}
            enc = Encoder(spec, where, vals, b"")
            out = bytearray()
            enc.structure(frame["structure"], out)
            frames.append(bytes(out))
        return frames, {}
    enc = Encoder(spec, where, step.get("fields"), hexbytes(vectors[0]))
    out = bytearray()
    enc.structure(frame["structure"], out)
    unused = set(enc.values) - enc.used
    if unused:
        raise SpecError(f"fields not in the layout: {sorted(unused)}")
    return [bytes(out)], enc.computed


# --------------------------------------------------------------------------------------------------- scenarios


def fixture_bytes(spec, ref):
    vals = spec.fixtures.get("values", {})
    return hexbytes(vals[ref]) if ref in vals else hexbytes(ref)


def check_derivation(spec, where, d, computed):
    check_keys(spec, where, d, "derivation")
    if "hmac_sha256" in d:
        h = d["hmac_sha256"]
        check_keys(spec, where, h, "hmac_sha256")
        digest = hmac.new(fixture_bytes(spec, h["key"]), hexbytes(h["message"]), hashlib.sha256).digest()
        if digest.hex() != str(d.get("digest", "")).replace(" ", ""):
            spec.err(where, f"HMAC digest is {digest.hex()}, the YAML says {d.get('digest')}")
        fld = d.get("field")
        if fld and computed.get(fld) != digest[:len(computed.get(fld, b""))]:
            spec.err(where, f"`{fld}` bytes are not the first bytes of the HMAC digest")
    elif "crc32" in d:
        # The crc32 input is a separate literal, not cross-checked against the read frames' entry
        # bytes; the vector scan against the firmware's real CRC holds that invariant instead.
        crc = zlib.crc32(hexbytes(d["crc32"]))
        if crc != d.get("value"):
            spec.err(where, f"CRC-32 is 0x{crc:08x}, the YAML says {d.get('value')}")
    else:
        spec.err(where, "derivation needs `hmac_sha256` or `crc32`")


def validate_scenarios(spec):
    ids = set()
    for sc in spec.scenarios:
        where = f"scenarios.yaml {sc.get('id')}"
        check_keys(spec, where, sc, "scenario")
        if sc.get("id") in ids:
            spec.err(where, "duplicate id")
        ids.add(sc.get("id"))
        steps = {}
        for st in sc.get("steps", []):
            sw = f"{where}/{st.get('step')}"
            if not check_keys(spec, sw, st, "step"):
                continue
            if st["step"] in steps:
                spec.err(sw, "duplicate step")
            steps[st["step"]] = st
            if "instead_of" in st and st["instead_of"] not in steps:
                spec.err(sw, f"instead_of `{st['instead_of']}` is not an earlier step")
            if st["characteristic"] not in spec.carriers:
                spec.err(sw, f"unknown characteristic `{st['characteristic']}`")
                continue
            if st["frame"] == "raw":
                continue  # deliberately off-layout bytes (malformed, foreign PROTO): only tag-checked
            frame = spec.frame(st["characteristic"], st["frame"])
            if frame is None:
                spec.err(sw, f"{st['characteristic']} has no frame `{st['frame']}`")
                continue
            if frame["access"] != st["access"] and not (frame["access"] == "notify" and st["access"] == "read"):
                spec.err(sw, f"access `{st['access']}` but frame `{st['frame']}` is `{frame['access']}`")
            try:
                frames, computed = encode_step(spec, sw, st)
            except (SpecError, KeyError, TypeError, ValueError) as e:
                spec.err(sw, str(e))
                continue
            vectors = st["hex"] if isinstance(st["hex"], list) else [st["hex"]]
            if [hexstr(f) for f in frames] != [hexstr(hexbytes(v)) for v in vectors]:
                spec.err(sw, "fields encode to\n      " + "\n      ".join(hexstr(f) for f in frames)
                         + "\n    but hex is\n      " + "\n      ".join(hexstr(hexbytes(v)) for v in vectors))
            for frm in frames:
                size = frame.get("size")
                lo, hi = (size, size) if isinstance(size, int) else size
                if not lo <= len(frm) <= hi:
                    spec.err(sw, f"{len(frm)} B outside the frame's size {size}")
                if frame["access"] in ("advertise", "scan_response"):
                    if len(frm) > spec.C.get("MAX_ADV", 31):
                        spec.err(sw, f"{len(frm)} B exceeds MAX_ADV")
                elif len(frm) > spec.C.get("MAX_FRAME", 20):
                    spec.err(sw, f"{len(frm)} B exceeds MAX_FRAME")
            for d in (st.get("derivation") or []) if isinstance(st.get("derivation"), list) else \
                    ([st["derivation"]] if st.get("derivation") else []):
                check_derivation(spec, sw, d, computed)


def scenario_steps(spec):
    """Every scenario step as (tag, [vector bytes]): what the implementations' tests must carry."""
    out = []
    for sc in spec.scenarios:
        for st in sc.get("steps", []):
            vectors = st["hex"] if isinstance(st["hex"], list) else [st["hex"]]
            out.append((f"{sc['id']}/{st['step']}", [hexbytes(v) for v in vectors]))
    return out


# --------------------------------------------------------------------------------------------------- markdown


def md_cell(text):
    return " ".join(str(text).split()).replace("|", "\\|")


def fmt_value(spec, value, enum=None):
    if isinstance(value, int):
        return f"`0x{value:02X}`" if value > 9 else f"`{value}`"
    if "." in str(value):
        e, n = str(value).split(".", 1)
        return f"`0x{spec.enum_values[e][n]:02X}` {n}"
    if value in spec.C:
        v = spec.C[value]
        return f"`{value}` (`0x{v:02X}`)" if isinstance(v, int) else f"`{value}`"
    return f"`{value}`"


def enum_inline(spec, enum):
    e = spec.enums[enum]
    if spec.is_bits(enum):
        return "; ".join(f"bit {v['bit']} {v['name']}" for v in e["values"])
    return ", ".join(f"`{v['value']}` {v['name']}" for v in e["values"])


def field_rows(spec, structure, offset, frame_bounds):
    rows = []
    branch = {}  # `x < y` / `x >= y` fields exclude each other: the second branch restarts at the first's offset
    for fld in structure:
        name = f"`{fld['field']}`"
        m = re.fullmatch(r"\s*(\w+)\s*(<|>=)\s*(\w+)\s*", str(fld.get("present_if", "")))
        if m:
            st = branch.setdefault((m[1], m[3]), [offset, m[2]])
            if st[1] != m[2]:
                st[1], offset = m[2], st[0]
        if "switch" in fld:
            target = "the command table (10.2)" if fld["cases"] == "commands" else \
                "by `" + fld["switch"] + "`: " + ", ".join(
                    f"{k} = {('`' + v + '`') if v else 'none'}" for k, v in fld["cases"].items())
            rows.append(("-" if offset is None else offset, "n", name, md_cell(fld["description"]) + " - " + target))
            offset = None
            continue
        if "structure" in fld:
            rows.append(("-" if offset is None else offset, "-", name,
                         md_cell(fld["description"]) + f" (`{fld['structure']}`)"))
            offset = None
            continue
        size = fld.get("size", INT_TYPES.get(fld["type"]))
        if size == "rest":
            lo, hi = frame_bounds
            n = f"{lo - offset}-{hi - offset}" if offset is not None and lo is not None else "rest"
        elif isinstance(size, int):
            n = size
        elif size in spec.C:
            n = spec.C[size]
        else:
            n = f"`{size}`"
        text = md_cell(fld["description"])
        if "value" in fld:
            text = fmt_value(spec, fld["value"]) + " - " + text
        if "enum" in fld:
            text += " " + enum_inline(spec, fld["enum"]) if not spec.is_bits(fld["enum"]) or \
                len(spec.enums[fld["enum"]]["values"]) <= 3 else f" (flags `{fld['enum']}`)"
        if "range" in fld:
            text += f" Range {fld['range'][0]}-{fld['range'][1]}."
        if "present_if" in fld:
            text += f" Present only when `{fld['present_if']}`."
        rows.append(("-" if offset is None else offset, n, name, text))
        offset = offset + n if isinstance(n, int) and offset is not None else None
    return rows


def table(rows, head=("Off", "Size", "Field", "Meaning")):
    out = ["| " + " | ".join(head) + " |", "|" + "---|" * len(head)]
    out += ["| " + " | ".join(str(c) for c in r) + " |" for r in rows]
    return out


def size_text(size):
    if isinstance(size, int):
        return f"{size} B"
    return f"{size[0]}-{size[1]} B"


def render_frames(spec, carrier):
    c = spec.carriers[carrier]
    out = []
    for f in c["frames"]:
        size = f["size"]
        bounds = (size, size) if isinstance(size, int) else tuple(size)
        out += [f"**{f['name']}** ({f['access']}, {f['sender']} -> "
                f"{'watch' if f['sender'] == 'board' else 'board'}, {size_text(size)}): "
                + md_cell(f["description"]), ""]
        out += table(field_rows(spec, f["structure"], 0, bounds)) + [""]
        out += [md_cell(f["usage"]), ""]
    return out[:-1]


def render_structure(spec, name):
    s = spec.structures[name]
    out = [md_cell(s["description"]), ""]
    return out + table(field_rows(spec, s["structure"], s.get("offset", 0), (None, None)))


def render_enum(spec, name):
    e = spec.enums[name]
    if spec.is_bits(name):
        rows = [(v["bit"], f"`{v['name']}`", md_cell(v["description"])) for v in e["values"]]
        return table(rows, ("Bit", "Name", "Meaning"))
    rows = [(f"`0x{v['value']:02X}`", f"`{v['name']}`", md_cell(v["description"])) for v in e["values"]]
    return table(rows, ("Value", "Name", "Meaning"))


def render_constants(spec):
    rows = []
    for c in spec.constants:
        unit = f" {c['unit']}" if c.get("unit") else ""
        v = c["value"]
        shown = f"`0x{v:02X}`" if c.get("type") in INT_TYPES else f"{v}{unit}"
        rows.append((f"`{c['name']}`", shown, md_cell(c["description"])))
    return table(rows, ("Name", "Value", "Meaning"))


def render_uuids(spec):
    return table([(f"`{u['name']}`", f"`{u['value']}`", md_cell(u["description"])) for u in spec.uuids],
                 ("Name", "UUID", "Item"))


def render_gatt(spec):
    rows = []
    for c in spec.carriers.values():
        if c["_kind"] != "characteristic":
            continue
        rows.append((c["name"], ", ".join(c["properties"]), "yes" if c["auth_required"] else "no",
                     md_cell(c["description"])))
    return table(rows, ("Characteristic", "Properties", "Auth required", "Purpose"))


def render_commands(spec):
    rows = []
    for cmd in spec.commands:
        args = ", ".join(f"`{a['field']}` {a.get('type', '')}" for a in cmd.get("args", [])) or "-"
        size = 3 + sum(INT_TYPES.get(a.get("type"), 0) for a in cmd.get("args", []))
        rows.append((f"`0x{cmd['id']:02X}`", cmd["name"], args, "-" if cmd.get("unused") else size,
                     ", ".join(cmd.get("valid_on", [])) or "-", md_cell(cmd["description"])))
    return table(rows, ("Id", "Name", "Args", "Size", "Valid on", "Board effect"))


def render_scenarios(spec):
    out, last_section = [], None
    intro = spec.fixtures.get("description")
    if intro:
        out += [" ".join(str(intro).split()), ""]
    for sc in spec.scenarios:
        if sc["section"] != last_section:
            out += [f"### {sc['section']} {sc['title']}", ""]
            last_section = sc["section"]
        else:
            out += [f"**{sc['title']}**", ""]
        out += [" ".join(str(sc["goal"]).split()), ""]
        for st in sc["steps"]:
            vectors = st["hex"] if isinstance(st["hex"], list) else [st["hex"]]
            alt = f" (instead of `{st['instead_of']}`)" if "instead_of" in st else ""
            label = st.get("label") or f"{st['characteristic']} {st['access']}"
            out += [f"`{sc['id']}/{st['step']}`{alt} - {label}:", "```"]
            for v in vectors:
                b = hexbytes(v)
                out.append(f"{hexstr(b)}    ({len(b)} B)")
            derivs = st.get("derivation") or []
            for d in derivs if isinstance(derivs, list) else [derivs]:
                if "hmac_sha256" in d:
                    out += [f"  HMAC-SHA256(K, {d['hmac_sha256']['message']})", f"    = {d['digest']}"]
                elif "crc32" in d:
                    out += [f"  CRC-32({d['crc32']})", f"    = 0x{d['value']:08x}"]
            out += ["```", " ".join(str(st["why"]).split()), ""]
        if sc.get("notes"):
            out += [str(sc["notes"]).strip(), ""]
    return out[:-1]


def regions(spec):
    """Region id -> rendered lines. Every carrier and the scenarios must appear in the markdown."""
    r = {"constants": render_constants(spec), "uuids": render_uuids(spec), "gatt": render_gatt(spec),
         "scenarios": render_scenarios(spec)}
    if spec.commands:
        r["commands"] = render_commands(spec)
    for name in spec.carriers:
        r[f"frames:{name}"] = render_frames(spec, name)
    for name in spec.structures:
        r[f"structure:{name}"] = render_structure(spec, name)
    for name in spec.enums:
        r[f"enum:{name}"] = render_enum(spec, name)
    return r


REQUIRED_REGIONS = re.compile(r"^(frames:.*|scenarios|gatt|constants|uuids|commands)$")
REGION_RE = re.compile(r"(<!-- generated: (\S+) -->\n)(.*?)(<!-- /generated: \2 -->)", re.S)


def sync_markdown(spec, md_path, write):
    text = md_path.read_text(encoding="utf-8")
    rendered = regions(spec)
    found = set()
    banner = "<!-- Do not edit: generated from docs/protocol/ by `python helpers/check_protocol.py build`. -->\n"

    def repl(m):
        rid = m.group(2)
        found.add(rid)
        if rid not in rendered:
            spec.err(md_path.name, f"region `{rid}` has no source in docs/protocol/")
            return m.group(0)
        body = banner + "\n".join(rendered[rid]) + "\n"
        if m.group(3) != body and not write:
            spec.err(md_path.name, f"region `{rid}` is out of date (run `check_protocol.py build`)")
        return m.group(1) + body + m.group(4)

    new = REGION_RE.sub(repl, text)
    for rid in sorted(rendered):
        if REQUIRED_REGIONS.match(rid) and rid not in found:
            spec.err(md_path.name, f"region `{rid}` is missing (add `<!-- generated: {rid} -->` "
                                   f"... `<!-- /generated: {rid} -->`)")
    if write and new != text:
        md_path.write_text(new, encoding="utf-8", newline="\n")
        return True
    return False


# --------------------------------------------------------------------------------------------------- entry points


def run_checks(spec_dir, md_path, repos=()):
    spec = Spec(spec_dir)
    validate(spec)
    sync_markdown(spec, md_path, write=False)
    return spec.errors


def selftest():
    """Each mutation of a copy of the real spec must turn the check red; the unmutated copy must stay green."""

    def mutate_yaml(rel, fn):
        def apply(d):
            p = d / "protocol" / rel
            doc = yaml.safe_load(p.read_text(encoding="utf-8"))
            fn(doc)
            p.write_text(yaml.safe_dump(doc, sort_keys=False), encoding="utf-8")
        return apply

    def first_step(doc):
        return doc["scenarios"][0]["steps"][0]

    def flip_vector(doc):
        b = bytearray(hexbytes(first_step(doc)["hex"] if not isinstance(first_step(doc)["hex"], list)
                               else first_step(doc)["hex"][0]))
        b[-1] ^= 0x01
        first_step(doc)["hex"] = hexstr(b)

    def edit_md(d):
        p = d / "garmin-protocol.md"
        p.write_text(p.read_text(encoding="utf-8").replace("<!-- generated: scenarios -->\n",
                                                           "<!-- generated: scenarios -->\nhand edit\n", 1),
                     encoding="utf-8")

    cases = [
        ("unmutated copy", None, True),
        ("wrong constant (PROTO 0x02)", mutate_yaml("constants.yaml", lambda d: next(
            c for c in d["constants"] if c["name"] == "PROTO").update(value=0x02)), False),
        ("wrong vector byte", mutate_yaml("scenarios.yaml", flip_vector), False),
        ("missing usage", mutate_yaml("constants.yaml", lambda d: d["constants"][0].pop("usage")), False),
        ("unknown key", mutate_yaml("constants.yaml", lambda d: d["constants"][0].update(useage="x")), False),
        ("hand-edited generated region", edit_md, False),
    ]
    failed = 0
    for title, mutation, want_green in cases:
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            shutil.copytree(SPEC_DIR, d / "protocol")
            shutil.copy(SPEC_MD, d / "garmin-protocol.md")
            if mutation:
                mutation(d)
            errors = run_checks(d / "protocol", d / "garmin-protocol.md")
        green = not errors
        ok = green == want_green
        failed += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} {title}: {'green' if green else 'red'}"
              + ("" if ok else f" (expected {'green' if want_green else 'red'})"
                 + "".join(f"\n         {e}" for e in errors[:5])))

    # The repo scan must turn red too: a missing tag and a wrong literal both fail.
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        spec = Spec(SPEC_DIR)
        missing = d / "missing"
        missing.mkdir()
        (missing / "a.cpp").write_text('const auto v = hex("01 00 aa aa aa aa aa aa aa aa");\n',
                                       encoding="utf-8")
        errs = scan_vectors(spec, "firmware", {"auth/challenge"}, missing, "*.cpp")
        ok = any("no `spec: auth/challenge`" in e for e in errs)
        failed += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} scan missing tag: {'red' if errs else 'green'}"
              + ("" if ok else "".join(f"\n         {e}" for e in errs[:5])))
        wrong = d / "wrong"
        wrong.mkdir()
        (wrong / "b.cpp").write_text('// spec: auth/challenge\n'
                                     'const auto v = hex("01 00 aa aa aa aa aa aa aa aa");\n',
                                     encoding="utf-8")
        errs = scan_vectors(spec, "firmware", {"auth/challenge"}, wrong, "*.cpp")
        ok = any("bytes differ" in e for e in errs)
        failed += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} scan wrong bytes: {'red' if errs else 'green'}"
              + ("" if ok else "".join(f"\n         {e}" for e in errs[:5])))

    return failed == 0


# --------------------------------------------------------------------------------------------------- repo scan

REPO_DIRS = {
    "firmware": REPO,
    "client": REPO,
    "watch": REPO.parent / "squash-scoreboard-display-garmin-app",
    "android": REPO.parent / "squash-scoreboard-display-android-app",
}

# YAML enum name -> the C++ `enum class` that carries it in src/Garmin/GarminProtocol.h. Enums
# with no firmware counterpart (e.g. `side`, which is a left/right bool there) are simply not
# listed, so they are not required.
FIRMWARE_ENUMS = {
    "ack_status": "AckStatus",
    "screen": "ScreenId",
    "sport": "SportId",
    "menu_entry": "MenuEntry",
    "auth_type": "AuthType",
    "auth_status": "AuthStatus",
    "pairing_type": "PairingType",
    "pairing_status": "PairingStatus",
    "tournament_write": "TournamentWrite",
    "tournament_read": "TournamentRead",
    "game_flags": "GameFlags",
}

# The scenario steps the client (helpers/garmin_client.py) must carry, tagged in
# helpers/test_garmin_client.py. The client encodes commands and decodes state/heart/results,
# plus the auth/pairing MACs, so it carries the steps those functions touch; the firmware (the
# reference) carries every step. Advertising and roster are firmware-only: parse_adv is
# BLE-stack-dependent and read_roster is GATT-async, so neither has a pure client function.
CLIENT_STEPS = frozenset({
    "auth/challenge", "auth/proof", "auth/result_ok", "auth/result_bad_proof",
    "pairing/request", "pairing/result_ok", "pairing/result_bad_code",
    "menu_state/menu", "score_confirm/score", "score_confirm/state",
    "padel_playing/tiebreak", "padel_playing/ladder",
    "focus/focus_squash", "focus/menu_state", "focus/focus_krystian", "focus/choose_players_state",
    "focus/focus_start", "focus/focus_unknown_uid",
    "tablet_mode_entry/menu", "tablet_mode_entry/focus_tablet", "tablet_mode_entry/focus_tablet_ack",
    "tablet_mode_entry/select_tablet", "tablet_mode_entry/select_tablet_ack",
    "tablet_mode_entry/back_on_wait", "tablet_mode_entry/back_on_wait_ack",
    "heart/write_a", "heart/write_c", "heart/write_c_clear", "heart/notify_a", "heart/notify_b",
    "heart/notify_c", "heart/notify_a_empty", "heart/discard_v0", "heart/discard_short",
    "sides_swapped/playing", "last_known/score_right", "last_known/state",
    "results/write", "results/read_1", "results/read_2", "results/read_3", "results/read_past_end",
    "results/read_no_tournament", "results/unauth_read", "results/discard_short", "results/discard_v0",
    "offline_games/add", "offline_games/add_illegal", "offline_games/remove", "offline_games/read_offline",
    "offline_games/read_end_rev",
    # 16.15 tablet tournament: the client builds/decodes every frame of it.
    "tablet_begin/begin", "tablet_begin/begin_ack", "tablet_begin/roster_anna",
    "tablet_begin/roster_krystian", "tablet_begin/status_begun", "tablet_begin/status_none",
    "tablet_begin/wait_state", "tablet_mirror/commit", "tablet_mirror/commit_ack",
    "tablet_mirror/mirror_0", "tablet_mirror/mirror_1", "tablet_mirror/mirror_2",
    "tablet_mirror/results_2", "tablet_mirror/results_end", "tablet_mirror/status_mirrored",
    "tablet_outbox/ack", "tablet_outbox/ack_ack", "tablet_outbox/cursor_outbox",
    "tablet_outbox/cursor_status", "tablet_outbox/read_1", "tablet_outbox/read_2",
    "tablet_outbox/read_end", "tablet_outbox/status", "tablet_outbox/status_acked",
    "tablet_refusals/back", "tablet_refusals/back_ack", "tablet_refusals/begin_on_menu",
    "tablet_refusals/begin_on_menu_ack", "tablet_refusals/begin_other_tid",
    "tablet_refusals/begin_other_tid_ack", "tablet_refusals/commit_bad_crc",
    "tablet_refusals/commit_bad_crc_ack", "tablet_refusals/remove", "tablet_refusals/remove_ack",
    "tablet_repush/repush", "tablet_repush/repush_ack", "tablet_repush/roster_anna",
    "tablet_repush/roster_krystian", "tablet_repush/roster_ola", "tablet_set_side/set_side",
    "tablet_set_side/set_side_ack",
})


# Steps the board never produces as its own literal: too_old/too_new are a foreign PROTO in the
# advertisement, refused by the watch (the board always advertises PROTO 0x01), so those two belong
# to the watch app (#567). The firmware is otherwise the reference and carries every board-side step.
FIRMWARE_EXCLUDE = frozenset({
    "board_version/too_old", "board_version/too_new",
})


def pascal(name):
    return "".join(w.capitalize() for w in str(name).lower().split("_"))


def repo_errors(repo, spec, checkout):
    """The constant/vector scan for one repo checkout; a list of findings ([] = green)."""
    if repo == "firmware":
        return scan_firmware(spec, checkout)
    if repo == "client":
        return scan_client(spec, checkout)
    return []


# --- constants by naming convention ---

CONST_RE = re.compile(r"constexpr\s+\w+\s+(\w+)\s*=\s*([^;]+);")
UUID_RE = re.compile(r"constexpr\s+const\s+char\s*\*\s*(\w+)\s*=\s*\"([0-9a-f-]+)\";")
ENUM_RE = re.compile(r"enum\s+class\s+(\w+)\s*:\s*uint8_t\s*\{(.*?)\};", re.S)
MEMBER_RE = re.compile(r"(\w+)\s*=\s*([^,}]+)")


def parse_int(text):
    return int(text.strip(), 0)


def scan_firmware(spec, root):
    errors = []
    hdr = root / "src" / "Garmin" / "GarminProtocol.h"
    text = hdr.read_text(encoding="utf-8")

    consts = {}
    for m in CONST_RE.finditer(text):
        try:
            consts[m.group(1)] = parse_int(m.group(2))
        except ValueError:
            pass
    for c in spec.constants:
        if c["name"] not in consts:
            errors.append(f"firmware: constant `{c['name']}` not found in {hdr.name}")
        elif consts[c["name"]] != c["value"]:
            errors.append(f"firmware: `{c['name']}` = {consts[c['name']]} but the YAML says {c['value']}")

    uuids = dict(UUID_RE.findall(text))
    for u in spec.uuids:
        if u["name"] not in uuids:
            errors.append(f"firmware: uuid `{u['name']}` not found in {hdr.name}")
        elif uuids[u["name"]] != u["value"]:
            errors.append(f"firmware: `{u['name']}` = {uuids[u['name']]} but the YAML says {u['value']}")

    enums = {}
    for m in ENUM_RE.finditer(text):
        members = {}
        for k, v in MEMBER_RE.findall(m.group(2)):
            try:
                members[k] = parse_int(v)
            except ValueError:
                pass
        enums[m.group(1)] = members
    for ename, ctype in FIRMWARE_ENUMS.items():
        if ctype not in enums:
            errors.append(f"firmware: enum `{ename}` (C++ `{ctype}`) not found in {hdr.name}")
            continue
        for v in spec.enums[ename]["values"]:
            member = pascal(v["name"])
            want = (1 << v["bit"]) if "bit" in v else v.get("value")
            if member not in enums[ctype]:
                errors.append(f"firmware: `{ctype}` has no member `{member}` (`{v['name']}`)")
            elif enums[ctype][member] != want:
                errors.append(f"firmware: `{ctype}.{member}` = {enums[ctype][member]} but the YAML says {want}")

    # CommandId is derived from commands.yaml, not constants.yaml, so check it separately.
    cmd_ids = enums.get("CommandId", {})
    if not cmd_ids:
        errors.append(f"firmware: enum `CommandId` not found in {hdr.name}")
    else:
        for cmd in spec.commands:
            if cmd.get("unused"):
                continue
            member = pascal(cmd["name"])
            if member not in cmd_ids:
                errors.append(f"firmware: `CommandId` has no member `{member}` (`{cmd['name']}`)")
            elif cmd_ids[member] != cmd["id"]:
                errors.append(f"firmware: `CommandId.{member}` = {cmd_ids[member]} but the YAML says {cmd['id']}")

    errors += scan_vectors(spec, "firmware", spec_scenario_tags(spec) - FIRMWARE_EXCLUDE, root / "test", "*.cpp")
    return errors


PY_INT = re.compile(r"^([A-Z_]+)\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*$", re.M)


def scan_client(spec, root):
    errors = []
    src = root / "helpers" / "garmin_client.py"
    text = src.read_text(encoding="utf-8")

    ints = {m.group(1): parse_int(m.group(2)) for m in PY_INT.finditer(text)}
    # PROTO maps 1:1; COMPANY is the client's name for the YAML's COMPANY_ID.
    for yaml_name, client_name in (("PROTO", "PROTO"), ("COMPANY_ID", "COMPANY")):
        c = next(c for c in spec.constants if c["name"] == yaml_name)
        if client_name not in ints:
            errors.append(f"client: `{client_name}` not found in {src.name}")
        elif ints[client_name] != c["value"]:
            errors.append(f"client: `{client_name}` = {ints[client_name]} but the YAML says {c['value']}")

    # The one UUID template derives the nine 128-bit UUIDs (SERVICE, then VERSION..RESULTS).
    tmpl = re.search(r'UUID\s*=\s*"([^"]+)"', text)
    if not tmpl:
        errors.append(f"client: `UUID` template not found in {src.name}")
    else:
        for i, u in enumerate(spec.uuids, 1):
            want = u["value"]
            got = tmpl.group(1).format(i)
            if got != want:
                errors.append(f"client: UUID {i} = {got} but the YAML says {want}")

    # Value -> name dicts; the sport names need a space folded back to an underscore.
    for dict_name, enum_name in (("SCREENS", "screen"), ("SPORTS", "sport"), ("ACKS", "ack_status")):
        d = py_dict(text, dict_name)
        if d is None:
            errors.append(f"client: `{dict_name}` not found in {src.name}")
            continue
        rev = {}
        for val, name in d.items():
            rev[str(name).replace(" ", "_")] = val
        for v in spec.enums[enum_name]["values"]:
            want = (1 << v["bit"]) if "bit" in v else v.get("value")
            if v["name"] not in rev:
                errors.append(f"client: `{dict_name}` has no `{v['name']}`")
            elif rev[v["name"]] != want:
                errors.append(f"client: `{dict_name}` `{v['name']}` = {rev[v['name']]} but the YAML says {want}")

    # COMMANDS: name is the YAML's SCREAMING_SNAKE lowercased; the tuple's first element is the id.
    cmds = py_dict(text, "COMMANDS")
    if cmds is None:
        errors.append(f"client: `COMMANDS` not found in {src.name}")
    else:
        for cmd in spec.commands:
            if cmd.get("unused"):
                continue
            key = cmd["name"].lower()
            if key not in cmds:
                errors.append(f"client: `COMMANDS` has no `{key}` (`{cmd['name']}`)")
            elif cmds[key][0] != cmd["id"]:
                errors.append(f"client: `COMMANDS` `{key}` = 0x{cmds[key][0]:02x} but the YAML says 0x{cmd['id']:02x}")

    errors += scan_vectors(spec, "client", CLIENT_STEPS, root / "helpers", "test_garmin_client.py")
    return errors


def py_dict(text, name):
    i = text.find(name + " = {")
    if i < 0:
        return None
    j = text.index("{", i)
    depth = 0
    k = j
    for k in range(j, len(text)):
        if text[k] == "{":
            depth += 1
        elif text[k] == "}":
            depth -= 1
            if depth == 0:
                break
    try:
        return ast.literal_eval(text[j:k + 1])
    except (ValueError, SyntaxError):
        return None


# --- tagged vectors ---

TAG_RE = re.compile(r"spec:\s*([A-Za-z0-9_]+)/([A-Za-z0-9_]+)")
HEX_LIT_RE = re.compile(r"\"((?:[0-9a-fA-F]{2}\s+)*[0-9a-fA-F]{2})\"")


def hex_literals(line):
    """All whole-string hex literals on one line, in order (labels like "16.6 chunk 0" are skipped)."""
    out = []
    for m in HEX_LIT_RE.finditer(line):
        out.append(bytes.fromhex(m.group(1)))
    return out


def scan_vectors(spec, repo, wanted_tags, base, pattern):
    errors = []
    want = {tag: vecs for tag, vecs in scenario_steps(spec) if tag in wanted_tags}
    for tag in sorted(wanted_tags - set(want)):
        errors.append(f"{repo}: `spec: {tag}` is wanted but names no scenario step (stale tag list)")
    found = {}
    for path in base.rglob(pattern):
        lines = path.read_text(encoding="utf-8").splitlines()
        for i, line in enumerate(lines):
            m = TAG_RE.search(line)
            if not m:
                continue
            tag = f"{m.group(1)}/{m.group(2)}"
            want_n = len(want.get(tag, ()))
            frames = hex_literals(line)  # a literal on the tag's own line, if any
            for nxt in lines[i + 1:]:
                if len(frames) >= want_n:
                    break
                frames += hex_literals(nxt)
            if tag in found and found[tag] != frames:
                errors.append(f"{repo}: `spec: {tag}` is tagged twice with different bytes")
            found[tag] = frames
    for tag, vecs in sorted(want.items()):
        if tag not in found:
            errors.append(f"{repo}: no `spec: {tag}` tag in its tests")
        elif found[tag] != vecs:
            errors.append(f"{repo}: `spec: {tag}` bytes differ from the YAML")
    for tag in found:
        if tag not in want:
            errors.append(f"{repo}: `spec: {tag}` does not name a scenario step")
    return errors


def spec_scenario_tags(spec):
    return {f"{sc['id']}/{st['step']}" for sc in spec.scenarios for st in sc.get("steps", [])}


def scan_repos(spec, names):
    errors = []
    for n in names:
        for name in (["firmware", "client", "watch", "android"] if n == "all" else [n]):
            checkout = REPO_DIRS[name]
            if not checkout.exists():
                errors.append(f"--repo {name}: sibling checkout not found at {checkout}")
                continue
            if name in ("watch", "android"):
                errors.append(f"--repo {name}: scan not implemented yet (#567/#568), so it is not checked")
                continue  # #567 / #568 add these scans
            errors += repo_errors(name, spec, checkout)
    return errors


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", nargs="?", default="check", choices=["check", "build", "selftest"])
    ap.add_argument("--repo", action="append", default=[],
                    choices=["firmware", "client", "watch", "android", "all"])
    args = ap.parse_args(argv)

    if args.command == "selftest":
        print("check_protocol selftest:")
        return 0 if selftest() else 1

    spec = Spec(SPEC_DIR)
    validate(spec)
    repo_errors = scan_repos(spec, args.repo) if args.repo else []
    if args.command == "build":
        if spec.errors:
            print("\n".join(spec.errors), file=sys.stderr)
            print(f"check_protocol: {len(spec.errors)} error(s); markdown not rebuilt", file=sys.stderr)
            return 1
        changed = sync_markdown(spec, SPEC_MD, write=True)
        print(f"check_protocol: {SPEC_MD.name} {'rebuilt' if changed else 'already up to date'}")
        return 1 if spec.errors else 0
    sync_markdown(spec, SPEC_MD, write=False)
    if spec.errors or repo_errors:
        for e in spec.errors + repo_errors:
            print(e, file=sys.stderr)
        print(f"check_protocol: RED, {len(spec.errors)} spec error(s), {len(repo_errors)} repo error(s)", file=sys.stderr)
        return 1
    print(f"check_protocol: green ({len(scenario_steps(spec))} scenario steps)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
