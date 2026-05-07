//! Compatibility re-exports for IPC message types.
//!
//! The authoritative definitions live in
//! `crate::ipc::generated::{EventIn, Event}`, auto-generated from
//! `protocol/waywallen_ipc_v1.xml`. The XML uses `<event_in>` for the
//! daemon→subprocess (inbound) direction; this module publishes that
//! enum as `ControlMsg` and the renderer→daemon `<event>` enum as
//! `EventMsg` so existing call sites compile unchanged.

pub use crate::ipc::generated::{
    DecodeError, Event as EventMsg, EventIn as ControlMsg, PROTOCOL_NAME, PROTOCOL_VERSION,
};
