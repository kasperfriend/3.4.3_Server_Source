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

#include "SslContext.h"
#include "Config.h"
#include "Log.h"
#include "Memory.h"
#include <algorithm>
#include <string>
#include <vector>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <openssl/store.h>
#include <openssl/ui.h>

namespace fs = boost::filesystem;

namespace
{
struct ResolvedDataFile
{
    std::string ConfigName;       ///< name of the configuration field the path came from
    std::string ConfiguredValue;  ///< value as written in bnetserver.conf
    fs::path Path;                ///< file that is actually going to be opened
    bool Found;                   ///< false when none of the candidates exists
    std::vector<fs::path> Candidates; ///< every location that was looked at
};

auto CreatePasswordUiMethodFromPemCallback(::pem_password_cb* callback)
{
    return Trinity::make_unique_ptr_with_deleter(UI_UTIL_wrap_read_pem_callback(callback, 0), ::UI_destroy_method);
}

auto OpenOpenSSLStore(fs::path const& storePath, UI_METHOD const* passwordCallback, void* passwordCallbackData)
{
    std::string uri;
    uri.reserve(6 + storePath.size());

    uri += "file:";
    std::string genericPath = storePath.generic_string();
    if (!genericPath.empty() && !genericPath.starts_with('/'))
        uri += '/'; // ensure the path starts with / (windows special case, unix absolute paths already do)

    uri += genericPath;

    return Trinity::make_unique_ptr_with_deleter(OSSL_STORE_open(uri.c_str(), passwordCallback, passwordCallbackData, nullptr, nullptr), ::OSSL_STORE_close);
}

boost::system::error_code GetLastOpenSSLError()
{
    auto ossl_error = ::ERR_get_error();
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    if (ERR_SYSTEM_ERROR(ossl_error))
        return boost::system::error_code(static_cast<int>(::ERR_GET_REASON(ossl_error)), boost::asio::error::get_system_category());
#endif

    return boost::system::error_code(static_cast<int>(ossl_error), boost::asio::error::get_ssl_category());
}

/// Directories a relative path from the configuration file is resolved against.
/// This mirrors the bnetserver.conf lookup in Main.cpp: the deployment layout
/// (executables in bin\, configuration templates in ..\etc\) and the post build
/// step that drops the .pem files next to the executable must both work no
/// matter which directory the server was started from. Opening the certificate
/// strictly relative to the working directory made bnetserver abort with
/// "OSSL_STORE_open failed: The system cannot find the file specified" whenever
/// it was launched from anywhere else.
std::vector<fs::path> GetDataFileSearchDirectories()
{
    std::vector<fs::path> dirs;

    boost::system::error_code ec;
    auto AddDir = [&dirs, &ec](fs::path dir)
    {
        if (dir.empty())
            return;

        dir = fs::absolute(dir, ec);
        if (ec)
            return;

        dir = dir.lexically_normal();
        if (std::find(dirs.begin(), dirs.end(), dir) == dirs.end())
            dirs.push_back(std::move(dir));
    };

    AddDir(fs::current_path(ec));                              ///< the working directory (historic behaviour)
    AddDir(boost::dll::program_location().parent_path());       ///< next to bnetserver.exe
    AddDir(fs::path(sConfigMgr->GetFilename()).parent_path());  ///< next to the loaded bnetserver.conf

    std::vector<fs::path> const baseDirs = dirs;
    for (fs::path const& base : baseDirs)
        AddDir(base / ".." / "etc");                            ///< ..\etc of each of the above

    return dirs;
}

/// Resolves a configured file to an existing file. Absolute paths are honoured
/// verbatim; relative ones are searched in the data directories above, so the
/// packaged and the in-build-tree layouts both work.
ResolvedDataFile ResolveDataFile(std::string const& configName, std::string const& configuredValue)
{
    ResolvedDataFile result{ configName, configuredValue, fs::path(configuredValue), false, {} };

    fs::path const requested{ configuredValue };
    if (requested.empty())
        return result;

    // An absolute path (or one carrying a drive on Windows) is exactly what the
    // operator asked for, so never look anywhere else.
    if (requested.is_absolute() || requested.has_root_path())
    {
        result.Candidates.push_back(requested);

        boost::system::error_code ec;
        if (fs::is_regular_file(requested, ec) && !ec)
        {
            result.Path = requested;
            result.Found = true;
        }
        return result;
    }

    for (fs::path const& dir : GetDataFileSearchDirectories())
    {
        fs::path candidate = (dir / requested).lexically_normal();
        if (std::find(result.Candidates.begin(), result.Candidates.end(), candidate) == result.Candidates.end())
            result.Candidates.push_back(std::move(candidate));
    }

    boost::system::error_code ec;
    for (fs::path const& candidate : result.Candidates)
    {
        if (fs::is_regular_file(candidate, ec) && !ec)
        {
            result.Path = candidate;
            result.Found = true;
            return result;
        }
    }

    // Nothing there: report the first candidate, which is what the operator
    // configured resolved against the working directory.
    if (!result.Candidates.empty())
        result.Path = result.Candidates.front();

    return result;
}

void LogMissingDataFile(ResolvedDataFile const& file)
{
    std::string searched;
    for (fs::path const& candidate : file.Candidates)
    {
        searched += "\n";
        searched += "        * ";
        searched += candidate.generic_string();
    }

    TC_LOG_ERROR("server.ssl", "{} = \"{}\" was not found. Tried:{}", file.ConfigName, file.ConfiguredValue, searched.empty() ? std::string(" no candidate paths") : searched);
    TC_LOG_ERROR("server.ssl", "Copy bnetserver.cert.pem and bnetserver.key.pem next to bnetserver.exe (they live in src/server/bnetserver in the source tree and are placed there by the build), or set {} to an absolute path. Keep both files in the same directory unless the certificate and the key are in separate files.", file.ConfigName);
}

std::string DisplayPath(fs::path const& path)
{
    return path.generic_string();
}
}

