#!/usr/bin/env python3
import argparse
import json
import os
import re
import sys

PRIMITIVES = {
    "bool": ("bool", "boolean"),
    "u8": ("uint8_t", "u8"),
    "i8": ("int8_t", "i8"),
    "byterot": ("uint8_t", "u8"),
    "u16": ("uint16_t", "be"),
    "i16": ("int16_t", "be"),
    "u32": ("uint32_t", "be"),
    "i32": ("int32_t", "be"),
    "u64": ("uint64_t", "be"),
    "i64": ("int64_t", "be"),
    "f32": ("float", "be"),
    "f64": ("double", "be"),
    "lu16": ("uint16_t", "le"),
    "li16": ("int16_t", "le"),
    "lu32": ("uint32_t", "le"),
    "li32": ("int32_t", "le"),
    "lu64": ("uint64_t", "le"),
    "li64": ("int64_t", "le"),
    "lf32": ("float", "le"),
    "lf64": ("double", "le"),
    "varint": ("uint32_t", "varint"),
    "varint64": ("uint64_t", "varint"),
    "zigzag32": ("int32_t", "zigzag"),
    "zigzag64": ("int64_t", "zigzag"),
    "uuid": ("Uuid", "uuid"),
    "restBuffer": ("std::vector<uint8_t>", "rest"),
    "varint128": ("std::array<uint64_t, 2>", "varint128"),
}

INTEGRAL = {"u8", "i8", "byterot", "u16", "i16", "u32", "i32", "u64", "i64", "lu16", "li16", "lu32", "li32",
            "lu64", "li64", "varint", "varint64", "zigzag32", "zigzag64"}

KEYWORDS = set("""alignas alignof and and_eq asm auto bitand bitor bool break case catch char class compl concept
const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype default delete do
double dynamic_cast else enum explicit export extern false final float for friend goto if import inline int long
module mutable namespace new noexcept not not_eq nullptr operator or or_eq override private protected public
register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch
template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile
wchar_t while xor xor_eq""".split())

class GenError(Exception):
    pass

def words(name):
    parts = re.split(r"[^0-9A-Za-z]+", name)
    out = []
    for part in parts:
        if not part:
            continue
        out.extend(re.findall(r"[A-Z]+(?![a-z])|[A-Z]?[a-z]+|[0-9]+", part) or [part])
    return out

def pascal(name):
    text = "".join(w[:1].upper() + w[1:] for w in words(name))
    if not text or text[0].isdigit():
        text = "V" + text
    return text

def camel(name):
    text = pascal(name)
    text = text[:1].lower() + text[1:]
    if text in KEYWORDS:
        text += "_"
    return text

class Decl:
    def __init__(self, kind, name):
        self.kind = kind
        self.name = name
        self.lines = []
        self.members = []
        self.codec = []
        self.packet = None
        self.extra = []

