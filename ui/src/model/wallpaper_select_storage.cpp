module;
#include "waywallen/model/wallpaper_select_storage.moc.h"

module waywallen;
import :model.wallpaper_select_storage;

namespace waywallen::model
{

WallpaperSelectStorage::WallpaperSelectStorage(QObject* parent): SelectStorage(parent) {}

WallpaperSelectStorage::~WallpaperSelectStorage() = default;

auto WallpaperSelectStorage::playlistEditTarget() const -> const QVariant& {
    return m_playlist_edit_target;
}

void WallpaperSelectStorage::setPlaylistEditTarget(const QVariant& playlist) {
    const auto nextId = playlistId(playlist);
    if (m_playlist_edit_target == playlist && m_playlist_edit_target_id == nextId) return;

    m_playlist_edit_target    = playlist;
    m_playlist_edit_target_id = nextId;
    Q_EMIT playlistEditTargetChanged();
    notifyActiveChanged();
}

auto WallpaperSelectStorage::playlistEditTargetId() const -> qint64 {
    return m_playlist_edit_target_id;
}

auto WallpaperSelectStorage::hasPlaylistEditTarget() const -> bool {
    return m_playlist_edit_target_id > 0;
}

auto WallpaperSelectStorage::isEditingPlaylist(const QVariant& playlist) const -> bool {
    const auto id = playlistId(playlist);
    return id > 0 && id == m_playlist_edit_target_id;
}

void WallpaperSelectStorage::editPlaylistSelection(const QVariant& playlist) {
    setPlaylistEditTarget(playlist);
    setSelectedKeys(playlistEntryIds(playlist));
    setSelectionMode(true);
    setAnchorIndex(-1);
}

auto WallpaperSelectStorage::selectedWallpaperIds() const -> QVariantList {
    QVariantList out;
    const auto   keys = selectedKeys();
    out.reserve(keys.size());
    for (const auto& key : keys) out.append(key);
    return out;
}

void WallpaperSelectStorage::clear() {
    if (hasPlaylistEditTarget()) {
        m_playlist_edit_target    = {};
        m_playlist_edit_target_id = 0;
        Q_EMIT playlistEditTargetChanged();
    }
    SelectStorage::clear();
}

auto WallpaperSelectStorage::keepActiveWithoutSelection() const -> bool {
    return hasPlaylistEditTarget();
}

auto WallpaperSelectStorage::playlistId(const QVariant& playlist) -> qint64 {
    return playlist.toMap().value(QStringLiteral("id")).toLongLong();
}

auto WallpaperSelectStorage::playlistEntryIds(const QVariant& playlist) -> QStringList {
    return playlist.toMap().value(QStringLiteral("entryIds")).toStringList();
}

} // namespace waywallen::model

#include "waywallen/model/wallpaper_select_storage.moc.cpp"
