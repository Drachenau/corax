#include <corax/application/ProjectService.h>
#include <corax/jobs/JobScheduler.h>
#include <corax/platform/ApplicationIdentity.h>
#include <corax/presentation/JobsController.h>
#include <corax/presentation/ProjectController.h>
#include <corax/storage_sqlite/SqliteProjectStore.h>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <QtQml/qqmlextensionplugin.h>

#include <algorithm>
#include <cstdlib>

Q_IMPORT_QML_PLUGIN(Corax_UiPlugin)

int main(int argc, char* argv[])
{
    QCoreApplication::setApplicationName(corax::platform::ApplicationIdentity::displayName());
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCoreApplication::setOrganizationName(corax::platform::ApplicationIdentity::organizationName());
    QCoreApplication::setOrganizationDomain(
        corax::platform::ApplicationIdentity::organizationDomain());

    QGuiApplication application(argc, argv);

    corax::storage_sqlite::SqliteProjectStore projectStore;
    corax::application::ProjectService projectService{projectStore};
    const int workerCount = std::clamp(QThread::idealThreadCount(), 1, 4);
    corax::jobs::JobScheduler jobScheduler{workerCount};
    corax::presentation::ProjectController projectController{projectService, jobScheduler};
    corax::presentation::JobsController jobsController{jobScheduler};

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &application,
        [] { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);
    engine.setInitialProperties({
        {QStringLiteral("projectController"), QVariant::fromValue(&projectController)},
        {QStringLiteral("jobsController"), QVariant::fromValue(&jobsController)},
    });
    engine.loadFromModule(QStringLiteral("Corax.Ui"), QStringLiteral("Main"));

    if (qEnvironmentVariableIsSet("CORAX_SMOKE_TEST"))
    {
        QTimer::singleShot(250, &application, &QCoreApplication::quit);
    }

    return application.exec();
}