class Generator:
    def __init__(self, spec, overrides):
        self.types = dict(spec["types"])
        for name, expr in overrides.get("types", {}).items():
            self.types[name] = expr
        for path, expr in overrides.get("retype", {}).items():
            owner, field = path.split(".")
            body = self.types[owner]
            for entry in body[1]:
                if entry.get("name") == field:
                    entry["type"] = expr
                    break
            else:
                raise GenError(f"retype target {path} not found")
        self.custom = overrides.get("custom", {})
        self.extra_packets = overrides.get("packets", {})
        self.skip_types = set(overrides.get("skip", []))
        self.decls = []
        self.names = set()
        self.memo = {}
        self.named = {}
        self.building = set()
        self.reserved = {}
        self.recursive = set()
        self.warnings = []
        self.opaque = []

    def unique(self, base):
        name = base
        n = 2
        while name in self.names or name in KEYWORDS:
            name = f"{base}{n}"
            n += 1
        self.names.add(name)
        return name

    def native(self, name):
        return self.types.get(name) == "native"

    def resolve(self, expr):
        seen = 0
        while isinstance(expr, str) and expr in self.types and self.types[expr] != "native":
            body = self.types[expr]
            if isinstance(body, list) and body[0] in ("container", "mapper", "bitfield"):
                return expr
            expr = body
            seen += 1
            if seen > 16:
                raise GenError(f"alias loop at {expr}")
        return expr

    def definition(self, expr):
        if isinstance(expr, str) and expr in self.types and self.types[expr] != "native":
            return self.types[expr]
        return expr

    def kind_of(self, expr):
        if isinstance(expr, str):
            if expr == "void":
                return "void"
            if expr in PRIMITIVES:
                return "prim"
            if expr in ("nbt", "lnbt", "nbtLoop"):
                return "nbt"
            if expr in self.types and self.types[expr] != "native":
                return "named"
            raise GenError(f"unsupported type '{expr}'")
        return expr[0]

    def prim_cpp(self, name):
        return PRIMITIVES[name][0]

    def cpp_type(self, expr, hint):
        key = id(expr)
        if isinstance(expr, str):
            kind = self.kind_of(expr)
            if kind == "void":
                return "void"
            if kind == "prim":
                return self.prim_cpp(expr)
            if kind == "nbt":
                return "Nbt" if expr != "nbtLoop" else "std::vector<Nbt>"
            return self.named_type(expr)
        if key in self.memo:
            return self.memo[key]
        kind = expr[0]
        args = expr[1] if len(expr) > 1 else None
        if kind == "container":
            result = self.make_struct(self.unique(pascal(hint)), args).name
        elif kind == "mapper":
            result = self.make_enum(self.unique(pascal(hint)), args)
        elif kind == "bitfield":
            result = self.make_bitfield(self.unique(pascal(hint)), expr[1])
        elif kind == "array":
            inner = self.cpp_type(args["type"], hint + "_entry")
            if "countType" in args:
                result = f"std::vector<{inner}>"
            elif "count" in args and isinstance(args["count"], int):
                result = f"std::array<{inner}, {args['count']}>"
            else:
                result = f"std::vector<{inner}>"
        elif kind == "maybeIncompleteArray":
            result = f"std::vector<{self.cpp_type(args['type'], hint + '_entry')}>"
        elif kind == "option" or kind == "optionalOnRemaining":
            inner = args if kind == "option" else args["type"]
            result = f"std::optional<{self.cpp_type(inner, hint)}>"
        elif kind == "bitflags":
            result = self.prim_cpp(args["type"])
            self.record_flags(hint, args)
        elif kind == "pstring":
            result = "std::string"
        elif kind == "buffer":
            result = "std::vector<uint8_t>"
        elif kind == "encapsulated":
            result = self.cpp_type(args["type"], hint)
        elif kind == "switch":
            result = self.switch_type(args, hint)
        else:
            raise GenError(f"unsupported construct '{kind}'")
        self.memo[key] = result
        return result

    def named_type(self, name):
        if name in self.named:
            return self.named[name]
        body = self.types[name]
        if name in self.building:
            if name in self.reserved:
                self.recursive.add(self.reserved[name])
                return self.reserved[name]
            raise GenError(f"recursive type '{name}'")
        self.building.add(name)
        try:
            if isinstance(body, list) and body[0] == "container":
                reserved = self.unique(pascal(name))
                self.reserved[name] = reserved
                self.names.discard(reserved)
                cpp = self.make_struct(reserved, body[1]).name
            elif isinstance(body, list) and body[0] == "mapper":
                cpp = self.make_enum(self.unique(pascal(name)), body[1])
            elif isinstance(body, list) and body[0] == "bitfield":
                cpp = self.make_bitfield(self.unique(pascal(name)), body[1])
            else:
                if isinstance(body, list) and body[0] == "bitflags":
                    self.record_flags(name, body[1])
                cpp = self.cpp_type(body, name)
        finally:
            self.building.discard(name)
        self.named[name] = cpp
        return cpp

    def record_flags(self, hint, args):
        flags = args.get("flags")
        if isinstance(flags, list):
            items = [(n, 1 << i) for i, n in enumerate(flags)]
        else:
            items = [(n, int(v)) for n, v in flags.items()]
        name = self.unique("Flags" + pascal(hint)) if hint else None
        if not name:
            return
        decl = Decl("flags", name)
        ctype = self.prim_cpp(args["type"])
        decl.lines.append(f"namespace {name} {{")
        used = set()
        bits = 64 if "64" in ctype else 32 if "32" in ctype else 16 if "16" in ctype else 8
        unsigned = "uint" + str(bits) + "_t"
        for n, v in items:
            if v >= (1 << bits) or v < 0:
                continue
            ident = pascal(n)
            while ident in used:
                ident += "_"
            used.add(ident)
            decl.lines.append(f"    constexpr {unsigned} {ident} = {v}ull;")
        decl.lines.append("}")
        self.decls.append(decl)

    def make_enum(self, name, args):
        under = self.prim_cpp(args["type"])
        decl = Decl("enum", name)
        used = set()
        values = []
        for key, label in args["mappings"].items():
            number = int(key, 0)
            ident = pascal(str(label))
            while ident in used:
                ident += "_"
            used.add(ident)
            values.append((ident, number, str(label)))
        decl.lines.append(f"enum class {name} : {under} {{")
        bits = int(re.search(r"(\d+)", under).group(1))
        for ident, number, _ in values:
            if under.startswith("uint") and number < 0:
                number += 1 << bits
            decl.lines.append(f"    {ident} = {number}{'ull' if bits == 64 else ''},")
        decl.lines.append("};")
        decl.extra = values
        decl.under = args["type"]
        self.decls.append(decl)
        self.enums = getattr(self, "enums", {})
        self.enums[name] = decl
        return name

    def make_bitfield(self, name, fields):
        decl = Decl("struct", name)
        total = sum(f["size"] for f in fields)
        if total % 8:
            raise GenError(f"bitfield {name} is not byte aligned")
        decl.lines.append(f"struct {name} {{")
        used = set()
        body = []
        shift = total
        nbytes = total // 8
        for f in fields:
            ident = camel(f["name"])
            while ident in used:
                ident += "_"
            used.add(ident)
            ctype = "int32_t" if f.get("signed") else "uint32_t"
            if f["size"] > 32:
                ctype = "int64_t" if f.get("signed") else "uint64_t"
            decl.lines.append(f"    {ctype} {ident}{{}};")
            shift -= f["size"]
            mask = (1 << f["size"]) - 1
            body.append((ident, ctype, shift, mask, f["size"], bool(f.get("signed"))))
        decl.lines.append("};")
        codec = ["uint64_t bits = 0;", "if constexpr (S::kReading) {", f"    std::vector<uint8_t> raw;", f"    s.bytes(raw, {nbytes});",
                 "    if (!s.ok) return;", "    for (uint8_t b : raw) bits = (bits << 8) | b;"]
        for ident, ctype, sh, mask, size, signed in body:
            if signed:
                codec.append(f"    {{ uint64_t part = (bits >> {sh}) & {mask}ull; v.{ident} = (part & {1 << (size - 1)}ull) ? static_cast<{ctype}>(part) - {1 << size} : static_cast<{ctype}>(part); }}")
            else:
                codec.append(f"    v.{ident} = static_cast<{ctype}>((bits >> {sh}) & {mask}ull);")
        codec.append("} else {")
        for ident, ctype, sh, mask, size, signed in body:
            codec.append(f"    bits |= (static_cast<uint64_t>(v.{ident}) & {mask}ull) << {sh};")
        codec.append(f"    std::vector<uint8_t> raw({nbytes});")
        codec.append(f"    for (int i = 0; i < {nbytes}; i++) raw[i] = static_cast<uint8_t>(bits >> (8 * ({nbytes - 1} - i)));")
        codec.append(f"    s.bytes(raw, {nbytes});")
        codec.append("}")
        decl.codec = codec
        self.decls.append(decl)
        return name

    def switch_cases(self, args):
        cases = []
        for key, body in args["fields"].items():
            cases.append((key, body))
        return cases

    def switch_type(self, args, hint):
        distinct = []
        for key, body in args["fields"].items():
            if self.is_void(body):
                continue
            if body not in distinct:
                distinct.append(body)
        default = args.get("default")
        if default is not None and not self.is_void(default) and default not in distinct:
            distinct.append(default)
        if not distinct:
            return None
        types = []
        for i, body in enumerate(distinct):
            suffix = "" if len(distinct) == 1 else f"_case{i}"
            types.append(self.cpp_type(body, hint + suffix))
        if len(types) == 1:
            return f"std::optional<{types[0]}>"
        return "std::variant<std::monostate, " + ", ".join(types) + ">"

    def is_void(self, body):
        return body is None or body == "void"

    def condition(self, scope, discriminator, key):
        if key.startswith("/"):
            if discriminator not in scope:
                raise GenError(f"switch discriminator '{discriminator}' not found")
            return f"{scope[discriminator][0]} == net::context::{key[1:]}()"
        if discriminator not in scope:
            raise GenError(f"switch discriminator '{discriminator}' not found")
        member, texpr = scope[discriminator]
        resolved = self.resolve(texpr)
        if isinstance(resolved, list) and resolved[0] == "option":
            inner = self.condition({discriminator: (f"(*{member})", resolved[1])}, discriminator, key)
            return f"{member}.has_value() && {inner}"
        target = self.definition(resolved)
        if isinstance(target, list) and target[0] == "mapper":
            enum = self.cpp_type(resolved if isinstance(resolved, str) else texpr, discriminator)
            for ident, number, label in self.enums[enum].extra:
                if label == key:
                    return f"{member} == {enum}::{ident}"
            try:
                return f"static_cast<int64_t>({member}) == {int(key, 0)}"
            except ValueError:
                raise GenError(f"case '{key}' not in mapper for {discriminator}")
        if resolved == "bool":
            return f"{member} == {'true' if key == 'true' else 'false'}"
        if isinstance(resolved, str) and resolved in INTEGRAL:
            try:
                return f"{member} == {int(key, 0)}"
            except ValueError:
                raise GenError(f"unsupported switch key '{key}'")
        if isinstance(resolved, str) and resolved == "string" or (isinstance(target, list) and target[0] == "pstring"):
            return f"{member} == \"{key}\""
        raise GenError(f"cannot compare switch on {discriminator}: {resolved}")

    def plan_fields(self, fields, ctx):
        steps = []
        for f in fields:
            ftype = f["type"]
            if f.get("anon"):
                if isinstance(ftype, list) and ftype[0] == "switch":
                    args = ftype[1]
                    branches = []
                    cases = list(args["fields"].items())
                    if args.get("default") is not None:
                        cases.append(("__default", args["default"]))
                    for key, body in cases:
                        if self.is_void(body):
                            branches.append((key, []))
                            continue
                        body = self.definition(body)
                        if not (isinstance(body, list) and body[0] == "container"):
                            raise GenError("anonymous switch branch is not a container")
                        branches.append((key, self.plan_fields(body[1], ctx)))
                    steps.append(("anon_switch", args, branches))
                elif isinstance(ftype, list) and ftype[0] == "container":
                    steps.extend(self.plan_fields(ftype[1], ctx))
                else:
                    raise GenError("unsupported anonymous field")
                continue
            spec = f["name"]
            key = (spec, json.dumps(ftype, sort_keys=True))
            if key in ctx["keys"]:
                mname = ctx["keys"][key]
            else:
                ident = camel(spec)
                n = 2
                base = ident
                while ident in ctx["names"]:
                    ident = f"{base}{n}"
                    n += 1
                ctx["names"].add(ident)
                ctx["keys"][key] = ident
                ctx["members"].append((ident, ftype, spec))
                mname = ident
            ctx["scope"][spec] = (f"v.{mname}", ftype)
            steps.append(("field", mname, ftype, spec))
        return steps

    def make_struct(self, name, fields):
        decl = Decl("struct", name)
        self.names.add(name)
        ctx = {"keys": {}, "names": set(), "members": [], "scope": {}}
        steps = self.plan_fields(fields, ctx)
        scope = ctx["scope"]
        members = ctx["members"]

        member_lines = []
        for mname, mtype, spec in members:
            cpp = self.cpp_type(mtype, f"{name}_{spec}")
            if cpp is None or cpp == "void":
                continue
            member_lines.append(f"    {cpp} {mname}{{}};")
        decl.lines.append(f"struct {name} {{")
        decl.lines.extend(member_lines)
        decl.lines.append("};")
        decl.member_types = [self.cpp_type(t, f"{name}_{sp}") for _, t, sp in members]

        code = self.emit_steps(steps, scope, name)
        for mname, mtype, spec in members:
            definition = self.definition(mtype)
            if isinstance(definition, list) and definition[0] == "array" and isinstance(definition[1].get("count"), str):
                ref = definition[1]["count"]
                code.insert(0, f"if constexpr (!S::kReading) {scope[ref][0]} = static_cast<decltype({scope[ref][0]})>(v.{mname}.size());")
        decl.codec = code
        self.decls.append(decl)
        return decl

    def emit_steps(self, steps, scope, owner, depth=0):
        code = []
        for step in steps:
            if step[0] == "field":
                _, mname, mtype, spec = step
                code.extend(self.emit(mtype, f"v.{mname}", scope, f"{owner}_{spec}", "s", "S", depth, True))
            else:
                _, args, branches = step
                code.extend(self.emit_anon_switch(args, branches, scope, owner, depth))
        return code

    def emit_anon_switch(self, args, branches, scope, owner, depth):
        lines = []
        first = True
        for key, steps in branches:
            body = self.emit_steps(steps, scope, owner, depth + 1)
            if key == "__default":
                if first:
                    lines.extend(body)
                    continue
                lines.append("else {")
            else:
                cond = self.condition(scope, args["compareTo"], key)
                lines.append(f"{'if' if first else 'else if'} ({cond}) {{")
            lines.extend("    " + l for l in body)
            lines.append("}")
            first = False
        return lines

    def emit(self, expr, target, scope, hint, s, S, depth, top=False):
        kind = self.kind_of(expr)
        lines = []
        if kind == "void":
            return lines
        if kind == "prim":
            op = PRIMITIVES[expr][1]
            lines.append(f"{s}.{op}({target});")
            return lines
        if kind == "nbt":
            if expr == "nbtLoop":
                lines += [f"if constexpr ({S}::kReading) {{", f"    {target}.clear();",
                          f"    while ({s}.more() && {s}.ok) {{ Nbt item; {s}.nbt(item, false); {target}.push_back(std::move(item)); }}",
                          "} else {", f"    for (auto& item : {target}) {s}.nbt(item, false);", "}"]
                return lines
            lines.append(f"{s}.nbt({target}, {'true' if expr == 'lnbt' else 'false'});")
            return lines
        if kind == "named":
            body = self.types[expr]
            if isinstance(body, list) and body[0] in ("container", "bitfield"):
                lines.append(f"Transfer({s}, {target});")
                return lines
            if isinstance(body, list) and body[0] == "mapper":
                return self.emit_mapper(body[1], self.named_type(expr), target, s, S)
            return self.emit(body, target, scope, expr, s, S, depth)
        args = expr[1] if len(expr) > 1 else None
        if kind in ("container", "bitfield"):
            lines.append(f"Transfer({s}, {target});")
            return lines
        if kind == "mapper":
            return self.emit_mapper(args, self.cpp_type(expr, hint), target, s, S)
        if kind == "bitflags":
            return self.emit(args["type"], target, scope, hint, s, S, depth)
        if kind == "pstring":
            ctype, op = PRIMITIVES[args["countType"]][0], PRIMITIVES[args["countType"]][1]
            n = f"n{depth}"
            lines += ["{", f"    {ctype} {n} = static_cast<{ctype}>({target}.size());", f"    {s}.{op}({n});",
                      f"    if constexpr ({S}::kReading) {{ if (!{s}.length({n})) return; }}",
                      f"    {s}.chars({target}, static_cast<size_t>({n}));", "}"]
            return lines
        if kind == "buffer":
            ctype, op = PRIMITIVES[args["countType"]][0], PRIMITIVES[args["countType"]][1]
            n = f"n{depth}"
            lines += ["{", f"    {ctype} {n} = static_cast<{ctype}>({target}.size());", f"    {s}.{op}({n});",
                      f"    if constexpr ({S}::kReading) {{ if (!{s}.length({n})) return; }}",
                      f"    {s}.bytes({target}, static_cast<size_t>({n}));", "}"]
            return lines
        if kind in ("array", "maybeIncompleteArray"):
            element = f"e{depth}"
            inner = self.emit(args["type"], element, scope, hint + "_entry", s, S, depth + 1)
            if "countType" in args:
                ctype, op = PRIMITIVES[args["countType"]][0], PRIMITIVES[args["countType"]][1]
                n = f"n{depth}"
                lines += ["{", f"    {ctype} {n} = static_cast<{ctype}>({target}.size());", f"    {s}.{op}({n});",
                          f"    if constexpr ({S}::kReading) {{ if (!{s}.count({n})) return; {target}.clear(); {target}.resize(static_cast<size_t>({n})); }}"]
                if kind == "maybeIncompleteArray":
                    lines.append(f"    if constexpr ({S}::kReading) {{ size_t kept = 0; for (auto& {element} : {target}) {{ if (!{s}.more()) break; kept++; ")
                    lines.extend("        " + l for l in inner)
                    lines.append("    } {target}.resize(kept); } else {".replace("{target}", target))
                    lines.append(f"    for (auto& {element} : {target}) {{")
                    lines.extend("        " + l for l in inner)
                    lines.append("    } }")
                else:
                    lines.append(f"    for (auto& {element} : {target}) {{")
                    lines.extend("        " + l for l in inner)
                    lines.append("    }")
                lines.append("}")
                return lines
            count = args["count"]
            if isinstance(count, int):
                lines.append(f"for (auto& {element} : {target}) {{")
                lines.extend("    " + l for l in inner)
                lines.append("}")
                return lines
            if count not in scope:
                raise GenError(f"array count field '{count}' not found")
            counter = scope[count][0]
            lines += [f"if constexpr ({S}::kReading) {{ if (!{s}.count({counter})) return; {target}.clear(); {target}.resize(static_cast<size_t>({counter})); }}",
                      f"for (auto& {element} : {target}) {{"]
            lines.extend("    " + l for l in inner)
            lines.append("}")
            return lines
        if kind == "option":
            inner = self.emit(args, f"(*{target})", scope, hint, s, S, depth + 1)
            lines.append(f"if (Present({s}, {target})) {{")
            lines.extend("    " + l for l in inner)
            lines.append("}")
            return lines
        if kind == "optionalOnRemaining":
            inner = self.emit(args["type"], f"(*{target})", scope, hint, s, S, depth + 1)
            lines.append(f"if (PresentIfRemaining({s}, {target})) {{")
            lines.extend("    " + l for l in inner)
            lines.append("}")
            return lines
        if kind == "encapsulated":
            inner = self.emit(args["type"], target, scope, hint, "sub", "SS", depth + 1)
            lines.append(f"Frame({s}, [&](auto& sub) {{")
            if any(re.search(r"\bSS\b", l) for l in inner):
                lines.append("    using SS = std::remove_cvref_t<decltype(sub)>;")
            lines.extend("    " + l for l in inner)
            lines.append("});")
            return lines
        if kind == "switch":
            return self.emit_switch(args, target, scope, hint, s, S, depth)
        raise GenError(f"cannot emit '{kind}'")

    def emit_mapper(self, args, enum, target, s, S):
        under = PRIMITIVES[args["type"]][0]
        op = PRIMITIVES[args["type"]][1]
        return ["{", f"    {under} raw = static_cast<{under}>({target});", f"    {s}.{op}(raw);",
                f"    {target} = static_cast<{enum}>(raw);", "}"]

    def emit_switch(self, args, target, scope, hint, s, S, depth):
        distinct = []
        for key, body in args["fields"].items():
            if not self.is_void(body) and body not in distinct:
                distinct.append(body)
        default = args.get("default")
        if default is not None and not self.is_void(default) and default not in distinct:
            distinct.append(default)
        if not distinct:
            return []
        lines = [f"if constexpr ({S}::kReading) {target} = {{}};"]
        groups = []
        for body in distinct:
            keys = [k for k, b in args["fields"].items() if b == body]
            groups.append((body, keys))
        first = True
        single = len(distinct) == 1
        for index, (body, keys) in enumerate(groups):
            is_default = default is not None and not self.is_void(default) and body == default and not keys
            cpp = self.cpp_type(body, hint if single else f"{hint}_case{index}")
            pointer = f"p{depth}"
            inner = self.emit(body, f"(*{pointer})", scope, hint, s, S, depth + 1)
            if is_default:
                lines.append("else {" if not first else "{")
            else:
                parts = [self.condition(scope, args['compareTo'], k) for k in keys]
                cond = parts[0] if len(parts) == 1 else " || ".join(f"({c})" for c in parts)
                lines.append(f"{'if' if first else 'else if'} ({cond}) {{")
            picker = f"Pick<{cpp}>" if single else f"PickAt<{index + 1}>"
            lines.append(f"    if (auto* {pointer} = {picker}({s}, {target})) {{")
            lines.extend("        " + l for l in inner)
            lines.append("    }")
            lines.append("}")
            first = False
        return lines

