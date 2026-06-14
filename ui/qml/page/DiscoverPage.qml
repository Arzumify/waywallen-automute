pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Qcm.Material as MD
import waywallen.ui as W

MD.Page {
    id: root
    showBackground: false
    padding: MD.MProp.size.isCompact ? 0 : 12

    property var detailRow: null
    property int detailState: 0

    property string sourceId: ""
    property var sortOptions: []
    property int sortIndex: 0

    function sourceInfo(id) {
        for (const s of availabilityQuery.sources) {
            if (s.id === id)
                return s;
        }
        return null;
    }

    function sourceName(id) {
        const s = sourceInfo(id);
        return s ? s.name : "";
    }

    function sortLabel() {
        if (sortOptions.length === 0)
            return qsTr("Sort");
        return sortOptions[Math.max(0, Math.min(sortIndex, sortOptions.length - 1))].label;
    }

    function setSource(id) {
        const s = sourceInfo(id);
        if (!s)
            return;
        sourceId = id;
        sortOptions = s.sorts ?? [];
        sortIndex = 0;
        searchQuery.sourceId = id;
        detailsQuery.sourceId = id;
        searchQuery.sortKey = sortOptions.length > 0 ? sortOptions[0].key : "";
        detailRow = null;
        detailState = 0;
    }

    function pickSort(idx) {
        if (idx < 0 || idx >= sortOptions.length)
            return;
        sortIndex = idx;
        searchQuery.sortKey = sortOptions[idx].key;
    }

    function selectItem(index) {
        detailRow = searchQuery.model.get(index);
        detailState = detailRow.installed ? 3 : 0;
        detailsQuery.sourceId = detailRow.sourceId;
        detailsQuery.itemId = detailRow.itemId;
    }

    function fmtSize(s) {
        const m = String(s).match(/^([\d.,]+)\s*([KMGT]?B)$/i);
        if (!m)
            return s;
        const num = parseFloat(m[1].replace(/,/g, ""));
        if (isNaN(num))
            return s;
        const unit = m[2].toUpperCase();
        return num.toFixed(unit === "B" ? 0 : 1) + " " + unit;
    }

    W.RemoteAvailabilityQuery {
        id: availabilityQuery
        onSourcesChanged: {
            if (sources.length === 0)
                return;
            if (root.sourceId.length === 0)
                root.setSource(defaultSourceId.length > 0 ? defaultSourceId : sources[0].id);
            else
                root.setSource(root.sourceId);
        }
    }

    W.RemoteSearchQuery {
        id: searchQuery
        onStateChanged: {
            if (errorText.length > 0)
                W.Action.toast(qsTr("Remote search failed: ") + errorText);
        }
    }

    W.RemoteFilterDialog {
        id: m_filter_dialog
        parent: T.Overlay.overlay
        anchors.centerIn: parent
        onApply: function(tags) {
            searchQuery.tags = tags;
        }
    }

    W.RemoteDetailsQuery {
        id: detailsQuery
    }

    W.RemoteDownloadQuery {
        id: dlQuery
        function onUninstalled(sourceId, id) {
            searchQuery.model.setInstalled(sourceId, id, false);
            if (root.detailRow && root.detailRow.sourceId === sourceId && root.detailRow.itemId === id) {
                root.detailRow.installed = false;
                root.detailState = 0;
            }
            W.Action.toast(qsTr("Uninstalled"));
        }
        function onUninstallFailed(sourceId, id, error) {
            W.Action.toast(qsTr("Uninstall failed: ") + error);
        }
        function onRejected(sourceId, id, error) {
            if (root.detailRow && root.detailRow.sourceId === sourceId && root.detailRow.itemId === id)
                root.detailState = 0;
            W.Action.toast(qsTr("Download rejected: ") + error);
        }
    }

    Connections {
        target: W.Notify
        function onRemoteDownloadProgress(sourceId, id, state, error) {
            if (state === 3)
                searchQuery.model.setInstalled(sourceId, id, true);
            if (root.detailRow && root.detailRow.sourceId === sourceId && root.detailRow.itemId === id) {
                root.detailState = state;
                if (state === 3)
                    root.detailRow.installed = true;
            }
            if (state === 5 && error.length > 0)
                W.Action.toast(qsTr("Download failed: ") + error);
        }
    }

    function reloadAll() {
        searchQuery.tags = m_filter_dialog.collect();
        availabilityQuery.reload();
        if (root.sourceId.length > 0)
            searchQuery.reload();
    }

    Connections {
        target: W.Notify
        function onDaemonReady() {
            root.reloadAll();
        }
    }

    Component.onCompleted: {
        if (W.Notify.daemonPhase === W.Notify.DaemonPhase.Ready)
            reloadAll();
    }

    contentItem: RowLayout {
        spacing: 12

        MD.Pane {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: root.MD.MProp.page.backgroundRadius
            padding: 0
            showBackground: true

            contentItem: ColumnLayout {
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.topMargin: 4
                    spacing: 8

                    MD.EmbedChip {
                        id: sourceChip
                        visible: availabilityQuery.sources.length > 1
                        text: root.sourceName(root.sourceId)
                        trailingIconName: MD.Token.icon.arrow_drop_down
                        mdState.borderWidth: 1
                        onClicked: sourceMenu.open()

                        MD.Menu {
                            id: sourceMenu
                            parent: sourceChip
                            y: parent.height
                            model: availabilityQuery.sources
                            contentDelegate: MD.MenuItem {
                                required property var modelData
                                text: modelData.name
                                icon.name: modelData.id === root.sourceId ? MD.Token.icon.check : ' '
                                onClicked: {
                                    root.setSource(modelData.id);
                                    sourceMenu.close();
                                }
                            }
                        }
                    }

                    MD.EmbedChip {
                        id: sortChip
                        visible: root.sortOptions.length > 0
                        text: root.sortLabel()
                        trailingIconName: MD.Token.icon.arrow_drop_down
                        mdState.borderWidth: 1
                        onClicked: sortMenu.open()

                        MD.Menu {
                            id: sortMenu
                            parent: sortChip
                            y: parent.height
                            model: root.sortOptions
                            contentDelegate: MD.MenuItem {
                                required property var modelData
                                required property int index
                                text: modelData.label
                                icon.name: index === root.sortIndex ? MD.Token.icon.check : ' '
                                onClicked: {
                                    root.pickSort(index);
                                    sortMenu.close();
                                }
                            }
                        }
                    }

                    W.SearchChip {
                        id: m_search_field
                        Layout.preferredWidth: 120
                        placeholderText: qsTr("Search")
                        onTextEdited: searchQuery.query = text
                    }

                    MD.ActionToolBar {
                        Layout.fillWidth: true
                        actions: [
                            MD.Action {
                                icon.name: MD.Token.icon.filter_list
                                text: 'Filters'
                                checked: searchQuery.tags.length > 0
                                onTriggered: m_filter_dialog.open()
                            },
                            MD.Action {
                                icon.name: MD.Token.icon.refresh
                                text: 'Refresh'
                                enabled: !searchQuery.querying
                                onTriggered: searchQuery.reload()
                            }
                        ]
                    }
                }

                MD.LinearIndicator {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    visible: searchQuery.querying && searchQuery.model.count > 0
                    running: visible
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    MD.VerticalGridView {
                        id: m_grid
                        anchors.fill: parent
                        clip: true
                        cacheBuffer: 300
                        displayMarginBeginning: 300
                        displayMarginEnd: 300
                        currentIndex: -1
                        topMargin: 2
                        bottomMargin: 8
                        leftMargin: 8
                        rightMargin: 8
                        visible: count > 0

                        readonly property int _cols: Math.max(1, Math.floor(width / 162))
                        cellWidth: (width - leftMargin - rightMargin) / _cols
                        cellHeight: cellWidth

                        model: searchQuery.model

                        delegate: RemoteCard {
                            onClicked: {
                                m_grid.currentIndex = index;
                                root.selectItem(index);
                            }
                        }

                        highlightFollowsCurrentItem: true
                        highlight: Component {
                            Item {
                                visible: m_grid.currentItem !== null
                                z: 2
                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: 4
                                    color: "transparent"
                                    border.color: MD.Token.color.primary
                                    border.width: 3
                                    radius: MD.Token.shape.corner.small + 2
                                }
                            }
                        }

                        onContentYChanged: {
                            if (searchQuery.hasMore && !searchQuery.querying
                                && contentY + height >= contentHeight - cellHeight * 2)
                                searchQuery.loadMore();
                        }
                    }

                    ColumnLayout {
                        anchors.centerIn: parent
                        visible: m_grid.count === 0
                        spacing: 8

                        MD.BusyIndicator {
                            Layout.alignment: Qt.AlignHCenter
                            running: searchQuery.querying
                            visible: running
                        }

                        MD.Label {
                            Layout.alignment: Qt.AlignHCenter
                            visible: !searchQuery.querying
                            text: qsTr("No wallpapers found")
                            typescale: MD.Token.typescale.body_large
                            color: MD.Token.color.on_surface_variant
                        }
                    }
                }
            }
        }

        MD.Pane {
            Layout.preferredWidth: 300
            Layout.maximumWidth: 300
            Layout.fillHeight: true
            visible: root.detailRow !== null
            radius: root.MD.MProp.page.backgroundRadius
            padding: 0
            showBackground: true

            contentItem: ColumnLayout {
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    Item { Layout.fillWidth: true }
                    MD.IconButton {
                        action: MD.Action {
                            icon.name: MD.Token.icon.close
                            onTriggered: { root.detailRow = null; m_grid.currentIndex = -1; }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.preferredHeight: width * 0.56
                    radius: MD.Token.shape.corner.medium
                    clip: true
                    color: MD.Token.color.surface_container

                    AnimatedImage {
                        anchors.fill: parent
                        source: root.detailRow ? root.detailRow.previewUrl : ""
                        fillMode: Image.PreserveAspectCrop
                        cache: true
                        playing: true
                        sourceSize.width: 640
                        sourceSize.height: 640
                    }
                }

                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    clip: true
                    contentWidth: width
                    contentHeight: m_info.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds

                    ColumnLayout {
                        id: m_info
                        width: parent.width
                        spacing: 8

                        MD.Label {
                            Layout.fillWidth: true
                            text: root.detailRow ? root.detailRow.title : ""
                            typescale: MD.Token.typescale.title_medium
                            wrapMode: Text.WordWrap
                        }

                        MD.Label {
                            Layout.fillWidth: true
                            text: root.detailRow ? qsTr("by ") + root.detailRow.author : ""
                            visible: root.detailRow && root.detailRow.author.length > 0
                            typescale: MD.Token.typescale.body_medium
                            color: MD.Token.color.on_surface_variant
                            wrapMode: Text.WordWrap
                        }

                        MD.Text {
                            Layout.topMargin: 4
                            visible: detailsQuery.size.length > 0
                            text: "Size"
                            typescale: MD.Token.typescale.label_medium
                            color: MD.Token.color.on_surface_variant
                        }
                        MD.Text {
                            visible: detailsQuery.size.length > 0
                            text: root.fmtSize(detailsQuery.size)
                            typescale: MD.Token.typescale.body_medium
                            color: MD.Token.color.on_surface
                        }

                        Flow {
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            spacing: 4
                            visible: detailsQuery.tags.length > 0

                            Repeater {
                                model: detailsQuery.tags
                                delegate: MD.AssistChip {
                                    required property string modelData
                                    text: modelData
                                }
                            }
                        }

                        MD.Divider {
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            visible: detailsQuery.description.length > 0 || detailsQuery.querying
                        }

                        MD.Text {
                            visible: detailsQuery.description.length > 0 || detailsQuery.querying
                            text: "Description"
                            typescale: MD.Token.typescale.label_large
                            color: MD.Token.color.on_surface_variant
                        }
                        MD.Label {
                            Layout.fillWidth: true
                            text: detailsQuery.querying ? qsTr("Loading…") : detailsQuery.description
                            visible: text.length > 0
                            typescale: MD.Token.typescale.body_medium
                            color: MD.Token.color.on_surface
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                MD.Button {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.bottomMargin: 16
                    mdState.type: root.detailState === 3 ? MD.Enum.BtFilledTonal : MD.Enum.BtFilled
                    enabled: root.detailState === 0 || root.detailState === 3
                    text: {
                        switch (root.detailState) {
                        case 1: return qsTr("Pending");
                        case 2: return qsTr("Downloading");
                        case 3: return qsTr("Uninstall");
                        case 4: return qsTr("Retry");
                        case 5: return qsTr("Retry");
                        default: return qsTr("Download");
                        }
                    }
                    onClicked: {
                        if (!root.detailRow) return;
                        if (root.detailState === 3) {
                            dlQuery.uninstall(root.detailRow.sourceId, root.detailRow.itemId);
                        } else {
                            root.detailState = 1;
                            dlQuery.start(root.detailRow.sourceId, root.detailRow.itemId);
                        }
                    }
                }
            }
        }
    }
}
