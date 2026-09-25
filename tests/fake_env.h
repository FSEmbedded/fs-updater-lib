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

} // namespace fake_env
