#pragma once

#include <QString>

// A QR code for a text, as modules: what the Configs page's "Show QR code" draws
// for a config's deep link, so that a phone can scan it in (the Android app's Scan
// QR code). Made by Nayuki's qrcodegen (vendor/qrcodegen), the library the Android
// app makes its own QR codes with.
namespace QrMatrix {

struct Matrix {
    // Modules a side, 21 to 177; 0 when the text does not fit in a QR code.
    int size = 0;
    // size * size characters, row by row from the top left: '1' for a dark module,
    // '0' for a light one. The quiet zone around the code is not in it.
    QString modules;
};

// The smallest code that holds the text's UTF-8, at the highest error correction
// that still fits that size (at least the lowest, L). A deep link with a pinned
// certificate can be too long for any QR code: then size is 0.
Matrix encode(const QString &text);

} // namespace QrMatrix
