pragma ValueTypeBehavior: Assertable
import QtQuick
import waywallen.ui as W

// Wallpaper thumbnail view.
//
// Drives a `W.ThumbnailRequest` from the given `source`/`resource`/`wpType`
// (`source` is the daemon-supplied preview path; `resource` is the original
// media path used as a video fallback when `source` is empty). A `Loader`
// switches between the loading / ready / failed components as the request
// state advances. Customise `loadingComponent` and `failedComponent` from
// the call site to tailor the placeholder UX without subclassing.
Item {
    id: root

    property string source
    property string resource
    property string wpType
    property int    fillMode: Image.PreserveAspectFit
    property Component loadingComponent: defaultLoading
    property Component failedComponent : defaultFailed
    readonly property int state: req.state
    readonly property string cachePath: req.cachePath

    W.ThumbnailRequest {
        id: req
        source  : root.source
        resource: root.resource
        wpType  : root.wpType
    }

    Loader {
        anchors.fill: parent
        sourceComponent: req.state === W.ThumbnailRequest.Ready  ? readyComp
                       : req.state === W.ThumbnailRequest.Failed ? root.failedComponent
                       :                                           root.loadingComponent
    }

    Component {
        id: readyComp
        Image {
            source: req.cachePath ? "file://" + req.cachePath : ""
            fillMode: root.fillMode
            asynchronous: false
        }
    }
    Component { id: defaultLoading; Item {} }
    Component { id: defaultFailed;  Item {} }
}
