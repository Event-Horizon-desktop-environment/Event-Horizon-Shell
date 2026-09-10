-- ! Auto-generated file. Do not edit directly.
--
-- Usage in hyprland.lua:
--   local c = require("eh.colors")
--   hl.config({
--     general = {
--       col = {
--         active_border   = "rgb(" .. c.primary .. ")",
--         inactive_border = "rgb(" .. c.outline .. ")",
--       },
--     },
--     group = {
--       col = {
--         border_active          = "rgb(" .. c.primary .. ")",
--         border_inactive        = "rgb(" .. c.outline .. ")",
--         border_locked_active   = "rgb(" .. c.error .. ")",
--         border_locked_inactive = "rgb(" .. c.outline .. ")",
--       },
--       groupbar = {
--         col = {
--           active          = "rgb(" .. c.primary .. ")",
--           inactive        = "rgb(" .. c.outline .. ")",
--           locked_active   = "rgb(" .. c.error .. ")",
--           locked_inactive = "rgb(" .. c.outline .. ")",
--         },
--       },
--     },
--   })

local M = {}

M.primary = "{{colors.primary.default.hex_stripped}}"
M.outline = "{{colors.outline.default.hex_stripped}}"
M.error   = "{{colors.error.default.hex_stripped}}"

return M
