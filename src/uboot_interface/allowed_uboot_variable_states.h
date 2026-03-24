#pragma once

#include <vector>
#include <string>
#include <cstdint>

/* allowed update variables
 * number in vector :
 * 0 -> fw_a commit bit
 * 1 -> app_a commit bit
 * 2 -> fw_b commit bit
 * 3 -> app_b commit bit
 */
inline constexpr int FIRMWARE_A_INDEX = 0;
inline constexpr int FIRMWARE_B_INDEX = 2;
inline constexpr int APPLICATION_A_INDEX = 1;
inline constexpr int APPLICATION_B_INDEX = 3;
inline constexpr int STATE_UPDATE_COMMITED = 0;
inline constexpr int STATE_UPDATE_UNCOMMITED = 1;
inline constexpr int STATE_UPDATE_BAD = 2;
/* allow followed update states:
0000 - commited all, 0001 - app_b uncommited, 0011 - fw_/app_b uncommited, 0010 - fw_b uncommted
0100 - app_a uncommited, 1000 - fw_a uncommited, 1100 - fw/app_a uncommited, 0110 - fw_b and app_a uncommited
1001 - fw_a and app_b uncommited, 0200 - app_a bad, 0002 - app_b bad, 0020 - fw_b bad , 2000 - fw_a bad
0300 - app_a - uncommited and bad, app_b - uncommited and bad.
*/
inline const std::vector<std::string> allowed_update_variables({"0000", "0001", "0011", "0010", "0100", "1000", "1100", "0110", "1001", "0200", "0002", "0020", "2000", "0021", "0012", "1200", "2100", "0300", "0003"});
inline const std::vector<uint8_t> allowed_update_reboot_state_variables({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
inline const std::vector<std::string> allowed_boot_order_variables({"A B", "B A"});
inline const std::vector<uint8_t> allowed_boot_ab_left_variables({0, 1, 2, 3});
inline const std::vector<std::string> allowed_rauc_cmd_variables({"rauc.slot=A", "rauc.slot=B"});
inline const std::vector<char> allowed_application_variables({'A', 'B'});
