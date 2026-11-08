from pathlib import Path
from sys import argv

def gen_normalized_power_of_five():
    payload = '/**\n* @brief Normalized powers of 5 for float parsing (Positive Exponents)\n* @cite 10.1002_spe.2984\n*/\n'
    payload += "inline constexpr Bytes16 kNormalizedPowerOfFive[] = {\n"
    
    # Paper Section 8: powers q in [0, 308]
    for q in range(309):
        val = 5**q
        bit_len = val.bit_length()
        
        # Normalize so the most significant bit is exactly at bit 127 (making it a 128-bit value)
        if bit_len <= 128:
            shifted_val = val << (128 - bit_len)
        else:
            # Truncate less significant bits if the value exceeds 128 bits
            shifted_val = val >> (bit_len - 128)

        hi = (shifted_val >> 64) & 0xFFFFFFFFFFFFFFFF
        lo = shifted_val & 0xFFFFFFFFFFFFFFFF
        payload += f"\t{{0x{hi:016x}ull, 0x{lo:016x}ull}}, // 5 ^ {q}\n"
        
    payload += '};\n\n'
    return payload

def gen_power_of_five_reciprocal():
    payload = '/**\n* @brief Reciprocals of powers of 5 for float parsing (Negative Exponents)\n* @cite 10.1002_spe.2984\n*/\n'
    payload += "inline constexpr Bytes16 kPowerOfFiveReciprocal[] = {\n"
    
    # Paper Section 9: powers q in [-1, -342]
    for q in range(-1, -343, -1):
        p = -q
        val = 5**p
        
        # ceil(log2(5^p)) is equivalent to bit_length() for integer powers of 5
        z = val.bit_length() 

        if q >= -27:
            # Section 9.1: Negative exponents near zero (q >= -27)
            # b = 127 + ceil(log2(5^-q))
            b = 127 + z
            # c = ceiling(2^b / 5^-q)
            c = ((1 << b) + val - 1) // val
        else:
            # Section 9.2: Other negative powers (q < -27)
            # b = 64 + ceil(log2(5^-q))
            b = 64 + z
            # t = 2^2b, compute ceiling(t / 5^-q)
            c_full = ((1 << (2 * b)) + val - 1) // val
            
            # The paper states: "We only store the most significant 128 bits of c"
            shift = c_full.bit_length() - 128
            if shift > 0:
                c = c_full >> shift
            else:
                c = c_full << (-shift)

        hi = (c >> 64) & 0xFFFFFFFFFFFFFFFF
        lo = c & 0xFFFFFFFFFFFFFFFF
        payload += f"\t{{0x{hi:016x}ull, 0x{lo:016x}ull}}, // 5 ^ {q}\n"
        
    payload += '};\n\n'
    return payload

def gen_utf8_validation_tables():
    payload = '/**\n* @brief Vectorized classification tables for UTF-8 validation\n* @cite 10.1002_spe.2920\n*/\n'

    byte_1_high = [
        0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, # 0x00 - 0x7F (ASCII)
        0x80, 0x80, 0x80, 0x80,                         # 0x80 - 0xBF (Continuation)
        0x21, 0x01, 0x15, 0x49                          # 0xC0 - 0xFF (Leading Bytes)
    ]

    # byte_1_low: Dipetakan dari low-nibble byte pertama (prev byte)
    # Bersama byte_1_high, tabel ini memilah edge cases (seperti overlong/surrogate).
    byte_1_low = [
        0xE7, 0xA3, 0x83, 0x83, 0xCB, 0xCB, 0xCB, 0xCB,
        0xCB, 0xCB, 0xCB, 0xCB, 0xCB, 0xCB, 0xCB, 0xCB
    ]

    # byte_2_high: Dipetakan dari high-nibble byte kedua (current byte)
    # Memeriksa apakah byte setelahnya valid sebagai continuation.
    byte_2_high = [
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, # 0x00 - 0x7F (ASCII)
        0xAE, 0xE6, 0xBA, 0xBA,                         # 0x80 - 0xBF (Continuation ranges)
        0x01, 0x01, 0x01, 0x01                          # 0xC0 - 0xFF (Invalid as 2nd byte)
    ]

    def format_table(name, data):
        res = f"inline constexpr uint8_t {name}[] = {{\n\t"
        res += ", ".join([f"0x{x:02x}" for x in data])
        res += "\n};\n\n"
        return res

    payload += format_table("kUtf8Byte1High", byte_1_high)
    payload += format_table("kUtf8Byte1Low", byte_1_low)
    payload += format_table("kUtf8Byte2High", byte_2_high)

    return payload

