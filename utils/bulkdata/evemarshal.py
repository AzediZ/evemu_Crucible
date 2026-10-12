"""
evemarshal.py - reads EVE marshal streams (the client's bulkdata/*.cache2 files, cached method calls, ...) into plain
python objects.  Stdlib only.  A port of src/eve-common/marshal/EVEUnmarshal.cpp.

    import evemarshal
    obj = evemarshal.load_file(path)
    cols, rows = evemarshal.table(obj)      # for the rowset shapes bulkdata uses

Strings come back as str (byte strings are decoded latin-1, so they round-trip), tokens as Token, buffers as bytes,
objects as Object / ObjectEx, packed rows as Row.
"""

import decimal
import os
import re
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
STRING_TABLE_CPP = os.path.normpath(os.path.join(HERE, '..', '..', 'src', 'eve-common', 'marshal',
                                                 'EVEMarshalStringTable.cpp'))

_string_table = None


def string_table():
    """the marshal string table, read from the server's own source so the two never drift"""
    global _string_table
    if _string_table is None:
        src = open(STRING_TABLE_CPP, encoding='latin-1').read()
        body = src[src.index('s_mStringTable[] ='):]
        body = body[body.index('{') + 1:body.index('};')]
        body = re.sub(r'//[^\n]*', '', body)
        _string_table = re.findall(r'"((?:[^"\\]|\\.)*)"', body)
    return _string_table


class Token(str):
    """a global name (class or function) the client resolves on load"""


class Object(object):
    def __init__(self, type_, args):
        self.type = type_
        self.args = args

    def __repr__(self):
        return 'Object(%r, %r)' % (self.type, self.args)


class ObjectEx(object):
    def __init__(self, type2, header):
        self.type2 = type2
        self.header = header
        self.list = []
        self.dict = {}

    def __repr__(self):
        return 'ObjectEx(%r, list=%d, dict=%d)' % (self.header, len(self.list), len(self.dict))


class SubStream(object):
    def __init__(self, data):
        self.data = data

    def load(self):
        return loads(self.data)


class Row(object):
    """a blue.DBRow: columns is the descriptor's [(name, dbtype), ...], values in column order"""
    __slots__ = ('columns', 'values')

    def __init__(self, columns, values):
        self.columns = columns
        self.values = values

    def __repr__(self):
        return 'Row(%r)' % dict(zip([c[0] for c in self.columns], self.values))


# DBTYPE sizes in bits (src/eve-common/database/dbtype.h)
DBTYPE_I2, DBTYPE_I4, DBTYPE_R4, DBTYPE_R8, DBTYPE_CY, DBTYPE_BOOL = 2, 3, 4, 5, 6, 11
DBTYPE_I1, DBTYPE_UI1, DBTYPE_UI2, DBTYPE_UI4, DBTYPE_I8, DBTYPE_UI8 = 16, 17, 18, 19, 20, 21
DBTYPE_FILETIME, DBTYPE_BYTES, DBTYPE_STR, DBTYPE_WSTR = 64, 128, 129, 130

_SIZE = {DBTYPE_I8: 64, DBTYPE_UI8: 64, DBTYPE_CY: 64, DBTYPE_FILETIME: 64, DBTYPE_R8: 64,
         DBTYPE_I4: 32, DBTYPE_UI4: 32, DBTYPE_R4: 32, DBTYPE_I2: 16, DBTYPE_UI2: 16,
         DBTYPE_I1: 8, DBTYPE_UI1: 8, DBTYPE_BOOL: 1, DBTYPE_BYTES: 0, DBTYPE_STR: 0, DBTYPE_WSTR: 0}
_FMT = {DBTYPE_I8: 'q', DBTYPE_UI8: 'Q', DBTYPE_CY: 'q', DBTYPE_FILETIME: 'q', DBTYPE_R8: 'd',
        DBTYPE_I4: 'i', DBTYPE_UI4: 'I', DBTYPE_R4: 'f', DBTYPE_I2: 'h', DBTYPE_UI2: 'H',
        DBTYPE_I1: 'b', DBTYPE_UI1: 'B'}


def descriptor_columns(desc):
    """[(name, dbtype), ...] of a blue.DBRowDescriptor ObjectEx"""
    if isinstance(desc, ObjectEx):
        hdr = desc.header
        # (Token('blue.DBRowDescriptor'), (((name, type), ...),))
        return [(str(c[0]), int(c[1])) for c in hdr[1][0]]
    raise ValueError('not a DBRowDescriptor: %r' % (desc,))


