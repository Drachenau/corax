#include <corax/application/ProjectService.h>
#include <corax/jobs/JobScheduler.h>
#include <corax/platform/ApplicationIdentity.h>
#include <corax/presentation/JobsController.h>
#include <corax/presentation/ProjectController.h>
#include <corax/storage_sqlite/SqliteProjectStore.h>
#include <corax/ui/UiConfiguration.h>

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
    corax::platform::ApplicationIdentity::applyToQt();

    QGuiApplication application(argc, argv);
    corax::ui::configureQuickControls();

    corax::storage_sqlite::SqliteProjectStore projectStore;
    corax::application::ProjectService projectService{projectStore};
    const int workerCount = std::clamp(QThread::idealThreadCount(), 1, 4);
    corax::jobs::JobScheduler jobScheduler{workerCount};
    corax::presentation::ProjectController projectController{projectService, jobScheduler};
    corax::presentation::JobsController jobsController{jobScheduler};

    QQmlApplicationEngine engine;
    bool rootCreationFailed = false;
    QObject::connect(&engine,
                     &QQmlApplicationEngine::objectCreationFailed,
                     &application,
                     [&rootCreationFailed]
                     {
                         rootCreationFailed = true;
                         QCoreApplication::exit(EXIT_FAILURE);
                     });
    engine.setInitialProperties({
        {QStringLiteral("projectController"), QVariant::fromValue(&projectController)},
        {QStringLiteral("jobsController"), QVariant::fromValue(&jobsController)},
    });
    const bool smokeTest = qEnvironmentVariableIsSet("CORAX_SMOKE_TEST");
    const bool forceQmlFailure =
        smokeTest && qEnvironmentVariableIsSet("CORAX_SMOKE_TEST_FORCE_QML_FAILURE");
    engine.loadFromModule(QStringLiteral("Corax.Ui"),
                          forceQmlFailure ? QStringLiteral("MissingSmokeRoot")
                                          : QStringLiteral("Main"));

    if (rootCreationFailed || engine.rootObjects().isEmpty())
    {
        return EXIT_FAILURE;
    }

    if (smokeTest)
    {
        QTimer::singleShot(250, &application, &QCoreApplication::quit);
    }

    return application.exec();
}
