pragma ComponentBehavior: Bound
import QtQml

QtObject {
    id: root

    readonly property int layoutFillCell: 0
    readonly property int layoutFixed: 1
    property int itemSize: 162
    property real itemAspectRatio: 1
    property int layoutMode: layoutFillCell
    readonly property real itemHeight: itemSize / Math.max(itemAspectRatio, 0.1)

    function setItemAspectRatio(ratio) {
        itemAspectRatio = ratio;
    }
}
