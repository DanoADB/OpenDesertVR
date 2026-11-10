// SPDX-License-Identifier: MIT
// Included after the pinned Devkit's command-list/device state definitions.
// Narrow diagnostic only; it does not implement a camera or stereo adapter.
// Native payload, forwarding and compiler headers are included at file scope,
// before the upstream addon's anonymous namespace.

bool odv_clip_enabled = false;
bool odv_clip_large = false; // Read once before publishing a nonzero mode.
bool odv_clip_collapse = false; // Scale-independent influence diagnostic only.
bool odv_clip_work_enabled = false;
struct ODVClipWorkSample {
  odv::research::ConstantCaptureDriver::Ticket ticket;
  unsigned mode = 0;
  D3D12_QUERY_TYPE type{};
  UINT maximum_count = 0;
};
std::mutex odv_clip_work_mutex;
std::array<ODVClipWorkSample, 6> odv_clip_work_samples{};
std::atomic_uint odv_clip_mode = 0;
std::atomic_uint64_t odv_clip_lease = 0;
std::atomic_bool odv_clip_failed = false;
std::array<std::atomic_uint64_t, 4> odv_clip_recordings{};
std::atomic_uint64_t odv_clip_candidates = 0;
std::atomic_uint odv_clip_compile_failure_stage = 0;
std::atomic_uint32_t odv_clip_compile_failure_hr = 0;

void PollODVClipProbe() noexcept {
  if (!odv_clip_enabled) return;
  // Present callbacks may run concurrently on different queues.
  static std::mutex mutex;
  const std::unique_lock lock(mutex, std::try_to_lock);
  if (!lock.owns_lock()) return;
  static uint64_t next_poll = 0;
  const auto now = GetTickCount64();
  if (now < next_poll) return;
  next_poll = now + 250;
  static bool config_read = false;
  if (!config_read) {
    uint32_t large = 0, collapse = 0, work = 0;
    reshade::get_config_value(nullptr, "renodx-dev", "ODVClipProbeLarge", large);
    reshade::get_config_value(nullptr, "renodx-dev", "ODVClipProbeCollapse", collapse);
    reshade::get_config_value(nullptr, "renodx-dev", "ODVClipProbeWork", work);
    if (work > 1) { odv_clip_failed = true; return; }
    if (work) {
      try { odv_constant_driver.begin_capture(); }
      catch (...) { odv_clip_failed = true; return; }
      odv_clip_work_enabled = true;
    }
    if (large > 1 || collapse > 1 || (large && collapse)) { odv_clip_failed = true; return; }
    odv_clip_large = large == 1;
    odv_clip_collapse = collapse == 1;
    config_read = true;
  }
  unsigned mode = 0;
  try {
    const auto directory = renodx::utils::path::GetReShadeBasePath();
    const auto control = directory / "clip-probe-control.txt";
    if (!odv_clip_failed && std::filesystem::is_regular_file(control) && std::filesystem::file_size(control) == 2 &&
        std::filesystem::file_time_type::clock::now() - std::filesystem::last_write_time(control) < std::chrono::seconds(3)) {
      std::ifstream input(control, std::ios::binary);
      char bytes[2]{};
      if (input.read(bytes, sizeof(bytes)) && bytes[0] >= '0' && bytes[0] <= '3' && bytes[1] == '\n')
        mode = static_cast<unsigned>(bytes[0] - '0');
    }
    odv_clip_mode = mode;
    odv_clip_lease = now + 1000;
    std::ofstream status(directory / "clip-probe-status.json", std::ios::trunc);
    status << "{\"schema_version\":1,\"mode\":" << mode << ",\"failed\":" << (odv_clip_failed ? "true" : "false")
           << ",\"candidates\":" << odv_clip_candidates.load() << ",\"recorded\":[";
    for (unsigned i = 0; i < 4; ++i) status << (i ? "," : "") << odv_clip_recordings[i].load();
    status << "],\"compile_failure_stage\":" << odv_clip_compile_failure_stage.load()
           << ",\"large_probe\":" << (odv_clip_large ? "true" : "false")
           << ",\"collapse_probe\":" << (odv_clip_collapse ? "true" : "false")
           << ",\"compile_failure_hr\":" << odv_clip_compile_failure_hr.load()
           << ",\"evidence\":\"CPU_RECORDING_ONLY\",\"game_stereo\":\"NOT_RUN\",\"gpu_work\":[";
    {
      const std::lock_guard work_lock(odv_clip_work_mutex);
      bool comma = false;
      for (const auto& work : odv_clip_work_samples) {
        if (!work.ticket) continue;
        const auto sample = odv_constant_driver.read(work.ticket);
        status << (comma ? "," : "") << "{\"mode\":" << work.mode
               << ",\"type\":" << static_cast<unsigned>(work.type)
               << ",\"maximum_count\":" << work.maximum_count
               << ",\"state\":\"" << sample.state << "\"";
        comma = true;
        if (std::strcmp(sample.state, "READY") == 0) {
          if (work.type == D3D12_QUERY_TYPE_PIPELINE_STATISTICS && sample.bytes.size() == sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS)) {
            D3D12_QUERY_DATA_PIPELINE_STATISTICS stats{};
            std::memcpy(&stats, sample.bytes.data(), sizeof(stats));
            status << ",\"ia_vertices\":" << stats.IAVertices << ",\"ia_primitives\":" << stats.IAPrimitives
                   << ",\"vs_invocations\":" << stats.VSInvocations << ",\"clip_primitives\":" << stats.CPrimitives
                   << ",\"ps_invocations\":" << stats.PSInvocations;
          } else if (work.type == D3D12_QUERY_TYPE_OCCLUSION && sample.bytes.size() == sizeof(UINT64)) {
            UINT64 samples = 0;
            std::memcpy(&samples, sample.bytes.data(), sizeof(samples));
            status << ",\"occlusion_samples\":" << samples;
          }
        }
        status << "}";
      }
    }
    status << "]}\n";
  } catch (...) {
    odv_clip_mode = 0;
    odv_clip_lease = 0;
  }
}