class _Reader(object):
    def __init__(self, data):
        self.d = data
        self.p = 0
        self.store = []
        self.store_idx = []
        self.store_pos = 0
        self.desc_cache = {}

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def take(self, n):
        v = self.d[self.p:self.p + n]
        if len(v) != n:
            raise ValueError('marshal stream truncated at %d' % self.p)
        self.p += n
        return v

    def unpack(self, fmt):
        v = struct.unpack_from('<' + fmt, self.d, self.p)
        self.p += struct.calcsize('<' + fmt)
        return v[0]

    def size_ex(self):
        n = self.u8()
        if n == 0xFF:
            n = self.unpack('I')
        return n

    def load_stream(self):
        start = self.p
        if self.u8() != 0x7E:
            raise ValueError('not a marshal stream (header 0x%02X)' % self.d[start])
        save_count = self.unpack('I')
        if save_count:
            end = len(self.d)
            self.store_idx = list(struct.unpack_from('<%dI' % save_count, self.d, end - 4 * save_count))
            self.store = [None] * save_count
        return self.load()

    def load(self):
        hdr = self.u8()
        op = hdr & 0x3F
        idx = 0
        if hdr & 0x40:
            idx = self.store_idx[self.store_pos]
            self.store_pos += 1
        v = self.op(op)
        if idx:
            self.store[idx - 1] = v
        return v

    def op(self, op):
        if op == 0x01: return None
        if op == 0x02: return Token(self.take(self.u8()).decode('latin-1'))
        if op == 0x03: return self.unpack('q')
        if op == 0x04: return self.unpack('i')
        if op == 0x05: return self.unpack('h')
        if op == 0x06: return self.unpack('b')
        if op == 0x07: return -1
        if op == 0x08: return 0
        if op == 0x09: return 1
        if op == 0x0A: return self.unpack('d')
        if op == 0x0B: return 0.0
        if op == 0x0D: return bytes(self.take(self.size_ex()))
        if op == 0x0E: return ''
        if op == 0x0F: return self.take(1).decode('latin-1')
        if op == 0x10: return self.take(self.u8()).decode('latin-1')
        if op == 0x11:
            i = self.u8()
            return string_table()[i - 1]
        if op == 0x12: return self.take(self.size_ex() * 2).decode('utf-16-le')
        if op == 0x13: return self.take(self.size_ex()).decode('latin-1')
        if op == 0x14: return tuple(self.load() for _ in range(self.size_ex()))
        if op == 0x15: return [self.load() for _ in range(self.size_ex())]
        if op == 0x16:
            d = {}
            for _ in range(self.size_ex()):
                v = self.load()
                k = self.load()
                d[_hashable(k)] = v
            return d
        if op == 0x17:
            t = self.load()
            return Object(t, self.load())
        if op == 0x19: return self.load()
        if op == 0x1B:
            i = self.size_ex()
            return self.store[i - 1]
        if op == 0x1C:
            self.unpack('I')
            return self.load()
        if op == 0x1F: return True
        if op == 0x20: return False
        if op in (0x22, 0x23): return self.object_ex(op == 0x23)
        if op == 0x24: return ()
        if op == 0x25: return (self.load(),)
        if op == 0x26: return []
        if op == 0x27: return [self.load()]
        if op == 0x28: return ''
        if op == 0x29: return self.take(2).decode('utf-16-le')
        if op == 0x2A: return self.packed_row()
        if op == 0x2B: return SubStream(bytes(self.take(self.size_ex())))
        if op == 0x2C:
            a = self.load()
            return (a, self.load())
        if op == 0x2E: return self.take(self.size_ex()).decode('utf-8')
        if op == 0x2F:
            b = self.take(self.size_ex())
            return int.from_bytes(b, 'little', signed=True)
        raise ValueError('bad marshal opcode 0x%02X at %d' % (op, self.p - 1))

    def object_ex(self, type2):
        o = ObjectEx(type2, self.load())
        while self.d[self.p] != 0x2D:
            o.list.append(self.load())
        self.p += 1
        while self.d[self.p] != 0x2D:
            k = self.load()
            o.dict[_hashable(k)] = self.load()
        self.p += 1
        return o

    def rle(self, size):
        n = self.size_ex()
        src = self.take(n)
        out = bytearray(size)
        oi = 0
        i = 0
        nib = 0
        run = 0
        while i < n:
            nib = not nib
            if nib:
                run = src[i]
                i += 1
                cnt = (run & 0x0F) - 8
            else:
                cnt = (run >> 4) - 8
            if cnt >= 0:
                oi += cnt + 1
            else:
                while cnt and i < n:
                    out[oi] = src[i]
                    oi += 1
                    i += 1
                    cnt += 1
        if oi > size:
            raise ValueError('packed row overflow at %d' % self.p)
        return bytes(out)

    def packed_row(self):
        desc = self.load()
        key = id(desc)
        layout = self.desc_cache.get(key)
        if layout is None:
            cols = descriptor_columns(desc)
            sizes = [_SIZE[t] for _, t in cols]
            order = sorted(range(len(cols)), key=lambda c: -sizes[c])     # stable: equal sizes keep column order
            byte_bits = sum(s for s in sizes if s >= 8)
            bools = {}
            for c, (_, t) in enumerate(cols):
                if t == DBTYPE_BOOL:
                    bools[c] = len(bools)
            nbytes = (byte_bits >> 3) + ((len(bools) + len(cols)) >> 3) + 1
            layout = (cols, order, byte_bits, bools, nbytes)
            self.desc_cache[key] = (layout, desc)
        else:
            layout = layout[0]
        cols, order, byte_bits, bools, nbytes = layout
        buf = self.rle(nbytes)
        vals = [None] * len(cols)
        pos = 0
        nbool = len(bools)
        for c in order:
            t = cols[c][1]
            nullbit = byte_bits + nbool + c
            isnull = buf[nullbit >> 3] & (1 << (nullbit & 7))
            if t in (DBTYPE_BYTES, DBTYPE_STR, DBTYPE_WSTR):
                v = self.load()
                vals[c] = None if isnull else v
                continue
            if t == DBTYPE_BOOL:
                if not isnull:
                    b = byte_bits + bools[c]
                    vals[c] = bool(buf[b >> 3] & (1 << (b & 7)))
                continue
            fmt = _FMT[t]
            if not isnull:
                vals[c] = struct.unpack_from('<' + fmt, buf, pos)[0]
                if t == DBTYPE_CY:
                    vals[c] = decimal.Decimal(vals[c]).scaleb(-4)     # currency: fixed point, 4 decimals
            pos += _SIZE[t] >> 3
        return Row(cols, vals)


