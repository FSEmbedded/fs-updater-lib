#include <gtest/gtest.h>

#include "handle_update/cert_image_verifier.h"
#include "handle_update/app_image_format.h"
#include "logger/LoggerHandler.h"
#include "logger/LoggerSinkStdout.h"

#include <botan/auto_rng.h>
#include <botan/pkcs10.h>
#include <botan/pk_keys.h>
#include <botan/pubkey.h>
#include <botan/rsa.h>
#include <botan/x509_ca.h>
#include <botan/x509cert.h>
#include <botan/x509self.h>

extern "C" {
#include <zlib.h>
#include <unistd.h>
}

#include <chrono>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr size_t kTimestampSize = 26;

std::shared_ptr<logger::LoggerHandler> test_logger()
{
    static auto handler = logger::LoggerHandler::initLogger(
        std::make_shared<logger::LoggerSinkStdout>(logger::logLevel::ERROR));
    return handler;
}

void put_be64(std::vector<uint8_t>& out, uint64_t v)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<uint8_t>((v >> shift) & 0xFF));
    }
}

void put_be32(std::vector<uint8_t>& out, uint32_t v)
{
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<uint8_t>((v >> shift) & 0xFF));
    }
}

std::vector<uint8_t> build_header(uint64_t declared_size, uint32_t version)
{
    std::vector<uint8_t> header;
    put_be64(header, declared_size);
    put_be32(header, version);
    uint32_t crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, header.data(), 12);
    put_be32(header, crc);
    return header;
}

class TempFile {
  public:
    explicit TempFile(const std::string& bytes, const char* tag = "generic")
    {
        std::string tmpl = std::string("/tmp/certver-") + tag + "-XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        fd_ = mkstemp(buf.data());
        EXPECT_GE(fd_, 0);
        path_.assign(buf.data());
        EXPECT_EQ(write(fd_, bytes.data(), bytes.size()),
                  static_cast<ssize_t>(bytes.size()));
    }
    ~TempFile()
    {
        if (fd_ >= 0) {
            close(fd_);
        }
        unlink(path_.c_str());
    }
    [[nodiscard]] const std::string& path() const { return path_; }

  private:
    int fd_ = -1;
    std::string path_;
};

// One CA + one CA-signed leaf, generated fresh per suite run. RSA-3072:
// Botan scores RSA-2048 at ~110 bits, below the verifier's 112-bit floor.
struct PkiFixture {
    Botan::AutoSeeded_RNG rng;
    Botan::RSA_PrivateKey ca_key{rng, 3072};
    Botan::RSA_PrivateKey leaf_key{rng, 3072};
    Botan::X509_Certificate ca_cert;
    Botan::X509_Certificate leaf_cert;

    Botan::X509_Certificate leaf_without_eku;

    PkiFixture()
    {
        Botan::X509_Cert_Options ca_opts("Test CA/DE/fsup/testing");
        ca_opts.CA_key(1);
        ca_cert = Botan::X509::create_self_signed_cert(ca_opts, ca_key, "SHA-256", rng);

        Botan::X509_CA ca(ca_cert, ca_key, "SHA-256", rng);
        auto now = std::chrono::system_clock::now();
        const Botan::X509_Time not_before(now - std::chrono::hours(1));
        const Botan::X509_Time not_after(now + std::chrono::hours(24 * 365));

        // The verifier accepts only codeSigning leaves.
        Botan::X509_Cert_Options leaf_opts("Test Leaf/DE/fsup/testing");
        leaf_opts.add_ex_constraint("PKIX.CodeSigning");
        leaf_cert = ca.sign_request(
            Botan::X509::create_cert_req(leaf_opts, leaf_key, "SHA-256", rng), rng,
            not_before, not_after);

        Botan::X509_Cert_Options plain_opts("Test Leaf NoEKU/DE/fsup/testing");
        leaf_without_eku = ca.sign_request(
            Botan::X509::create_cert_req(plain_opts, leaf_key, "SHA-256", rng), rng,
            not_before, not_after);
    }
};

PkiFixture& pki()
{
    static PkiFixture fixture;
    return fixture;
}

