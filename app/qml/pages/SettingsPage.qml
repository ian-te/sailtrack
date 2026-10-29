import QtQuick 2.6
import Sailfish.Silica 1.0
import "../js/format.js" as Format

Dialog {
    id: dialog

    // The daemon's wakeup slots, in seconds; others work but don't share wakeups.
    readonly property var intervals: [60, 150, 300, 600, 900, 1800, 3600]

    function indexOf(seconds) {
        var i = intervals.indexOf(seconds)
        return i < 0 ? 2 : i
    }

    allowedOrientations: Orientation.All
    canAccept: urlField.acceptableInput || urlField.text.length === 0

    onAccepted: sailtrack.save(urlField.text, keyField.text, intervals[intervalBox.currentIndex],
                               accuracySlider.value, timeoutSlider.value)

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            DialogHeader { title: qsTr("Settings") }

            SectionHeader { text: qsTr("Dawarich") }

            TextField {
                id: urlField
                width: parent.width
                label: qsTr("Base URL, e.g. https://dawarich.example.com")
                placeholderText: qsTr("Dawarich URL")
                text: sailtrack.url
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                validator: RegExpValidator { regExp: /^https?:\/\/[^\s\/]+(\/\S*)?$/ }
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: keyField.focus = true
            }

            PasswordField {
                id: keyField
                width: parent.width
                label: qsTr("API key (Dawarich → Account → API key)")
                placeholderText: qsTr("API key")
                text: sailtrack.apiKey
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Use the final address: one that redirects from http to https loses the upload.")
            }

            SectionHeader { text: qsTr("Tracking") }

            ComboBox {
                id: intervalBox
                label: qsTr("Every")
                currentIndex: indexOf(sailtrack.interval)
                menu: ContextMenu {
                    Repeater {
                        model: dialog.intervals
                        MenuItem { text: Format.interval(modelData) }
                    }
                }
            }

            Slider {
                id: accuracySlider
                width: parent.width
                label: qsTr("Drop fixes worse than")
                minimumValue: 10
                maximumValue: 1000
                stepSize: 10
                value: sailtrack.minAccuracy
                valueText: "±" + value + " m"
            }

            Slider {
                id: timeoutSlider
                width: parent.width
                label: qsTr("Give up on a fix after")
                minimumValue: 15
                maximumValue: 300
                stepSize: 15
                value: sailtrack.fixTimeout
                valueText: value + " s"
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("The GPS is on only while waiting for a fix. A GPS fix (±25 m or better) ends the wait. Without one, the best rougher fix within the limit, such as a network position, is sent at the timeout, which stays 15 s under the interval.")
            }

            SectionHeader { text: qsTr("About") }

            DetailItem { label: qsTr("Version"); value: sailtrack.version }
        }

        VerticalScrollDecorator {}
    }
}