def _hashable(k):
    if isinstance(k, list):
        return tuple(_hashable(x) for x in k)
    return k


def loads(data):
    if data[:1] == b'\x78':
        data = zlib.decompress(data)
    return _Reader(data).load_stream()


def load_file(path):
    with open(path, 'rb') as f:
        return loads(f.read())


def _rowset_header_columns(hdr):
    if isinstance(hdr, ObjectEx):
        return [c[0] for c in descriptor_columns(hdr)]
    return [str(c) for c in hdr]


def table(obj):
    """(columns, rows) of the rowset shapes bulkdata uses:  dbutil.CRowset / CIndexedRowset / CFilterRowset
    (ObjectEx2), util.Rowset / IndexRowset / FilterRowset (Object with a header dict), plain lists of Rows."""
    if isinstance(obj, ObjectEx):
        name = obj.header[0][0] if isinstance(obj.header[0], tuple) else obj.header[0]
        kw = obj.header[1] if len(obj.header) > 1 and isinstance(obj.header[1], dict) else {}
        if 'header' in kw:
            cols = _rowset_header_columns(kw['header'])
        elif obj.list and isinstance(obj.list[0], Row):
            cols = [c[0] for c in obj.list[0].columns]
        else:
            cols = []
        if name in ('dbutil.CRowset',):
            return cols, [list(r.values) for r in obj.list]
        if name in ('dbutil.CIndexedRowset',):
            return cols, [list(r.values) for r in obj.dict.values()]
        if name in ('dbutil.CFilterRowset',):
            rows = []
            for v in obj.dict.values():
                if isinstance(v, ObjectEx):
                    rows.extend(list(r.values) for r in v.list)
                else:
                    rows.extend(list(r.values) for r in v)
            return cols, rows
        raise ValueError('unknown rowset class %r' % (name,))
    if isinstance(obj, Object):
        args = obj.args
        if isinstance(args, dict) and 'header' in args:
            cols = [str(c) for c in args['header']]
            lines = args.get('lines')
            if lines is not None:
                return cols, [list(l) for l in lines]
            items = args.get('items')
            if isinstance(items, dict):
                rows = []
                for v in items.values():
                    if v and isinstance(v[0], (list, tuple)):
                        rows.extend(list(x) for x in v)
                    else:
                        rows.append(list(v))
                return cols, rows
        raise ValueError('unknown object %r' % (obj.type,))
    if isinstance(obj, list) and obj and isinstance(obj[0], Row):
        return [c[0] for c in obj[0].columns], [list(r.values) for r in obj]
    raise ValueError('not a rowset: %r' % (type(obj),))
