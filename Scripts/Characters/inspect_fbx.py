"""Read-only binary FBX inspection (no Autodesk dependency)."""
import json
import struct
import sys
import zlib
from pathlib import Path


def read_fbx(path):
    data = Path(path).read_bytes()
    if not data.startswith(b"Kaydara FBX Binary"):
        raise ValueError("Expected a binary FBX")
    version = struct.unpack_from("<I", data, 23)[0]
    header = "<QQQB" if version >= 7500 else "<IIIB"
    header_size = struct.calcsize(header)

    def prop(pos):
        tag = chr(data[pos]); pos += 1
        scalar = {"Y": "h", "C": "?", "I": "i", "F": "f", "D": "d", "L": "q"}
        array = {"f": "f", "d": "d", "l": "q", "i": "i", "b": "?", "c": "B"}
        if tag in scalar:
            fmt = "<" + scalar[tag]
            return struct.unpack_from(fmt, data, pos)[0], pos + struct.calcsize(fmt)
        if tag in "SR":
            n = struct.unpack_from("<I", data, pos)[0]; pos += 4
            val = data[pos:pos+n]
            return (val.decode("utf8", "replace") if tag == "S" else val), pos + n
        if tag in array:
            n, enc, size = struct.unpack_from("<III", data, pos); pos += 12
            raw = data[pos:pos+size]
            if enc: raw = zlib.decompress(raw)
            return list(struct.unpack("<" + array[tag] * n, raw)), pos + size
        raise ValueError(f"Unsupported property {tag}")

    def node(pos):
        end, count, length, name_len = struct.unpack_from(header, data, pos)
        if not end: return None, pos + header_size
        pos += header_size
        name = data[pos:pos+name_len].decode("utf8"); pos += name_len
        props = []
        for _ in range(count):
            val, pos = prop(pos); props.append(val)
        children = []
        while pos < end - header_size:
            c, pos = node(pos)
            if c: children.append(c)
            else: break
        return {"name": name, "props": props, "children": children}, end

    nodes, pos = [], 27
    while pos < len(data):
        c, pos = node(pos)
        if not c: break
        nodes.append(c)
    return version, nodes


def child(node, name):
    return next((c for c in node["children"] if c["name"] == name), None)


if __name__ == "__main__":
    for path in sys.argv[1:]:
        version, nodes = read_fbx(path)
        objects = next(n for n in nodes if n["name"] == "Objects")
        print(f"\nFILE {path}, FBX {version}")
        globals_ = next(n for n in nodes if n["name"] == "GlobalSettings")
        print("GLOBAL", [n["props"] for n in child(globals_, "Properties70")["children"]])
        for obj in objects["children"]:
            if obj["name"] in ("Model", "Geometry", "Deformer", "AnimationStack", "Material"):
                print(obj["name"], repr(obj["props"]))
                properties = child(obj, "Properties70")
                if obj["name"] == "Model" and properties:
                    print(" ", [p["props"] for p in properties["children"] if p["props"][0] in ("Lcl Translation", "Lcl Rotation", "Lcl Scaling")])
                vertices = child(obj, "Vertices")
                if vertices:
                    v = vertices["props"][0]
                    print("  vertices", len(v)//3, "bounds", [(min(v[a::3]),max(v[a::3])) for a in range(3)])
                indices = child(obj, "Indexes")
                if indices: print("  weights",len(indices["props"][0]))
        connections = next(n for n in nodes if n["name"] == "Connections")
        print("CONNECTIONS", json.dumps([c["props"] for c in connections["children"]]))
