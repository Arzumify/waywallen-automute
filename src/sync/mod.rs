//! drm_syncobj plumbing for the daemon's release-fence path.
//!
//! Producers signal the release point on a TIMELINE drm_syncobj they
//! own and exported once via the IPC `release_syncobj` event. For each
//! frame fanned out to a consumer the daemon allocates a fresh BINARY
//! drm_syncobj here and ships its fd alongside `frame_ready`. Consumers
//! signal that fd from the GPU work that consumes the frame; the
//! daemon's reaper merges all per-consumer signals for a frame and
//! transfers the merged fence onto the producer's timeline at the
//! `release_point` carried by `frame_ready`.
//!
//! This module exposes the device + syncobj primitives. Tracking and
//! the reaper task land in follow-up changes.

pub mod drm_syncobj;
pub mod reaper;

pub use drm_syncobj::{merge_sync_files, DrmDevice, SyncobjHandle};
pub use reaper::{spawn_reaper, FrameRecord};

/// Daemon-global, lazily-opened DRM render node used for every
/// drm_syncobj operation. Returns the same `&'static DrmDevice` on
/// every call once a node has been opened. Callers that want a hard
/// startup-time check can call this once at boot and propagate the
/// error.
pub fn drm_device() -> std::io::Result<&'static DrmDevice> {
    use std::sync::OnceLock;
    static DEV: OnceLock<DrmDevice> = OnceLock::new();
    if let Some(d) = DEV.get() {
        return Ok(d);
    }
    let new = DrmDevice::open_first_render_node()?;
    let _ = DEV.set(new);
    Ok(DEV.get().expect("just set"))
}
