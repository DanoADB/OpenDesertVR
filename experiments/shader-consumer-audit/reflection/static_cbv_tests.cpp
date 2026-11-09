// SPDX-License-Identifier: MIT
#include "../static_cbv_access.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

static void require(bool ok) {
    if (!ok)
        throw std::runtime_error("owned_static_cbv_control_failed");
}
int main(int argc, char **argv) {
    try {
        if (argc == 2) {
            // Owned SDK disassembly only in the qualification harness. Game
            // analysis calls the header in-process, never this file interface.
            std::ifstream input(argv[1], std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(input)), {});
            auto result = odv::research::observe_static_cbv(text, 18, 35, 2816);
            std::cout << result.reason << " loads=" << result.loads << " end=" << result.maximum_byte_end << '\n';
            return result.supported ? 0 : 3;
        }
        const std::string metadata = R"(
!dx.resources = !{!4}
!4 = !{null, null, !5, null}
!5 = !{!6}
!6 = !{i32 0, %Owned* undef, !"", i32 35, i32 18, i32 1, i32 2816, null}
)";
        const std::string create =
            "%1 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 2, i32 0, i32 18, i1 false)\n";
        const std::string load =
            "%2 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %1, i32 175)\n";
        auto inspect = [&](const std::string &body) {
            return odv::research::observe_static_cbv(metadata + body, 18, 35, 2816);
        };
        auto result = inspect(create + load);
        require(result.supported && result.loads == 1 && result.maximum_byte_end == 2816 && result.rows.contains(175));
        std::uint32_t signless = 0;
        require(odv::research::cbv_detail::i32("i32 -1", signless) && signless == 0xffffffffu);
        require(odv::research::cbv_detail::i32("i32 -2147483648", signless) && signless == 0x80000000u);
        require(!odv::research::cbv_detail::i32("i32 -2147483649", signless));
        auto extra = metadata;
        extra.replace(extra.find("!5 = !{!6}"), 10, "!5 = !{!6, !7}");
        extra += "!7 = !{i32 1, %Other* undef, !\"\", i32 -2147483648, i32 0, i32 -1, i32 16, null}\n";
        require(odv::research::observe_static_cbv(extra + create + load, 18, 35, 2816).supported);
        require(inspect(create +
                        "%srv = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 0, i32 %index, i1 true)\n" +
                        load)
                    .supported);
        require(!inspect("%1 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 2, i32 0, i32 %index, i1 true)\n" +
                         load)
                     .supported);
        require(
            !inspect(
                 create +
                 "%2 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %1, i32 176)\n")
                 .supported);
        require(!inspect(create + "%2 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, "
                                  "%dx.types.Handle %1, i32 %dynamic)\n")
                     .supported);
        require(
            !inspect("%x = select i1 true, %dx.types.Handle %1, %dx.types.Handle %other\n" + create + load).supported);
        require(!inspect(create + load + "%x = call i32 @unknown(%dx.types.Handle %1)\n").supported);
        require(!inspect(create + load +
                         "%x = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)\n")
                     .supported);
        auto array = metadata;
        array.replace(array.find("i32 18, i32 1"), 13, "i32 18, i32 2");
        require(!odv::research::observe_static_cbv(array + create + load, 18, 35, 2816).supported);
        require(!odv::research::observe_static_cbv(std::string(1024 * 1024 + 1, ' '), 18, 35, 2816).supported);
        const std::string modern = "%1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, "
                                   "%dx.types.ResBind { i32 18, i32 18, i32 35, i8 2 }, i32 18, i1 false)\n";
        const std::string annotation = "%3 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %1, "
                                       "%dx.types.ResourceProperties { i32 13, i32 2816 })\n";
        auto annotated = load;
        annotated.replace(annotated.find("Handle %1"), 9, "Handle %3");
        require(inspect(modern + annotation + annotated).supported);
        require(!inspect(modern + annotation + annotated + "%4 = phi %dx.types.Handle [ %3, %entry ]\n").supported);
        require(!inspect(create + "%2 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, "
                                  "%dx.types.Handle %1, i32 42949672960)\n")
                     .supported);
        std::cout << "PASS: bounded singleton loads, SM6.0/6.6 handles; dynamic/OOB/array/unknown/forward uses "
                     "rejected; downstream consumption and frozen safety UNVERIFIED\n";
        return 0;
    } catch (const std::exception &) {
        std::cerr << "owned_static_cbv_control_failed\n";
        return 3;
    }
}
