#include <corax/platform/ApplicationIdentity.h>

#include <corax/build/BuildConfiguration.h>

#include <QCoreApplication>

namespace corax::platform
{

void ApplicationIdentity::applyToQt()
{
    QCoreApplication::setApplicationName(displayName());
    QCoreApplication::setApplicationVersion(applicationVersion());
    QCoreApplication::setOrganizationName(organizationName());
    QCoreApplication::setOrganizationDomain(organizationDomain());
}

QString ApplicationIdentity::applicationIdentifier()
{
    return QString::fromLatin1(build::kApplicationIdentifier);
}

QString ApplicationIdentity::applicationVersion()
{
    return QString::fromLatin1(build::kApplicationVersion);
}

QString ApplicationIdentity::displayName()
{
    return QString::fromLatin1(build::kApplicationDisplayName);
}

QString ApplicationIdentity::organizationDomain()
{
    return QString::fromLatin1(build::kOrganizationDomain);
}

QString ApplicationIdentity::organizationName()
{
    return QString::fromLatin1(build::kOrganizationName);
}

} // namespace corax::platform
