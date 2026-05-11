pragma ComponentBehavior: Bound
import QtQml
import QtQuick
import Qcm.Material as MD

QtObject {
    id: root
    property var filter: null
    property int condition: 0
    readonly property var conditionModel: [
        { name: qsTr("select type"), value: 0 }
    ]

    readonly property Component valueDelegate: Component {
        MD.Text {
            text: qsTr("Choose a filter type")
            color: MD.Token.color.on_surface_variant
        }
    }
}
