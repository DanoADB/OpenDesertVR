// SPDX-License-Identifier: MIT
#pragma once
#include <charconv>
#include <cstdint>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace odv::research {
// Narrow DXC-text observation, NOT a general LLVM verifier or permission to
// replace a game binding. The caller must supply DXC output from the exact
// retained shader. No text, names, instructions or payload leave this helper.
// Downstream use of loaded words and frozen-payload safety remain unproven.
struct StaticCbvAccess {
    bool supported = false;
    std::uint32_t declarations = 0, descriptor_count = 0, declared_bytes = 0;
    std::uint32_t handles = 0, loads = 0, maximum_byte_end = 0;
    std::set<std::uint32_t> rows;
    const char *reason = "UNSUPPORTED";
};
namespace cbv_detail {
inline std::string trim(std::string_view s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    return begin == s.npos ? std::string{} : std::string(s.substr(begin, s.find_last_not_of(" \t\r\n") - begin + 1));
}
inline bool number(std::string_view s, std::uint32_t &out) {
    const auto parsed = std::from_chars(s.data(), s.data() + s.size(), out);
    return !s.empty() && parsed.ec == std::errc{} && parsed.ptr == s.data() + s.size();
}
inline bool i32(const std::string &s, std::uint32_t &out) {
    return s.starts_with("i32 ") && number(std::string_view(s).substr(4), out);
}
inline std::vector<std::string> fields(std::string_view s) {
    std::vector<std::string> result;
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    std::size_t start = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
            continue;
        }
        if (c == '"')
            quoted = true;
        else if (c == '(' || c == '[' || c == '{' || c == '<')
            ++depth;
        else if (c == ')' || c == ']' || c == '}' || c == '>') {
            if (!depth)
                return {};
            --depth;
        } else if (c == ',' && !depth) {
            result.push_back(trim(s.substr(start, i - start)));
            start = i + 1;
        }
        if (depth > 32 || result.size() > 256)
            return {};
    }
    if (quoted || depth)
        return {};
    result.push_back(trim(s.substr(start)));
    return result;
}
} // namespace cbv_detail

