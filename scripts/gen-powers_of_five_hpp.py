from pathlib import Path

OUTPUT_PATH = Path(__file__).resolve().parents[1] / 'include/ze_json/lookups/powers_of_five.hpp'
if not OUTPUT_PATH.exists():
    OUTPUT_PATH.mkdir(parents=True, exist_ok=True)

with open(OUTPUT_PATH, "w") as f:
    f.write("#pragma once\n\n")
    f.write("#include <ze_json/types/integral.hpp>\n\n")
    f.write("namespace zuu::lookups {\n\n")
    f.write("struct FloatTable { types::uint64_t high; types::uint64_t low; };\n\n")

    # 1. POSITIVE POWERS [0, 308]
    f.write("inline constexpr FloatTable kPowerOfFive128[] = {\n")
    for q in range(0, 309):
        power5 = 5**q
        while power5 < (1<<127): power5 *= 2
        while power5 >= (1<<128): power5 //= 2
        high = power5 >> 64
        low = power5 & ((1<<64)-1)
        f.write(f"    {{0x{high:016x}ULL, 0x{low:016x}ULL}}, // 5^{q}\n")
    f.write("};\n\n")

    # 2. NEGATIVE POWERS [-342, 0) (Reciprocals)
    f.write("inline constexpr FloatTable kPowerOfFive128Inverse[] = {\n")
    f.write("    {0x0ULL, 0x0ULL}, // Padding index 0\n")
    for q in range(1, 343):
        power5 = 5**q
        z = 0
        while (1<<z) < power5: z += 1
        
        # Logika dari Appendix B (Lemire Paper)
        if q <= 27:
            b = z + 127
            c = ((1 << b) // power5) + 1
        else:
            b = 2 * z + 2 * 64
            c = ((1 << b) // power5) + 1
            while c >= (1 << 128): c //= 2
            
        high = c >> 64
        low = c & ((1<<64)-1)
        f.write(f"    {{0x{high:016x}ULL, 0x{low:016x}ULL}}, // 5^-{q}\n")
    f.write("};\n\n")
    f.write("} // namespace zuu::lookups\n")

print(f"File {OUTPUT_PATH} berhasil di-generate!")