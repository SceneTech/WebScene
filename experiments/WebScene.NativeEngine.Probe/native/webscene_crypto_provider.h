#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace webscene_native {

enum class crypto_digest_algorithm : std::uint8_t {
    sha1,
    sha256
};

enum class crypto_provider_status : std::uint8_t {
    success,
    cancelled,
    invalid_output,
    provider_failure
};

[[nodiscard]] constexpr std::size_t crypto_digest_size(
    crypto_digest_algorithm algorithm) noexcept
{
    return algorithm == crypto_digest_algorithm::sha1 ? 20U : 32U;
}

[[nodiscard]] crypto_provider_status crypto_digest(
    crypto_digest_algorithm algorithm,
    std::span<const std::uint8_t> input,
    std::span<std::uint8_t> output,
    std::stop_token stop = {}) noexcept;

class secure_bytes final {
public:
    secure_bytes() = default;
    explicit secure_bytes(std::span<const std::uint8_t> value);
    ~secure_bytes();

    secure_bytes(const secure_bytes&) = delete;
    secure_bytes& operator=(const secure_bytes&) = delete;
    secure_bytes(secure_bytes&& other) noexcept;
    secure_bytes& operator=(secure_bytes&& other) noexcept;

    [[nodiscard]] std::span<const std::uint8_t> view() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    void clear() noexcept;

private:
    std::vector<std::uint8_t> bytes_;
};

struct crypto_key_metadata final {
    std::string algorithm;
    std::vector<std::string> usages;
    bool extractable{false};
};

class crypto_key_store final {
public:
    using handle = std::uint64_t;
    using key_operation = std::function<void(
        const crypto_key_metadata&,
        std::span<const std::uint8_t>)>;

    [[nodiscard]] handle create(
        std::uintptr_t realm,
        crypto_key_metadata metadata,
        std::span<const std::uint8_t> material);
    [[nodiscard]] bool use(
        handle key,
        std::uintptr_t realm,
        std::string_view required_usage,
        const key_operation& operation) const;
    [[nodiscard]] bool destroy(handle key, std::uintptr_t realm) noexcept;
    std::size_t clear_realm(std::uintptr_t realm) noexcept;
    void clear() noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    struct record final {
        std::uintptr_t realm{};
        crypto_key_metadata metadata;
        secure_bytes material;
    };

    handle next_handle_{1U};
    std::unordered_map<handle, record> records_;
};

} // namespace webscene_native
