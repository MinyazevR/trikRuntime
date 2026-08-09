import QtQuick 2.15

Item {
	id: root
	objectName: "videoOutput"

	property int frameCounter: 0
	property string imageSource: ""

	Image {
		id: videoImage
		anchors.fill: parent
		fillMode: Image.PreserveAspectFit
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