bool Battlenet::SslContext::Initialize()
{
    boost::system::error_code err;
#define LOAD_CHECK(fn) do { fn; \
    if (err) \
    { \
        TC_LOG_ERROR("server.ssl", #fn " failed: {}", err.message()); \
        return false; \
    } } while (0)

    ResolvedDataFile certificateFile = ResolveDataFile("CertificatesFile", sConfigMgr->GetStringDefault("CertificatesFile", "./bnetserver.cert.pem"));

    auto passwordCallback = [](std::size_t /*max_length*/, boost::asio::ssl::context::password_purpose /*purpose*/) -> std::string
    {
        return sConfigMgr->GetStringDefault("PrivateKeyPassword", "");
    };

    LOAD_CHECK(instance().set_password_callback(passwordCallback, err));

    SSL_CTX* nativeContext = instance().native_handle();
    auto password_ui_method = CreatePasswordUiMethodFromPemCallback(SSL_CTX_get_default_passwd_cb(nativeContext));

    auto store = OpenOpenSSLStore(certificateFile.Path,
        password_ui_method.get(), SSL_CTX_get_default_passwd_cb_userdata(nativeContext));

    if (!store)
    {
        err = GetLastOpenSSLError();
        TC_LOG_ERROR("server.ssl", "OSSL_STORE_open failed for certificate file \"{}\": {}", DisplayPath(certificateFile.Path), err.message());
        if (!certificateFile.Found)
            LogMissingDataFile(certificateFile);
        return false;
    }

    EVP_PKEY* key = nullptr;
    STACK_OF(X509)* certs = sk_X509_new_null();
    while (!OSSL_STORE_eof(store.get()))
    {
        OSSL_STORE_INFO* info = OSSL_STORE_load(store.get());
        if (!info)
            continue;

        switch (OSSL_STORE_INFO_get_type(info))
        {
            case OSSL_STORE_INFO_PKEY:
                key = OSSL_STORE_INFO_get1_PKEY(info);
                break;
            case OSSL_STORE_INFO_CERT:
                sk_X509_push(certs, OSSL_STORE_INFO_get1_CERT(info));
                break;
            default:
                break;
        }
    }

    if (sk_X509_num(certs) > 0)
    {
        X509* cert = sk_X509_shift(certs);
        SSL_CTX_use_cert_and_key(nativeContext, cert, key, certs, 1);
    }

    sk_X509_free(certs);

    if (!key)
    {
        ResolvedDataFile privateKeyFile = ResolveDataFile("PrivateKeyFile", sConfigMgr->GetStringDefault("PrivateKeyFile", "./bnetserver.key.pem"));
        instance().use_private_key_file(DisplayPath(privateKeyFile.Path), boost::asio::ssl::context::pem, err);
        if (err)
        {
            TC_LOG_ERROR("server.ssl", "Failed to load private key file \"{}\": {}", DisplayPath(privateKeyFile.Path), err.message());
            if (!privateKeyFile.Found)
                LogMissingDataFile(privateKeyFile);
            return false;
        }
    }

#undef LOAD_CHECK

    return true;
}

boost::asio::ssl::context& Battlenet::SslContext::instance()
{
    static boost::asio::ssl::context context(boost::asio::ssl::context::tls);
    return context;
}
