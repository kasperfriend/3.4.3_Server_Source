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
#include "DataPaths.h"
#include "Log.h"
#include "Memory.h"
#include <string>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/store.h>
#include <openssl/ui.h>
#include <openssl/x509.h>

namespace fs = boost::filesystem;

namespace
{
auto CreatePasswordUiMethodFromPemCallback(::pem_password_cb* callback)
{
    return Trinity::make_unique_ptr_with_deleter(UI_UTIL_wrap_read_pem_callback(callback, 0), ::UI_destroy_method);
}

auto OpenOpenSSLStore(fs::path const& storePath, UI_METHOD const* passwordCallback, void* passwordCallbackData)
{
    std::string uri;
    uri.reserve(6 + storePath.string().size());

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

std::string DisplayPath(fs::path const& path)
{
    return path.generic_string();
}

void LogMissingDataFile(Trinity::ResolvedDataPath const& file)
{
    TC_LOG_ERROR("server.ssl", "{} = \"{}\" was not found. Tried:{}", file.ConfigName, file.ConfiguredValue, Trinity::DescribeDataPathCandidates(file.Candidates));
    TC_LOG_ERROR("server.ssl", "Copy bnetserver.cert.pem and bnetserver.key.pem next to bnetserver.exe (they live in src/server/bnetserver in the source tree and are placed there by the build), or set {} to an absolute path. Keep both files in the same directory unless the certificate and the key are in separate files.", file.ConfigName);
    TC_LOG_ERROR("server.ssl", "For a private or bot server with no certificate of its own, \"GenerateSelfSignedCertificate = 1\" in {} lets bnetserver create a self-signed key pair at these paths instead.", sConfigMgr->GetFilename());
}

std::string GetOpenSSLErrorString()
{
    char buffer[256] = "";
    ::ERR_error_string_n(::ERR_get_error(), buffer, sizeof(buffer));
    return buffer;
}

/// Writes the PEM blocks OpenSSL hands it into a new file, and closes it again even
/// when a write fails, so a half written file is never retried as valid.
class PemFileWriter
{
public:
    explicit PemFileWriter(fs::path const& path) :
        _bio(::BIO_new_file(path.generic_string().c_str(), "wb"))
    {
    }

    PemFileWriter(PemFileWriter const&) = delete;
    PemFileWriter& operator=(PemFileWriter const&) = delete;

    ~PemFileWriter()
    {
        if (_bio)
            ::BIO_free(_bio);
    }

    bool Ok() const { return _bio != nullptr; }

    bool WriteCertificate(X509* x509) { return ::PEM_write_bio_X509(_bio, x509) == 1; }

    bool WritePrivateKey(EVP_PKEY* pkey) { return ::PEM_write_bio_PrivateKey(_bio, pkey, nullptr, nullptr, 0, nullptr, nullptr) == 1; }

private:
    BIO* _bio;
};

// 2048 bit RSA signed with SHA-256, i.e. what `openssl req -x509 -newkey rsa:2048
// -sha256` produces: the TLS stack of the client accepts it without extra
// configuration, and the key is small enough that generating it at startup is free.
constexpr int SELF_SIGNED_KEY_BITS = 2048;
constexpr long SELF_SIGNED_VALIDITY_SECONDS = 60L * 60L * 24L * 365L * 10L; // ten years, like the shipped certificate

/// Creates a self-signed certificate and its private key for a server that has no TLS
/// material of its own. When certificatePath and privateKeyPath are the same file,
/// both PEM blocks go into that one file, which the store reader below picks up just
/// as happily as two separate files.
bool GenerateSelfSignedCertificate(fs::path const& certificatePath, fs::path const& privateKeyPath, std::string& error)
{
    auto keyCtx = Trinity::make_unique_ptr_with_deleter(::EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), ::EVP_PKEY_CTX_free);
    if (!keyCtx)
    {
        error = GetOpenSSLErrorString();
        return false;
    }

    EVP_PKEY* generatedKey = nullptr;
    if (::EVP_PKEY_keygen_init(keyCtx.get()) <= 0 ||
        ::EVP_PKEY_CTX_set_rsa_keygen_bits(keyCtx.get(), SELF_SIGNED_KEY_BITS) <= 0 ||
        ::EVP_PKEY_keygen(keyCtx.get(), &generatedKey) <= 0)
    {
        error = GetOpenSSLErrorString();
        return false;
    }

    auto pkey = Trinity::make_unique_ptr_with_deleter(generatedKey, ::EVP_PKEY_free);

    auto x509 = Trinity::make_unique_ptr_with_deleter(::X509_new(), ::X509_free);
    if (!x509)
    {
        error = GetOpenSSLErrorString();
        return false;
    }

    X509_NAME* subjectName = ::X509_get_subject_name(x509.get());
    if (!subjectName)
    {
        error = GetOpenSSLErrorString();
        return false;
    }

    auto AddNameEntry = [subjectName](char const* field, char const* value)
    {
        ::X509_NAME_add_entry_by_txt(subjectName, field, MBSTRING_ASC, reinterpret_cast<unsigned char const*>(value), -1, -1, 0);
    };

    // The subject of the certificate that ships with TrinityCore, so that a
    // generated one behaves exactly like the distributed one towards the client.
    AddNameEntry("C", "US");
    AddNameEntry("O", "TrinityCore");
    AddNameEntry("OU", "Developers");
    AddNameEntry("CN", "*.*");

    ::X509_set_version(x509.get(), 2 /* X509 V3 */);
    ::ASN1_INTEGER_set(::X509_get_serialNumber(x509.get()), 1);
    ::X509_gmtime_adj(::X509_getm_notBefore(x509.get()), 0);
    ::X509_gmtime_adj(::X509_getm_notAfter(x509.get()), SELF_SIGNED_VALIDITY_SECONDS);

    if (::X509_set_pubkey(x509.get(), pkey.get()) == 0 ||
        ::X509_set_issuer_name(x509.get(), subjectName) == 0 /* self signed */ ||
        ::X509_sign(x509.get(), pkey.get(), ::EVP_sha256()) == 0)
    {
        error = GetOpenSSLErrorString();
        return false;
    }

    bool const sameFile = certificatePath == privateKeyPath;

    {
        PemFileWriter certificateWriter(certificatePath);
        if (!certificateWriter.Ok() || !certificateWriter.WriteCertificate(x509.get()) || (sameFile && !certificateWriter.WritePrivateKey(pkey.get())))
        {
            error = "could not write \"" + DisplayPath(certificatePath) + "\" (" + GetOpenSSLErrorString() + ")";
            return false;
        }
    }

    if (!sameFile)
    {
        PemFileWriter keyWriter(privateKeyPath);
        if (!keyWriter.Ok() || !keyWriter.WritePrivateKey(pkey.get()))
        {
            error = "could not write \"" + DisplayPath(privateKeyPath) + "\" (" + GetOpenSSLErrorString() + ")";
            return false;
        }
    }

    return true;
}

/// Called when the configured certificate does not exist and
/// GenerateSelfSignedCertificate is on: creates the certificate where the
/// configuration points at, and re-resolves the file so the caller loads it.
void GenerateMissingSelfSignedCertificate(Trinity::ResolvedDataPath& certificateFile)
{
    Trinity::ResolvedDataPath privateKeyFile = Trinity::ResolveDataFile("PrivateKeyFile", sConfigMgr->GetStringDefault("PrivateKeyFile", "./bnetserver.key.pem"));
    if (privateKeyFile.Found)
    {
        TC_LOG_ERROR("server.ssl", "GenerateSelfSignedCertificate is enabled, but a private key ({}) exists without a matching certificate. Not overwriting it - remove \"{}\" or fix {}.", privateKeyFile.ConfiguredValue, DisplayPath(privateKeyFile.Path), certificateFile.ConfigName);
        return;
    }

    if (!Trinity::EnsureParentDirectoryExists(certificateFile.Path))
    {
        TC_LOG_ERROR("server.ssl", "GenerateSelfSignedCertificate is enabled, but the directory of \"{}\" neither exists nor could be created.", DisplayPath(certificateFile.Path));
        return;
    }

    std::string error;
    if (!GenerateSelfSignedCertificate(certificateFile.Path, privateKeyFile.Path, error))
    {
        TC_LOG_ERROR("server.ssl", "Failed to generate the self-signed certificate \"{}\": {}", DisplayPath(certificateFile.Path), error);
        return;
    }

    if (certificateFile.Path == privateKeyFile.Path)
        TC_LOG_WARN("server.ssl", "Generated a self-signed certificate with its private key (RSA {}, valid for {} days) and wrote both to \"{}\". Use a certificate of your own before exposing bnetserver.",
            SELF_SIGNED_KEY_BITS, SELF_SIGNED_VALIDITY_SECONDS / (60 * 24), DisplayPath(certificateFile.Path));
    else
        TC_LOG_WARN("server.ssl", "Generated a self-signed key and certificate (RSA {}, valid for {} days) and wrote them to \"{}\" and \"{}\". Use a certificate of your own before exposing bnetserver.",
            SELF_SIGNED_KEY_BITS, SELF_SIGNED_VALIDITY_SECONDS / (60 * 24), DisplayPath(certificateFile.Path), DisplayPath(privateKeyFile.Path));

    certificateFile = Trinity::ResolveDataFile(certificateFile.ConfigName, certificateFile.ConfiguredValue);
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

    Trinity::ResolvedDataPath certificateFile = Trinity::ResolveDataFile("CertificatesFile", sConfigMgr->GetStringDefault("CertificatesFile", "./bnetserver.cert.pem"));

    // Read quietly: the option is documented in bnetserver.conf.dist and pointed at
    // from the error below, a warning on every single start for the servers that never
    // want a generated certificate would just be noise.
    if (!certificateFile.Found && sConfigMgr->GetBoolDefault("GenerateSelfSignedCertificate", false, true))
        GenerateMissingSelfSignedCertificate(certificateFile);

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
        Trinity::ResolvedDataPath privateKeyFile = Trinity::ResolveDataFile("PrivateKeyFile", sConfigMgr->GetStringDefault("PrivateKeyFile", "./bnetserver.key.pem"));
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
