// SPDX-License-Identifier: MIT
#pragma once
#include <array>
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
    std::uint32_t malformed_record_fields = 0, malformed_record_integer_mask = 0;
    std::set<std::uint32_t> rows;
    // Conservative SSA value dependencies only. Includes both phi/select
    // alternatives, not control dependencies or proof of executed contribution.
    bool position_dependencies_observed = false;
    std::array<std::set<std::uint32_t>, 4> position_rows;
    std::array<std::set<std::uint32_t>, 4> position_words;
    const char *position_reason = "NOT_OBSERVED";
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
    if (!s.starts_with("i32 "))
        return false;
    const auto value = std::string_view(s).substr(4);
    if (!value.starts_with("-"))
        return number(value, out);
    // LLVM integers are signless. DXC prints high-bit spaces/unbounded
    // descriptor counts as negative decimal literals; preserve all 32 bits.
    std::int32_t signed_value = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), signed_value);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        return false;
    out = static_cast<std::uint32_t>(signed_value);
    return true;
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

namespace cbv_detail {
inline void observe_position_dependencies(StaticCbvAccess &result, const std::vector<std::string> &lines,
                                          const std::map<std::string, std::vector<std::string>> &metadata,
                                          const std::map<std::string, std::uint32_t> &target_loads) {
    const auto reject = [&](const char *reason) { result.position_reason = reason; };
    const auto get = [&](const std::string &key) -> const std::vector<std::string> & {
        static const std::vector<std::string> empty;
        const auto found = metadata.find(key);
        return found == metadata.end() ? empty : found->second;
    };
    const auto &models = get("!dx.shaderModel"), &entries = get("!dx.entryPoints");
    if (models.size() != 1 || entries.size() != 1)
        return reject("POSITION_METADATA_UNSUPPORTED");
    const auto &model = get(models[0]), &entry = get(entries[0]);
    if (model.size() != 3 || model[0] != "!\"vs\"" || entry.size() != 5)
        return reject("NOT_SINGLE_VERTEX_ENTRY");
    const auto &signature = get(entry[2]);
    if (signature.size() != 3)
        return reject("POSITION_SIGNATURE_UNSUPPORTED");
    std::uint32_t position = 0;
    unsigned positions = 0;
    for (const auto &reference : get(signature[1])) {
        const auto &element = get(reference);
        if (element.size() != 11)
            return reject("POSITION_SIGNATURE_UNSUPPORTED");
        if (element[1] != "!\"SV_Position\"" && element[1] != "!\"SV_POSITION\"")
            continue;
        if (element[2] != "i8 9" || element[3] != "i8 3" || element[6] != "i32 1" || element[7] != "i8 4" ||
            !i32(element[0], position))
            return reject("POSITION_SIGNATURE_UNSUPPORTED");
        ++positions;
    }
    if (positions != 1)
        return reject("POSITION_NOT_UNIQUE");
    static const std::regex assignment(R"(^(%[A-Za-z0-9_.]+) = (.*)$)");
    static const std::regex token(R"(%[A-Za-z0-9_.]+)");
    static const std::regex store(
        R"(^call void @dx.op.storeOutput\.f32\(i32 5, i32 ([0-9]+), i32 ([0-9]+), i8 ([0-9]+), float ([A-Za-z0-9_%+.-]+)\)(?:,.*)?$)");
    std::map<std::string, std::string> definitions;
    std::array<std::string, 4> roots;
    unsigned functions = 0;
    for (const auto &line : lines) {
        if (line.starts_with("define "))
            ++functions;
        std::smatch match;
        if (std::regex_match(line, match, assignment)) {
            if (definitions.size() >= 8192 || !definitions.emplace(match[1].str(), match[2].str()).second)
                return reject("SSA_DEFINITION_BOUND_OR_DUPLICATE");
        } else if (std::regex_match(line, match, store)) {
            std::uint32_t id = 0, row = 0, column = 0;
            if (!number(match[1].str(), id) || !number(match[2].str(), row) || !number(match[3].str(), column))
                return reject("POSITION_STORE_UNSUPPORTED");
            if (id == position) {
                if (row != 0 || column > 3 || !roots[column].empty())
                    return reject("POSITION_STORE_NOT_UNIQUE");
                roots[column] = match[4].str();
            }
        } else if (!line.starts_with("declare ") && line.find("@dx.op.storeOutput.f32") != line.npos) {
            return reject("POSITION_STORE_UNSUPPORTED");
        }
    }
    if (functions != 1)
        return reject("NOT_SINGLE_FUNCTION");
    std::array<std::set<std::uint32_t>, 4> rows;
    std::array<std::set<std::uint32_t>, 4> words;
    std::size_t edges = 0;
    for (unsigned column = 0; column < 4; ++column) {
        if (roots[column].empty())
            return reject("POSITION_STORE_MISSING");
        std::vector<std::string> pending{roots[column]};
        std::set<std::string> visited;
        while (!pending.empty()) {
            auto name = std::move(pending.back());
            pending.pop_back();
            if (!name.starts_with('%') || !visited.insert(name).second)
                continue;
            if (const auto load = target_loads.find(name); load != target_loads.end()) {
                return reject("TARGET_AGGREGATE_USE_UNSUPPORTED");
            }
            const auto found = definitions.find(name);
            if (found == definitions.end())
                return reject("SSA_VALUE_UNRESOLVED");
            const auto &rhs = found->second;
            const auto op = rhs.substr(0, rhs.find(' '));
            if (op == "extractvalue") {
                static const std::regex extract(
                    R"(^extractvalue %dx.types.CBufRet\.(?:f32|i32) (%[A-Za-z0-9_.]+), ([0-9]+)$)");
                std::smatch match;
                if (std::regex_match(rhs, match, extract)) {
                    const auto load = target_loads.find(match[1].str());
                    if (load != target_loads.end()) {
                        std::uint32_t component = 0;
                        if (!number(match[2].str(), component) || component > 3)
                            return reject("TARGET_COMPONENT_UNSUPPORTED");
                        rows[column].insert(load->second);
                        words[column].insert(load->second * 4 + component);
                        continue;
                    }
                }
            }
            static const std::set<std::string> allowed{
                "extractvalue", "fadd",   "fsub",   "fmul", "fdiv", "fneg",  "select", "phi",  "bitcast", "uitofp",
                "sitofp",       "fptoui", "fptosi", "zext", "sext", "trunc", "shl",    "lshr", "ashr",    "and",
                "or",           "xor",    "icmp",   "fcmp", "add",  "sub",   "mul",    "udiv", "sdiv",    "call"};
            if (!allowed.contains(op) || (op == "call" && rhs.find("@dx.op.") == rhs.npos))
                return reject("SSA_OPERATION_UNSUPPORTED");
            std::set<std::string> labels;
            if (op == "phi") {
                static const std::regex label(R"(,\s*(%[A-Za-z0-9_.]+)\s*\])");
                for (auto it = std::sregex_iterator(rhs.begin(), rhs.end(), label); it != std::sregex_iterator(); ++it)
                    labels.insert((*it)[1].str());
            }
            for (auto it = std::sregex_iterator(rhs.begin(), rhs.end(), token); it != std::sregex_iterator(); ++it) {
                auto dependency = it->str();
                if (dependency.starts_with("%dx.types."))
                    continue; // DXIL aggregate type, not an SSA value.
                if (++edges > 65536)
                    return reject("SSA_EDGE_BOUND");
                // A phi's block labels do not contribute numeric values.
                if (op == "phi" && labels.contains(dependency))
                    continue;
                pending.push_back(std::move(dependency));
            }
        }
    }
    result.position_rows = std::move(rows);
    result.position_words = std::move(words);
    result.position_dependencies_observed = true;
    result.position_reason = "POTENTIAL_SSA_VALUE_DEPENDENCIES";
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
        if (record.size() != 8) {
            result.malformed_record_fields = static_cast<std::uint32_t>(record.size());
            return fail("CBV_DECLARATION_FORMAT");
        }
        const bool known_id = i32(record[0], id), known_space = i32(record[3], sp), known_slot = i32(record[4], slot),
                   known_count = i32(record[5], count);
        if (!known_id || !known_space || !known_slot || !known_count) {
            result.malformed_record_fields = 8;
            result.malformed_record_integer_mask =
                (known_id ? 1u : 0u) | (known_space ? 2u : 0u) | (known_slot ? 4u : 0u) | (known_count ? 8u : 0u);
            return fail("CBV_DECLARATION_FORMAT");
        }
        if (sp != space || slot > reg || (count && std::uint64_t(slot) + count <= reg))
            continue;
        if (!i32(record[6], bytes))
            return fail("TARGET_SIZE_FORMAT");
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
        R"(^(%[A-Za-z0-9_.]+) = call %dx.types.Handle @dx.op.createHandleFromBinding\(i32 217, %dx.types.ResBind \{ i32 (-?[0-9]+), i32 (-?[0-9]+), i32 (-?[0-9]+), i8 ([0-9]+) \}, i32 ([A-Za-z0-9_%.-]+), i1 (?:false|true)\)$)");
    static const std::regex legacy(
        R"(^(%[A-Za-z0-9_.]+) = call %dx.types.Handle @dx.op.createHandle\(i32 57, i8 ([0-9]+), i32 ([0-9]+), i32 ([A-Za-z0-9_%.-]+), i1 (?:false|true)\)$)");
    static const std::regex annotate(
        R"(^(%[A-Za-z0-9_.]+) = call %dx.types.Handle @dx.op.annotateHandle\(i32 216, %dx.types.Handle (%[A-Za-z0-9_.]+), %dx.types.ResourceProperties \{ i32 [0-9]+, i32 [0-9]+ \}\)$)");
    static const std::regex load(
        R"(^%[A-Za-z0-9_.]+ = call %dx.types.CBufRet.(?:f32|i32) @dx.op.cbufferLoadLegacy.(?:f32|i32)\(i32 59, %dx.types.Handle (%[A-Za-z0-9_.]+), i32 ([0-9]+)\)$)");
    static const std::regex token(R"(%[A-Za-z0-9_.]+)");
    std::set<std::string> handles;
    std::map<std::string, std::uint32_t> target_loads;
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
            if (!i32("i32 " + match[2].str(), low) || !i32("i32 " + match[3].str(), high) ||
                !i32("i32 " + match[4].str(), sp) || !number(match[5].str(), kind))
                return fail("OPERAND_BOUND");
            if (kind == 2 && sp == space && low <= reg && high >= reg) {
                if (low != reg || high != reg || !number(match[6].str(), index) || index != reg)
                    return fail("DESCRIPTOR_SELECTION");
                handles.insert(match[1].str());
                creations.insert(i);
            }
        } else if (std::regex_match(line, match, legacy)) {
            std::uint32_t kind = 0, id = 0, index = 0;
            if (!number(match[2].str(), kind) || !number(match[3].str(), id))
                return fail("OPERAND_BOUND");
            if (kind == 2 && id == target_id) {
                if (!number(match[4].str(), index) || index != reg)
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
            target_loads.emplace(line.substr(0, line.find(" = ")), row);
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
    observe_position_dependencies(result, lines, metadata, target_loads);
    return result;
}
} // namespace odv::research
