import QtQuick 2.15

Item {
    id: root
    objectName: "videoOutput"

    property int frameCounter: 0
    property string imageSource: ""

    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    Image {
        id: videoImage
        anchors.top: parent.top
        anchors.left: parent.left
        fillMode: Image.Pad
        cache: false
        source: root.imageSource
    }

    Connections {
        target: videoDisplayProvider
        function onFrameUpdated() {
            root.frameCounter++
            root.imageSource = "image://dspVideo/" + root.frameCounter
        }
    }
}
