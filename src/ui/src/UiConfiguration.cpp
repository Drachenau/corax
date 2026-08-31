#include <corax/ui/UiConfiguration.h>

#include <QQuickStyle>
#include <QString>

namespace corax::ui
{

void configureQuickControls()
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
}

} // namespace corax::ui
