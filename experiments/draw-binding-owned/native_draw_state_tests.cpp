// SPDX-License-Identifier: MIT
#include "../renodx-owned/native_draw_events.hpp"
#include "draw_recording_state.hpp"
#include <iostream>
#include <stdexcept>
static void require(bool value) {
    if (!value)
        throw std::runtime_error("native_draw_metadata_control_failed");
}
int main() {
    try {
        odv::research::DrawRecordingState state;
        // Synthetic identities only; never dereferenced or sent to a device.
        auto *heap = reinterpret_cast<ID3D12QueryHeap *>(std::uintptr_t{123});
        require(!state.eligible());
        require(!state.eligible_single_forward());
        state.reset();
        require(!state.eligible()); // Missing native-wrapper capability proof.
        state.capabilities(odv::research::native_draw_capabilities);
        require(state.eligible());
        state.query(heap, D3D12_QUERY_TYPE_TIMESTAMP, 0, false);
        require(state.eligible());
        state.query(heap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0, true);
        require(!state.eligible() && state.active_queries() == 1);
        require(state.eligible_single_forward());
        state.query(heap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0, false);
        require(state.eligible());
        state.query(heap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0, false);
        require(state.failed() && !state.eligible());
        require(!state.eligible_single_forward());
        state.reset();
        for (UINT i = 0; i < 64; ++i)
            state.query(heap, D3D12_QUERY_TYPE_OCCLUSION, i, true);
        require(!state.failed() && state.active_queries() == 64);
        state.query(heap, D3D12_QUERY_TYPE_OCCLUSION, 64, true);
        require(state.failed());
        state.reset();
        state.query(heap, D3D12_QUERY_TYPE_OCCLUSION, 1, true);
        state.query(heap, D3D12_QUERY_TYPE_OCCLUSION, 1, true);
        require(state.failed());
        state.reset();
        const D3D12_STREAM_OUTPUT_BUFFER_VIEW view{256, 32, 512};
        state.stream_output(2, 1, &view);
        require(state.eligible() && state.stream_outputs()[2].BufferLocation == 256 &&
                state.stream_outputs()[1].SizeInBytes == 0);
        state.stream_output(2, 1, nullptr);
        require(state.eligible() && state.stream_outputs()[2].SizeInBytes == 0);
        state.stream_output(4, 1, &view);
        require(state.failed());
        state.reset();
        state.stream_output(0, 0, nullptr); // Ambiguous zero-count semantics reject.
        require(state.failed());
        state.reset();
        state.render_pass(true);
        require(!state.eligible() && state.render_pass_blocked());
        require(!state.eligible_single_forward());
        state.render_pass(false);
        require(state.eligible());
        state.render_pass(true, D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS);
        state.render_pass(false);
        require(!state.eligible() && state.render_pass_blocked());
        state.render_pass(true, D3D12_RENDER_PASS_FLAG_RESUMING_PASS);
        state.render_pass(false);
        require(state.eligible());
        state.render_pass(false);
        require(state.failed());
        state.reset();
        state.invalidate(); // Bundle/ClearState/protected-session uncertainty.
        require(!state.eligible());
        state.reset();
        require(state.eligible());
        std::cout << "PASS: bounded synthetic native-draw state metadata; queries, SO, render-pass suspension, "
                     "invalidation and capability gates; native_wrapper=NOT_RUN; game=NOT_RUN\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
