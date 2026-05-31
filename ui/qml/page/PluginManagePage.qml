pragma ValueTypeBehavior: Assertable
import QtQuick
import QtQuick.Layouts
import Qcm.Material as MD
import waywallen.ui as W

MD.Page {
    id: root
    title: 'Plugins'

    actions: [
        MD.Action {
            icon.name: MD.Token.icon.add
            text: qsTr("Install from .zip")
            enabled: !installQuery.querying
            onTriggered: zipDialog.open()
        }
    ]

    W.PluginListQuery {
        id: pluginListQuery
    }

    W.PluginInstallQuery {
        id: installQuery
    }

    Connections {
        target: W.Notify
        function onDaemonReady() {
            pluginListQuery.reload();
        }
    }

    Connections {
        target: installQuery
        function onInstalled(pluginId, needsRestart) {
            W.Action.toast(needsRestart
                ? qsTr("Installed \"%1\" — restart waywallen to load it").arg(pluginId)
                : qsTr("Installed \"%1\"").arg(pluginId));
            pluginListQuery.reload();
        }
    }

    Component.onCompleted: {
        if (W.Notify.daemonPhase === W.Notify.DaemonPhase.Ready)
            pluginListQuery.reload();
    }

    MD.FileDialog {
        id: zipDialog
        title: qsTr("Choose plugin package")
        fileMode: MD.FileDialog.OpenFile
        nameFilters: ["Plugin package (*.zip)", "All files (*)"]
        onAccepted: {
            installQuery.zipPath = selectedFile.toString().replace(/^file:\/\//, "");
            installQuery.reload();
        }
    }

    contentItem: MD.VerticalFlickable {
        id: m_flick
        topMargin: 4
        leftMargin: 12
        rightMargin: 12
        bottomMargin: 12

        ColumnLayout {
            width: m_flick.contentWidth
            spacing: 8

            MD.Text {
                Layout.fillWidth: true
                visible: !pluginListQuery.plugins || pluginListQuery.plugins.length === 0
                text: "No plugins installed"
                typescale: MD.Token.typescale.body_medium
                color: MD.Token.color.on_surface_variant
                wrapMode: Text.WordWrap
            }

            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: contentHeight
                implicitHeight: contentHeight
                interactive: false
                spacing: 4

                model: pluginListQuery.plugins

                delegate: MD.ListItem {
                    id: pluginItem
                    required property var modelData

                    width: ListView.view.width
                    radius: 12
                    mdState.backgroundColor: MD.Token.color.surface_container
                    text: modelData.name || modelData.id || ""
                    supportText: modelData.id
                    leader: MD.Icon {
                        name: MD.Token.icon.extension
                        size: 24
                        color: MD.Token.color.on_surface_variant
                    }
                    trailing: MD.Text {
                        text: "v" + (pluginItem.modelData.version || "0.0.0")
                        typescale: MD.Token.typescale.label_small
                        color: MD.Token.color.on_surface_variant
                    }
                }
            }
        }
    }
}
