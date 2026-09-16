#include "webscene_crypto_provider.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

std::string hex(std::span<const std::uint8_t> bytes)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result(bytes.size() * 2U, '0');
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        result[index * 2U] = digits[bytes[index] >> 4U];
        result[index * 2U + 1U] = digits[bytes[index] & 0x0fU];
    }
    return result;
}

void test_nist_aes_gcm_vector_and_authentication()
{
    const std::array<std::uint8_t, 16U> key{};
    const std::array<std::uint8_t, 12U> iv{};
    const std::array<std::uint8_t, 16U> plaintext{};
    webscene_native::secure_bytes encrypted;
    require(webscene_native::crypto_aes_gcm_encrypt(
        key, iv, {}, plaintext, 16U, encrypted)
            == webscene_native::crypto_provider_status::success,
        "Mbed TLS rejected the NIST AES-GCM vector");
    require(hex(encrypted.view()) ==
        "0388dace60b6a392f328c2b971b2fe78ab6e47d42cec13bdf53a67b21257bddf",
        "NIST AES-GCM ciphertext or tag changed");
    webscene_native::secure_bytes decrypted;
    require(webscene_native::crypto_aes_gcm_decrypt(
        key, iv, {}, encrypted.view(), 16U, decrypted)
            == webscene_native::crypto_provider_status::success,
        "Mbed TLS could not decrypt the NIST AES-GCM vector");
    require(decrypted.view().size() == plaintext.size()
        && std::equal(decrypted.view().begin(), decrypted.view().end(), plaintext.begin()),
        "AES-GCM plaintext changed after authenticated round trip");

    auto tampered = std::vector<std::uint8_t>(
        encrypted.view().begin(), encrypted.view().end());
    tampered.back() ^= 1U;
    webscene_native::secure_bytes rejected;
    require(webscene_native::crypto_aes_gcm_decrypt(
        key, iv, {}, tampered, 16U, rejected)
            == webscene_native::crypto_provider_status::provider_failure,
        "AES-GCM accepted a modified authentication tag");
    require(rejected.empty(), "AES-GCM exposed plaintext after authentication failure");

    std::stop_source cancelled;
    cancelled.request_stop();
    require(webscene_native::crypto_aes_gcm_encrypt(
        key, iv, {}, plaintext, 16U, rejected, cancelled.get_token())
            == webscene_native::crypto_provider_status::cancelled,
        "AES-GCM ignored cancellation before provider entry");
}

void test_nist_aes_cbc_decrypt_and_padding()
{
    const std::array<std::uint8_t, 16U> key{
        0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c};
    const std::array<std::uint8_t, 16U> iv{
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f};
    const std::array<std::uint8_t, 32U> ciphertext{
        0x76,0x49,0xab,0xac,0x81,0x19,0xb2,0x46,0xce,0xe9,0x8e,0x9b,0x12,0xe9,0x19,0x7d,
        0x89,0x64,0xe0,0xb1,0x49,0xc1,0x0b,0x7b,0x68,0x2e,0x6e,0x39,0xaa,0xeb,0x73,0x1c};
    webscene_native::secure_bytes plaintext;
    require(webscene_native::crypto_aes_cbc_decrypt(key, iv, ciphertext, plaintext)
            == webscene_native::crypto_provider_status::success,
        "Mbed TLS rejected the NIST AES-CBC vector with Web Crypto padding");
    require(hex(plaintext.view()) == "6bc1bee22e409f96e93d7e117393172a",
        "AES-CBC plaintext changed");
    auto tampered = ciphertext;
    tampered.back() ^= 1U;
    webscene_native::secure_bytes rejected;
    require(webscene_native::crypto_aes_cbc_decrypt(key, iv, tampered, rejected)
            == webscene_native::crypto_provider_status::provider_failure,
        "AES-CBC accepted invalid PKCS#7 padding");
    require(rejected.empty(), "AES-CBC exposed plaintext after a padding failure");
    std::stop_source cancelled; cancelled.request_stop();
    require(webscene_native::crypto_aes_cbc_decrypt(
        key, iv, ciphertext, rejected, cancelled.get_token())
            == webscene_native::crypto_provider_status::cancelled,
        "AES-CBC ignored cancellation before provider entry");
}

