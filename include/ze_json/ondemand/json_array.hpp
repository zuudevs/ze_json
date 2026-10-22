#pragma once

#include <ze_json/ondemand/json_object.hpp>

namespace zuu::ondemand {

// On-demand iterator atas elemen array pada SATU kursor forward-only.
// Konstruksi menghabiskan '[' sehingga kursor tepat berada di posisi
// elemen pertama (atau ']' untuk array kosong).
class JsonArray {
  private:
    JsonIterator& iter_;

  public:
    constexpr explicit JsonArray(JsonIterator& iter) NOEXCEPT : iter_(iter) {
        iter_.advance(); // Lewati '['
    }

    // Hitung jumlah elemen top-level array TANPA memindahkan kursor
    // (state kursor di-snapshot, dihitung, lalu di-restore).
    //
    // Elemen angka menempati 0 entry tape, sehingga posisi kursor elemen
    // angka justru berupa karakter struktural PENERUSNYA (',' atau ']').
    // Kasus '[1]' vs '[]' menghasilkan tape yang sama persis; keduanya
    // dibedakan melalui teks payload di antara entry sebelumnya dan entry
    // penutup: hanya-whitespace berarti array memang kosong.
    [[nodiscard]] types::size_t
    count_elements() NOEXCEPT;
};

} // namespace zuu::ondemand
