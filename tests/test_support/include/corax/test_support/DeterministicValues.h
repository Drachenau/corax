#pragma once

#include <QDateTime>
#include <QUuid>

namespace corax::test_support
{

[[nodiscard]] inline QUuid fixedProjectId()
{
    return QUuid(QStringLiteral("12345678-1234-4abc-8def-1234567890ab"));
}

[[nodiscard]] inline QDateTime fixedCreationTime()
{
    return QDateTime::fromString(QStringLiteral("2026-08-30T12:00:00.000Z"), Qt::ISODateWithMs);
}

} // namespace corax::test_support
