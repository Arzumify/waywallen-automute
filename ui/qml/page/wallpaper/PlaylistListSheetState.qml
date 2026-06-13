pragma ComponentBehavior: Bound
import QtQml

QtObject {
    id: root

    required property var page
    required property var playlistListQuery
    required property var playlistMutation
    required property var playlistPlaybackMutation

    readonly property bool listLoading: page.playlistListLoading
    readonly property var playlists: playlistListQuery.playlists || []
    readonly property var playDisplays: page.playlistPlayDisplays || []
    readonly property var selectedDisplay: {
        const displays = playDisplays;
        if (displays.length === 0)
            return null;

        const key = String(page.playlistPlayDisplayId);
        for (let i = 0; i < displays.length; ++i) {
            if (String(displays[i].id) === key)
                return displays[i];
        }
        return displays[0];
    }
    readonly property var selectedDisplayId: selectedDisplay ? selectedDisplay.id : null

    function displayLabel(display) {
        return page.displayLabel(display);
    }

    function selectDisplay(display) {
        page.playlistPlayDisplayId = display.id;
    }

    function isEditingPlaylist(playlist) {
        return page.isEditingPlaylist(playlist);
    }

    function playlistIsPlayingOnSelectedDisplay(playlist) {
        return page.playlistIsPlayingOnSelectedDisplay(playlist);
    }

    function playlistDisplayLabels(playlist) {
        return page.playlistDisplayLabels(playlist);
    }

    function togglePlayback(playlist) {
        page.togglePlaylistPlayback(playlist);
    }

    function editSelection(playlist) {
        page.editPlaylistSelection(playlist);
    }

    function deletePlaylist(playlist) {
        page.deletePlaylist(playlist);
    }
}
