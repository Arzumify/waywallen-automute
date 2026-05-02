//! Daemon-wide typed error.
//!
//! Most of the daemon's internal layers (`control`, `repo`,
//! `renderer_manager`, …) still return `anyhow::Result`. The
//! `From<anyhow::Error>` impl lets their callers `?`-propagate without
//! a flag day. New code that **is** the source of a typed failure (the
//! WebSocket dispatch arms, future control-layer migrations) should
//! construct the matching variant directly so the wire-side
//! `pb::ErrorCode` lands correctly — `Internal` is the "no better
//! mapping known" fallback, not the default.
//!
//! Wire conversion lives here too (`Error::to_response`, `ok_response`)
//! so this module is the single authority for the daemon-error → wire
//! shape mapping. The legacy coarse `pb::Status` is derived from
//! `error_code()`; old clients keep working without touching the daemon.
//!
//! Naming: import as `use crate::error::{Error, Result};` rather than
//! re-exporting from `lib.rs`. Several modules `use anyhow::Result;` and
//! a root-level shadow would silently change their meaning.
//!
//! Layering: `error.rs` depends on `control_proto` (the generated
//! protobuf types). The reverse never holds — `control_proto.rs` is
//! pure codegen + lightweight conversion helpers and must stay free of
//! daemon error types so the build graph keeps the proto layer at the
//! bottom.

use thiserror::Error;

use crate::control_proto as pb;