def gen_json_char_classes():
    payload = '/**\n* @brief JSON Character Classes for fast scalar fallback and validation\n* 0=None, 1=Whitespace, 2=Structural, 4=Quote, 8=Escape\n*/\n'
    payload += "inline constexpr uint8_t kJsonCharClass[] = {\n\t"
    
    classes = [0] * 256
    # Whitespaces
    for c in [0x09, 0x0A, 0x0D, 0x20]:
        classes[c] = 1
    # Structural characters
    for c in [ord('{'), ord('}'), ord('['), ord(']'), ord(':'), ord(',')]:
        classes[c] = 2
    # Quote
    classes[ord('"')] = 4
    # Escape
    classes[ord('\\')] = 8

    chunks = []
    for i in range(0, 256, 16):
        chunk = ", ".join([f"0x{x:02x}" for x in classes[i:i+16]])
        chunks.append(chunk)
    
    payload += ",\n\t".join(chunks)
    payload += "\n};\n\n"
    return payload

def gen_ondemand_node_types():
    payload = '/**\n* @brief On-Demand JSON Node Type resolution table\n* Maps a starting byte directly to its JSON entity type to eliminate branching.\n* Types: 0=Unknown, 1=Object, 2=Array, 3=String, 4=Number, 5=True, 6=False, 7=Null\n*/\n'
    payload += "inline constexpr uint8_t kOndemandNodeType[] = {\n\t"
    
    types = [0] * 256
    types[ord('{')] = 1 # Object
    types[ord('[')] = 2 # Array
    types[ord('"')] = 3 # String
    
    # Numbers (- and 0-9)
    types[ord('-')] = 4
    for i in range(ord('0'), ord('9') + 1):
        types[i] = 4
        
    types[ord('t')] = 5 # True
    types[ord('f')] = 6 # False
    types[ord('n')] = 7 # Null

    chunks = []
    for i in range(0, 256, 16):
        chunk = ", ".join([f"0x{x:02x}" for x in types[i:i+16]])
        chunks.append(chunk)
    
    payload += ",\n\t".join(chunks)
    payload += "\n};\n\n"
    return payload

def main():
    if len(argv) < 2:
        print('[-] Required output path argument!')
        exit(1)
    elif len(argv) > 3:
        print('[-] Too many argument!')
        exit(1)
        
    output_path = Path(argv[1]).resolve()
    
    if output_path.suffix != '.hpp':
        print('[-] File Extension must be \'hpp\'!')
        exit(1)
    
    if not output_path.parent.exists():
        output_path.parent.mkdir(parents=True, exist_ok=True)
    
    with open(argv[1], 'w') as w:
        w.write('#pragma once\n\n')
        w.write('#include <ze_json/core/integral.hpp>\n\n')
        w.write('namespace zuu::core {\n\n')
        w.write('struct Bytes16 {\n')
        w.write('\tuint64_t high;\n')
        w.write('\tuint64_t low;\n')
        w.write('};\n\n')
        w.write(gen_normalized_power_of_five())
        w.write(gen_power_of_five_reciprocal())
        w.write(gen_utf8_validation_tables())
        w.write(gen_json_char_classes())
        w.write(gen_ondemand_node_types())
        w.write('} // namespace zuu::core\n')

if __name__ == '__main__':
    main()