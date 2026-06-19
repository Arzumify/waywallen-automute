local discover = import("wallhaven.discover")
local wallpaper = import("wallhaven.wallpaper")

local M = {}

function M.info()
    return {
        name = "wallhaven",
        capabilities = {
            discover = {
                search = true,
                details = true,
                download = true,
                sorts = {
                    { key = "trend", label = "Trending" },
                    { key = "recent", label = "Recent" },
                    { key = "popular", label = "Popular" },
                },
                tags = {},
            },
            wallpaper = {
                extras = true,
            },
        },
    }
end

M.discover = discover
M.wallpaper = wallpaper

return M
