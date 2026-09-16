#include "webscene_crypto_provider.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

#include <mbedtls/platform_util.h>
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

secure_bytes::secure_bytes(std::span<const std::uint8_t> value)
    : bytes_(value.begin(), value.end())
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

bool secure_bytes::empty() const noexcept
{
    return bytes_.empty();
}

void secure_bytes::clear() noexcept
{
    if (!bytes_.empty()) {
        mbedtls_platform_zeroize(bytes_.data(), bytes_.size());
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
