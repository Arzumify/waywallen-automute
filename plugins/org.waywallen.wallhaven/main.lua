local api = import("wallhaven.api")
local map = import("wallhaven.map")

local M = {}

function M.info()
    return {
        name = "wallhaven",
        types = {"image"},
        version = "0.1.0",
        discover = {
            supports_search = true,
            sorts = {
                { key = "trend", label = "Trending" },
                { key = "recent", label = "Recent" },
                { key = "popular", label = "Popular" },
            },
            tags = {},
        },
    }
end

function M.scan(ctx)
    local entries = {}
    for _, dir in ipairs(ctx.libraries()) do
        if ctx.file_exists(dir) then
            for _, path in ipairs(map.image_paths(ctx, dir)) do
                local entry = map.scan_entry(ctx, dir, path)
                if entry then
                    table.insert(entries, entry)
                end
            end
        end
    end
    return entries
end

function M.discover(ctx, params)
    local payload = api.search(ctx, params)
    local items = {}
    for _, item in ipairs(payload.data or {}) do
        table.insert(items, map.search_item(item))
    end
    local meta = payload.meta or {}
    return {
        items = items,
        has_more = (meta.current_page or 1) < (meta.last_page or 1),
    }
end

function M.details(ctx, id)
    return map.details(api.wallpaper(ctx, id))
end

function M.download(ctx, id)
    return map.download(api.wallpaper(ctx, id))
end

function M.extras(entry)
    return {
        path = entry.resource,
    }
end

return M
