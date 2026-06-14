local M = {}

local IMAGE_EXTS = {
    png = true,
    jpg = true,
    jpeg = true,
}

local function strip_ext(name)
    return name:match("(.+)%.[^.]+$") or name
end

local function file_ext(path)
    return string.lower(path:match("%.([^./?#]+)$") or "jpg")
end

local function basename(path)
    return path:match("([^/]+)$") or path
end

local function tags_from_detail(detail)
    local out = {}
    for _, tag in ipairs(detail.tags or {}) do
        if tag.name and tag.name ~= "" then
            table.insert(out, tag.name)
        end
    end
    return out
end

local function rating(purity)
    if purity == "nsfw" then
        return "Mature"
    end
    if purity == "sketchy" then
        return "Questionable"
    end
    return "Everyone"
end

local function title(item)
    if item.id and item.id ~= "" then
        return "Wallhaven " .. item.id
    end
    return item.url or item.path or "Wallhaven"
end

function M.search_item(item)
    local thumbs = item.thumbs or {}
    return {
        id = item.id or "",
        title = title(item),
        preview_url = thumbs.large or thumbs.original or thumbs.small or item.path or "",
        author = "",
        extra = {
            resolution = item.resolution or "",
            purity = item.purity or "",
        },
    }
end

function M.details(detail)
    local description = detail.source or detail.url or ""
    return {
        description = description,
        size = tostring(detail.file_size or ""),
        tags = tags_from_detail(detail),
        extra = {
            url = detail.url or "",
            path = detail.path or "",
            purity = detail.purity or "",
            resolution = detail.resolution or "",
        },
    }
end

function M.download(detail)
    local ext = file_ext(detail.path or "")
    local thumbs = detail.thumbs or {}
    return {
        url = detail.path or "",
        filename = "wallhaven-" .. tostring(detail.id or "wallpaper") .. "." .. ext,
        title = title(detail),
        preview_url = thumbs.large or thumbs.original or thumbs.small or "",
        description = detail.source or detail.url or "",
        tags = tags_from_detail(detail),
        external_id = tostring(detail.id or ""),
        size = detail.file_size,
        width = detail.dimension_x,
        height = detail.dimension_y,
        content_rating = rating(detail.purity),
    }
end

function M.image_paths(ctx, dir)
    local out = {}
    local seen = {}
    for _, path in ipairs(ctx.glob(dir .. "/*.*")) do
        local ext = ctx.extension(path)
        if ext and IMAGE_EXTS[string.lower(ext)] and not seen[path] then
            seen[path] = true
            table.insert(out, path)
        end
    end
    return out
end

function M.scan_entry(ctx, dir, path)
    local sidecar = path .. ".json"
    local meta = {}
    local raw = ctx.read_file(sidecar)
    if raw and raw ~= "" then
        meta = ctx.json_parse(raw) or {}
    end

    local filename = ctx.filename(path) or basename(path)
    local name = meta.title or strip_ext(filename)
    return {
        name = name,
        wp_type = "image",
        resource = path,
        library_root = dir,
        description = meta.description,
        tags = meta.tags or {},
        external_id = meta.external_id,
        size = meta.size or ctx.file_size(path),
        width = meta.width,
        height = meta.height,
        content_rating = meta.content_rating,
    }
end

return M
