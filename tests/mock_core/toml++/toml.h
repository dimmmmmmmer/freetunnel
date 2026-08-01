// cppcheck-suppress-file missingIncludeSystem
// Stand-in for toml++ in the mock-core build.
//
// It is deliberately NOT a full TOML implementation, but it is no longer a
// no-op either: the previous version hardcoded `operator bool` to true and made
// every lookup miss, so QtTrustTunnelClient::loadConfigFromToml's two failure
// branches (parse error -> State::Error, invalid structure -> State::Error) and
// the `loglevel` readback that drives the Verbose-logs toggle were unreachable
// under test — the suite fed it the string "mock = true" and it happily
// connected.
//
// So this scanner is line-based and understands only what the client actually
// asks of it: section headers, `key = value` pairs, multi-line basic strings and
// comments. Anything it cannot make sense of is a parse error, which is what
// makes the production error paths testable.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace toml {

namespace detail {

inline std::string trim(const std::string &s)
{
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return {};
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

// Strip an unquoted trailing comment. Quotes are tracked so a '#' inside a
// value (a password, a base64 blob) is not mistaken for one.
inline std::string stripComment(const std::string &line)
{
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"' && (i == 0 || line[i - 1] != '\\'))
            inQuotes = !inQuotes;
        else if (line[i] == '#' && !inQuotes)
            return line.substr(0, i);
    }
    return line;
}

inline std::string unquote(const std::string &v)
{
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
        return v.substr(1, v.size() - 2);
    return v;
}

} // namespace detail

class node_view {
public:
    node_view() = default;
    explicit node_view(std::string raw) : m_raw(std::move(raw)), m_present(true) {}

    template <typename T>
    std::optional<T> value() const
    {
        return std::nullopt;
    }

private:
    std::string m_raw;
    bool m_present = false;
};

template <>
inline std::optional<std::string> node_view::value<std::string>() const
{
    if (!m_present)
        return std::nullopt;
    return detail::unquote(m_raw);
}

template <>
inline std::optional<bool> node_view::value<bool>() const
{
    if (!m_present)
        return std::nullopt;
    if (m_raw == "true")
        return true;
    if (m_raw == "false")
        return false;
    return std::nullopt;
}

class table {
public:
    node_view operator[](std::string_view key) const
    {
        const auto it = m_values.find(std::string(key));
        if (it == m_values.end())
            return {};
        return node_view(it->second);
    }

    bool contains(std::string_view key) const
    {
        return m_values.find(std::string(key)) != m_values.end();
    }

    bool empty() const { return m_values.empty(); }

    void insert(const std::string &key, const std::string &value) { m_values[key] = value; }

private:
    std::map<std::string, std::string> m_values;
};

struct parse_error {
    std::string_view description() const { return m_description; }
    std::string m_description;
};

class parse_result {
public:
    parse_result() = default;
    explicit parse_result(std::string error) : m_error{std::move(error)}, m_ok(false) {}

    explicit operator bool() const { return m_ok; }
    parse_error error() const { return m_error; }
    toml::table &table() { return m_table; }
    const toml::table &table() const { return m_table; }

    void insert(const std::string &key, const std::string &value) { m_table.insert(key, value); }

private:
    toml::table m_table;
    parse_error m_error;
    bool m_ok = true;
};

inline parse_result parse(const std::string &content)
{
    parse_result result;
    std::string section;
    bool inMultiline = false;
    std::string multilineKey;
    size_t lineNo = 0;

    size_t pos = 0;
    while (pos <= content.size()) {
        const size_t nl = content.find('\n', pos);
        const std::string rawLine =
                content.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? content.size() + 1 : nl + 1;
        ++lineNo;

        if (inMultiline) {
            // Inside a """ block: only its terminator matters.
            if (rawLine.find("\"\"\"") != std::string::npos)
                inMultiline = false;
            continue;
        }

        const std::string line = detail::trim(detail::stripComment(rawLine));
        if (line.empty())
            continue;

        if (line.front() == '[') {
            if (line.back() != ']')
                return parse_result("unterminated table header at line " + std::to_string(lineNo));
            section = line.substr(1, line.size() - 2);
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            return parse_result("expected a key/value pair at line " + std::to_string(lineNo));

        const std::string key = detail::trim(line.substr(0, eq));
        const std::string value = detail::trim(line.substr(eq + 1));
        if (key.empty())
            return parse_result("empty key at line " + std::to_string(lineNo));

        if (value.rfind("\"\"\"", 0) == 0) {
            // Opening a multi-line basic string; it may also close on this line.
            const bool closesHere = value.size() > 3 && value.find("\"\"\"", 3) != std::string::npos;
            inMultiline = !closesHere;
            multilineKey = key;
            result.insert(key, "");
            if (!section.empty())
                result.insert(section + "." + key, "");
            continue;
        }

        // An odd number of unescaped quotes means the string never closes.
        size_t quotes = 0;
        for (size_t i = 0; i < value.size(); ++i) {
            if (value[i] == '"' && (i == 0 || value[i - 1] != '\\'))
                ++quotes;
        }
        if (quotes % 2 != 0)
            return parse_result("unterminated string at line " + std::to_string(lineNo));

        result.insert(key, value);
        if (!section.empty())
            result.insert(section + "." + key, value);
    }

    if (inMultiline)
        return parse_result("unterminated multi-line string for key '" + multilineKey + "'");
    if (result.table().empty())
        return parse_result("config is empty");
    return result;
}

inline parse_result parse_file(const std::string &)
{
    return {};
}

} // namespace toml
