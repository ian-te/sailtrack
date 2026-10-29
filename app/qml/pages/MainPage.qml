import QtQuick 2.6
import Sailfish.Silica 1.0
import "../js/format.js" as Format

Page {
    id: page

    readonly property var st: sailtrack.status
    readonly property var fix: st.lastFix
    readonly property var attempt: st.lastAttempt
    readonly property var upload: st.lastUpload

    function stateText() {
        if (sailtrack.busy)
            return qsTr("…")
        if (!sailtrack.active)
            return qsTr("Stopped")
        switch (st.state) {
        case "fixing": return qsTr("Getting a fix")
        case "uploading": return qsTr("Uploading")
        case "waiting": return qsTr("Waiting, next %1").arg(st.nextWake ? Format.when(st.nextWake, app.now) : "—")
        default: return qsTr("Starting")
        }
    }

    allowedOrientations: Orientation.All

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: qsTr("Settings")
                onClicked: pageStack.animatorPush(Qt.resolvedUrl("SettingsPage.qml"))
            }
            MenuItem {
                text: qsTr("Send now")
                enabled: sailtrack.active
                onClicked: sailtrack.sendNow()
            }
            MenuItem {
                text: qsTr("Get a fix now")
                enabled: sailtrack.active
                onClicked: sailtrack.fixNow()
            }
        }

        Column {
            id: column
            width: parent.width

            PageHeader { title: "Sailtrack" }

            Label {
                visible: !sailtrack.configured
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                bottomPadding: Theme.paddingLarge
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                text: qsTr("Set the Dawarich URL and API key in Settings (pull down).")
            }

            TextSwitch {
                text: qsTr("Track in the background")
                description: qsTr("Starts at boot and runs with the app closed")
                checked: sailtrack.enabled
                busy: sailtrack.busy
                automaticCheck: false
                onClicked: sailtrack.setEnabled(!sailtrack.enabled)
            }

            SectionHeader { text: qsTr("Daemon") }

            DetailItem { label: qsTr("State"); value: stateText() }
            DetailItem { label: qsTr("Interval"); value: Format.interval(sailtrack.interval) }

            ButtonLayout {
                Button {
                    text: qsTr("Start")
                    enabled: !sailtrack.active && !sailtrack.busy
                    onClicked: sailtrack.start()
                }
                Button {
                    text: qsTr("Stop")
                    enabled: sailtrack.active && !sailtrack.busy
                    onClicked: sailtrack.stop()
                }
            }

            SectionHeader { text: qsTr("Location") }

            DetailItem {
                label: qsTr("Last fix")
                value: fix ? Format.when(fix.time, app.now) : qsTr("None yet")
            }
            DetailItem {
                visible: !!fix
                label: qsTr("Position")
                value: fix ? fix.lat.toFixed(5) + ", " + fix.lon.toFixed(5) + "  ±" + fix.acc + " m" : ""
            }
            DetailItem {
                visible: !!attempt
                label: qsTr("Last try")
                value: attempt ? Format.when(attempt.time, app.now) : ""
            }
            Label {
                visible: !!attempt
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                horizontalAlignment: Text.AlignRight
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: attempt && attempt.ok ? Theme.secondaryHighlightColor : Theme.errorColor
                text: attempt ? attempt.message : ""
            }

            SectionHeader { text: qsTr("Upload") }

            DetailItem {
                label: qsTr("Waiting to send")
                value: st.queue !== undefined ? st.queue : "—"
            }
            DetailItem {
                label: qsTr("Last upload")
                value: upload ? Format.when(upload.time, app.now) : qsTr("None yet")
            }
            DetailItem {
                visible: !!upload
                label: qsTr("Result")
                value: !upload ? ""
                       : upload.ok ? qsTr("%n sent", "", upload.sent) + (upload.message ? ", " + upload.message : "")
                                   : upload.message
            }

            ButtonLayout {
                Button {
                    text: qsTr("Send now")
                    enabled: sailtrack.active
                    onClicked: sailtrack.sendNow()
                }
                Button {
                    text: qsTr("Get a fix now")
                    enabled: sailtrack.active
                    onClicked: sailtrack.fixNow()
                }
            }
        }

        VerticalScrollDecorator {}
    }
}
