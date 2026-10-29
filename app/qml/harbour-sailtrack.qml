import QtQuick 2.6
import Sailfish.Silica 1.0
import "pages"

ApplicationWindow {
    id: app

    // Ticks the "N min ago" labels and picks up the unit's state.
    property date now: new Date()

    initialPage: Component { MainPage {} }
    cover: Qt.resolvedUrl("cover/CoverPage.qml")
    allowedOrientations: defaultAllowedOrientations

    Timer {
        // Slower while only the cover shows.
        interval: Qt.application.active ? 5000 : 30000
        repeat: true
        triggeredOnStart: true
        running: true
        onTriggered: {
            app.now = new Date()
            sailtrack.refresh()
        }
    }

    Notice {
        id: notice
        duration: 4000
    }

    Connections {
        target: sailtrack
        onError: {
            notice.text = message
            notice.show()
        }
    }
}
