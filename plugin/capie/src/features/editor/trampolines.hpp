/**
 * @file trampolines.hpp
 * @brief Signatures of the hooked editor functions and their MinHook trampolines.
 *
 * Each pointer is filled by `MH_CreateHook` during initialization and then only called,
 * never changed. Calling a trampoline runs the original game function. Tests assign fakes
 * to these pointers to observe how often and with which arguments the originals run.
 */
#pragma once
#include "../../engine/records.hpp"

#include <cstdint>

namespace capie::editor {

#if !defined(__APPLE__)
using engine::MsvcString;
#endif
using engine::Record;
using engine::RecordString;
using engine::TempVector;

#if defined(__APPLE__)
/// @brief Native SysV tick window/force arguments.
using Tick = void (*)(void*, char);
/// @brief Native SysV bind window argument.
using Bind = void (*)(void*);
/// @brief Key handler return register forwarded opaquely.
using HandleKey = std::uint64_t (*)(void*, void*);
/// @brief Native context/selector dispatch.
using Dispatch = void (*)(void*, void*);
/// @brief Native command execution.
using Execute = void (*)(void*);
/// @brief Five-register native constructor boundary; see macos_records.hpp.
using Constructor = engine::macos::records::Constructor;
#else
/// @brief `void Tick(window*, char force)`.
using Tick = void(__fastcall*)(void*, char);
/// @brief `void Bind(window*)`.
using Bind = void(__fastcall*)(void*);
/// @brief Key handler / wrapper: `u64 (object*, event*)`; RAX is passed through opaquely.
using HandleKey = std::uint64_t(__fastcall*)(void*, void*);
/// @brief `void Dispatch(context*, selector*)`.
using Dispatch = void(__fastcall*)(void*, void*);
/// @brief `void Execute(command*)`.
using Execute = void(__fastcall*)(void*);
/// @brief `effect* ctor(self*, gui name*, selector*, scope*, records*)`.
using Constructor = void*(__fastcall*)(void*, const MsvcString*, const MsvcString*, const void*, const TempVector*);

#endif

/// @brief Original Tick.
inline Tick original_tick = nullptr;
/// @brief Original Bind.
inline Bind original_bind = nullptr;
/// @brief Original text-buffer key handler (debug flavor only).
inline HandleKey original_key = nullptr;
/// @brief Original CEditBox key wrapper (debug flavor only).
inline HandleKey original_wrapper = nullptr;
/// @brief Original Dispatch.
inline Dispatch original_dispatch = nullptr;
/// @brief Original Execute (debug flavor only).
inline Execute original_execute = nullptr;
/// @brief Original effect constructor.
inline Constructor original_ctor = nullptr;

}  // namespace capie::editor
