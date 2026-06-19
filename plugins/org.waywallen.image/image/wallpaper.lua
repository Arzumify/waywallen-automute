local M = {}

function M.properties()
    return {
        ["waywallen.scheme_color"] = {
            text = "Scheme color",
            type = "color",
            value = {0.0, 0.0, 0.0, 1.0},
        },
    }
end

function M.extras(entry)
    return {
        path = entry.resource,
    }
end

return M
