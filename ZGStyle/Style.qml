pragma Singleton
import QtQuick

QtObject {
    property bool isDarkTheme: Qt.styleHints.colorScheme === Qt.Dark

    property color accentColor: isDarkTheme ? "#43b5ff" : "#0078d4"

    property color lighter: isDarkTheme ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(0, 0, 0, 0.06)
    property color lighter2: isDarkTheme ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.08)
    property color darker: isDarkTheme ? Qt.rgba(0, 0, 0, 0.3) : Qt.rgba(0, 0, 0, 0.2)

    property color windowBackgroundQWKLegacy: isDarkTheme ? "#00091a" : "#c3ccd9"
    property color windowBackgroundNoQWK: isDarkTheme ? "#282828" : "#f8f8f8"
    property color windowColor: isDarkTheme ? Qt.rgba(0.2, 0.2, 0.2, 0.7) : Qt.rgba(1, 1, 1, 0.5) // for dark, Qt.rgba(0.3, 0.3, 0.3, 0.4) matches "explorer"
    property color masonryViewBackground: isDarkTheme ? Qt.rgba(0, 0, 0, 0.35) : Qt.rgba(1, 1, 1, 0.5)
    property color masonryViewBackgroundBorder: isDarkTheme ? "transparent" : Qt.rgba(0, 0, 0, 0.06)
    property color opaqueMasonryViewBackground: isDarkTheme ? "#242828" : "#eff4f8"
    property color opaqueMasonryViewBackgroundWithOpacity: isDarkTheme ? "#b3242828" : "#b3eff4f8"

    property color scrollBarHandle: isDarkTheme ? "#4a4a4a" : "#888888"
    property color scrollBarHandleBackgroundHovered: "#676767"
    property color scrollBarHandleHovered: "#878787"
    property color scrollBarHandlePressed: "#505050"

    property color text: isDarkTheme ? "#fff" : "#000"
    property color textError: "#ff4f32"
    property color textSelected: isDarkTheme ? text : "#fff"
    property color textSelectedBackground: isDarkTheme ? "#2980b9" : accentColor

    property color buttonIconDisabled: "#f07070"

    property color tooltipBackground: isDarkTheme ? "#363636" : "#f9f9f9"
    property color tooltipBorder: isDarkTheme ? "#505050" : "#eaebec"

    property color popupBackground: isDarkTheme ? "#303030" : "#d5d5d5"
    property color popupBorder: isDarkTheme ? "#404040" : "#bcbcbc"

    property color menuBackground: isDarkTheme ? "#2c2c2c" : "#d5d5d5"
    property color menuBorder: isDarkTheme ? "#33000000" : "#bcbcbc"

    property color brickSelected: isDarkTheme ? "#385671" : Qt.rgba(0, 0.55, 1, 0.2)
    property color brickSelectedBorder: isDarkTheme ? "#456b8c" : Qt.rgba(0, 0.55, 1, 0.4)
    property color brickHovered: isDarkTheme ? Qt.rgba(0.7, 0.7, 0.7, 0.1) : Qt.rgba(0, 0.55, 1, 0.1)
    property color brickPressed: isDarkTheme ? "#2e465c" : Qt.darker(Qt.rgba(0, 0.55, 1, 0.2), 1.2)

    property color brickImageSelected: isDarkTheme ? "#7cbdf9" : Qt.rgba(0, 0.55, 1, 0.4)
    property color brickImageHovered: isDarkTheme ? "#545454" : Qt.lighter(Qt.rgba(0, 0.55, 1, 0.2), 1.5)
    property color brickImagePressed: isDarkTheme ? "#4d769b" : Qt.darker(Qt.rgba(0, 0.55, 1, 0.4), 1.2)

    property color brickInfoPanelSelected: isDarkTheme ? "#B33d5d7b" : Qt.lighter(Qt.rgba(0, 0.55, 1, 0.7), 1.7)
    property color brickInfoPanelHovered: isDarkTheme ? Qt.rgba(0.15, 0.15, 0.15, 0.7) : Qt.lighter(Qt.rgba(0, 0.55, 1, 0.7), 1.9)
    property color brickInfoPanelPressed: isDarkTheme ? "#B31a384e" : Qt.lighter(Qt.rgba(0, 0.55, 1, 0.7), 1.6)
    property color persistentSelectionBorder: "#ffd43b"

    property color folderIcon: isDarkTheme ? "#397db1" : "#397db2"
    property color closeButtonPressed: "#b3271c"
    property color closeButtonHovered: "#c42b1c"
    property color closeButtonHoveredIcon: "#fff"

    property color viewerBackground: isDarkTheme ? "#000" : "#fff"
    property color viewerPanelBackground: isDarkTheme ? "#282828" : "#fff"
    property color viewerMainText: isDarkTheme ? "#fff" : "#000"
    property color viewerMainTextOutline: isDarkTheme ? "#000" : "#fff"
    property color viewerSecondaryText: isDarkTheme ? "#ababab" : "#555555"

    property color sliderBackgroundColor: isDarkTheme ? Qt.rgba(1, 1, 1, 0.55) : Qt.rgba(0, 0, 0, 0.45)
    property color sliderFilledColor: isDarkTheme ? accentColor : Qt.darker(accentColor, 1.15)

    property color sliderNoHandleBackgroundColor: isDarkTheme ? lighter2 : lighter2
    property color sliderNoHandleFilledColor: isDarkTheme ? scrollBarHandle : darker

    property color sliderHandleBorder: isDarkTheme ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(0, 0, 0, 0.05)
    property color sliderHandleBackground: isDarkTheme ? Qt.rgba(1, 1, 1, 0.13) : "#fff"
    property color sliderHandle: sliderFilledColor
    property color sliderHandleHovered: isDarkTheme ? Qt.darker(accentColor, 1.03) : Qt.darker(accentColor, 1.02)
    property color sliderHandlePressed: isDarkTheme ? Qt.darker(accentColor, 1.07) : Qt.lighter(accentColor, 1.08)

    property color pathBackground: isDarkTheme ? lighter : Qt.rgba(1, 1, 1, 0.5)
    property color pathBackgroundHovered: isDarkTheme ? lighter2 : Qt.rgba(1, 1, 1, 0.35)
    property color pathItemHovered: isDarkTheme ? Style.lighter : Qt.rgba(0, 0, 0, 0.03)
    property color pathItemPressed: isDarkTheme ? Qt.rgba(1, 1, 1, 0.035) : Qt.rgba(0, 0, 0, 0.07)

    property color tabBarBorder: Qt.rgba(1, 1, 1, 0.07)
    property color tabBarBackground: Qt.rgba(0, 0, 0, 0.1)
}
