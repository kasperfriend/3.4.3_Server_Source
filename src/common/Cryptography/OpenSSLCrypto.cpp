/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "OpenSSLCrypto.h"
#include "Log.h"
#include <boost/filesystem/operations.hpp>
#include <openssl/crypto.h>
#include <cstdlib>

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#include <openssl/provider.h>
OSSL_PROVIDER* LegacyProvider;
#endif

namespace
{
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
constexpr char const* LEGACY_PROVIDER_MODULE_NAME = "legacy.dll";
#else
constexpr char const* LEGACY_PROVIDER_MODULE_NAME = "legacy.so";
#endif

/// OpenSSL 3 does not keep its provider modules next to the application: an installed
/// runtime puts them into a "providers" directory below its own bin. The callers hand
/// in the executable's directory, which is where a portable deployment ships the
/// OpenSSL DLLs, so look for the module in both places and use the directory that
/// actually holds it. When nothing holds it, say so: a provider that cannot be loaded
/// is otherwise completely invisible, until something asks for an algorithm that only
/// exists in it and OpenSSL reports an opaque "unsupported" error.
boost::filesystem::path GetProviderModuleDirectory(boost::filesystem::path const& preferred, bool& found)
{
    found = false;

    boost::system::error_code ec;
    boost::filesystem::path const candidates[] = {
        preferred,
        preferred / "providers",
        preferred.parent_path() / "providers"
    };

    for (boost::filesystem::path const& candidate : candidates)
    {
        if (candidate.empty())
            continue;

        ec.clear();
        if (boost::filesystem::exists(candidate / LEGACY_PROVIDER_MODULE_NAME, ec) && !ec)
        {
            found = true;
            return candidate;
        }
    }

    return preferred;
}
#endif
}

void OpenSSLCrypto::threadsSetup([[maybe_unused]] boost::filesystem::path const& providerModulePath)
{
#ifdef VALGRIND
    ValgrindRandomSetup();
#endif

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    bool providerDirectoryLocated = false;
    boost::filesystem::path const moduleDirectory = GetProviderModuleDirectory(providerModulePath, providerDirectoryLocated);

#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
    // Keep the string alive over the call: OpenSSL documents the path as borrowed
    // until the context is freed on some versions.
    std::string const modulesPath = moduleDirectory.string();
    if (!std::getenv("OPENSSL_MODULES"))
        OSSL_PROVIDER_set_default_search_path(nullptr, modulesPath.c_str());
#endif

    LegacyProvider = OSSL_PROVIDER_try_load(nullptr, "legacy", 1);
    if (!LegacyProvider)
    {
        if (providerDirectoryLocated)
            TC_LOG_INFO("server.loading", "The OpenSSL legacy provider was found in \"{}\" but could not be loaded; algorithms that only exist in it (RC4, MD4, DES, ...) are unavailable.", moduleDirectory.generic_string());
        else
            TC_LOG_INFO("server.loading", "The OpenSSL legacy provider ({}) was not found next to the executable or in its \"providers\" directory, so it is not loaded. The core does not need it; copy {} into \"{}\" if a tool or feature asks for it.", LEGACY_PROVIDER_MODULE_NAME, LEGACY_PROVIDER_MODULE_NAME, moduleDirectory.generic_string());
    }
#endif
}

void OpenSSLCrypto::threadsCleanup()
{
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    OSSL_PROVIDER_unload(LegacyProvider);
    OSSL_PROVIDER_set_default_search_path(nullptr, nullptr);
#endif
}

#ifdef VALGRIND
#include <openssl/rand.h>

RAND_METHOD const* default_rand;

static int Valgrind_RAND_seed(const void* buf, int num)
{
    VALGRIND_DISCARD(VALGRIND_MAKE_MEM_DEFINED(buf, num));
    return default_rand->seed(buf, num);
}

static int Valgrind_RAND_bytes(unsigned char* buf, int num)
{
    int ret = default_rand->bytes(buf, num);
    VALGRIND_DISCARD(VALGRIND_MAKE_MEM_DEFINED(buf, num));
    return ret;
}

static void Valgrind_RAND_cleanup(void)
{
    default_rand->cleanup();
}

static int Valgrind_RAND_add(const void* buf, int num, double randomness)
{
    VALGRIND_DISCARD(VALGRIND_MAKE_MEM_DEFINED(buf, num));
    return default_rand->add(buf, num, randomness);
}

static int Valgrind_RAND_pseudorand(unsigned char* buf, int num)
{
    int ret = default_rand->pseudorand(buf, num);
    VALGRIND_DISCARD(VALGRIND_MAKE_MEM_DEFINED(buf, num));
    return ret;
}

static int Valgrind_RAND_status(void)
{
    return default_rand->status();
}

static RAND_METHOD valgrind_rand;

void ValgrindRandomSetup()
{
    memset(&valgrind_rand, 0, sizeof(RAND_METHOD));
    default_rand = RAND_get_rand_method();
    if (default_rand->seed)
        valgrind_rand.seed = &Valgrind_RAND_seed;
    if (default_rand->bytes)
        valgrind_rand.bytes = &Valgrind_RAND_bytes;
    if (default_rand->cleanup)
        valgrind_rand.cleanup = &Valgrind_RAND_cleanup;
    if (default_rand->add)
        valgrind_rand.add = &Valgrind_RAND_add;
    if (default_rand->pseudorand)
        valgrind_rand.pseudorand = &Valgrind_RAND_pseudorand;
    if (default_rand->status)
        valgrind_rand.status = &Valgrind_RAND_status;
    RAND_set_rand_method(&valgrind_rand);
}
#endif
