#pragma once
// The environment behind the libuboot stub (stub_libuboot.cpp): what a
// libuboot_env_store() wrote and what the next libuboot_open() reads back.
// Tests seed it before constructing UBoot::UBoot and inspect it afterwards.
#include <map>
#include <string>

namespace fake_env
{

std::map<std::string, std::string> &flash();
void reset(std::map<std::string, std::string> variables);
// How many libuboot_open() calls are not yet closed: 1 while a transaction
// holds the environment (and, on the device, its file lock), 0 otherwise.
int open_depth();

} // namespace fake_env
