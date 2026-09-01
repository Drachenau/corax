#pragma once

#include <QString>

namespace corax::platform
{

struct ApplicationIdentity
{
    static void applyToQt();

    [[nodiscard]] static QString applicationIdentifier();
    [[nodiscard]] static QString applicationVersion();
    [[nodiscard]] static QString displayName();
    [[nodiscard]] static QString organizationDomain();
    [[nodiscard]] static QString organizationName();
};

} // namespace corax::platform