inline StaticCbvAccess observe_static_cbv(std::string_view text, std::uint32_t reg, std::uint32_t space,
                                          std::uint32_t byte_limit) {
    using namespace cbv_detail;
    StaticCbvAccess result;
    const auto fail = [&](const char *reason) {
        result.reason = reason;
        return result;
    };
    if (text.empty() || text.size() > 1024 * 1024 || !byte_limit || byte_limit > 65536)
        return fail("INPUT_BOUND");
    std::vector<std::string> lines;
    std::map<std::string, std::vector<std::string>> metadata;
    static const std::regex node(R"(^(![A-Za-z0-9_.]+) = !\{(.*)\}$)");
    for (std::size_t pos = 0; pos < text.size();) {
        auto end = text.find('\n', pos);
        if (end == text.npos)
            end = text.size();
        auto line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.size() > 16384 || lines.size() >= 32768)
            return fail("LINE_BOUND");
        std::smatch match;
        if (std::regex_match(line, match, node)) {
            if (metadata.size() >= 4096 || metadata.contains(match[1].str()))
                return fail("METADATA_BOUND");
            metadata.emplace(match[1].str(), fields(match[2].str()));
        }
        if (!line.empty() && line[0] != '!' && line[0] != ';') {
            if (const auto comment = line.find(';'); comment != line.npos)
                line.resize(comment);
            lines.push_back(trim(line));
        }
    }
    const auto get = [&](const std::string &key) -> const std::vector<std::string> & {
        static const std::vector<std::string> empty;
        auto found = metadata.find(key);
        return found == metadata.end() ? empty : found->second;
    };
    const auto &resource_ref = get("!dx.resources");
    if (resource_ref.size() != 1)
        return fail("RESOURCE_ROOT");
    const auto &resources = get(resource_ref[0]);
    if (resources.size() != 4)
        return fail("RESOURCE_LIST");
    std::uint32_t target_id = 0;
    for (const auto &reference : get(resources[2])) {
        const auto &record = get(reference);
        std::uint32_t id = 0, slot = 0, sp = 0, count = 0, bytes = 0;
        if (record.size() != 8 || !i32(record[0], id) || !i32(record[3], sp) || !i32(record[4], slot) ||
            !i32(record[5], count) || !i32(record[6], bytes))
            return fail("CBV_DECLARATION_FORMAT");
        if (sp != space || slot > reg || (count && std::uint64_t(slot) + count <= reg))
            continue;
        ++result.declarations;
        target_id = id;
        result.descriptor_count = count;
        result.declared_bytes = bytes;
        if (slot != reg || count != 1 || !bytes || bytes > byte_limit)
            return fail("CBV_NOT_BOUNDED_SINGLETON");
    }
    if (result.declarations != 1)
        return fail("TARGET_DECLARATION_NOT_UNIQUE");
    static const std::regex binding(
        R"(^(%[A-Za-z0-9_.]+) = call %dx.types.Handle @dx.op.createHandleFromBinding\(i32 217, %dx.types.ResBind \{ i32 ([0-9]+), i32 ([0-9]+), i32 ([0-9]+), i8 ([0-9]+) \}, i32 ([0-9]+), i1 (?:false|true)\)$)");
    static const std::regex legacy(
        R"(^(%[A-Za-z0-9_.]+) = call %dx.types.Handle @dx.op.createHandle\(i32 57, i8 ([0-9]+), i32 ([0-9]+), i32 ([0-9]+), i1 (?:false|true)\)$)");
    static const std::regex annotate(
        R"(^(%[A-Za-z0-9_.]+) = call %dx.types.Handle @dx.op.annotateHandle\(i32 216, %dx.types.Handle (%[A-Za-z0-9_.]+), %dx.types.ResourceProperties \{ i32 [0-9]+, i32 [0-9]+ \}\)$)");
    static const std::regex load(
        R"(^%[A-Za-z0-9_.]+ = call %dx.types.CBufRet.(?:f32|i32) @dx.op.cbufferLoadLegacy.(?:f32|i32)\(i32 59, %dx.types.Handle (%[A-Za-z0-9_.]+), i32 ([0-9]+)\)$)");
    static const std::regex token(R"(%[A-Za-z0-9_.]+)");
    std::set<std::string> handles;
    std::set<std::size_t> creations;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto &line = lines[i];
        if (line.starts_with("declare "))
            continue;
        if (line.find("@dx.op.createHandleFromHeap") != line.npos ||
            line.find("@dx.op.createHandleForLib") != line.npos)
            return fail("UNRESOLVED_HANDLE_ORIGIN");
        std::smatch match;
        if (std::regex_match(line, match, binding)) {
            std::uint32_t low = 0, high = 0, sp = 0, kind = 0, index = 0;
            if (!number(match[2].str(), low) || !number(match[3].str(), high) || !number(match[4].str(), sp) ||
                !number(match[5].str(), kind) || !number(match[6].str(), index))
                return fail("OPERAND_BOUND");
            if (kind == 2 && sp == space && low <= reg && high >= reg) {
                if (low != reg || high != reg || index != reg)
                    return fail("DESCRIPTOR_SELECTION");
                handles.insert(match[1].str());
                creations.insert(i);
            }
        } else if (std::regex_match(line, match, legacy)) {
            std::uint32_t kind = 0, id = 0, index = 0;
            if (!number(match[2].str(), kind) || !number(match[3].str(), id) || !number(match[4].str(), index))
                return fail("OPERAND_BOUND");
            if (kind == 2 && id == target_id) {
                if (index != reg)
                    return fail("DESCRIPTOR_SELECTION");
                handles.insert(match[1].str());
                creations.insert(i);
            }
        } else if (line.find("@dx.op.createHandle") != line.npos)
            return fail("UNSUPPORTED_HANDLE_CREATION");
    }
    // Find annotations to a fixed point, then inspect every use, including
    // forward references. A phi/select/unknown call involving a target handle
    // is rejected even when it appears before the handle's definition.
    bool changed = true;
    for (unsigned pass = 0; changed && pass < 64; ++pass) {
        changed = false;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            std::smatch match;
            if (std::regex_match(lines[i], match, annotate) && handles.contains(match[2].str())) {
                changed |= handles.insert(match[1].str()).second;
                creations.insert(i);
            }
        }
        if (handles.size() > 64)
            return fail("HANDLE_BOUND");
    }
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto &line = lines[i];
        if (creations.contains(i) || line.starts_with("declare "))
            continue;
        std::smatch match;
        if (std::regex_match(line, match, load) && handles.contains(match[1].str())) {
            std::uint32_t row = 0;
            if (!number(match[2].str(), row))
                return fail("ROW_FORMAT");
            const std::uint64_t end = (std::uint64_t(row) + 1) * 16;
            if (end > byte_limit || end > result.declared_bytes)
                return fail("LOAD_OUT_OF_RANGE");
            ++result.loads;
            result.rows.insert(row);
            if (end > result.maximum_byte_end)
                result.maximum_byte_end = static_cast<std::uint32_t>(end);
            continue;
        }
        for (auto it = std::sregex_iterator(line.begin(), line.end(), token); it != std::sregex_iterator(); ++it)
            if (handles.contains(it->str()))
                return fail("UNSUPPORTED_HANDLE_USE");
    }
    result.handles = static_cast<std::uint32_t>(handles.size());
    if (!result.handles || !result.loads)
        return fail("NO_TARGET_LOADS");
    result.supported = true;
    result.reason = "OBSERVED_STATIC_LEGACY_LOAD_BOUNDS";
    return result;
}
} // namespace odv::research
