"""Readers of the game's file formats, no dependencies: Yaz0, SARC, BYML (v2-v4, little endian), MSBT text."""
import struct


def yaz0(data):
    if data[:4] != b"Yaz0":
        return data
    size = struct.unpack_from(">I", data, 4)[0]
    out = bytearray(size)
    src, dst = 16, 0
    while dst < size:
        header = data[src]
        src += 1
        for bit in range(7, -1, -1):
            if dst >= size:
                break
            if header & (1 << bit):
                out[dst] = data[src]
                src += 1
                dst += 1
            else:
                b1, b2 = data[src], data[src + 1]
                src += 2
                dist = ((b1 & 0x0F) << 8 | b2) + 1
                count = b1 >> 4
                if count == 0:
                    count = data[src] + 0x12
                    src += 1
                else:
                    count += 2
                start = dst - dist
                for i in range(count):
                    out[dst] = out[start + i]
                    dst += 1
    return bytes(out)


def sarc(data):
    """{name: bytes} of a SARC archive."""
    data = yaz0(data)
    assert data[:4] == b"SARC", data[:4]
    bom = struct.unpack_from(">H", data, 6)[0]
    e = "<" if bom == 0xFFFE else ">"
    header_size, = struct.unpack_from(e + "H", data, 4)
    data_offset, = struct.unpack_from(e + "I", data, 0xC)
    sfat = header_size
    node_count, = struct.unpack_from(e + "H", data, sfat + 6)
    nodes = sfat + 12
    sfnt = nodes + node_count * 16
    names = sfnt + 8
    files = {}
    for i in range(node_count):
        _, attrs, start, end = struct.unpack_from(e + "IIII", data, nodes + i * 16)
        if attrs & 0x01000000:
            off = names + (attrs & 0xFFFF) * 4
            name = data[off:data.index(b"\0", off)].decode("utf-8")
        else:
            name = "%08x" % i
        files[name] = data[data_offset + start:data_offset + end]
    return files


class Byml:
    def __init__(self, data):
        data = yaz0(data)
        self.d = data
        self.e = "<" if data[:2] == b"YB" else ">"
        _, keys, strings, root = struct.unpack_from(self.e + "HIII", data, 2)
        self.keys = self._table(keys) if keys else []
        self.strings = self._table(strings) if strings else []
        self.root = self._node(data[root], root) if root else None

    def _u32(self, off):
        return struct.unpack_from(self.e + "I", self.d, off)[0]

    def _count(self, off):
        # A node header: one byte of type, then a 24-bit count.
        word = self._u32(off)
        return word >> 8 if self.e == "<" else word & 0xFFFFFF

    def _table(self, off):
        count = self._count(off)
        out = []
        for i in range(count):
            s = off + self._u32(off + 4 + 4 * i)
            out.append(self.d[s:self.d.index(b"\0", s)].decode("utf-8"))
        return out

    def _value(self, kind, off):
        """A value of type `kind` stored at off (inline or by offset)."""
        if kind in (0xC0, 0xC1):
            return self._node(kind, self._u32(off))
        if kind == 0xA0:
            return self.strings[self._u32(off)]
        if kind == 0xD0:
            return self._u32(off) != 0
        if kind == 0xD1:
            return struct.unpack_from(self.e + "i", self.d, off)[0]
        if kind == 0xD2:
            return struct.unpack_from(self.e + "f", self.d, off)[0]
        if kind == 0xD3:
            return self._u32(off)
        if kind in (0xD4, 0xD5, 0xD6):
            at = self._u32(off)
            fmt = {0xD4: "q", 0xD5: "Q", 0xD6: "d"}[kind]
            return struct.unpack_from(self.e + fmt, self.d, at)[0]
        if kind == 0xFF:
            return None
        raise ValueError("byml type %x" % kind)

    def _node(self, kind, off):
        if kind == 0xC0:
            count = self._count(off)
            types = self.d[off + 4:off + 4 + count]
            values = (off + 4 + count + 3) & ~3
            return [self._value(types[i], values + 4 * i) for i in range(count)]
        if kind == 0xC1:
            count = self._count(off)
            out = {}
            for i in range(count):
                entry = off + 4 + 8 * i
                word = self._u32(entry)
                key = word & 0xFFFFFF if self.e == "<" else word >> 8
                vtype = self.d[entry + 3]
                out[self.keys[key]] = self._value(vtype, entry + 4)
            return out
        return self._value(kind, off)


def byml(data):
    return Byml(data).root


def msbt_texts(data):
    """{label: text} of an MSBT file (UTF-16 little endian, control codes dropped)."""
    assert data[:8] == b"MsgStdBn"
    e = "<" if data[8:10] == b"\xff\xfe" else ">"
    sections, = struct.unpack_from(e + "H", data, 0x0E)
    pos = 0x20
    labels = {}
    texts = []
    for _ in range(sections):
        magic = data[pos:pos + 4]
        size, = struct.unpack_from(e + "I", data, pos + 4)
        body = pos + 16
        if magic == b"LBL1":
            groups, = struct.unpack_from(e + "I", data, body)
            for g in range(groups):
                count, off = struct.unpack_from(e + "II", data, body + 4 + 8 * g)
                p = body + off
                for _ in range(count):
                    n = data[p]
                    name = data[p + 1:p + 1 + n].decode("utf-8")
                    index, = struct.unpack_from(e + "I", data, p + 1 + n)
                    labels[name] = index
                    p += 1 + n + 4
        elif magic == b"TXT2":
            count, = struct.unpack_from(e + "I", data, body)
            offs = [struct.unpack_from(e + "I", data, body + 4 + 4 * i)[0] for i in range(count)]
            for i, off in enumerate(offs):
                start = body + off
                end = body + (offs[i + 1] if i + 1 < count else size)
                raw = data[start:end]
                chars = []
                j = 0
                while j + 1 < len(raw):
                    c, = struct.unpack_from(e + "H", raw, j)
                    if c == 0:
                        break
                    if c == 0x0E:  # control: group, type, size, data
                        _, _, n = struct.unpack_from(e + "HHH", raw, j + 2)
                        j += 8 + n
                        continue
                    if c == 0x0F:
                        j += 6
                        continue
                    chars.append(chr(c))
                    j += 2
                texts.append("".join(chars))
        pos = (body + size + 15) & ~15
    return {name: texts[i] for name, i in labels.items() if i < len(texts)}
