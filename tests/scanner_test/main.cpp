#ifndef __wasm__
#include <cstdio>
#include <iostream>
#include <ze_json/scanner/scanner.hpp>

constexpr const char PAYLOAD[] =
    "{\"name\":\"zuu\",\"array{},[]\":[1, 2, 3, 4, 5, 6, 7, 8, 9]}";

void
print_structural_layout() {
    for (int i = 0; i < 256; ++i) {
        if (i == 0) {
            std::cout << "{\n  ";
        } else if (i % 16 == 0) {
            std::cout << "\n  ";
        }

        switch ((char)i) {
        case ',':
        case ':':
        case '[':
        case ']':
        case '{':
        case '}':
        case '"':
        case 't':
        case 'f':
        case 'n':
            std::cout << "1, ";
            continue;
        }

        std::cout << "0, ";
    }

    std::cout << "\n}";
}

int
main() {
    std::cout << "== Payload ==\n";
    std::cout << PAYLOAD << '\n';

    uint32_t scanner_result[sizeof(PAYLOAD)]{};
    int len = scanner8(
        PAYLOAD, sizeof(PAYLOAD), scanner_result, sizeof(scanner_result));

    std::cout << "== Unvalidated Tape ==\n";
    for (int i = 0; i < len; ++i) {
        printf(
            "%03d -> %c = %d\n", i, PAYLOAD[scanner_result[i]],
            scanner_result[i]);
    }

    uint32_t validated_tape[sizeof(PAYLOAD)]{};
    len = tape_validator8(
        PAYLOAD, scanner_result, len, validated_tape, sizeof(validated_tape));

    std::cout << "== Validated Tape ==\n";
    for (int i = 0; i < len; ++i) {
        printf(
            "%03d -> %c = %d\n", i, PAYLOAD[validated_tape[i]],
            validated_tape[i]);
    }

    // print_structural_layout();

    return 0;
}

#endif // __wasm__