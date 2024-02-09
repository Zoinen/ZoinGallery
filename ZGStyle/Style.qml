pragma Singleton
import QtQuick

QtObject {
    property bool isDarkTheme: Qt.styleHints.colorScheme === Qt.Dark
    property color lighter: isDarkTheme ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(0, 0, 0, 0.06)
    property color lighter2: isDarkTheme ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.08)
    property color darker: isDarkTheme ? Qt.rgba(0, 0, 0, 0.3) : Qt.rgba(0, 0, 0, 0.2)

    property color windowBackgroundNoQWK: isDarkTheme ? "#282828" : "#f8f8f8"
    property color windowColor: isDarkTheme ? Qt.rgba(1, 1, 1, 0.03) : "transparent"
    property color opaqueMasonryViewBackground: isDarkTheme ? "#242828" : "#eff4f8"
    property color opaqueMasonryViewBackgroundWithOpacity: isDarkTheme ? "#b3242828" : "#b3eff4f8"

    property color handle: isDarkTheme ? "#4a4a4a" : "#888888"
    property color handleBackgroundHovered: "#676767"
    property color handleHovered: "#878787"
    property color handlePressed: "#505050"

    property color text: isDarkTheme ? "#fff" : "#000"
    property color textGray: "#ababab"
    property color textError: "#ff4f32"
    property color textSelected: isDarkTheme ? "#2980b9" : "#9ac3de"

    property color buttonIconSelected: isDarkTheme ? "#43b5ff" : "#43b5ff"
    property color buttonIconDisabled: "#707070"

    property color tooltipBackground: isDarkTheme ? "#404040" : "#d5d5d5"
    property color tooltipBorder: isDarkTheme ? "#505050" : "#bcbcbc"

    property color popupBackground: isDarkTheme ? "#303030" : "#d5d5d5"
    property color popupBorder: isDarkTheme ? "#404040" : "#bcbcbc"

    property color menuBackground: isDarkTheme ? "#2c2c2c" : "#d5d5d5"
    property color menuBorder: isDarkTheme ? "#33000000" : "#bcbcbc"

    property color brickSelected: isDarkTheme ? "#385671" : "#b4cedb"
    property color brickHovered: isDarkTheme ? "#353535" : "#b4b4b4" // TODO: LIGHT THEME
    property color brickPressed: isDarkTheme ? "#2e465c" : "#8699a3" // TODO: LIGHT THEME

    property color brickImageSelected: isDarkTheme ? "#7cbdf9" : "#b4cedb"
    property color brickImageHovered: isDarkTheme ? "#545454" : "#b4b4b4" // TODO: LIGHT THEME
    property color brickImagePressed: isDarkTheme ? "#4d769b" : "#8699a3" // TODO: LIGHT THEME

    property color brickInfoPanelSelected: isDarkTheme ? "#B33d5d7b" : "#99bce9ff" // TODO: LIGHT THEME
    property color brickInfoPanelHovered: isDarkTheme ? Qt.rgba(40/255, 40/255, 40/255, 0.7) : Qt.rgba(1, 1, 1, 0.6)
    property color brickInfoPanelPressed: isDarkTheme ? "#B31a384e" : "#99bce9ff"

    property color folderIcon: isDarkTheme ? "#397db1" : "#70a5cf"
    property color closeButtonPressed: "#b3271c"
    property color closeButtonHovered: "#c42b1c"
    property color closeButtonHoveredIcon: "#fff"

    property color viewerBackground: isDarkTheme ? "#000" : "#fff"
    property color viewerPanel: "transparent" // Qt.rgba(0, 0, 0, 0.1) //isDarkTheme ? "#000" : "#fff"
    property color pathFadeGradient: isDarkTheme ? "#333436" : "#e2e5e8"
}
