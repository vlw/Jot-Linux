// Design tokens — port of DesignTokens.swift / MotionTokens.swift.
import QtQuick

QtObject {
    id: t
    property bool dark: Qt.styleHints.colorScheme === Qt.ColorScheme.Dark

    // Colors (light / dark)
    readonly property color primary: dark ? "#A8C7FA" : "#0B57D0"
    readonly property color primaryInk: dark ? "#062E6F" : "#FFFFFF"
    readonly property color primaryContainer: dark ? "#0842A0" : "#D3E3FD"
    readonly property color primaryContainerInk: dark ? "#D3E3FD" : "#041E49"
    readonly property color surface: dark ? "#1E1F20" : "#FFFFFF"
    readonly property color surfaceContainer: dark ? "#28292A" : "#F0F4F9"
    readonly property color windowBackground: dark ? "#131314" : "#F8FAFD"
    readonly property color ink: dark ? "#E3E3E3" : "#1F1F1F"
    readonly property color inkVariant: dark ? "#C4C7C5" : "#444746"
    readonly property color outline: dark ? "#8E918F" : "#747775"
    readonly property color outlineVariant: dark ? "#444746" : "#C4C7C5"
    readonly property color error: dark ? "#F2B8B5" : "#B3261E"
    readonly property color errorContainer: dark ? "#8C1D18" : "#F9DEDC"
    readonly property color errorContainerInk: dark ? "#F9DEDC" : "#410E0B"
    readonly property color success: dark ? "#6DD58C" : "#146C2E"

    readonly property color gBlue: "#4285F4"
    readonly property color gRed: "#EA4335"
    readonly property color gYellow: "#FBBC04"
    readonly property color gGreen: "#34A853"
    readonly property var brandQuad: [gBlue, gRed, gYellow, gGreen]
    readonly property color cutInk: "#C5221F"
    readonly property color sidebarBackground: dark ? "#1B1C1D" : "#EEF2F7"

    // Tiles
    readonly property color tileOrange: dark ? "#FF9F0A" : "#FF9500"
    readonly property color tileGray: "#8E8E93"
    readonly property color tileTeal: dark ? "#40C8E0" : "#30B0C7"
    readonly property color tileGreen: dark ? "#30D158" : "#34C759"
    readonly property color tileIndigo: dark ? "#5E5CE6" : "#5856D6"
    readonly property color tilePink: dark ? "#FF375F" : "#FF2D55"

    // Radii & spacing
    readonly property int radiusXs: 4
    readonly property int radiusSmall: 8
    readonly property int radiusMedium: 12
    readonly property int radiusLarge: 16
    readonly property int radiusXl: 28
    readonly property int sXxs: 4
    readonly property int sXs: 8
    readonly property int sS: 12
    readonly property int sM: 16
    readonly property int sL: 20
    readonly property int sXl: 24
    readonly property int sXxl: 32

    // Type
    readonly property string family: fonts.uiFamily
    readonly property string codeFamily: fonts.codeFamily
    // Every axis is pinned explicitly: the file also carries slnt (−10…0) and
    // wdth, and letting Qt pick a named instance produced slanted, uneven glyphs.
    function font(size, weight, code) {
        if (!fonts.brandFonts)
            return { family: code ? codeFamily : family, pixelSize: size, weight: weight, italic: false }
        if (code)
            return { family: codeFamily, pixelSize: size, weight: weight, italic: false,
                     variableAxes: { "wght": weight, "MONO": 1 } }
        return {
            family: family,
            pixelSize: size,
            weight: weight,
            italic: false,
            variableAxes: { "wght": weight, "wdth": 100, "slnt": 0,
                            "opsz": Math.max(17, Math.min(144, size)), "GRAD": dark ? 25 : 0, "ROND": 0 }
        }
    }
    readonly property var display: font(32, Font.Normal)
    readonly property var headline: font(24, Font.Normal)
    readonly property var title: font(16, Font.Medium)
    readonly property var bodyLarge: font(16, Font.Normal)
    readonly property var body: font(14, Font.Normal)
    readonly property var label: font(12, Font.Medium)
    readonly property var labelSmall: font(11, Font.Medium)
    readonly property var code: font(13, Font.Normal, true)

    // Motion
    function springDuration(response) { return Math.round(response * 1000 * 1.6) }
}
