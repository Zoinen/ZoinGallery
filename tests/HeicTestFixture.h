#ifndef ZOINGALLERY_HEICTESTFIXTURE_H
#define ZOINGALLERY_HEICTESTFIXTURE_H

#include <QByteArray>

namespace ZoinGalleryTest {

inline QByteArray portraitHeicFixture() {
    // 48x64 HEVC-backed HEIF generated once with macOS ImageIO (`sips -s
    // format heic`) from a synthetic two-color ImageMagick gradient. It is
    // embedded so decoder/provider regressions remain hermetic and do not
    // depend on platform image writers or a developer's photo library.
    return QByteArray::fromBase64(QByteArrayLiteral(
        "AAAAJGZ0eXBoZWl4AAAAAG1pZjFNaVByTWlIQW1pYWZoZWl4AAABh21ldGEAAAAA"
        "AAAAIWhkbHIAAAAAAAAAAHBpY3QAAAAAAAAAAAAAAAAAAAAAJGRpbmYAAAAcZHJl"
        "ZgAAAAAAAAABAAAADHVybCAAAAABAAAADnBpdG0AAAAAAAEAAAAjaWluZgAAAAAA"
        "AQAAABVpbmZlAgAAAAABAABodmMxAAAAAOdpcHJwAAAAxmlwY28AAAATY29scm5j"
        "bHgAAgACAAaAAAAADGNsbGkAywBAAAAAFGlzcGUAAAAAAAAAMAAAAEAAAAAJaXJv"
        "dAAAAAAQcGl4aQAAAAADCgoKAAAAcmh2Y0MBAiAAAACwAAAAAAAe8AD8/fr6AAAL"
        "A6AAAQAYQAEMAf//AiAAAAMAsAAAAwAAAwAeFwJAoQABACNCAQECIAAAAwCwAAAD"
        "AAADAB6gFCBBwc7YgXuRZVNwICBgCKIAAQAJRAHAYJyyERTZAAAAGWlwbWEAAAAA"
        "AAAAAQABBoECAwWGhAAAAB5pbG9jAAAAAEQAAAEAAQAAAAEAAAG7AAAAkgAAAAFt"
        "ZGF0AAAAAAAAAKIAAACOKAGvo9OAeEurFCgSka1v2LY2ffvsbWev7xIx8XC75NrH"
        "U7DYMR7J11WaVVVNTGN8y3KnfnlMBW7PBgV25sBRG70zrkn5nwTu15//t6+8Ye4"
        "H7P5H4RbTAM/0VGeIeyfQXB5Ztfs7LCdm+NrbbmULyQ/j70kuh3StoJkJ0n4Me9"
        "X8c58Wv9vsg/77MTc/gA=="));
}

inline QByteArray embeddedThumbnailHeicFixture() {
    // 128x96 HEVC-backed HEIF with a 32x24 embedded thumbnail. Generated once
    // with `heif-enc -q 45 -t 32` from a synthetic gradient. Keeping this
    // fixture in-tree makes the embedded-preview path testable without a
    // platform image writer or Qt's optional JPEG image plugin.
    return QByteArray::fromBase64(QByteArrayLiteral(
        "AAAAHGZ0eXBoZWl4AAAAAG1pZjFoZWl4bWlhZgAAAkxtZXRhAAAAAAAAACFoZGxy"
        "AAAAAAAAAABwaWN0AAAAAAAAAAAAAAAAAAAAADRpbG9jAAAAAERAAAIAAQAAAAAC"
        "cAABAAAAAAAAAE8AAgAAAAACvwABAAAAAAAAAEEAAAA4aWluZgAAAAAAAgAAABVp"
        "bmZlAgAAAAABAABodmMxAAAAABVpbmZlAgAAAAACAABodmMxAAAAAA5waXRtAAAA"
        "AAABAAABi2lwcnAAAAFkaXBjbwAAAHZodmNDAQQIAAAAAAAAAAAAHvAA/P36+gAA"
        "DwNgAAEAF0ABDAH//wQIAAADAJ24AAADAAAeugJAYQABACpCAQEECAAAAwCduAAA"
        "AwAAHqAQIGE2W6kkprm4CGgwIAAAAwMgAAADACFiAAEAB0QBwXKwIkAAAAATY29s"
        "cm5jbHgAAQANAAaAAAAAFGlzcGUAAAAAAAAAgAAAAGAAAAAOcGl4aQAAAAABCgAA"
        "AHVodmNDAQQIAAAAAAAAAAAAHvAA/P36+gAADwNgAAEAF0ABDAH//wQIAAADAJ24"
        "AAADAAAeugJAYQABACpCAQEECAAAAwCduAAAAwAAHqAggQTZbqrprm4CGgwIAAAD"
        "AMgAAAMACEBiAAEABkQBwXPBiQAAABRpc3BlAAAAAAAAAEAAAABAAAAAKGNsYXAA"
        "AAAgAAAAAQAAABgAAAAB////4AAAAAL////YAAAAAgAAAB9pcG1hAAAAAAAAAAIA"
        "AQSBAgMEAAIFhQIGBIcAAAAaaXJlZgAAAAAAAAAOdGhtYgACAAEAAQAAAJhtZGF0"
        "AAAASygBry7rCjpoBMek/PgRRO7n3j9g138VDMYuJu+7lzcvCzan17RgyZCVfvuS"
        "naOmuv6HMyLspoGBB+bVwqjA+S5QaMlNL6sVmKSTsAAAAD0oAa8shZwX9w7QJpw7"
        "d9wVvvFbHB9tVnwRlpT0f1qUx/gyXof5KkaRU6vUNOrEzlfeKJ/ITlx5SIiwcqtw"));
}

} // namespace ZoinGalleryTest

#endif // ZOINGALLERY_HEICTESTFIXTURE_H
