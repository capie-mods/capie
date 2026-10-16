/** @file mod_identity.hpp
 * @brief Shared platform-independent CAPIE playset descriptor identities.
 */
#pragma once
namespace capie::launcher {
/**
 * @brief Descriptor path of the local "CAPIE" switch mod as it appears in dlc_load.json.
 *
 * The switch mod ships in `plugin/capie/toggle-mod/`. A local install (the bootstrap, or a
 * development copy) registers it as `mod/capie.mod`.
 */
inline constexpr char capie_mod_descriptor[] = "mod/capie.mod";
/**
 * @brief Steam Workshop ID of the "CAPIE" switch mod (decimal).
 *
 * The Paradox Launcher lists a subscribed Workshop item in dlc_load.json as
 * `mod/ugc_<ID>.mod` (the descriptor it writes into the user mod folder), so a subscriber's
 * playset entry is `mod/ugc_3819929471.mod`. The local descriptor above matches as well.
 */
inline constexpr char capie_mod_workshop_id[] = "3819929471";

} // namespace capie::launcher
