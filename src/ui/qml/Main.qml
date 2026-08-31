pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "components"
import "design" as Design

ApplicationWindow {
    id: root

    required property ProjectController projectController
    required property JobsController jobsController

    objectName: "mainWindow"
    width: 1180
    height: 760
    minimumWidth: 860
    minimumHeight: 600
    visible: true
    title: projectController.hasProject ? qsTr("%1 - Corax").arg(projectController.projectName) : qsTr("Corax")
    color: Design.DesignTokens.window
    palette.window: Design.DesignTokens.window
    palette.windowText: Design.DesignTokens.text
    palette.base: Design.DesignTokens.surface
    palette.text: Design.DesignTokens.text
    palette.button: Design.DesignTokens.surface
    palette.buttonText: Design.DesignTokens.text
    palette.highlight: Design.DesignTokens.accent
    palette.highlightedText: "#ffffff"
    Component.onCompleted: newProjectButton.forceActiveFocus()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Frame {
            id: errorBanner

            objectName: "errorBanner"
            Layout.fillWidth: true
            visible: root.projectController.hasError
            padding: Design.DesignTokens.space3
            Accessible.name: root.projectController.errorMessage
            Accessible.role: Accessible.AlertMessage

            RowLayout {
                anchors.fill: parent
                spacing: Design.DesignTokens.space3

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Design.DesignTokens.space1

                    Label {
                        objectName: "errorMessage"
                        Layout.fillWidth: true
                        text: root.projectController.errorMessage
                        color: Design.DesignTokens.errorText
                        font.bold: true
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: root.projectController.errorRemediation
                        color: Design.DesignTokens.errorText
                        wrapMode: Text.Wrap
                    }

                    Label {
                        text: root.projectController.errorCode
                        color: Design.DesignTokens.errorText
                        font.pixelSize: 11
                    }
                }

                FocusButton {
                    text: qsTr("Details")
                    onClicked: errorDetails.open()
                }

                FocusButton {
                    text: qsTr("Dismiss")
                    onClicked: root.projectController.clearError()
                }
            }

            background: Rectangle {
                color: Design.DesignTokens.errorSurface
                border.color: Design.DesignTokens.errorText
                border.width: 1
            }
        }

        Frame {
            objectName: "recoveryRequiredBanner"
            Layout.fillWidth: true
            visible: root.projectController.recoveryRequired
            padding: Design.DesignTokens.space3
            Accessible.name: recoveryRequiredMessage.text
            Accessible.role: Accessible.AlertMessage

            Label {
                id: recoveryRequiredMessage

                objectName: "recoveryRequiredMessage"
                anchors.fill: parent
                text: qsTr("Project recovery is required. Corax cannot verify writer-lock ownership. Restart Corax after you verify which process owns the project lock.")
                color: Design.DesignTokens.errorText
                font.bold: true
                wrapMode: Text.Wrap
            }

            background: Rectangle {
                color: Design.DesignTokens.errorSurface
                border.color: Design.DesignTokens.errorText
                border.width: 1
            }
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            Frame {
                SplitView.preferredWidth: 190
                SplitView.minimumWidth: 150
                padding: Design.DesignTokens.space4
                Accessible.name: qsTr("Project navigation")
                Accessible.role: Accessible.Grouping

                ColumnLayout {
                    anchors.fill: parent
                    spacing: Design.DesignTokens.space3

                    Label {
                        text: qsTr("Library")
                        color: Design.DesignTokens.text
                        font.bold: true
                        font.pixelSize: 16
                    }

                    Label {
                        text: qsTr("Sources")
                        color: Design.DesignTokens.mutedText
                    }

                    Label {
                        text: qsTr("All assets")
                        color: Design.DesignTokens.mutedText
                    }

                    Label {
                        text: qsTr("Collections")
                        color: Design.DesignTokens.mutedText
                    }

                    Item {
                        Layout.fillHeight: true
                    }
                }

                background: Rectangle {
                    color: Design.DesignTokens.surface
                    border.color: Design.DesignTokens.border
                    border.width: 1
                }
            }

            Frame {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 400
                padding: Design.DesignTokens.space5
                Accessible.name: qsTr("Workspace")
                Accessible.role: Accessible.Grouping

                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width, 560)
                    spacing: Design.DesignTokens.space4

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: root.projectController.hasProject ? qsTr("Empty project") : qsTr("No project open")
                        color: Design.DesignTokens.text
                        font.bold: true
                        font.pixelSize: 28
                    }

                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: root.projectController.hasProject ? qsTr("No images are in this project.") : qsTr("Create a project or open an existing .corax project.")
                        color: Design.DesignTokens.mutedText
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: root.projectController.hasProject
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("Project ID: %1").arg(root.projectController.projectId)
                        color: Design.DesignTokens.mutedText
                        wrapMode: Text.WrapAnywhere
                    }

                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: Design.DesignTokens.space3

                        FocusButton {
                            id: newProjectButton

                            text: qsTr("Create project")
                            enabled: !root.projectController.busy && !root.projectController.recoveryRequired
                            onClicked: newProjectDialog.open()
                        }

                        FocusButton {
                            text: qsTr("Open project")
                            enabled: !root.projectController.busy && !root.projectController.recoveryRequired
                            onClicked: openProjectDialog.open()
                        }

                        FocusButton {
                            visible: root.projectController.hasProject
                            text: qsTr("Close project")
                            enabled: !root.projectController.busy
                            onClicked: root.projectController.closeProject()
                        }
                    }
                }

                background: Rectangle {
                    color: Design.DesignTokens.window
                    border.color: Design.DesignTokens.border
                    border.width: 1
                }
            }

            Frame {
                SplitView.preferredWidth: 250
                SplitView.minimumWidth: 190
                padding: Design.DesignTokens.space4
                Accessible.name: qsTr("Inspector")
                Accessible.role: Accessible.Grouping

                ColumnLayout {
                    anchors.fill: parent
                    spacing: Design.DesignTokens.space3

                    Label {
                        text: qsTr("Inspector")
                        color: Design.DesignTokens.text
                        font.bold: true
                        font.pixelSize: 16
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Select an item to inspect it.")
                        color: Design.DesignTokens.mutedText
                        wrapMode: Text.Wrap
                    }

                    Item {
                        Layout.fillHeight: true
                    }
                }

                background: Rectangle {
                    color: Design.DesignTokens.surface
                    border.color: Design.DesignTokens.border
                    border.width: 1
                }
            }
        }

        Frame {
            Layout.fillWidth: true
            Layout.preferredHeight: 190
            padding: Design.DesignTokens.space3
            Accessible.name: qsTr("Background tasks")
            Accessible.role: Accessible.StatusBar

            ColumnLayout {
                anchors.fill: parent
                spacing: Design.DesignTokens.space2

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        text: qsTr("Background tasks")
                        color: Design.DesignTokens.text
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.jobsController.jobs.activitySummary
                        color: Design.DesignTokens.mutedText
                    }

                    FocusButton {
                        text: qsTr("Architecture demo job (temporary)")
                        enabled: root.jobsController.acceptingWork
                        onClicked: root.jobsController.startFakeJob()
                    }
                }

                Label {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.jobsController.jobs.count === 0
                    text: qsTr("No background tasks.")
                    color: Design.DesignTokens.mutedText
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                }

                ListView {
                    id: jobsView

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.jobsController.jobs.count > 0
                    clip: true
                    spacing: Design.DesignTokens.space2
                    model: root.jobsController.jobs
                    Accessible.name: qsTr("Task list")

                    delegate: Rectangle {
                        id: jobDelegate

                        required property string jobId
                        required property string title
                        required property string stateLabel
                        required property string phaseLabel
                        required property double progress
                        required property bool indeterminate
                        required property bool canCancel
                        required property int completedUnits
                        required property int totalUnits
                        required property int issueCount
                        required property string errorMessage

                        readonly property string issueSummary: issueCount === 1 ? qsTr("1 issue") : (issueCount > 1 ? qsTr("%1 issues").arg(issueCount) : "")

                        width: ListView.view.width
                        height: Math.max(58, jobContent.implicitHeight + Design.DesignTokens.space2 * 2)
                        color: Design.DesignTokens.surface
                        radius: Design.DesignTokens.radius
                        border.color: Design.DesignTokens.border
                        Accessible.name: qsTr("%1, %2").arg(title).arg(stateLabel)
                        Accessible.description: errorMessage.length > 0 ? errorMessage : issueSummary
                        Accessible.role: Accessible.Grouping

                        RowLayout {
                            id: jobContent

                            anchors.fill: parent
                            anchors.margins: Design.DesignTokens.space2
                            spacing: Design.DesignTokens.space3

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Design.DesignTokens.space1

                                Label {
                                    Layout.fillWidth: true
                                    text: jobDelegate.title
                                    color: Design.DesignTokens.text
                                    font.bold: true
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: jobDelegate.phaseLabel.length > 0 ? qsTr("%1 - %2").arg(jobDelegate.stateLabel).arg(jobDelegate.phaseLabel) : jobDelegate.stateLabel
                                    color: Design.DesignTokens.mutedText
                                    elide: Text.ElideRight
                                }

                                Label {
                                    objectName: jobDelegate.errorMessage.length > 0 ? "jobFailureMessage" : ""
                                    Layout.fillWidth: true
                                    visible: jobDelegate.errorMessage.length > 0
                                    text: jobDelegate.errorMessage
                                    color: Design.DesignTokens.errorText
                                    wrapMode: Text.Wrap
                                    Accessible.name: qsTr("Task failure: %1").arg(text)
                                }

                                Label {
                                    objectName: jobDelegate.issueCount > 0 ? "jobIssueCount" : ""
                                    Layout.fillWidth: true
                                    visible: jobDelegate.issueCount > 0
                                    text: jobDelegate.issueSummary
                                    color: Design.DesignTokens.mutedText
                                    wrapMode: Text.Wrap
                                    Accessible.name: qsTr("Task result: %1").arg(text)
                                }
                            }

                            ProgressBar {
                                Layout.preferredWidth: 180
                                from: 0
                                to: 1
                                value: jobDelegate.progress
                                indeterminate: jobDelegate.indeterminate
                                Accessible.name: jobDelegate.totalUnits >= 0 ? qsTr("%1 of %2 work units").arg(jobDelegate.completedUnits).arg(jobDelegate.totalUnits) : qsTr("Progress unavailable")
                            }

                            FocusButton {
                                text: qsTr("Cancel")
                                visible: jobDelegate.canCancel
                                onClicked: root.jobsController.cancelJob(jobDelegate.jobId)
                            }
                        }
                    }
                }
            }

            background: Rectangle {
                color: Design.DesignTokens.surfaceRaised
                border.color: Design.DesignTokens.border
                border.width: 1
            }
        }
    }

    Dialog {
        id: newProjectDialog

        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Create project")
        standardButtons: Dialog.Cancel
        onOpened: projectNameField.forceActiveFocus()

        ColumnLayout {
            width: 420
            spacing: Design.DesignTokens.space3

            Label {
                text: qsTr("Project name")
                color: Design.DesignTokens.text
            }

            TextField {
                id: projectNameField

                Layout.fillWidth: true
                placeholderText: qsTr("Project name")
                Accessible.name: qsTr("Project name")
                onAccepted: {
                    if (text.trim().length > 0) {
                        createProjectDialog.open();
                    }
                }
            }

            FocusButton {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Choose location")
                enabled: projectNameField.text.trim().length > 0
                onClicked: createProjectDialog.open()
            }
        }
    }

    FileDialog {
        id: createProjectDialog

        title: qsTr("Create Corax project")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Corax projects (*.corax)")]
        defaultSuffix: "corax"
        onAccepted: {
            newProjectDialog.close();
            root.projectController.createProject(selectedFile, projectNameField.text);
            projectNameField.clear();
        }
    }

    FolderDialog {
        id: openProjectDialog

        title: qsTr("Open Corax project")
        onAccepted: root.projectController.openProject(selectedFolder)
    }

    Dialog {
        id: errorDetails

        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Error details")
        standardButtons: Dialog.Close

        ColumnLayout {
            width: 520
            spacing: Design.DesignTokens.space3

            Label {
                Layout.fillWidth: true
                text: qsTr("Code: %1").arg(root.projectController.errorCode)
                color: Design.DesignTokens.text
                wrapMode: Text.WrapAnywhere
            }

            Label {
                Layout.fillWidth: true
                visible: root.projectController.errorAffectedPath.length > 0
                text: qsTr("Location: %1").arg(root.projectController.errorAffectedPath)
                color: Design.DesignTokens.text
                wrapMode: Text.WrapAnywhere
            }

            TextArea {
                Layout.fillWidth: true
                Layout.preferredHeight: 130
                readOnly: true
                text: root.projectController.errorTechnicalContext
                wrapMode: TextEdit.Wrap
                Accessible.name: qsTr("Technical context")
            }
        }
    }

    header: ToolBar {
        Accessible.name: qsTr("Application bar")
        Accessible.role: Accessible.ToolBar

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Design.DesignTokens.space4
            anchors.rightMargin: Design.DesignTokens.space4
            spacing: Design.DesignTokens.space3

            Label {
                text: qsTr("Corax")
                color: Design.DesignTokens.text
                font.bold: true
                font.pixelSize: 18
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                Layout.topMargin: Design.DesignTokens.space2
                Layout.bottomMargin: Design.DesignTokens.space2
                color: Design.DesignTokens.border
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Label {
                    objectName: "projectTitle"
                    Layout.fillWidth: true
                    text: root.projectController.hasProject ? root.projectController.projectName : qsTr("No project")
                    color: Design.DesignTokens.text
                    font.bold: true
                    elide: Text.ElideRight
                    Accessible.name: qsTr("Project title: %1").arg(text)
                }

                Label {
                    visible: root.projectController.hasProject
                    text: root.projectController.projectId
                    color: Design.DesignTokens.mutedText
                    font.pixelSize: 11
                    elide: Text.ElideMiddle
                }
            }

            Label {
                visible: root.projectController.busy
                text: root.projectController.currentOperation
                color: Design.DesignTokens.mutedText
            }

            FocusButton {
                text: Design.DesignTokens.dark ? qsTr("Light theme") : qsTr("Dark theme")
                Accessible.description: qsTr("Change the application color theme")
                onClicked: Design.DesignTokens.themeMode = Design.DesignTokens.dark ? 1 : 2
            }
        }

        background: Rectangle {
            color: Design.DesignTokens.surface
            border.color: Design.DesignTokens.border
            border.width: 1
        }
    }
}
