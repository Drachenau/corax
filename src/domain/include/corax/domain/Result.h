// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "corax/domain/AppError.h"

#include <optional>
#include <utility>
#include <variant>

namespace corax::domain
{

template<typename T> class [[nodiscard]] Result final
{
public:
    [[nodiscard]] static Result success(T value)
    {
        return Result(std::move(value));
    }
    [[nodiscard]] static Result failure(AppError error)
    {
        return Result(std::move(error));
    }

    [[nodiscard]] bool hasValue() const noexcept
    {
        return std::holds_alternative<T>(storage_);
    }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return hasValue();
    }

    [[nodiscard]] T& value() &
    {
        return std::get<T>(storage_);
    }
    [[nodiscard]] const T& value() const&
    {
        return std::get<T>(storage_);
    }
    [[nodiscard]] T&& value() &&
    {
        return std::get<T>(std::move(storage_));
    }

    [[nodiscard]] AppError& error() &
    {
        return std::get<AppError>(storage_);
    }
    [[nodiscard]] const AppError& error() const&
    {
        return std::get<AppError>(storage_);
    }
    [[nodiscard]] AppError&& error() &&
    {
        return std::get<AppError>(std::move(storage_));
    }

private:
    explicit Result(T value) : storage_(std::move(value)) {}

    explicit Result(AppError error) : storage_(std::move(error)) {}

    std::variant<T, AppError> storage_;
};

template<> class [[nodiscard]] Result<void> final
{
public:
    [[nodiscard]] static Result success()
    {
        return Result();
    }
    [[nodiscard]] static Result failure(AppError error)
    {
        return Result(std::move(error));
    }

    [[nodiscard]] bool hasValue() const noexcept
    {
        return !error_.has_value();
    }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return hasValue();
    }

    void value() const {}

    [[nodiscard]] AppError& error() &
    {
        return error_.value();
    }
    [[nodiscard]] const AppError& error() const&
    {
        return error_.value();
    }
    [[nodiscard]] AppError&& error() &&
    {
        return std::move(error_.value());
    }

private:
    Result() = default;

    explicit Result(AppError error) : error_(std::move(error)) {}

    std::optional<AppError> error_;
};

} // namespace corax::domain
