#pragma once

#include <vector>
#include <string>
#include <cstdint>

/* update variable layout:
 * index 0 -> fw_a state
 * index 1 -> app_a state
 * index 2 -> fw_b state
 * index 3 -> app_b state
 *
 * Per-bit state encoding:
 *   bit 0 (& 1): uncommitted
 *   bit 1 (& 2): bad
 *   0 = committed, 1 = uncommitted, 2 = bad, 3 = uncommitted+bad
 */
inline constexpr int FIRMWARE_A_INDEX = 0;
inline constexpr int FIRMWARE_B_INDEX = 2;
inline constexpr int APPLICATION_A_INDEX = 1;
inline constexpr int APPLICATION_B_INDEX = 3;
inline constexpr int STATE_UPDATE_COMMITED = 0;
inline constexpr int STATE_UPDATE_UNCOMMITED = 1;
inline constexpr int STATE_UPDATE_BAD = 2;

/*
 * Validate the 4-character "update" U-Boot variable using per-bit rules:
 *   - Each character must encode a state in 0–3.
 *   - At most one FW slot (indices 0, 2) may be uncommitted at a time.
 *   - At most one APP slot (indices 1, 3) may be uncommitted at a time.
 */
inline bool validate_update_bits(const std::string &val)
{
    if (val.size() != 4) return false;
    int uncommitted_fw  = 0;
    int uncommitted_app = 0;
    for (int i = 0; i < 4; ++i)
    {
        const int state = val[i] - '0';
        if (state < 0 || state > 3) return false;
        if (i == FIRMWARE_A_INDEX || i == FIRMWARE_B_INDEX)
        {
            if (state & STATE_UPDATE_UNCOMMITED) ++uncommitted_fw;
        }
        else
        {
            if (state & STATE_UPDATE_UNCOMMITED) ++uncommitted_app;
        }
    }
    return uncommitted_fw <= 1 && uncommitted_app <= 1;
}

inline const std::vector<uint8_t> allowed_update_reboot_state_variables({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
inline const std::vector<std::string> allowed_boot_order_variables({"A B", "B A"});
inline const std::vector<uint8_t> allowed_boot_ab_left_variables({0, 1, 2, 3});
inline const std::vector<std::string> allowed_rauc_cmd_variables({"rauc.slot=A", "rauc.slot=B"});
inline const std::vector<char> allowed_application_variables({'A', 'B'});
