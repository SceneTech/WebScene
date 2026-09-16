#include "webscene_crypto_provider.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

#include <mbedtls/platform_util.h>
#include <mbedtls/aes.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/sha1.h>
#include <mbedtls/sha256.h>

namespace webscene_native {
namespace {

constexpr std::size_t digest_chunk_size = 64U * 1024U;

crypto_provider_status sha1_digest(
    std::span<const std::uint8_t> input,
    std::span<std::uint8_t> output,
    std::stop_token stop) noexcept
{
    mbedtls_sha1_context context;
    mbedtls_sha1_init(&context);
    const auto cleanup = [&context] { mbedtls_sha1_free(&context); };
    if (mbedtls_sha1_starts(&context) != 0) {
        cleanup();
        return crypto_provider_status::provider_failure;
    }
    for (std::size_t offset = 0U; offset < input.size();) {
        if (stop.stop_requested()) {
            cleanup();
            return crypto_provider_status::cancelled;
        }
        const auto count = std::min(digest_chunk_size, input.size() - offset);
        if (mbedtls_sha1_update(&context, input.data() + offset, count) != 0) {
            cleanup();
            return crypto_provider_status::provider_failure;
        }
        offset += count;
    }
    const auto result = mbedtls_sha1_finish(&context, output.data());
    cleanup();
    return result == 0
        ? crypto_provider_status::success
        : crypto_provider_status::provider_failure;
}

crypto_provider_status sha256_digest(
    std::span<const std::uint8_t> input,
    std::span<std::uint8_t> output,
    std::stop_token stop) noexcept
{
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    const auto cleanup = [&context] { mbedtls_sha256_free(&context); };
    if (mbedtls_sha256_starts(&context, 0) != 0) {
        cleanup();
        return crypto_provider_status::provider_failure;
    }
    for (std::size_t offset = 0U; offset < input.size();) {
        if (stop.stop_requested()) {
            cleanup();
            return crypto_provider_status::cancelled;
        }
        const auto count = std::min(digest_chunk_size, input.size() - offset);
        if (mbedtls_sha256_update(&context, input.data() + offset, count) != 0) {
            cleanup();
            return crypto_provider_status::provider_failure;
        }
        offset += count;
    }
    const auto result = mbedtls_sha256_finish(&context, output.data());
    cleanup();
    return result == 0
        ? crypto_provider_status::success
        : crypto_provider_status::provider_failure;
}

} // namespace

void crypto_zeroize(std::span<std::uint8_t> bytes) noexcept
{
    if (!bytes.empty()) mbedtls_platform_zeroize(bytes.data(), bytes.size());
}

crypto_provider_status crypto_aes_gcm_encrypt(
    std::span<const std::uint8_t> key,
    std::span<const std::uint8_t> iv,
    std::span<const std::uint8_t> additional_data,
    std::span<const std::uint8_t> plaintext,
    std::size_t tag_bytes,
    secure_bytes& output,
    std::stop_token stop) noexcept
{
    if ((key.size() != 16U && key.size() != 24U && key.size() != 32U)
        || iv.empty() || tag_bytes < 4U || tag_bytes > 16U
        || plaintext.size() > std::numeric_limits<std::size_t>::max() - tag_bytes) {
        return crypto_provider_status::provider_failure;
    }
    if (stop.stop_requested()) return crypto_provider_status::cancelled;
    mbedtls_gcm_context context;
    mbedtls_gcm_init(&context);
    if (mbedtls_gcm_setkey(
            &context, MBEDTLS_CIPHER_ID_AES, key.data(), key.size() * 8U) != 0) {
        mbedtls_gcm_free(&context);
        return crypto_provider_status::provider_failure;
    }
    secure_bytes result(plaintext.size() + tag_bytes);
    auto bytes = result.mutable_view();
    const auto status = mbedtls_gcm_crypt_and_tag(
        &context,
        MBEDTLS_GCM_ENCRYPT,
        plaintext.size(),
        iv.data(), iv.size(),
        additional_data.data(), additional_data.size(),
        plaintext.data(), bytes.data(),
        tag_bytes, bytes.data() + plaintext.size());
    mbedtls_gcm_free(&context);
    if (status != 0) return crypto_provider_status::provider_failure;
    if (stop.stop_requested()) return crypto_provider_status::cancelled;
    output = std::move(result);
    return crypto_provider_status::success;
}

crypto_provider_status crypto_aes_gcm_decrypt(
    std::span<const std::uint8_t> key,
    std::span<const std::uint8_t> iv,
    std::span<const std::uint8_t> additional_data,
    std::span<const std::uint8_t> ciphertext_and_tag,
    std::size_t tag_bytes,
    secure_bytes& output,
    std::stop_token stop) noexcept
{
    if ((key.size() != 16U && key.size() != 24U && key.size() != 32U)
        || iv.empty() || tag_bytes < 4U || tag_bytes > 16U
        || ciphertext_and_tag.size() < tag_bytes) {
        return crypto_provider_status::provider_failure;
    }
    if (stop.stop_requested()) return crypto_provider_status::cancelled;
    mbedtls_gcm_context context;
    mbedtls_gcm_init(&context);
    if (mbedtls_gcm_setkey(
            &context, MBEDTLS_CIPHER_ID_AES, key.data(), key.size() * 8U) != 0) {
        mbedtls_gcm_free(&context);
        return crypto_provider_status::provider_failure;
    }
    const auto ciphertext_size = ciphertext_and_tag.size() - tag_bytes;
    secure_bytes result(ciphertext_size);
    const auto status = mbedtls_gcm_auth_decrypt(
        &context,
        ciphertext_size,
        iv.data(), iv.size(),
        additional_data.data(), additional_data.size(),
        ciphertext_and_tag.data() + ciphertext_size, tag_bytes,
        ciphertext_and_tag.data(), result.mutable_view().data());
    mbedtls_gcm_free(&context);
    if (status != 0) return crypto_provider_status::provider_failure;
    if (stop.stop_requested()) return crypto_provider_status::cancelled;
    output = std::move(result);
    return crypto_provider_status::success;
}

crypto_provider_status crypto_aes_cbc_decrypt(
    std::span<const std::uint8_t> key,
    std::span<const std::uint8_t> iv,
    std::span<const std::uint8_t> ciphertext,
    secure_bytes& output,
    std::stop_token stop) noexcept
{
    if ((key.size() != 16U && key.size() != 24U && key.size() != 32U)
        || iv.size() != 16U || ciphertext.empty() || ciphertext.size() % 16U != 0U) {
        return crypto_provider_status::provider_failure;
    }
    if (stop.stop_requested()) return crypto_provider_status::cancelled;
    mbedtls_aes_context context;
    mbedtls_aes_init(&context);
    if (mbedtls_aes_setkey_dec(&context, key.data(), key.size() * 8U) != 0) {
        mbedtls_aes_free(&context);
        return crypto_provider_status::provider_failure;
    }
    secure_bytes result(ciphertext.size());
    std::array<std::uint8_t, 16U> iv_copy{};
    std::copy(iv.begin(), iv.end(), iv_copy.begin());
    const auto status = mbedtls_aes_crypt_cbc(
        &context, MBEDTLS_AES_DECRYPT, ciphertext.size(), iv_copy.data(),
        ciphertext.data(), result.mutable_view().data());
    crypto_zeroize(iv_copy);
    mbedtls_aes_free(&context);
    if (status != 0) return crypto_provider_status::provider_failure;
    if (stop.stop_requested()) return crypto_provider_status::cancelled;

    const auto bytes = result.mutable_view();
    const auto padding = bytes.back();
    unsigned mismatch = padding == 0U || padding > 16U ? 1U : 0U;
    for (std::size_t offset = 0U; offset < 16U; ++offset) {
        const auto mask = static_cast<std::uint8_t>(
            offset < padding ? 0xffU : 0U);
        mismatch |= static_cast<unsigned>(
            (bytes[bytes.size() - 1U - offset] ^ padding) & mask);
    }
    if (mismatch != 0U) return crypto_provider_status::provider_failure;
    secure_bytes unpadded(bytes.first(bytes.size() - padding));
    result.clear();
    output = std::move(unpadded);
    return crypto_provider_status::success;
}

crypto_provider_status crypto_digest(
    crypto_digest_algorithm algorithm,
    std::span<const std::uint8_t> input,
    std::span<std::uint8_t> output,
    std::stop_token stop) noexcept
{
    if (output.size() != crypto_digest_size(algorithm)) {
        return crypto_provider_status::invalid_output;
    }
    if (stop.stop_requested()) return crypto_provider_status::cancelled;
    return algorithm == crypto_digest_algorithm::sha1
        ? sha1_digest(input, output, stop)
        : sha256_digest(input, output, stop);
}

crypto_provider_status crypto_hmac_sign(
    crypto_digest_algorithm algorithm,
    std::span<const std::uint8_t> key,
    std::span<const std::uint8_t> input,
    std::span<std::uint8_t> output,
    std::stop_token stop) noexcept
{
    if (key.empty() || output.size() != crypto_digest_size(algorithm)) {
        return crypto_provider_status::invalid_output;
    }
    if (stop.stop_requested()) return crypto_provider_status::cancelled;
    const auto type = algorithm == crypto_digest_algorithm::sha1
        ? MBEDTLS_MD_SHA1 : MBEDTLS_MD_SHA256;
    const auto* info = mbedtls_md_info_from_type(type);
    if (info == nullptr
        || mbedtls_md_hmac(
            info, key.data(), key.size(), input.data(), input.size(), output.data()) != 0) {
        return crypto_provider_status::provider_failure;
    }
    return stop.stop_requested()
        ? crypto_provider_status::cancelled
        : crypto_provider_status::success;
}

secure_bytes::secure_bytes(std::span<const std::uint8_t> value)
    : bytes_(value.begin(), value.end())
{
}

secure_bytes::secure_bytes(std::size_t size)
    : bytes_(size)
{
}

secure_bytes::~secure_bytes()
{
    clear();
}

secure_bytes::secure_bytes(secure_bytes&& other) noexcept
    : bytes_(std::move(other.bytes_))
{
    other.bytes_.clear();
}

secure_bytes& secure_bytes::operator=(secure_bytes&& other) noexcept
{
    if (this == &other) return *this;
    clear();
    bytes_ = std::move(other.bytes_);
    other.bytes_.clear();
    return *this;
}

std::span<const std::uint8_t> secure_bytes::view() const noexcept
{
    return bytes_;
}

std::span<std::uint8_t> secure_bytes::mutable_view() noexcept
{
    return bytes_;
}

std::size_t secure_bytes::size() const noexcept
{
    return bytes_.size();
}

bool secure_bytes::empty() const noexcept
{
    return bytes_.empty();
}

void secure_bytes::clear() noexcept
{
    if (!bytes_.empty()) {
        crypto_zeroize(bytes_);
        std::vector<std::uint8_t>{}.swap(bytes_);
    }
}

crypto_key_store::handle crypto_key_store::create(
    std::uintptr_t realm,
    crypto_key_metadata metadata,
    std::span<const std::uint8_t> material)
{
    if (realm == 0U || material.empty()) return 0U;
    while (next_handle_ == 0U || records_.contains(next_handle_)) ++next_handle_;
    const auto result = next_handle_++;
    records_.emplace(
        result,
        record{realm, std::move(metadata), secure_bytes(material)});
    return result;
}

bool crypto_key_store::use(
    handle key,
    std::uintptr_t realm,
    std::string_view required_usage,
    const key_operation& operation) const
{
    const auto found = records_.find(key);
    if (found == records_.end() || found->second.realm != realm || !operation) return false;
    const auto& usages = found->second.metadata.usages;
    if (!required_usage.empty()
        && std::find(usages.begin(), usages.end(), required_usage) == usages.end()) {
        return false;
    }
    operation(found->second.metadata, found->second.material.view());
    return true;
}

std::optional<crypto_key_metadata> crypto_key_store::describe(
    handle key,
    std::uintptr_t realm) const
{
    const auto found = records_.find(key);
    if (found == records_.end() || found->second.realm != realm) return std::nullopt;
    return found->second.metadata;
}

bool crypto_key_store::destroy(handle key, std::uintptr_t realm) noexcept
{
    const auto found = records_.find(key);
    if (found == records_.end() || found->second.realm != realm) return false;
    records_.erase(found);
    return true;
}

std::size_t crypto_key_store::clear_realm(std::uintptr_t realm) noexcept
{
    return std::erase_if(records_, [realm](const auto& item) {
        return item.second.realm == realm;
    });
}

void crypto_key_store::clear() noexcept
{
    records_.clear();
}

std::size_t crypto_key_store::size() const noexcept
{
    return records_.size();
}

} // namespace webscene_native