bool TryODVClipProbe(reshade::api::command_list* list) noexcept {
  const auto mode = odv_clip_mode.load();
  if (!odv_clip_enabled || !mode || mode > 3 || odv_clip_failed || GetTickCount64() > odv_clip_lease.load()) return false;
  try {
    auto* device = list->get_device();
    if (device->get_api() != reshade::api::device_api::d3d12) return false;
    const auto* data = renodx::utils::data::Get<CommandListData>(list);
    if (!data || !data->odv_native_state || data->odv_last_d3d12_pipeline != renodx::utils::pipeline::PipelineBindPoint::GRAPHICS)
      return false;
    const auto pipeline = renodx::utils::state::GetBoundShaderPipeline(list, reshade::api::pipeline_stage::vertex_shader);
    auto* original = reinterpret_cast<ID3D12PipelineState*>(pipeline.handle);
    if (!original) return false;
    auto source = odv::research::load_native_vertex_source(original);
    if (!source) return false; // Wrapper tags only the selected observed material-pass VS.
    ++odv_clip_candidates;
    const auto tables = renodx::utils::state::CopyBoundDescriptorTables(list, renodx::utils::state::PipelineBindPoint::GRAPHICS);
    if (!tables) return false;
    const auto& native = data->odv_native_state;
    odv::research::NativeIndirectCall call{};
    odv::research::NativeIndirectDescription description{};
    std::optional<D3D12_QUERY_TYPE> work_type;
    {
      const std::lock_guard lock(native->mutex);
      if (native->failed || native->protected_session || !native->draw_capable || !native->indirect_capable ||
          !native->indirect_active || !native->indirect_known || !native->draw_state.eligible_single_forward()) return false;
      for (const auto& view : native->draw_state.stream_outputs())
        if (view.BufferLocation || view.SizeInBytes || view.BufferFilledSizeLocation) return false;
      call = native->indirect_call;
      description = native->indirect_description;
      work_type = native->draw_state.work_query_type();
    }
    if (!odv::research::clip_probe_signature(description) || description.root_signature != tables->layout.handle)
      return false;
    // Restrict the probe to substantial screen-shaped color targets, using
    // the same bounded scene filter as the qualified observer.
    auto* device_data = renodx::utils::data::Get<DeviceData>(device);
    if (!device_data) return false;
    uint32_t primary_width = 0, primary_height = 0;
    {
      const std::shared_lock lock(device_data->mutex);
      if (!device_data->primary_swapchain_desc) return false;
      primary_width = device_data->primary_swapchain_desc->back_buffer.texture.width;
      primary_height = device_data->primary_swapchain_desc->back_buffer.texture.height;
    }
    if (!primary_width || !primary_height) return false;
    bool screen_target = false;
    for (const auto view : renodx::utils::swapchain::GetRenderTargets(list)) {
      if (!view.handle) continue;
      const auto resource = device->get_resource_from_view(view);
      if (!resource.handle) continue;
      const auto desc = device->get_resource_desc(resource);
      if (desc.type != reshade::api::resource_type::texture_2d) continue;
      const auto mip = device->get_resource_view_desc(view).texture.first_level;
      if (mip >= 32) continue;
      const uint64_t width = desc.texture.width >> mip, height = desc.texture.height >> mip;
      if (!width || !height || width * 4 < primary_width || height * 4 < primary_height ||
          width > uint64_t(primary_width) * 2 || height > uint64_t(primary_height) * 2) continue;
      const auto a = width * primary_height, b = height * primary_width;
      const auto difference = a > b ? a - b : b - a;
      if (difference * 100 <= b * 2) { screen_target = true; break; }
    }
    if (!screen_target) return false;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> variant;
    const unsigned slot = mode - 1;
    if (FAILED(source->GetVariant(slot, &variant))) {
      D3D12_SHADER_BYTECODE vertex{};
      if (FAILED(source->GetVertex(&vertex)) || vertex.BytecodeLength != 18680) return false;
      const auto* bytes = static_cast<const uint8_t*>(vertex.pShaderBytecode);
      if (renodx::utils::hash::ComputeCRC32(bytes, vertex.BytecodeLength) != 0x80800FE6u) return false;
      odv::research::ClipShiftCompiler compiler(renodx::utils::path::GetReShadeBasePath());
      const auto shift = mode == 1 ? odv::research::ClipShift::identity : mode == 2 ? odv::research::ClipShift::left : odv::research::ClipShift::right;
      const auto modified = compiler.compile({bytes, vertex.BytecodeLength}, shift, odv_clip_large, odv_clip_collapse);
      const HRESULT created = source->CreateVariant(reinterpret_cast<ID3D12Device*>(device->get_native()), slot,
          modified.data(), modified.size(), &variant);
      if (created == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
        if (FAILED(source->GetVariant(slot, &variant))) return false;
      } else if (FAILED(created)) {
        odv_clip_failed = true;
        reshade::log::message(reshade::log::level::error, "ODV clip probe: native variant creation failed; original draw retained");
        return false;
      }
    }
    auto* commands = reinterpret_cast<ID3D12GraphicsCommandList*>(list->get_native());
    bool forwarded = false, measured = false;
    const auto forward = [&]() noexcept {
      forwarded = odv::research::record_clip_indirect(commands, original, variant.Get(), call, description);
    };
    if (odv_clip_work_enabled && work_type) {
      const std::lock_guard work_lock(odv_clip_work_mutex);
      unsigned per_mode = 0;
      ODVClipWorkSample* free = nullptr;
      for (auto& sample : odv_clip_work_samples) {
        if (sample.ticket && sample.mode == mode) ++per_mode;
        if (!sample.ticket && !free) free = &sample;
      }
      if (free && per_mode < 3) {
        auto ticket = odv_constant_driver.record_work(reinterpret_cast<uintptr_t>(list),
            reinterpret_cast<uintptr_t>(variant.Get()), reinterpret_cast<ID3D12Device*>(device->get_native()),
            commands, *work_type, forward);
        if (ticket) {
          *free = {std::move(ticket), mode, *work_type, call.maximum_count};
          measured = true;
        }
      }
    }
    if (!measured) forward();
    if (forwarded) {
      ++odv_clip_recordings[mode];
      return true; // Original command was forwarded once, with native resets.
    }
  } catch (const odv::research::ClipCompileError& error) {
    odv_clip_compile_failure_stage = static_cast<unsigned>(error.stage);
    odv_clip_compile_failure_hr = static_cast<uint32_t>(error.result);
    odv_clip_failed = true;
    odv_clip_mode = 0;
    // Fixed stage/code only: never export compiler text or shader contents.
    reshade::log::message(reshade::log::level::error, "ODV clip probe: compiler rejected identity/shift; see numeric failure stage and HRESULT; original draw retained");
    if (!error.summary.empty())
      reshade::log::message(reshade::log::level::error, (std::string("ODV clip probe validation summary: ") + error.summary).c_str());
  } catch (...) {
    odv_clip_failed = true;
    odv_clip_mode = 0;
    reshade::log::message(reshade::log::level::error, "ODV clip probe: unsupported shader or private compiler failure; original draw retained");
  }
  return false;
}
