pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Qcm.Material as MD

MD.Page {
    id: root
    title: "Wallpaper info"
    scrolling: !infoFlick.atYBeginning

    property var wallpaper: null
    property real sizeBytes: 0

    readonly property string tagsText: formatList(wallpaper?.tags)
    readonly property string metadataText: formatObject(wallpaper?.metadata)
    readonly property string overridesText: formatJson(wallpaper?.userPropertyOverrides)
    readonly property string formatText: value(wallpaper?.format).toLowerCase()

    function value(v) {
        return v === undefined || v === null ? "" : String(v);
    }

    function hasText(v) {
        return value(v).length > 0;
    }

    function formatList(v) {
        if (!v || v.length === 0)
            return "";
        const out = [];
        for (let i = 0; i < v.length; ++i)
            out.push(String(v[i]));
        return out.join(", ");
    }

    function formatObject(v) {
        if (!v)
            return "";
        try {
            const keys = Object.keys(v).sort();
            if (keys.length === 0)
                return "";
            const out = {};
            for (let i = 0; i < keys.length; ++i)
                out[keys[i]] = String(v[keys[i]]);
            return JSON.stringify(out, null, 2);
        } catch (e) {
            return String(v);
        }
    }

    function formatJson(v) {
        const s = value(v);
        if (s.length === 0)
            return "";
        try {
            return JSON.stringify(JSON.parse(s), null, 2);
        } catch (e) {
            return s;
        }
    }

    function formatSize(b) {
        let v = Number(b ?? 0);
        if (!(v > 0))
            return "";
        const u = ["B", "KB", "MB", "GB", "TB"];
        let i = 0;
        while (v >= 1024 && i < u.length - 1) {
            v /= 1024;
            ++i;
        }
        return v.toFixed(i === 0 ? 0 : 1) + " " + u[i];
    }

    component InfoLabel: MD.Text {
        required property string label

        Layout.preferredWidth: 104
        Layout.alignment: Qt.AlignTop
        text: label
        typescale: MD.Token.typescale.label_medium
        color: MD.Token.color.on_surface_variant
        elide: Text.ElideRight
        maximumLineCount: 1
    }

    component InfoValue: MD.TextEdit {
        Layout.fillWidth: true
        Layout.preferredHeight: Math.max(24, contentHeight)
        readOnly: true
        selectByMouse: true
        persistentSelection: true
        typescale: MD.Token.typescale.body_medium
        color: MD.Token.color.on_surface
        wrapMode: TextEdit.WrapAnywhere
    }

    contentItem: MD.VerticalFlickable {
        id: infoFlick
        topMargin: 12
        bottomMargin: 24
        leftMargin: 16
        rightMargin: 16

        GridLayout {
            width: infoFlick.contentWidth
            columns: 2
            columnSpacing: 12
            rowSpacing: 10

            InfoLabel { label: "ID" }
            InfoValue { text: root.value(root.wallpaper?.id_proto) }

            InfoLabel {
                visible: root.hasText(root.wallpaper?.externalId)
                label: "External ID"
            }
            InfoValue {
                visible: root.hasText(root.wallpaper?.externalId)
                text: root.value(root.wallpaper?.externalId)
            }

            InfoLabel { label: "Name" }
            InfoValue { text: root.value(root.wallpaper?.name) }

            InfoLabel { label: "Type" }
            InfoValue { text: root.value(root.wallpaper?.wpType) }

            InfoLabel { label: "Resource" }
            InfoValue { text: root.value(root.wallpaper?.resource) }

            InfoLabel {
                visible: root.hasText(root.wallpaper?.preview)
                label: "Preview"
            }
            InfoValue {
                visible: root.hasText(root.wallpaper?.preview)
                text: root.value(root.wallpaper?.preview)
            }

            InfoLabel {
                visible: root.sizeBytes > 0
                label: "Size"
            }
            InfoValue {
                visible: root.sizeBytes > 0
                text: root.formatSize(root.sizeBytes)
            }

            InfoLabel {
                visible: Number(root.wallpaper?.width ?? 0) > 0
                label: "Width"
            }
            InfoValue {
                visible: Number(root.wallpaper?.width ?? 0) > 0
                text: String(root.wallpaper?.width ?? 0)
            }

            InfoLabel {
                visible: Number(root.wallpaper?.height ?? 0) > 0
                label: "Height"
            }
            InfoValue {
                visible: Number(root.wallpaper?.height ?? 0) > 0
                text: String(root.wallpaper?.height ?? 0)
            }

            InfoLabel {
                visible: root.hasText(root.formatText)
                label: "Format"
            }
            InfoValue {
                visible: root.hasText(root.formatText)
                text: root.formatText
            }

            InfoLabel {
                visible: root.hasText(root.wallpaper?.contentRating)
                label: "Rating"
            }
            InfoValue {
                visible: root.hasText(root.wallpaper?.contentRating)
                text: root.value(root.wallpaper?.contentRating)
            }

            InfoLabel {
                visible: root.hasText(root.tagsText)
                label: "Tags"
            }
            InfoValue {
                visible: root.hasText(root.tagsText)
                text: root.tagsText
            }

            InfoLabel {
                visible: root.hasText(root.wallpaper?.description)
                label: "Description"
            }
            InfoValue {
                visible: root.hasText(root.wallpaper?.description)
                text: root.value(root.wallpaper?.description)
            }

            InfoLabel {
                visible: root.hasText(root.metadataText)
                label: "Metadata"
            }
            InfoValue {
                visible: root.hasText(root.metadataText)
                text: root.metadataText
            }

            InfoLabel {
                visible: root.hasText(root.overridesText)
                label: "Overrides"
            }
            InfoValue {
                visible: root.hasText(root.overridesText)
                text: root.overridesText
            }
        }
    }
}
