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
    payload = '''/**
* @brief Vectorized classification tables for UTF-8 validation (Niagara
*        two-table method, Section 6 of the paper).
* @cite 10.1002_spe.2920
*
* Composition (paper Section 6.1-6.3; verified against its page-9 worked
* example "39 C3 A7 E9 8F A1 F0 9F 98 80"):
*   for every index i, with prev = input[i-1] (0x00 at stream start):
*     v = kUtf8Byte1High[prev >> 4] & kUtf8Byte1Low[prev & 0x0F]
*       & kUtf8Byte2High[input[i] >> 4]
*     v & 0x7F != 0  -> the pair (prev, input[i]) matches one of the seven
*                       error patterns of Table 8 -> invalid.
*     v & 0x80       -> continuation-pair marker; must appear EXACTLY at the
*                       indexes where a continuation pair is expected:
*                       input[i-2] >= 0xE0 (3-byte lead) or
*                       input[i-3] >= 0xF0 (4-byte lead).
*   end checks: input[-1] < 0xC0, input[-2] < 0xE0, input[-3] < 0xF0.
*
* Provenance note (P7-1): the tables below were corrected by an exhaustive
* differential sweep against a strict RFC 3629 oracle (CPython decode):
* 1-byte full, 2-byte full (65,536), 3-byte full for leads >= C0 (4,194,304),
* structured 4-byte (2,160) — zero mismatches. The corrections vs. the
* first transcription were: kUtf8Byte2High[8]/[9] swapped (false positive on
* F0 9x, missed overlong F0 8x), kUtf8Byte1Low[4] carried bit 6 (false
* positive on F4 8x = valid U+100000..U+10FFFF), kUtf8Byte1Low[D] lacked the
* surrogate bit 4 (ED A0-BF wrongly accepted).
*/

'''
    # kUtf8Byte1High: indexed by the HIGH nibble of the previous byte.
    # 0x00-0x7F (ASCII prev) -> bit 1 ("too long": ASCII + continuation);
    # 0x80-0xBF (continuation prev) -> bit 7 only (marker carrier, never an
    # error — a continuation always legally completes a previous lead);
    # 0xC0-0xDF (2-byte leads) -> bit 0 + bit 5 (C0/C1 overlong refinement
    # happens in kUtf8Byte1Low);
    # 0xE0-0xEF (3-byte leads) -> bit 0 + bits 2/4 (E0 overlong, ED surrogate
    # refinements in kUtf8Byte1Low);
    # 0xF0-0xFF (4-byte leads) -> bit 0 + bits 3/6 (F0 overlong, F4+/F5-FF
    # too-large refinements in kUtf8Byte1Low).
    byte_1_high = [
        0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,  # 0x00 - 0x7F (ASCII)
        0x80, 0x80, 0x80, 0x80,                          # 0x80 - 0xBF (Continuation)
        0x21, 0x01, 0x15, 0x49                           # 0xC0 - 0xFF (Leading Bytes)
    ]

    # kUtf8Byte1Low: indexed by the LOW nibble of the previous byte; refines
    # the overlong/surrogate/too-large patterns per lead instance.
    #   [0] 0xE7: E0 requires A0-BF (bit 2), C0 requires error on any
    #       continuation (bit 5), F0 requires 90-BF (bit 6);
    #   [1] 0xA3: C1 overlong (bit 5);
    #   [2],[3] 0x83: C2/C3 plain 2-byte leads (no extra pattern);
    #   [4] 0x8B: F4 is a LEGAL lead with 80-8F (bit 6 cleared — U+100000
    #       ..U+10FFFF), F4 + 90-BF errors via bit 3 in kUtf8Byte2High;
    #   [5]-[C] 0xCB: E1-EC/EE/EF legal 3-byte leads;
    #   [D] 0xDB: ED carries the surrogate bit 4 (ED A0-BF = U+D800-DFFF);
    #   [E],[F] 0xCB: EE/EF legal, F5-FF too-large via bit 6 (kept).
    byte_1_low = [
        0xE7, 0xA3, 0x83, 0x83, 0x8B, 0xCB, 0xCB, 0xCB,
        0xCB, 0xCB, 0xCB, 0xCB, 0xCB, 0xDB, 0xCB, 0xCB
    ]

    # kUtf8Byte2High: indexed by the HIGH nibble of the current byte; carries
    # the bits of the error patterns this byte can COMPLETE, plus bit 7 for
    # continuation bytes (marker). ASCII 0x00-0x7F and leads C0-FF share
    # 0x01 (both can only complete the "lead + non-continuation" pattern);
    # continuation nibbles carry their pattern memberships:
    #   8x 0xE6 (F0-overlong, C0/C1-overlong, ASCII+cont, marker),
    #   9x 0xAE (E0-overlong, F4-too-large, ASCII+cont, marker),
    #   Ax 0xBA (ED-surrogate, F4/F5+ too-large, ASCII+cont, marker),
    #   Bx 0xBA (idem Ax).
    byte_2_high = [
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  # 0x00 - 0x7F (ASCII)
        0xE6, 0xAE, 0xBA, 0xBA,                          # 0x80 - 0xBF (Continuation ranges)
        0x01, 0x01, 0x01, 0x01                           # 0xC0 - 0xFF (Invalid as 2nd byte)
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