def hard_deps(cpp, known):
    out = []
    i = 0
    text = cpp
    stripped = []
    while i < len(text):
        if text.startswith("std::vector<", i):
            depth = 0
            j = i + len("std::vector")
            while j < len(text):
                if text[j] == "<":
                    depth += 1
                elif text[j] == ">":
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            i = j + 1
            continue
        stripped.append(text[i])
        i += 1
    for ident in re.findall(r"[A-Za-z_][A-Za-z0-9_]*", "".join(stripped)):
        if ident in known:
            out.append(ident)
    return out

def order_decls(decls):
    known = {d.name for d in decls}
    by_name = {d.name: d for d in decls}
    done = set()
    result = []

    def visit(d, trail):
        if d.name in done:
            return
        if d.name in trail:
            return
        trail = trail | {d.name}
        for cpp in getattr(d, "member_types", []):
            if cpp is None:
                continue
            for dep in hard_deps(cpp, known):
                if dep != d.name:
                    visit(by_name[dep], trail)
        if d.name not in done:
            done.add(d.name)
            result.append(d)

    for d in decls:
        visit(d, frozenset())
    return result

def defer_incomplete(decls):
    position = {d.name: i for i, d in enumerate(decls) if d.kind == "struct"}
    for index, d in enumerate(decls):
        if d.kind != "struct":
            continue
        later = False
        for cpp in getattr(d, "member_types", []):
            for word in re.findall(r"[A-Za-z_][A-Za-z0-9_]*", cpp or ""):
                if word != d.name and position.get(word, -1) > index:
                    later = True
        if not later:
            continue
        d.deferred = True
        n = d.name
        for i, line in enumerate(d.lines):
            if line.startswith("    ") and line.endswith("{};") and any(
                    position.get(w, -1) > index for w in re.findall(r"[A-Za-z_][A-Za-z0-9_]*", line)):
                d.lines[i] = line[:-3] + ";"
        d.lines[-1:] = [f"    {n}();", f"    ~{n}();", f"    {n}(const {n}&);", f"    {n}({n}&&) noexcept;",
                        f"    {n}& operator=(const {n}&);", f"    {n}& operator=({n}&&) noexcept;", "};"]

