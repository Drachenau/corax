import "../design" as Design
import QtQuick
import QtQuick.Controls

Button {
    id: control

    Accessible.name: text
    Accessible.role: Accessible.Button
    implicitHeight: Design.DesignTokens.controlHeight
    leftPadding: Design.DesignTokens.space4
    rightPadding: Design.DesignTokens.space4

    contentItem: Label {
        text: control.text
        color: control.enabled ? Design.DesignTokens.text : Design.DesignTokens.mutedText
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        color: control.down ? Design.DesignTokens.surfaceRaised : Design.DesignTokens.surface
        radius: Design.DesignTokens.radius
        border.color: control.activeFocus ? Design.DesignTokens.focus : Design.DesignTokens.border
        border.width: control.activeFocus ? Design.DesignTokens.focusWidth : 1
    }
}
