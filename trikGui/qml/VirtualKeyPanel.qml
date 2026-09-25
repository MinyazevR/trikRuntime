import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: _virtualKeys
    implicitWidth: _layout.implicitWidth + 6
    color: activeTheme.backgroundColor
    opacity: 0.95
    border.color: activeTheme.delimeterLineColor
    border.width: 1

    ColumnLayout {
        id: _layout
        spacing: 4
        anchors.top: parent.top
        anchors.topMargin: 6
        anchors.left: parent.left
        anchors.leftMargin: 3

        VKeyButton { text: "▲"; key: Qt.Key_Up }
        VKeyButton { text: "▼"; key: Qt.Key_Down }
        VKeyButton { text: "◄"; key: Qt.Key_Left }
        VKeyButton { text: "►"; key: Qt.Key_Right }
        Rectangle {
            Layout.preferredHeight: 4
            Layout.preferredWidth: 1
            color: "transparent"
        }
        VKeyButton { text: "OK"; key: Qt.Key_Return }
        VKeyButton { text: "←"; key: Qt.Key_Escape }
        VKeyButton { text: "⏻"; key: Qt.Key_PowerOff }
    }

    component VKeyButton: Rectangle {
        id: _btn
        property string text
        property int key: Qt.Key_A
        implicitWidth: 48
        implicitHeight: 48
        radius: 8
        color: _ma.containsPress ? activeTheme.darkTrikColor : activeTheme.elementsOfListColor
        border.color: activeTheme.delimeterLineColor
        border.width: 1

        Text {
            text: _btn.text
            anchors.centerIn: parent
            font.pointSize: fontSizes.medium
            color: activeTheme.textColor
        }

        MouseArea {
            id: _ma
            anchors.fill: parent
            onClicked: {
                VirtualKeySender.sendKeyPress(_btn.key);
                if (typeof KeysController !== "undefined" && KeysController.emulateKeyPress) {
                    KeysController.emulateKeyPress(_btn.key);
                }
            }
        }
    }
}