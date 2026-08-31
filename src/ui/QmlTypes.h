#pragma once

#include <corax/presentation/JobListModel.h>
#include <corax/presentation/JobsController.h>
#include <corax/presentation/ProjectController.h>

#include <QQmlEngine>
#include <QtQmlIntegration/qqmlintegration.h>

struct ProjectControllerForeign final
{
    Q_GADGET
    QML_FOREIGN(corax::presentation::ProjectController)
    QML_NAMED_ELEMENT(ProjectController)
    QML_UNCREATABLE("ProjectController is created by the Corax composition root.")
};

struct JobsControllerForeign final
{
    Q_GADGET
    QML_FOREIGN(corax::presentation::JobsController)
    QML_NAMED_ELEMENT(JobsController)
    QML_UNCREATABLE("JobsController is created by the Corax composition root.")
};

struct JobListModelForeign final
{
    Q_GADGET
    QML_FOREIGN(corax::presentation::JobListModel)
    QML_ANONYMOUS
};
