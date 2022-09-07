#pragma once

#include <vector>
#include <string>
#include <cstdint>

const std::vector<std::string> allowed_update_variables({"0000", "0001", "0011", "0010"});
const std::vector<uint8_t> allowed_update_reboot_state_variables({0, 1, 2, 3, 4, 5, 6, 7, 8});
const std::vector<std::string> allowed_boot_order_variables({"A B", "B A"});
const std::vector<uint8_t> allowed_boot_ab_left_variables({0, 1, 2, 3});
const std::vector<std::string> allowed_rauc_cmd_variables({"rauc.slot=A", "rauc.slot=B"});
const std::vector<char> allowed_application_variables({'A', 'B'});