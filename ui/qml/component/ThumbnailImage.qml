pragma ComponentBehavior: Bound
pragma ValueTypeBehavior: Assertable
import QtQuick
import waywallen.ui as W

// Wallpaper thumbnail view.
//
// Drives a `W.ThumbnailRequest` from the given `source`/`resource`/`wpType`
// (`source` is the daemon-supplied preview path; `resource` is the original
// media path used as a video fallback when `source` is empty). The Image
// binds directly to `req.cachePath` — empty while Loading, populated once
// the worker (or sync cache hit) settles.
Item {
    id: root

    property string source
    property string resource
    property string wpType
    property int    fillMode: Image.PreserveAspectFit
    readonly property int state: req.state
    readonly property string cachePath: req.cachePath

    W.ThumbnailRequest {
        id: req
        source  : root.source
        resource: root.resource
        wpType  : root.wpType
    }

    Image {
        anchors.fill: parent
        source: req.cachePath
        fillMode: root.fillMode
        asynchronous: true
        cache: true
    }
}