/// Daemon-wide typed error. See module docs for construction guidance.
//
// `dead_code` is allowed at the enum level: this is intentional API
// surface. Several variants (`InvalidArgument`, `FailedPrecondition`,
// `LibraryNotFound`, …) are not yet constructed by any caller — they
// reserve a stable mapping into `pb::ErrorCode` for the next round of
// migration (control / repo layers moving off `anyhow::Result`).
#[allow(dead_code)]
#[derive(Debug, Error)]
pub enum Error {
    /// Catch-all for opaque errors bubbling up from layers still on
    /// `anyhow::Result`. Code that knows the failure category should
    /// construct a more specific variant.
    #[error("{0:#}")]
    Internal(#[from] anyhow::Error),

    /// Sea-ORM database access failure. Use the `?` operator on a
    /// `Result<_, sea_orm::DbErr>` to land here automatically.
    #[error("db: {0}")]
    Db(#[from] sea_orm::DbErr),

    /// Local I/O failure (file open, socket bind, …).
    #[error("io: {0}")]
    Io(#[from] std::io::Error),

    /// Wire-side protobuf decode failure. Surfaces as
    /// `ErrorCode::Decode`.
    #[error("decode: {0}")]
    Decode(#[from] prost::DecodeError),

    /// Inbound `Request.payload` was `None` — caller sent an envelope
    /// with no oneof variant set.
    #[error("{0}")]
    UnexpectedPayload(&'static str),

    /// Caller-supplied invalid argument that doesn't fit a more
    /// specific variant.
    #[error("{0}")]
    InvalidArgument(String),

    /// Precondition (e.g. "not enough free memory") that doesn't fit a
    /// more specific variant.
    #[error("{0}")]
    FailedPrecondition(String),

    /// Apply path: no display has registered with the daemon yet.
    #[error("no display registered")]
    NoDisplayRegistered,

    /// Apply path: source snapshot has no entry with this id.
    #[error("wallpaper '{0}' not found")]
    WallpaperNotFound(String),

    /// Renderer manager has no live renderer with this id.
    #[error("unknown renderer '{0}'")]
    RendererNotFound(String),

    /// Renderer registry has no manifest declaring support for this
    /// wallpaper type.
    #[error("no renderer for wallpaper type '{0}'")]
    NoRendererForType(String),

    /// The caller named a specific renderer but the wallpaper's type
    /// is not in the manifest's `types` list.
    #[error("renderer '{renderer}' does not support wallpaper type '{ty}'")]
    RendererTypeMismatch { renderer: String, ty: String },

    /// `renderer_manager.spawn` failed (fork/exec/handshake/timeout/…).
    #[error("spawn failed: {0}")]
    RendererSpawnFailed(String),

    /// `renderer_manager.send_control` (Play / Pause / Mouse / SetFps)
    /// failed — usually a closed socket or a renderer the manager has
    /// already reaped.
    #[error("renderer control failed: {0}")]
    RendererControlFailed(String),

    /// Source-plugin Lua name was not in the registered set.
    #[error("source plugin '{0}' not found")]
    SourcePluginNotFound(String),

    /// Source plugin's `extras(entry)` Lua callback raised. The
    /// callback's stringified error rides in `message` so we don't have
    /// to keep its lifetime around.
    #[error("source_plugin '{plugin}'.extras() failed: {message}")]
    SourceExtrasFailed { plugin: String, message: String },

    /// `coerce_and_validate` rejected a `SettingsSet` value.
    #[error("settings validation failed: {0}")]
    SettingsValidationFailed(String),

    /// Settings persisted, but live `ApplySettings` push to one or
    /// more running renderers failed. Carries the joined per-renderer
    /// failure list so the caller can show which ones broke.
    #[error("settings persisted but live hot-reload failed: {0}")]
    SettingsApplyFailed(String),

    /// Library row was not in the persisted set.
    #[error("library {0} not found")]
    LibraryNotFound(i64),

    /// Playlist activate / lookup found no matching row.
    #[error("playlist not found: {0}")]
    PlaylistNotFound(String),

    /// Playlist create / mutate rejected by the persistence layer
    /// (constraint violation, bad name, …).
    #[error("playlist invalid: {0}")]
    PlaylistInvalid(String),
}

/// Daemon-wide `Result` alias. Callers explicitly import as
/// `use crate::error::Result;` (see module docs for why this isn't
/// re-exported from `lib.rs`). Currently unused outside this module —
/// callers in `ws_server` spell out `Result<_, Error>` to keep the
/// alias from competing with the ubiquitous `use anyhow::Result;`. The
/// alias stays so future migrations of `control` / `repo` to typed
/// errors land on a single canonical name.
#[allow(dead_code)]
pub type Result<T, E = Error> = std::result::Result<T, E>;

impl Error {
    /// Map this error onto its wire-level `pb::ErrorCode`. Always
    /// returns a non-`Ok` code — `Ok` is reserved for the success
    /// path (`ok_response`).
    pub fn error_code(&self) -> pb::ErrorCode {
        use pb::ErrorCode as E;
        match self {
            Self::Internal(_) | Self::Io(_) => E::Internal,
            Self::Db(_) => E::Db,
            Self::Decode(_) => E::Decode,
            Self::UnexpectedPayload(_) => E::UnexpectedPayload,
            Self::InvalidArgument(_) => E::InvalidArgument,
            Self::FailedPrecondition(_) => E::FailedPrecondition,
            Self::NoDisplayRegistered => E::NoDisplayRegistered,
            Self::WallpaperNotFound(_) => E::WallpaperNotFound,
            Self::RendererNotFound(_) => E::RendererNotFound,
            Self::NoRendererForType(_) => E::NoRendererForType,
            Self::RendererTypeMismatch { .. } => E::RendererTypeMismatch,
            Self::RendererSpawnFailed(_) => E::RendererSpawnFailed,
            Self::RendererControlFailed(_) => E::RendererControlFailed,
            Self::SourcePluginNotFound(_) => E::SourcePluginNotFound,
            Self::SourceExtrasFailed { .. } => E::SourceExtrasFailed,
            Self::SettingsValidationFailed(_) => E::SettingsValidationFailed,
            Self::SettingsApplyFailed(_) => E::SettingsApplyFailed,
            Self::LibraryNotFound(_) => E::LibraryNotFound,
            Self::PlaylistNotFound(_) => E::PlaylistNotFound,
            Self::PlaylistInvalid(_) => E::PlaylistInvalid,
        }
    }

    /// Coarse legacy `pb::Status` derived from `error_code()`. Kept so
    /// pre-`error_code` clients see a sensible status without a daemon
    /// flag day.
    pub fn status(&self) -> pb::Status {
        use pb::ErrorCode as E;
        use pb::Status as S;
        match self.error_code() {
            E::Ok => S::Ok,
            E::Decode
            | E::InvalidArgument
            | E::UnexpectedPayload
            | E::RendererTypeMismatch
            | E::NoRendererForType
            | E::SettingsValidationFailed
            | E::PlaylistInvalid => S::InvalidArgument,
            E::FailedPrecondition | E::NoDisplayRegistered => S::FailedPrecondition,
            E::WallpaperNotFound
            | E::RendererNotFound
            | E::SourcePluginNotFound
            | E::LibraryNotFound
            | E::PlaylistNotFound => S::NotFound,
            E::Internal
            | E::Db
            | E::RendererSpawnFailed
            | E::RendererControlFailed
            | E::SourceExtrasFailed
            | E::SettingsApplyFailed => S::Internal,
        }
    }

    /// Build a wire `Response` for an errored dispatch. Counterpart of
    /// `ok_response` for the success path.
    pub fn to_response(&self, request_id: u64) -> pb::Response {
        pb::Response {
            request_id,
            status: self.status() as i32,
            error_code: self.error_code() as i32,
            message: self.to_string(),
            payload: None,
        }
    }
}

/// Build a wire `Response` for a successful dispatch. Pins
/// `error_code = OK` and `status = OK`.
pub fn ok_response(request_id: u64, payload: pb::response::Payload) -> pb::Response {
    pb::Response {
        request_id,
        status: pb::Status::Ok as i32,
        error_code: pb::ErrorCode::Ok as i32,
        message: String::new(),
        payload: Some(payload),
    }
}
