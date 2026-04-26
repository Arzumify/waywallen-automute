//! End-to-end probe for the release_syncobj path.
//!
//! Spawns `waywallen-image-renderer` against a UDS the test owns,
//! drains events, and verifies:
//!   - `ReleaseSyncobj` arrives once with exactly 1 fd (and the fd
//!     imports cleanly as a drm_syncobj on this host's render node).
//!   - `FrameReady` carries a non-zero `release_point` and 1 fd.
//!
//! Skipped if the renderer binary or the requested image asset isn't
//! present (CI sandbox), or if no usable `/dev/dri/renderD*` exists.

use std::os::fd::OwnedFd;
use std::os::unix::net::UnixListener;
use std::path::PathBuf;
use std::process::{Command, Stdio};
use std::time::Duration;

mod common;

use waywallen::ipc::generated::Event as EventMsg;
use waywallen::ipc::uds::recv_event;
use waywallen::sync::DrmDevice;

fn renderer_bin() -> Option<PathBuf> {
    let manifest = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let candidate = manifest
        .join("plugins/image/build/waywallen-image-renderer");
    if candidate.exists() {
        return Some(candidate);
    }
    let install = manifest
        .parent()
        .map(|p| p.join("install/bin/waywallen-image-renderer"))?;
    install.exists().then_some(install)
}

fn image_path() -> Option<PathBuf> {
    let manifest = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let candidate = manifest.join("ui/assets/main_page.png");
    candidate.exists().then_some(candidate)
}

#[test]
fn release_syncobj_round_trip() {
    let Some(bin) = renderer_bin() else {
        eprintln!("skip: waywallen-image-renderer binary not found");
        return;
    };
    let Some(img) = image_path() else {
        eprintln!("skip: ui/assets/main_page.png not found");
        return;
    };
    let drm = match DrmDevice::open_first_render_node() {
        Ok(d) => d,
        Err(e) => {
            eprintln!("skip: no DRM render node ({e})");
            return;
        }
    };

    let sock_path = common::tmp_sock("release-syncobj-e2e");
    let _ = std::fs::remove_file(&sock_path);
    let listener = UnixListener::bind(&sock_path).expect("bind");
    let _cleanup = common::SockCleanup(sock_path.clone());

    let child = Command::new(&bin)
        .arg("--ipc")
        .arg(&sock_path)
        .arg("--image")
        .arg(&img)
        .arg("--width")
        .arg("640")
        .arg("--height")
        .arg("360")
        .stdout(Stdio::inherit())
        .stderr(Stdio::inherit())
        .spawn()
        .expect("spawn waywallen-image-renderer");
    let mut guard = common::ChildGuard(child);

    let (stream, _) = match common::accept_with_timeout(&listener, Duration::from_secs(10)) {
        Some(Ok(x)) => x,
        _ => {
            let _ = guard.0.kill();
            panic!("accept timed out");
        }
    };
    stream
        .set_read_timeout(Some(Duration::from_secs(10)))
        .expect("set rd timeout");

    let mut saw_ready = false;
    let mut saw_release_syncobj_fd: Option<OwnedFd> = None;
    let mut saw_frame_with_release_point = false;
    let deadline = std::time::Instant::now() + Duration::from_secs(15);

    while std::time::Instant::now() < deadline {
        let (msg, mut fds) = match recv_event(&stream) {
            Ok(x) => x,
            Err(e) => {
                eprintln!("recv error: {e}");
                break;
            }
        };
        match msg {
            EventMsg::Ready { .. } => {
                saw_ready = true;
            }
            EventMsg::ReleaseSyncobj => {
                assert_eq!(fds.len(), 1, "ReleaseSyncobj expected exactly 1 fd");
                let fd = fds.remove(0);
                // Verify it imports cleanly as a drm_syncobj.
                let handle = drm.fd_to_handle(&fd).expect("import release_syncobj fd");
                drop(handle);
                saw_release_syncobj_fd = Some(fd);
            }
            EventMsg::BindBuffers { .. } => {
                // Drop dma-buf fds; we're not actually binding.
                drop(fds);
            }
            EventMsg::FrameReady {
                seq,
                release_point,
                ..
            } => {
                assert_eq!(fds.len(), 1, "FrameReady expected 1 acquire sync_fd");
                drop(fds);
                assert!(
                    release_point > 0,
                    "FrameReady seq={seq} release_point must be > 0 (got {release_point})"
                );
                saw_frame_with_release_point = true;
                break;
            }
            other => {
                eprintln!("unexpected msg: {other:?}");
            }
        }
    }

    assert!(saw_ready, "never saw Ready");
    assert!(
        saw_release_syncobj_fd.is_some(),
        "never saw ReleaseSyncobj event with importable drm_syncobj fd"
    );
    assert!(
        saw_frame_with_release_point,
        "never saw FrameReady with release_point > 0"
    );
}
