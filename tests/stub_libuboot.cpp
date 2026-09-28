// In-memory stand-in for the libuboot C API, so the real UBoot::UBoot can be
// driven natively. Mirrors the library's shape: open() loads a working copy
// of the stored environment, set_env() changes that copy, env_store() writes
// it back. get_env() hands out a malloc'd string like the library does.
#include "fake_env.h"

extern "C" {
#include <libuboot.h>
}

#include <cstring>
#include <map>
#include <string>
#include <utility>

struct uboot_ctx {
    std::map<std::string, std::string> working;
};

namespace
{

std::map<std::string, std::string> stored;
uboot_ctx context;
int opened = 0;

} // namespace

std::map<std::string, std::string> &fake_env::flash()
{
    return stored;
}

void fake_env::reset(std::map<std::string, std::string> variables)
{
    stored = std::move(variables);
    context.working.clear();
    opened = 0;
}

int fake_env::open_depth()
{
    return opened;
}

extern "C" {

int libuboot_initialize(struct uboot_ctx **out, struct uboot_env_device *)
{
    *out = &context;
    return 0;
}

int libuboot_read_config(struct uboot_ctx *, const char *)
{
    return 0;
}

void libuboot_exit(struct uboot_ctx *) {}

int libuboot_open(struct uboot_ctx *ctx)
{
    ctx->working = stored;
    ++opened;
    return 0;
}

void libuboot_close(struct uboot_ctx *)
{
    if (opened > 0) {
        --opened;
    }
}

char *libuboot_get_env(struct uboot_ctx *ctx, const char *varname)
{
    const auto it = ctx->working.find(varname);
    if (it == ctx->working.end()) {
        return nullptr;
    }
    return ::strdup(it->second.c_str());
}

int libuboot_set_env(struct uboot_ctx *ctx, const char *varname, const char *value)
{
    if (value == nullptr) {
        ctx->working.erase(varname);
    } else {
        ctx->working[varname] = value;
    }
    return 0;
}

int libuboot_env_store(struct uboot_ctx *ctx)
{
    stored = ctx->working;
    return 0;
}

} // extern "C"
