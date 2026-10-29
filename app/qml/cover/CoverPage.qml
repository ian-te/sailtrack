import QtQuick 2.6
import Sailfish.Silica 1.0
import "../js/format.js" as Format

CoverBackground {
    readonly property var st: sailtrack.status

    Column {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.paddingLarge
        spacing: Theme.paddingSmall

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: "Sailtrack"
            color: Theme.highlightColor
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.fontSizeSmall
            text: sailtrack.active ? qsTr("Tracking") : qsTr("Stopped")
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
            text: st.lastFix ? qsTr("Fix %1").arg(Format.ago(new Date(st.lastFix.time), app.now))
                             : qsTr("No fix yet")
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
            visible: st.queue > 0
            text: qsTr("%n waiting", "", st.queue)
        }
    }

    CoverActionList {
        enabled: sailtrack.active
        CoverAction {
            iconSource: "image://theme/icon-cover-sync"
            onTriggered: sailtrack.sendNow()
        }
    }
}
