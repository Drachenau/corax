#pragma once

#include <QString>

namespace corax::platform
{

struct ApplicationIdentity
{
    [[nodiscard]] static QString displayName();
    [[nodiscard]] static QString organizationDomain();
    [[nodiscard]] static QString organizationName();
};

} // namespace corax::platform
