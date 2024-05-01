def gf_degree(a):
    res = 0
    a >>= 1
    while (a != 0):
        a >>= 1
        res += 1
    return res

def gf_invert(a, mod=0x1B):
    v = mod
    g1 = 1
    g2 = 0
    j = gf_degree(a) - 8

    while (a != 1):
        if (j < 0):
            a, v = v, a
            g1, g2 = g2, g1
            j = -j

        a ^= v << j
        g1 ^= g2 << j

        a %= 256  # Emulating 8-bit overflow
        g1 %= 256 # Emulating 8-bit overflow

        j = gf_degree(a) - gf_degree(v)

    return g1

# Generate the full GF(2^8) inverse table
gf_inv = [0] * 256
for i in range(1, 256):  # Start from 1 since 0 has no inverse
    gf_inv[i] = gf_invert(i, 0x1B)

# Format the output for C/C++ usage
c_array = "static const unsigned char gf_inv[256] = {\n    "
c_array += ', '.join(f"0x{num:02x}" for num in gf_inv[:16]) + ",\n    "
for i in range(16, 256, 16):
    c_array += ', '.join(f"0x{num:02x}" for num in gf_inv[i:i+16])
    if i + 16 < 256:
        c_array += ",\n    "
c_array += "\n};"

# Print the C/C++ array
print(c_array)