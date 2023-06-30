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
#define FIRMWARE_A_INDEX 0
#define FIRMWARE_B_INDEX 2
#define APPLICATION_A_INDEX 1
#define APPLICATION_B_INDEX 3
#define STATE_UPDATE_COMMITED 0
#define STATE_UPDATE_UNCOMMITED 1
#define STATE_UPDATE_BAD 2

const std::vector<std::string> allowed_update_variables({"0000", "0001", "0011", "0010", "0100", "1000", "1100", "0200", "0202", "0002", "0300", "0003"});
const std::vector<uint8_t> allowed_update_reboot_state_variables({0, 1, 2, 3, 4, 5, 6, 7, 8});
const std::vector<std::string> allowed_boot_order_variables({"A B", "B A"});
const std::vector<uint8_t> allowed_boot_ab_left_variables({0, 1, 2, 3});
const std::vector<std::string> allowed_rauc_cmd_variables({"rauc.slot=A", "rauc.slot=B"});
const std::vector<char> allowed_application_variables({'A', 'B'});