void test_published_digest_vectors()
{
    constexpr std::array<std::uint8_t, 3U> abc{'a', 'b', 'c'};
    std::array<std::uint8_t, 20U> sha1{};
    std::array<std::uint8_t, 32U> sha256{};
    require(webscene_native::crypto_digest(
        webscene_native::crypto_digest_algorithm::sha1, abc, sha1)
            == webscene_native::crypto_provider_status::success,
        "Mbed TLS SHA-1 rejected the FIPS vector");
    require(hex(sha1) == "a9993e364706816aba3e25717850c26c9cd0d89d",
        "FIPS 180-4 SHA-1 abc vector changed");
    require(webscene_native::crypto_digest(
        webscene_native::crypto_digest_algorithm::sha256, abc, sha256)
            == webscene_native::crypto_provider_status::success,
        "Mbed TLS SHA-256 rejected the FIPS vector");
    require(hex(sha256) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "FIPS 180-4 SHA-256 abc vector changed");

    std::array<std::uint8_t, 32U> empty{};
    require(webscene_native::crypto_digest(
        webscene_native::crypto_digest_algorithm::sha256, {}, empty)
            == webscene_native::crypto_provider_status::success,
        "Mbed TLS SHA-256 rejected empty input");
    require(hex(empty) ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "NIST SHA-256 empty vector changed");
}

void test_cancellation_and_output_contract()
{
    std::array<std::uint8_t, 31U> short_output{};
    require(webscene_native::crypto_digest(
        webscene_native::crypto_digest_algorithm::sha256, {}, short_output)
            == webscene_native::crypto_provider_status::invalid_output,
        "Digest accepted a truncated output buffer");
    std::stop_source cancelled;
    cancelled.request_stop();
    std::array<std::uint8_t, 32U> output{};
    require(webscene_native::crypto_digest(
        webscene_native::crypto_digest_algorithm::sha256,
        {}, output, cancelled.get_token())
            == webscene_native::crypto_provider_status::cancelled,
        "Digest ignored cancellation before provider entry");
}

void test_opaque_key_lifecycle()
{
    webscene_native::crypto_key_store keys;
    constexpr std::array<std::uint8_t, 4U> secret{0xde, 0xad, 0xbe, 0xef};
    const auto key = keys.create(
        41U, {"HMAC", {"sign", "verify"}, "SHA-256", 32U, false}, secret);
    require(key != 0U && keys.size() == 1U, "Opaque key was not retained");
    bool visited = false;
    require(keys.use(key, 41U, "sign", [&](const auto& metadata, auto material) {
        visited = metadata.algorithm == "HMAC"
            && !metadata.extractable
            && std::equal(material.begin(), material.end(), secret.begin(), secret.end());
    }), "Owning realm could not use its key");
    require(visited, "Key metadata or material changed in storage");
    require(!keys.use(key, 42U, "sign", [](const auto&, auto) {}),
        "A foreign realm used an opaque key");
    require(!keys.use(key, 41U, "encrypt", [](const auto&, auto) {}),
        "A disallowed usage used an opaque key");
    require(!keys.destroy(key, 42U), "A foreign realm destroyed an opaque key");
    require(keys.clear_realm(42U) == 0U, "Foreign realm clearing removed a key");
    require(keys.clear_realm(41U) == 1U && keys.size() == 0U,
        "Realm shutdown did not destroy its opaque key");

    webscene_native::secure_bytes owned(secret);
    webscene_native::secure_bytes moved(std::move(owned));
    require(owned.empty() && moved.view().size() == secret.size(),
        "Secure material was duplicated during move");
    moved.clear();
    require(moved.empty(), "Explicit key zeroization did not release material");
}

void test_bounded_throughput()
{
    std::vector<std::uint8_t> input(8U * 1024U * 1024U, 0x5a);
    std::array<std::uint8_t, 32U> output{};
    const auto started = std::chrono::steady_clock::now();
    for (unsigned index = 0U; index < 4U; ++index) {
        require(webscene_native::crypto_digest(
            webscene_native::crypto_digest_algorithm::sha256, input, output)
                == webscene_native::crypto_provider_status::success,
            "Bounded SHA-256 throughput run failed");
    }
    require(std::chrono::steady_clock::now() - started < std::chrono::seconds(8),
        "32 MiB SHA-256 throughput exceeded the focused gate");
}

} // namespace

int main()
{
    try {
        test_published_digest_vectors();
        test_nist_aes_gcm_vector_and_authentication();
        test_nist_aes_cbc_decrypt_and_padding();
        test_cancellation_and_output_contract();
        test_opaque_key_lifecycle();
        test_bounded_throughput();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
