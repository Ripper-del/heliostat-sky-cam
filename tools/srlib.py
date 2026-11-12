"""Читання записів sigrok (.sr = zip: metadata + logic-1-N, один байт на відлік, біт n = канал Dn)."""
import re
import zipfile


def load(path):
    z = zipfile.ZipFile(path)
    meta = z.read("metadata").decode(errors="replace")
    m = re.search(r"samplerate=(\d+)\s*(k|M|G)?Hz", meta)
    rate = int(m.group(1)) * {"": 1, None: 1, "k": 10**3, "M": 10**6, "G": 10**9}[m.group(2)] if m else 1_000_000
    chunks = sorted((n for n in z.namelist() if n.startswith("logic-1")), key=lambda n: [int(t) if t.isdigit() else 0 for t in n.split("-")])
    data = b"".join(z.read(n) for n in chunks)
    return data, rate


def edges(data, ch):
    """[(індекс, 'r'|'f')] для каналу ch."""
    out, prev = [], (data[0] >> ch) & 1 if data else 0
    for i, b in enumerate(data):
        v = (b >> ch) & 1
        if v != prev:
            out.append((i, "r" if v else "f"))
            prev = v
    return out
