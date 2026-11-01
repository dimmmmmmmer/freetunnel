// cppcheck-suppress-file missingIncludeSystem
#pragma once

// Geo lists for split tunnelling: geosite/geoip .dat files (the V2Ray format)
// and plain text lists, turned into the entries the VPN core's exclusion list
// takes. Pure functions: downloading and caching the files is GeoListUpdater's.
//
// A list is named by one string, its "source": the address of the file, and for
// a .dat file the category to take from it after a '#', as in
//   https://github.com/runetfreedom/russia-blocked-geoip/releases/latest/download/geoip.dat#ru-blocked
// The fragment never reaches the server; it only says which part of the file is
// meant. Any other address is read as a text list, one entry per line.

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <string>
#include <vector>

namespace freetunnel {

// What a list holds, which decides how a .dat file is read: geosite files hold
// domains, geoip files subnets. A text list may hold either, whatever its kind.
enum class GeoListKind { Sites, Ips };

// Which way a list sends what it names. A list that takes traffic OUT of the
// tunnel is held to more: it may not name a subnet so wide that it would take
// out most of the internet with it (see isTooWideToBypass).
enum class GeoDirection { ThroughVpn, Bypass };

struct GeoSource {
    QUrl file;        // the address, without the fragment
    QString category; // from the fragment; required for a .dat file
    bool dat = false; // the address's path ends in .dat
    bool valid() const { return file.isValid() && !file.isEmpty(); }
};

// Reads a source string. Invalid (valid() false) when it is not an https
// address, or names a .dat file without a category; *why then says which.
GeoSource parseGeoSource(const QString &spec, QString *why = nullptr);

// What reading one list gave.
struct GeoListStats {
    qsizetype entries = 0; // core entries produced (a domain with its subdomains counts once)
    qsizetype skipped = 0; // entries the core cannot use: keywords, regular expressions, bad names
};

// Appends the core's exclusion entries for one list file to *out. A domain with
// its subdomains becomes "example.com" and "*.example.com"; an exact domain
// ("full:" in a text list, Full in a .dat) only the first; a subnet its
// normalised CIDR form. False, with the reason in *error, when the data is not a
// list of that kind, or a .dat file has no such category; nothing is appended
// then. A list in which nothing can be used is an error too: an empty answer is
// far likelier a broken download than a list meant to be empty.
bool appendGeoEntries(const QByteArray &data, const GeoSource &source, GeoListKind kind,
                      GeoDirection direction, std::vector<std::string> *out,
                      GeoListStats *stats, QString *error);

// The entries as the core's exclusion list takes them: duplicates dropped, one
// per line, in a stable order so that the same lists always give the same text.
std::string joinGeoEntries(std::vector<std::string> entries);

// A subnet that, taken out of the tunnel, would take most of the internet with
// it: an IPv4 prefix shorter than /8, an IPv6 one shorter than /16.
bool isTooWideToBypass(const QString &cidr);

} // namespace freetunnel
