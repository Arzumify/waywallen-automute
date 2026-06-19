local api = import("wallhaven.api")
local map = import("wallhaven.map")

local M = {}

function M.search(ctx, params)
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

return M
