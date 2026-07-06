// cppcheck-suppress-file missingIncludeSystem
// Tiny stand-in for toml++ used by the mock-core build: the mock
// TrustTunnelConfig::build_config ignores the parsed table entirely, so
// "parsing" always succeeds and lookups always miss.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace toml {

class node_view {
public:
    template <typename T>
    std::optional<T> value() const
    {
        return std::nullopt;
    }
};

class table {
public:
    node_view operator[](std::string_view) const { return {}; }
};

struct parse_error {
    std::string_view description() const { return {}; }
};

class parse_result {
public:
    explicit operator bool() const { return true; }
    parse_error error() const { return {}; }
    toml::table &table() { return m_table; }
    const toml::table &table() const { return m_table; }

private:
    toml::table m_table;
};

inline parse_result parse_file(const std::string &)
{
    return {};
}

inline parse_result parse(const std::string &)
{
    return {};
}

} // namespace toml
