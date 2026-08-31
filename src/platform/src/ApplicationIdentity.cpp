#include <corax/platform/ApplicationIdentity.h>

namespace corax::platform
{

QString ApplicationIdentity::displayName()
{
    return QStringLiteral("Corax");
}

QString ApplicationIdentity::organizationDomain()
{
    // OA-019 keeps the reverse-DNS identity open until packaging work starts.
    return {};
}

QString ApplicationIdentity::organizationName()
{
    return QStringLiteral("Corax");
}

} // namespace corax::platform