def struct_name_for_packet(name):
    return pascal(name) + "Packet"

def build(gen):
    packet_map = gen.types["mcpe_packet"]
    wrapper = packet_map[1]
    ids = None
    params = None
    for f in wrapper:
        if f.get("name") == "name":
            ids = {int(k): v for k, v in f["type"][1]["mappings"].items()}
        if f.get("name") == "params":
            params = f["type"][1]["fields"]
    packets = []
    for number, name in sorted(ids.items()):
        ptype = params.get(name)
        packets.append((number, name, ptype))
    for name, info in gen.extra_packets.items():
        packets.append((info["id"], name, info["type"]))
    return packets

def emit_codec(decl):
    name = decl.name
    out = []
    out.append(f"template <class S> static void Serialize(S& s, {name}& v) {{")
    out.append("    (void)s; (void)v;")
    out.extend("    " + l for l in decl.codec)
    out.append("}")
    if getattr(decl, "deferred", False):
        out.append(f"{name}::{name}() = default;")
        out.append(f"{name}::~{name}() = default;")
        out.append(f"{name}::{name}(const {name}&) = default;")
        out.append(f"{name}::{name}({name}&&) noexcept = default;")
        out.append(f"{name}& {name}::operator=(const {name}&) = default;")
        out.append(f"{name}& {name}::operator=({name}&&) noexcept = default;")
    out.append(f"void Write(WriteStream& s, const {name}& v) {{ Serialize(s, const_cast<{name}&>(v)); }}")
    out.append(f"bool Read(ReadStream& s, {name}& v) {{ Serialize(s, v); return s.ok; }}")
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--spec", required=True)
    ap.add_argument("--overrides")
    ap.add_argument("--out", required=True)
    ap.add_argument("--parts", type=int, default=6)
    ap.add_argument("--list", action="store_true")
    opts = ap.parse_args()

    spec = json.load(open(opts.spec))
    overrides = json.load(open(opts.overrides)) if opts.overrides else {}
    gen = Generator(spec, overrides)
    packets = build(gen)

    packet_decls = []
    registry = []
    opaque = []
    for number, name, ptype in packets:
        struct = struct_name_for_packet(name)
        if name in gen.custom:
            registry.append((number, name, struct, "custom", gen.custom[name]))
            gen.names.add(struct)
            continue
        if ptype is None or ptype not in gen.types:
            raise GenError(f"packet {name} has no type")
        body = gen.types[ptype]
        mark = len(gen.decls)
        names_before = set(gen.names)
        saved = dict(gen.named), dict(gen.memo)
        try:
            if not (isinstance(body, list) and body[0] == "container"):
                raise GenError("packet body is not a container")
            decl = gen.make_struct(gen.unique(struct), body[1])
            decl.packet = (number, name)
            packet_decls.append(decl)
            registry.append((number, name, decl.name, "generated", None))
        except GenError as e:
            del gen.decls[mark:]
            gen.names = names_before
            gen.named, gen.memo = saved
            decl = Decl("struct", gen.unique(struct))
            decl.lines = [f"struct {decl.name} {{", "    std::vector<uint8_t> payload;", "};"]
            decl.codec = ["s.rest(v.payload);"]
            decl.packet = (number, name)
            gen.decls.append(decl)
            packet_decls.append(decl)
            registry.append((number, name, decl.name, "opaque", str(e)))
            opaque.append((name, str(e)))

    os.makedirs(opts.out, exist_ok=True)
    hdr = ["#pragma once", '#include "network/Stream.hpp"', "", "#include <array>", "#include <cstdint>", "#include <optional>",
           "#include <string>", "#include <variant>", "#include <vector>", "", "namespace perlin::proto {", "",
           "using net::Nbt;", "using net::ReadStream;", "using net::Uuid;", "using net::WriteStream;", ""]
    type_decls = order_decls([d for d in gen.decls if d.packet is None])
    defer_incomplete(type_decls)
    forward = sorted(d.name for d in type_decls if d.kind == "struct")
    if forward:
        hdr.extend(f"struct {n};" for n in forward)
        hdr.append("")
    for decl in type_decls:
        hdr.extend(decl.lines)
        if decl.kind == "struct":
            hdr.append(f"void Write(WriteStream&, const {decl.name}&);")
            hdr.append(f"bool Read(ReadStream&, {decl.name}&);")
        hdr.append("")
    hdr.append("}")
    write_if_changed(os.path.join(opts.out, "Types.hpp"), "\n".join(hdr) + "\n")

    pk = ["#pragma once", '#include "protocol/Types.hpp"', ""]
    for number, name, struct, mode, info in registry:
        if mode == "custom":
            pk.append(f'#include "{info}"')
    pk += ["", "namespace perlin::proto {", ""]
    by_name = {d.name: d for d in packet_decls}
    for number, name, struct, mode, info in registry:
        if mode == "custom":
            continue
        decl = by_name[struct]
        lines = list(decl.lines)
        lines.insert(len(lines) - 1, f"    static constexpr uint32_t kId = {number};")
        lines.insert(len(lines) - 1, f'    static constexpr const char* kName = "{name}";')
        pk.extend(lines)
        pk.append(f"void Write(WriteStream&, const {struct}&);")
        pk.append(f"bool Read(ReadStream&, {struct}&);")
        pk.append("")
    pk.append("struct PacketInfo {")
    pk.append("    uint32_t id;")
    pk.append("    const char* name;")
    pk.append("};")
    pk.append("inline constexpr PacketInfo kPackets[] = {")
    for number, name, struct, mode, info in registry:
        pk.append(f'    {{{number}, "{name}"}},')
    pk.append("};")
    pk.append("const char* PacketName(uint32_t id);")
    pk.append("}")
    write_if_changed(os.path.join(opts.out, "Packets.hpp"), "\n".join(pk) + "\n")

    all_codec = [d for d in gen.decls if d.kind == "struct"]
    parts = [[] for _ in range(opts.parts)]
    weights = [0] * opts.parts
    for decl in all_codec:
        if decl.packet is not None and decl.name not in by_name:
            continue
        i = weights.index(min(weights))
        parts[i].append(decl)
        weights[i] += len(decl.codec) + 3
    for i, group in enumerate(parts):
        src = ['#include "protocol/Packets.hpp"', "", "namespace perlin::proto {", "namespace {", "using net::Transfer;", "using net::Frame;",
               "using net::Pick;", "using net::PickAt;", "using net::Present;", "using net::PresentIfRemaining;", "}", ""]
        for decl in group:
            src.extend(emit_codec(decl))
            src.append("")
        if i == 0:
            src.append("const char* PacketName(uint32_t id) {")
            src.append("    switch (id) {")
            for number, name, struct, mode, info in registry:
                src.append(f'    case {number}: return "{name}";')
            src.append('    default: return "unknown";')
            src.append("    }")
            src.append("}")
        src.append("}")
        write_if_changed(os.path.join(opts.out, f"Codec{i}.cpp"), "\n".join(src) + "\n")

    rt = ['#include "protocol/Packets.hpp"', "", "#include <string>", "", "namespace perlin::proto {", "namespace {",
          "template <class P> int Check(std::string& failures, int& skipped) {",
          "    P value{};", "    std::vector<uint8_t> first;", "    net::WriteStream out(first);", "    Write(out, value);",
          "    if (!out.ok) {", "        skipped++;", "        return 0;", "    }",
          "    P back{};", "    net::ReadStream in(first.data(), first.size());",
          "    if (!Read(in, back) || in.remaining() != 0) {", '        failures += std::string(P::kName) + " (read)\\n";', "        return 1;", "    }",
          "    std::vector<uint8_t> second;", "    net::WriteStream again(second);", "    Write(again, back);",
          "    if (first != second) {", '        failures += std::string(P::kName) + " (mismatch)\\n";', "        return 1;", "    }",
          "    return 0;", "}", "}", "",
          "int RoundTripAll(std::string& failures, int& skipped) {", "    int bad = 0;"]
    for number, name, struct, mode, info in registry:
        if mode == "generated":
            rt.append(f"    bad += Check<{struct}>(failures, skipped);")
    rt += ["    return bad;", "}", "}"]
    write_if_changed(os.path.join(opts.out, "RoundTrip.cpp"), "\n".join(rt) + "\n")

    report = [f"{len(registry)} packets, {len(type_decls)} shared types"]
    if opaque:
        report.append(f"{len(opaque)} opaque packets:")
        report.extend(f"  {n}: {why}" for n, why in opaque)
    sys.stdout.write("\n".join(report) + "\n")

def write_if_changed(path, text):
    if os.path.exists(path) and open(path).read() == text:
        return
    with open(path, "w", newline="\n") as f:
        f.write(text)

if __name__ == "__main__":
    try:
        main()
    except GenError as e:
        sys.stderr.write(f"protogen: {e}\n")
        sys.exit(1)