std::string assemble_image(const std::string& content, const std::string& timestamp,
                           const std::string& signature, const std::string& trailer = "")
{
    std::vector<uint8_t> header = build_header(content.size(), 1);
    std::string bytes(header.begin(), header.end());
    bytes += content;
    std::string ts = timestamp;
    ts.resize(kTimestampSize, '\0');
    bytes += ts;
    bytes += signature;
    bytes += trailer;
    return bytes;
}

const std::string kContent = "SQUASHFS-PAYLOAD-0123456789abcdef-SQUASHFS-PAYLOAD-0123456789ab";
const std::string kTimestamp = "2026-07-05T12:34:56Z";

std::string sign_image(Botan::RSA_PrivateKey& key, const std::string& content,
                       const std::string& timestamp)
{
    Botan::AutoSeeded_RNG rng;
    Botan::PK_Signer signer(key, rng, crypto::SIGNATURE_SCHEME, Botan::IEEE_1363);
    signer.update(reinterpret_cast<const uint8_t*>(content.data()), content.size());
    std::string ts = timestamp;
    ts.resize(kTimestampSize, '\0');
    signer.update(reinterpret_cast<const uint8_t*>(ts.data()), ts.size());
    const std::vector<uint8_t> sig = signer.signature(rng);
    return {sig.begin(), sig.end()};
}

/* ── ImageVerifier::verify_header ─────────────────────────────────────────── */

TEST(ImageVerifierHeader, ValidHeaderParses)
{
    updater::ImageVerifier verifier(test_logger());
    std::vector<uint8_t> const header = build_header(4096, 1);
    uint64_t size = 0;
    uint32_t version = 0;
    uint32_t crc = 0;
    EXPECT_TRUE(verifier.verify_header(header, size, version, crc));
    EXPECT_EQ(size, 4096U);
    EXPECT_EQ(version, 1U);
}

TEST(ImageVerifierHeader, ShortBufferRejected)
{
    updater::ImageVerifier verifier(test_logger());
    std::vector<uint8_t> short_buf(12, 0x11);
    uint64_t size = 0;
    uint32_t version = 0;
    uint32_t crc = 0;
    EXPECT_FALSE(verifier.verify_header(short_buf, size, version, crc));
}

TEST(ImageVerifierHeader, CorruptCrcRejected)
{
    updater::ImageVerifier verifier(test_logger());
    std::vector<uint8_t> header = build_header(4096, 1);
    header[15] ^= 0xFF;
    uint64_t size = 0;
    uint32_t version = 0;
    uint32_t crc = 0;
    EXPECT_FALSE(verifier.verify_header(header, size, version, crc));
}

TEST(ImageVerifierHeader, ZeroSizeHeaderInvalid)
{
    updater::ImageVerifier verifier(test_logger());
    std::vector<uint8_t> const header = build_header(0, 1);
    uint64_t size = 0;
    uint32_t version = 0;
    uint32_t crc = 0;
    EXPECT_FALSE(verifier.verify_header(header, size, version, crc));
}

/* ── ImageVerifier::verify_signature ──────────────────────────────────────── */

TEST(ImageVerifierSignature, ValidSignatureVerifies)
{
    const std::string sig = sign_image(pki().leaf_key, kContent, kTimestamp);
    TempFile img(assemble_image(kContent, kTimestamp, sig), "signed");

    applicationImage app(img.path(), test_logger());
    updater::ImageVerifier verifier(test_logger());
    EXPECT_TRUE(verifier.verify_signature(pki().leaf_cert, app, app.getSizeOfImage(),
                                          app.getTimestamp(), app.getSignature()));
}

TEST(ImageVerifierSignature, TamperedContentFailsVerification)
{
    const std::string sig = sign_image(pki().leaf_key, kContent, kTimestamp);
    std::string tampered = kContent;
    tampered[10] ^= 0x01;
    TempFile img(assemble_image(tampered, kTimestamp, sig), "tampered");

    applicationImage app(img.path(), test_logger());
    updater::ImageVerifier verifier(test_logger());
    EXPECT_FALSE(verifier.verify_signature(pki().leaf_cert, app, app.getSizeOfImage(),
                                           app.getTimestamp(), app.getSignature()));
}

