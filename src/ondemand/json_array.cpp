#include <ze_json/ondemand/json_array.hpp>

namespace zuu::ondemand {

types::size_t
JsonArray::count_elements() NOEXCEPT {
    // Snapshot kursor: penghitungan bersifat inspeksi. Karena JsonIterator
    // hanya memuat pointer/bilangan, penyalinan struct cukup dan murah.
    JsonIterator snapshot = iter_;

    types::size_t count = 0;
    for (;;) {
        char c = iter_.peek_char();

        if (c == ',') {
            // Angka non-terakhir: kursor menunjuk ',' yang sekaligus menjadi
            // terminator elemen angka (0 entry). Satu langkah melintasinya
            // sudah mendarat DI TERMINATOR ELEMEN BERIKUTNYA (',' berikutnya
            // ATAU bracket/quote elemen non-angka) — tidak ada separator
            // terpisah untuk di-skip, jadi lanjutkan tanpa langkah bawah.
            count++;
            iter_.advance();
            continue;
        } else if (c == '\"') {
            // String = tepat 2 entry (quote pembuka & penutup)
            count++;
            iter_.advance();
            iter_.advance();
        } else if (c == 't' || c == 'f' || c == 'n') {
            // Literal true/false/null = 1 entry
            count++;
            iter_.advance();
        } else if (c == '{' || c == '[') {
            // Kontainer = walk bracket depth sampai pasangan penutupnya
            count++;
            iter_.skip_child();
        } else {
            // c == ']' (atau '\0' yang jahat). ']' bisa berarti:
            //   (a) array benar-benar habis, atau
            //   (b) terminator elemen angka TERAKHIR.
            // Bedakan lewat teks payload di antara entry sebelumnya dan
            // entry ']': hanya-whitespace berarti kasus (a).
            if (iter_.index() >= 1) {
                const types::uint64_t prev_pos  = iter_.peek_prev_position();
                const types::uint64_t close_pos = iter_.peek_position();

                const char* p   = iter_.src() + prev_pos + 1;
                const char* end = iter_.src() + close_pos;
                for (; p < end; ++p) {
                    if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                        count++;
                        break;
                    }
                }
            }
            break;
        }

        // Setelah elemen non-angka, kursor berada di separator ',' atau
        // langsung di ']'. Lewati separator agar loop berikutnya mulai
        // dari posisi elemen berikutnya.
        if (iter_.peek_char() == ',') {
            iter_.advance();
        }
    }

    iter_ = snapshot;
    return count;
}

} // namespace zuu::ondemand
