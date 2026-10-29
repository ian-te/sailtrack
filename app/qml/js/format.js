.pragma library

// "14:05 (3 min ago)" for an ISO time from status.json, "—" without one.
function when(iso, now) {
    if (!iso)
        return "—"
    var t = new Date(iso)
    var clock = Qt.formatDateTime(t, sameDay(t, now) ? "hh:mm" : "d MMM hh:mm")
    return clock + " (" + ago(t, now) + ")"
}

function ago(t, now) {
    var s = Math.round((now.getTime() - t.getTime()) / 1000)
    if (s < 0)
        return "in " + span(-s)
    if (s < 45)
        return "just now"
    return span(s) + " ago"
}

function span(s) {
    if (s < 90)
        return s + " s"
    if (s < 90 * 60)
        return Math.round(s / 60) + " min"
    if (s < 36 * 3600)
        return Math.round(s / 3600) + " h"
    return Math.round(s / 86400) + " d"
}

function sameDay(a, b) {
    return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth()
            && a.getDate() === b.getDate()
}

function interval(seconds) {
    if (seconds < 60)
        return seconds + " s"
    if (seconds % 60 === 0)
        return seconds < 3600 || seconds % 3600 !== 0 ? (seconds / 60) + " min" : (seconds / 3600) + " h"
    return (seconds / 60).toFixed(1) + " min"
}
