use crate::settings::AutopauseMode;

/// Some mapped (non-minimized) window covers this display.
pub const FLAG_NON_MINIMIZED: u32 = 1 << 0;
/// Some window on this display has keyboard focus.
pub const FLAG_ACTIVE: u32 = 1 << 1;
/// Some window is H+V maximized (and NOT fullscreen).
pub const FLAG_MAXIMIZED: u32 = 1 << 2;
/// Some window is fullscreen.
pub const FLAG_FULLSCREEN: u32 = 1 << 3;

/// Bits the daemon understands. Higher bits are reserved and ignored.
pub const FLAGS_KNOWN: u32 = FLAG_NON_MINIMIZED | FLAG_ACTIVE | FLAG_MAXIMIZED | FLAG_FULLSCREEN;

/// Pure mapping: (mode, flags) → "autopause this display?".
pub fn decide(mode: AutopauseMode, flags: u32) -> bool {
    let has = |b: u32| flags & b != 0;
    match mode {
        AutopauseMode::Never => false,
        AutopauseMode::Any => has(FLAG_NON_MINIMIZED),
        AutopauseMode::Focus => has(FLAG_ACTIVE),
        AutopauseMode::Max => has(FLAG_MAXIMIZED) || has(FLAG_FULLSCREEN),
        AutopauseMode::FocusOrMax => {
            has(FLAG_ACTIVE) || has(FLAG_MAXIMIZED) || has(FLAG_FULLSCREEN)
        }
        AutopauseMode::FullScreen => has(FLAG_FULLSCREEN),
    }
}

/// Per-display autopause state held by the router.
#[derive(Debug, Default)]
pub struct State {
    /// Most recent flags the consumer reported.
    pub last_flags: u32,
    /// `decide(mode, last_flags)` — instantaneous raw signal.
    pub raw_want_pause: bool,
    /// Effective signal consumed by `reconcile_lifecycle`.
    /// Pause applies immediately; resume may be debounced.
    pub requested: bool,
    /// Bumped on every transition.
    /// Pending resume tasks no-op when their snapshot is stale.
    pub gen: u64,
}

impl State {
    pub fn new() -> Self {
        Self::default()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn decide_maps_modes_and_flags() {
        let stray = 1 << 30;
        let cases = [
            (AutopauseMode::Never, 0, false),
            (AutopauseMode::Never, 0xFFFFFFFF, false),
            (AutopauseMode::Any, 0, false),
            (AutopauseMode::Any, FLAG_NON_MINIMIZED, true),
            (AutopauseMode::Any, FLAG_FULLSCREEN, false),
            (AutopauseMode::Any, stray, false),
            (AutopauseMode::Any, stray | FLAG_NON_MINIMIZED, true),
            (AutopauseMode::Focus, FLAG_MAXIMIZED, false),
            (AutopauseMode::Focus, FLAG_ACTIVE, true),
            (AutopauseMode::Max, FLAG_MAXIMIZED, true),
            (AutopauseMode::Max, FLAG_FULLSCREEN, true),
            (AutopauseMode::Max, FLAG_ACTIVE, false),
            (AutopauseMode::Max, FLAG_NON_MINIMIZED, false),
            (AutopauseMode::FocusOrMax, FLAG_ACTIVE, true),
            (AutopauseMode::FocusOrMax, FLAG_MAXIMIZED, true),
            (AutopauseMode::FocusOrMax, FLAG_FULLSCREEN, true),
            (AutopauseMode::FocusOrMax, FLAG_NON_MINIMIZED, false),
            (AutopauseMode::FullScreen, FLAG_MAXIMIZED, false),
            (AutopauseMode::FullScreen, FLAG_FULLSCREEN, true),
        ];

        for (mode, flags, expected) in cases {
            assert_eq!(decide(mode, flags), expected, "{mode:?} flags={flags:#x}");
        }
    }
}
