import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15

Rectangle {
    id: _camera
    property var sensors: Sensors
    property var idList: _listSensors
    property var cameraObject: null
    property int photoCounter: 0
    property string port: "video1"
    color: activeTheme.backgroundColor

    function takePhoto() {
        if (_camera.cameraObject) {
            _camera.cameraObject.doPhoto(_camera.port)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 5

        RowLayout {
            id: _portsRow
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            spacing: 5

            Button {
                text: qsTr("video2")
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                onClicked: { _camera.port = "video2"; _camera.takePhoto() }
                background: Rectangle {
                    radius: 5
                    color: activeTheme.buttonsColor
                }
                contentItem: Text {
                    text: parent.text
                    color: "white"
                    font.pointSize: fontSizes.small
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Button {
                text: qsTr("video1")
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                onClicked: { _camera.port = "video1"; _camera.takePhoto() }
                background: Rectangle {
                    radius: 5
                    color: activeTheme.buttonsColor
                }
                contentItem: Text {
                    text: parent.text
                    color: "white"
                    font.pointSize: fontSizes.small
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Button {
                text: qsTr("usb-camera")
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                onClicked: { _camera.port = "usb-camera"; _camera.takePhoto() }
                background: Rectangle {
                    radius: 5
                    color: activeTheme.buttonsColor
                }
                contentItem: Text {
                    text: parent.text
                    color: "white"
                    font.pointSize: fontSizes.small
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        ListView {
            id: _listSensors
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: sensors

            Keys.onPressed: {
                if (event.key === Qt.Key_Return && _camera.cameraObject) {
                    _camera.takePhoto()
                    event.accepted = true
                }
            }

            delegate: Item {
                id: _item
                height: _listSensors.height
                width: _listSensors.width
                property string src: ""

                Component.onCompleted: {
                    _camera.cameraObject = display
                }

                Loader {
                    id: _loader
                    anchors.fill: parent
                    sourceComponent: null
                    Connections {
                        target: display
                        function onImageChanged() {
                            _camera.photoCounter++
                            _item.src = "image://cameraImageProvider/" + _camera.photoCounter
                            _loader.sourceComponent = _imageComponent
                        }
                        function onCameraUnavailable() {
                            _loader.sourceComponent = _txtComponent
                        }
                    }
                }
                Component {
                    id: _txtComponent
                    Text {
                        id: _txtNotify
                        text: qsTr("Camera is not available")
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pointSize: fontSizes.medium
                        color: activeTheme.textColor
                    }
                }
                Component {
                    id: _imageComponent
                    Image {
                        id: _imageView
                        source: _item.src
                        width: parent.width
                        height: parent.height
                        fillMode: Image.PreserveAspectFit
                        cache: false
                    }
                }
            }
        }
    }
}
