pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Qcm.Material as MD

MD.Dialog {
    id: root
    title: qsTr("Filters")
    horizontalPadding: 16
    implicitWidth: Math.min(760, parent ? parent.width - 48 : 760)
    standardButtons: T.Dialog.Cancel | T.Dialog.Reset | T.Dialog.Apply

    signal apply(var tags)

    readonly property var defaults: ({ "Scene": true, "Video": true, "Web": true, "Application": true })
    property var working: cloneDefaults()

    readonly property var groups: [
        { name: qsTr("Type"), tags: ["Scene", "Video", "Web", "Application"] },
        { name: qsTr("Genre"), tags: ["Abstract", "Animal", "Anime", "Cartoon", "CG",
            "Cyberpunk", "Fantasy", "Game", "Girls", "Guys", "Landscape", "Medieval",
            "Memes", "MMD", "Music", "Nature", "Pixel art", "Relaxing", "Retro",
            "Sci-Fi", "Sports", "Technology", "Vehicle", "Unspecified"] },
        { name: qsTr("Resolution"), tags: ["1280 x 720", "1920 x 1080", "2560 x 1440",
            "3840 x 2160", "2560 x 1080", "3440 x 1440", "5120 x 1440", "3840 x 1080",
            "7680 x 2160", "1080 x 1920", "720 x 1280", "1440 x 2560"] },
        { name: qsTr("Misc"), tags: ["Audio Responsive", "Customizable", "Come with Music"] }
    ]

    function cloneDefaults() {
        let w = {};
        for (const k in defaults)
            w[k] = defaults[k];
        return w;
    }
    function has(tag) {
        return working[tag] === true;
    }
    function toggle(tag, on) {
        let w = working;
        if (on)
            w[tag] = true;
        else
            delete w[tag];
        working = w;
    }
    function collect() {
        let out = [];
        for (const g of groups) {
            let checkedIn = g.tags.filter(t => working[t] === true);
            if (checkedIn.length > 0 && checkedIn.length < g.tags.length)
                out = out.concat(checkedIn);
        }
        if (working["Mature"] !== true)
            out.push("Everyone");
        return out;
    }

    onApplied: {
        root.apply(collect());
        accept();
    }
    onReset: working = cloneDefaults()

    contentItem: ColumnLayout {
        spacing: 16

        Repeater {
            model: root.groups
            delegate: ColumnLayout {
                id: groupCol
                required property var modelData
                Layout.fillWidth: true
                spacing: 6

                MD.Label {
                    text: groupCol.modelData.name
                    typescale: MD.Token.typescale.title_small
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 6

                    Repeater {
                        model: groupCol.modelData.tags
                        delegate: MD.FilterChip {
                            required property string modelData
                            text: modelData
                            checked: root.has(modelData)
                            onClicked: root.toggle(modelData, checked)
                        }
                    }
                }
            }
        }

        MD.Divider { Layout.fillWidth: true }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                MD.Label {
                    text: qsTr("Mature content (NSFW)")
                    typescale: MD.Token.typescale.title_small
                }
                MD.Label {
                    text: qsTr("18+ only. Shows mature-tagged wallpapers.")
                    typescale: MD.Token.typescale.body_small
                    color: MD.Token.color.on_surface_variant
                }
            }

            MD.Switch {
                id: m_nsfw
                checked: root.has("Mature")
                onClicked: {
                    if (!root.has("Mature"))
                        m_confirm.open();
                    else
                        root.toggle("Mature", false);
                }
            }
        }
    }

    MD.Dialog {
        id: m_confirm
        title: qsTr("Mature content")
        modal: true
        anchors.centerIn: T.Overlay.overlay
        standardButtons: T.Dialog.Cancel | T.Dialog.Ok
        onAccepted: root.toggle("Mature", true)
        onRejected: m_nsfw.checked = Qt.binding(() => root.has("Mature"))

        contentItem: MD.Label {
            text: qsTr("This shows wallpapers tagged as mature / NSFW. You must be 18 or older to enable this. Continue?")
            wrapMode: Text.WordWrap
        }
    }
}
