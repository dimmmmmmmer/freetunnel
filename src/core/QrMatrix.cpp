#include "core/QrMatrix.h"

#include "../../vendor/qrcodegen/qrcodegen.hpp"

#include <QByteArray>

namespace QrMatrix {

Matrix encode(const QString &text)
{
    Matrix out;
    const QByteArray utf8 = text.toUtf8();
    // encodeText reads a C string: a NUL would end the text early, and the code
    // would then hold less than it was asked to.
    if (utf8.isEmpty() || utf8.contains('\0'))
        return out;
    try {
        // L, and boostEcl (encodeText's default) raises it where the version that
        // L needs has room for more: a short link gets a sturdier code, and a long
        // one, with a certificate in it, still fits.
        const qrcodegen::QrCode code = qrcodegen::QrCode::encodeText(utf8.constData(), qrcodegen::QrCode::Ecc::LOW);
        const int size = code.getSize();
        QString modules;
        modules.reserve(size * size);
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                modules.append(code.getModule(x, y) ? QLatin1Char('1') : QLatin1Char('0'));
        out.size = size;
        out.modules = modules;
    } catch (const qrcodegen::data_too_long &) {
        // More than version 40 holds (2953 bytes at L): no code, and the caller says so.
    }
    return out;
}

} // namespace QrMatrix