TEST(ImageVerifierSignature, ForeignKeySignatureFailsVerification)
{
    // Signed with the CA key, verified against the leaf certificate.
    const std::string sig = sign_image(pki().ca_key, kContent, kTimestamp);
    TempFile img(assemble_image(kContent, kTimestamp, sig), "foreign");

    applicationImage app(img.path(), test_logger());
    updater::ImageVerifier verifier(test_logger());
    EXPECT_FALSE(verifier.verify_signature(pki().leaf_cert, app, app.getSizeOfImage(),
                                           app.getTimestamp(), app.getSignature()));
}

/* ── CertificateVerifier ──────────────────────────────────────────────────── */

TEST(CertificateVerifierChain, LeafSignedByTrustedCaVerifies)
{
    TempFile keyring(pki().ca_cert.PEM_encode(), "keyring");
    updater::CertificateVerifier verifier(keyring.path(), test_logger());
    EXPECT_TRUE(verifier.verify_certificate_chain({pki().leaf_cert}));
}

TEST(CertificateVerifierChain, WrongCaInKeyringRejected)
{
    Botan::AutoSeeded_RNG rng;
    Botan::RSA_PrivateKey other_key(rng, 3072);
    Botan::X509_Cert_Options other_opts("Other CA/DE/fsup/testing");
    other_opts.CA_key(1);
    const Botan::X509_Certificate other_ca =
        Botan::X509::create_self_signed_cert(other_opts, other_key, "SHA-256", rng);

    TempFile keyring(other_ca.PEM_encode(), "wrongca");
    updater::CertificateVerifier verifier(keyring.path(), test_logger());
    EXPECT_FALSE(verifier.verify_certificate_chain({pki().leaf_cert}));
}

TEST(CertificateVerifierChain, LeafWithoutCodeSigningEkuRejected)
{
    TempFile keyring(pki().ca_cert.PEM_encode(), "keyring3");
    updater::CertificateVerifier verifier(keyring.path(), test_logger());
    EXPECT_FALSE(verifier.verify_certificate_chain({pki().leaf_without_eku}));
}

TEST(CertificateVerifierChain, EmptyChainRejected)
{
    TempFile keyring(pki().ca_cert.PEM_encode(), "keyring2");
    updater::CertificateVerifier verifier(keyring.path(), test_logger());
    EXPECT_FALSE(verifier.verify_certificate_chain({}));
}

TEST(CertificateVerifierChain, MissingKeyringRejected)
{
    updater::CertificateVerifier verifier("/nonexistent/keyring.pem", test_logger());
    EXPECT_FALSE(verifier.verify_certificate_chain({pki().leaf_cert}));
}

TEST(CertificateVerifierExtract, ExtractsAppendedCertificates)
{
    const std::string sig(128, '\x5a');
    const std::string one_cert = "\n" + pki().leaf_cert.PEM_encode();
    TempFile img1(assemble_image(kContent, kTimestamp, sig, one_cert), "onecert");

    updater::CertificateVerifier verifier("/nonexistent/keyring.pem", test_logger());
    EXPECT_EQ(verifier.extract_certificates_from_image(img1.path()).size(), 1U);

    const std::string two_certs = one_cert + pki().ca_cert.PEM_encode();
    TempFile img2(assemble_image(kContent, kTimestamp, sig, two_certs), "twocerts");
    EXPECT_EQ(verifier.extract_certificates_from_image(img2.path()).size(), 2U);
}

TEST(CertificateVerifierExtract, NoCertificatesYieldsEmpty)
{
    const std::string sig(128, '\x5a');
    TempFile img(assemble_image(kContent, kTimestamp, sig), "nocert");

    updater::CertificateVerifier verifier("/nonexistent/keyring.pem", test_logger());
    EXPECT_TRUE(verifier.extract_certificates_from_image(img.path()).empty());
}

TEST(CertificateVerifierExtract, GarbagePemBlockSkipped)
{
    const std::string sig(128, '\x5a');
    const std::string garbage =
        "\n-----BEGIN CERTIFICATE-----\nnot base64 at all!!\n-----END CERTIFICATE-----\n";
    TempFile img(assemble_image(kContent, kTimestamp, sig, garbage), "garbage");

    updater::CertificateVerifier verifier("/nonexistent/keyring.pem", test_logger());
    EXPECT_TRUE(verifier.extract_certificates_from_image(img.path()).empty());
}

} // namespace
