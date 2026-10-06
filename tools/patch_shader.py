import argparse
import hashlib
import math
import struct
from pathlib import Path

def dxbc_checksum(shader):
    data = shader[20:]
    bits = (len(data) * 8) & 0xffffffff
    tail_size = len(data) % 64
    body = data[:len(data) - tail_size] if tail_size else data
    tail = data[len(body):]
    if tail_size >= 56:
        padding = tail + b"\x80" + bytes(63 - tail_size)
        padding += struct.pack("<I", bits) + bytes(56) + struct.pack("<I", (bits >> 2) | 1)
    else:
        padding = struct.pack("<I", bits) + tail + b"\x80"
        padding += bytes(55 - tail_size) + struct.pack("<I", (bits >> 2) | 1)
    shifts = ((7, 12, 17, 22), (5, 9, 14, 20), (4, 11, 16, 23), (6, 10, 15, 21))
    constants = [int(abs(math.sin(i + 1)) * 2**32) for i in range(64)]
    state = [0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476]
    message = body + padding
    for offset in range(0, len(message), 64):
        words = struct.unpack_from("<16I", message, offset)
        a, b, c, d = state
        for i in range(64):
            if i < 16:
                f, index = (b & c) | (~b & d), i
            elif i < 32:
                f, index = (d & b) | (~d & c), (5 * i + 1) % 16
            elif i < 48:
                f, index = b ^ c ^ d, (3 * i + 5) % 16
            else:
                f, index = c ^ (b | ~d), (7 * i) % 16
            value = (a + f + constants[i] + words[index]) & 0xffffffff
            shift = shifts[i // 16][i % 4]
            value = ((value << shift) | (value >> (32 - shift))) & 0xffffffff
            a, b, c, d = d, (b + value) & 0xffffffff, b, c
        state = [(x + y) & 0xffffffff for x, y in zip(state, (a, b, c, d))]
    return struct.pack("<4I", *state)


def code_chunk(data):
    if data[:4] != b"DXBC":
        raise ValueError("Not DXBC")
    count = struct.unpack_from("<I", data, 28)[0]
    for offset in struct.unpack_from("<" + "I" * count, data, 32):
        name, size = struct.unpack_from("<4sI", data, offset)
        if name in (b"SHEX", b"SHDR"):
            return data[offset + 8:offset + 8 + size]
    raise ValueError("No shader program chunk")


def main():
    parser = argparse.ArgumentParser(
        description="Validate and correct a locally extracted ELEX cloud shader. Do not distribute shader files.")
    parser.add_argument("original", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    original = args.original.read_bytes()
    if hashlib.sha256(original).hexdigest() != \
            "62c61e70601af0ee30a811cffbb66f55cd8eda6e0c88b69cbfbd646370237162":
        raise ValueError("Unsupported shader: exact original SHA256 fingerprint required")
    if dxbc_checksum(original) != original[4:20]:
        raise ValueError("Original DXBC checksum does not match calculated value")
    patched = bytearray(original)
    count = 0
    chunks = struct.unpack_from("<I", original, 28)[0]
    for offset in struct.unpack_from("<" + "I" * chunks, original, 32):
        tag, size = struct.unpack_from("<4sI", original, offset)
        if tag not in (b"SHEX", b"SHDR"):
            continue
        position, end = offset + 16, offset + 8 + size
        while position < end:
            token = struct.unpack_from("<I", original, position)[0]
            length = (token >> 24) & 127
            if not length or position + length * 4 > end:
                raise ValueError("Unexpected instruction length")
            if token == 0x010010BE:
                struct.pack_into("<I", patched, position, token | 0x800)
                count += 1
            position += length * 4
    if count != 1:
        raise ValueError("Expected exactly one memory-only group barrier, found " + str(count))
    patched[4:20] = dxbc_checksum(patched)
    result = bytes(patched)
    before, after = code_chunk(original), code_chunk(result)
    if len(before) != len(after):
        raise ValueError("Shader program size changed")
    differences = [(offset, struct.unpack_from("<I", before, offset)[0],
                    struct.unpack_from("<I", after, offset)[0])
                   for offset in range(0, len(before), 4)
                   if before[offset:offset + 4] != after[offset:offset + 4]]
    if len(differences) != 1:
        raise ValueError("Expected one instruction-token change: " + str(differences))
    if hashlib.sha256(result).hexdigest() != \
            "3bbd8c858dc31d5dfd455486071011308e35a02016b5c4eb405afa0bec2ceed6":
        raise ValueError("Corrected shader does not match the verified fingerprint")
    with args.output.open("xb") as output:
        output.write(result)
    print("Original SHA256:", hashlib.sha256(original).hexdigest())
    print("Original DXBC checksum bytes:", original[4:20].hex())
    print("Code changes:", differences)
    print("Original bytes:", len(original), "patched bytes:", len(result))


if __name__ == "__main__":
    main()
