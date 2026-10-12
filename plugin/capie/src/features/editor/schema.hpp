/**
 * @file schema.hpp
 * @brief Names and limits shared by the mod's inline editors and the editor bridge.
 *
 * The bridge recognises its own scripted GUIs only by exact names: three editor models
 * (leader, party, country), their five edit fields and prefill carriers, the epoch
 * carriers, the Apply/Open/Cancel selectors and the temp-variable keys of the transport.
 * Every one of those strings is defined here once. They must match the generated mod
 * (`tools/modgen/features/inline_editor.py`); a rename on either side makes the bridge
 * ignore the window (fail-closed), it never matches something else.
 *
 * Also here: the record budget of one Apply (at most 70 appended records), the
 * fixed-point scale of HOI4 variables (100000) and the stale-Apply sentinel codes the
 * script maps to its messages.
 *
 * Data only.
 */
#pragma once
#include <cstddef>
#include <cstdint>

namespace capie::editor {

/// @brief One editable field: its edit box, its prefill carrier and its model index.
struct Field {
    const char* edit;     ///< Edit-box definition name.
    const char* carrier;  ///< instantTextBox with the current game text (prefill source).
    std::size_t model;    ///< Index into `models`.
};

/// @brief One editor window model.
struct Model {
    const char* name;   ///< Scripted GUI model name.
    const char* epoch;  ///< Epoch carrier (opening counter) widget name.
    const char* apply;  ///< Apply selector.
    std::size_t first;  ///< First field index.
    std::size_t count;  ///< Field count (1 or 2).
};

/// @brief Indices into `models` (also the caption field of leader/party and the debug model bit).
namespace model_index {
inline constexpr std::size_t leader = 0;   ///< Leader editor.
inline constexpr std::size_t party = 1;    ///< Party (neutrality) editor.
inline constexpr std::size_t country = 2;  ///< Country editor.
}  // namespace model_index

/// @brief Indices into `fields`.
namespace field_index {
inline constexpr std::size_t leader = 0;             ///< Leader name.
inline constexpr std::size_t party_short = 1;        ///< Party short name.
inline constexpr std::size_t party_long = 2;         ///< Party long name.
inline constexpr std::size_t country_name = 3;       ///< Country name.
inline constexpr std::size_t country_adjective = 4;  ///< Country adjective.
}  // namespace field_index

/// @brief The five own edit fields (order = `field_index`).
inline constexpr Field fields[] = {
    {"rn_ip_leader_edit", "rn_ip_leader_prefill", model_index::leader},
    {"rn_ip_party_neutrality_edit", "rn_ip_party_neutrality_prefill", model_index::party},
    {"rn_ip_party_neutrality_long_edit", "rn_ip_party_neutrality_long_prefill", model_index::party},
    {"rn_ip_country_name_edit", "rn_ip_country_name_prefill", model_index::country},
    {"rn_ip_country_adj_edit", "rn_ip_country_adj_prefill", model_index::country},
};

/// @brief The three own editor models (order = `model_index`).
inline constexpr Model models[] = {
    {"rn_ip_leader_gui", "rn_ip_leader_epoch", "rn_ip_leader_apply_click", field_index::leader, 1},
    {"rn_ip_party_neutrality_gui", "rn_ip_party_neutrality_epoch", "rn_ip_party_neutrality_apply_click",
     field_index::party_short, 2},
    {"rn_ip_country_gui", "rn_ip_country_epoch", "rn_ip_country_apply_click", field_index::country_name, 2},
};

/// @brief Number of fields (5).
inline constexpr std::size_t field_count = sizeof(fields) / sizeof(fields[0]);
/// @brief Number of models (3).
inline constexpr std::size_t model_count = sizeof(models) / sizeof(models[0]);
/// @brief Most fields any model has.
inline constexpr std::size_t max_fields_per_model = 2;
/// @brief Expected number of fields (leader 1, party 2, country 2).
inline constexpr std::size_t expected_field_count = 5;
/// @brief Expected number of editor models.
inline constexpr std::size_t expected_model_count = 3;
static_assert(field_count == expected_field_count && model_count == expected_model_count &&
              models[model_index::leader].count == 1 && models[model_index::party].count == 2 &&
              models[model_index::country].count == 2);
/// @brief Index of the country model (needs `capie_core.dll` active at dispatch).
inline constexpr std::size_t country_model = model_index::country;
/// @brief Highest model index with a caption revision counter (leader, party).
inline constexpr std::size_t last_caption_model = model_index::party;

/// @brief Model name of the leader launcher (ticked for caption refresh, never edited).
inline constexpr char leader_launcher_model[] = "rn_ip_leader_launcher_gui";
/// @brief Model name of the party launcher (ticked for caption refresh, never edited).
inline constexpr char party_launcher_model[] = "rn_ip_party_neutrality_launcher_gui";
/// @brief Carrier showing the leader success counter `rn_ip_lui_rev` (editor and launcher).
inline constexpr char leader_revision_carrier[] = "rn_ip_leader_rev";
/// @brief Carrier showing the party success counter `rn_ip_pui_rev` (editor and launcher).
inline constexpr char party_revision_carrier[] = "rn_ip_party_neutrality_rev";

/// @brief One selector the debug flavor logs before dispatch.
struct DebugAction {
    const char* model;     ///< Model name.
    const char* selector;  ///< Selector.
    const char* action;    ///< Short label for the log.
};
/// @brief Seven own models and ten selectors observed by the debug flavor.
inline constexpr DebugAction debug_actions[] = {
    {"rn_ip_leader_launcher_gui", "rn_ip_leader_open_click", "open"},
    {"rn_ip_leader_gui", "rn_ip_leader_apply_click", "apply"},
    {"rn_ip_leader_gui", "rn_ip_leader_cancel_click", "cancel"},
    {"rn_ip_party_neutrality_launcher_gui", "rn_ip_party_neutrality_open_click", "open"},
    {"rn_ip_party_neutrality_gui", "rn_ip_party_neutrality_apply_click", "apply"},
    {"rn_ip_party_neutrality_gui", "rn_ip_party_neutrality_cancel_click", "cancel"},
    {"rn_ip_country_launcher_gui", "rn_ip_country_open_click", "open"},
    {"rn_ip_country_gui", "rn_ip_country_apply_click", "apply"},
    {"rn_ip_country_gui", "rn_ip_country_cancel_click", "cancel"},
    {"rn_ilqa_guard_gui", "rn_ilqa_guards_click", "qa_guards"},
};

// ---- Transport protocol -----------------------------------------------------------------------
/// @brief HOI4 variables are fixed point; the raw record value is `value * fixed_point`.
inline constexpr std::int64_t fixed_point = 100000;
/// @brief Largest original record vector accepted from the window context.
inline constexpr std::size_t max_original_records = 256;
/// @brief Most records the bridge appends for one Apply (country: 2*34 + epoch + capability).
inline constexpr std::size_t max_transport_records = 70;
/// @brief Records per field: ok, len and 32 slots.
inline constexpr std::size_t records_per_field = 34;
/// @brief Code slots per field (`rn_native_0..31`).
inline constexpr unsigned code_slots = 32;
/// @brief Slot that carries the InputError instead of a code when the field is invalid.
inline constexpr unsigned error_slot_index = 31;
/// @brief Longest key the SSO record builder accepts (15 bytes).
inline constexpr std::size_t max_record_key = 15;
/// @brief Size of a record-key formatting buffer.
inline constexpr std::size_t record_key_buffer = 16;

/// @brief Temp variable carrying the opening epoch (or a negative sentinel).
inline constexpr char command_epoch_key[] = "rn_ip_cmd_epoch";
static_assert(sizeof(command_epoch_key) - 1 == max_record_key);
/// @brief Key prefix of the first field.
inline constexpr char field_a_prefix[] = "rn_native";
/// @brief Key prefix of the second field.
inline constexpr char field_b_prefix[] = "rn_nativeb";
/// @brief Validity header of field A.
inline constexpr char key_a_ok[] = "rn_native_ok";
/// @brief Length header of field A.
inline constexpr char key_a_len[] = "rn_native_len";
/// @brief Validity header of field B.
inline constexpr char key_b_ok[] = "rn_nativeb_ok";
/// @brief Length header of field B.
inline constexpr char key_b_len[] = "rn_nativeb_len";
/// @brief Error slot of field A.
inline constexpr char key_a_err[] = "rn_native_err";
/// @brief Error slot of field B.
inline constexpr char key_b_err[] = "rn_nativeb_err";
/// @brief Country capability marker (value 1).
inline constexpr char key_capability[] = "rn_native_cap";

/// @brief Why an own Apply is forwarded as a sentinel instead of a payload (sent as epoch = -code).
enum class StaleCode : unsigned {
    stale = 1,          ///< Unseeded, jump, rollback suspicion or failed re-prefill: Cancel and reopen fixes it.
    no_counter = 2,     ///< Revision carrier missing or unreadable: reopening does not help.
    counter_limit = 3,  ///< Revision counter at 999: reopening does not help.
};

// ---- Timing and diagnostics ------------------------------------------------------------------
/// @brief An Enter observation older than this is not matched to a Tick (diagnostic only).
inline constexpr std::uint64_t enter_max_age_ms = 2000;
/// @brief Longest epoch carrier text in digits.
inline constexpr std::size_t max_epoch_digits = 15;

}  // namespace capie::editor
