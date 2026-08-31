pragma Singleton
import QtQuick

QtObject {
    // 0 follows the operating system, 1 is light, and 2 is dark.
    property int themeMode: 0
    readonly property bool systemDark: Application.styleHints.colorScheme === Qt.Dark
    readonly property bool dark: themeMode === 2 || (themeMode === 0 && systemDark)
    readonly property color window: dark ? "#16191d" : "#f4f5f7"
    readonly property color surface: dark ? "#20242a" : "#ffffff"
    readonly property color surfaceRaised: dark ? "#292e35" : "#f9fafb"
    readonly property color border: dark ? "#454c56" : "#ccd1d8"
    readonly property color text: dark ? "#f2f4f7" : "#20242a"
    readonly property color mutedText: dark ? "#b8c0ca" : "#58616d"
    readonly property color accent: dark ? "#75a7ff" : "#245fbf"
    readonly property color focus: dark ? "#9ac0ff" : "#174f9f"
    readonly property color errorSurface: dark ? "#47262a" : "#fff0f1"
    readonly property color errorText: dark ? "#ffb9bf" : "#8f1d2c"
    readonly property int space1: 4
    readonly property int space2: 8
    readonly property int space3: 12
    readonly property int space4: 16
    readonly property int space5: 24
    readonly property int radius: 6
    readonly property int controlHeight: 36
    readonly property int focusWidth: 3
}
