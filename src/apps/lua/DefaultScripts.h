#pragma once

namespace hp {

// Bootstrap scripts shipped in firmware; an installed /apps/<id>/main.lua on
// AppFS or SD /Apps/32/<id>/main.lua overrides them without a firmware flash.
inline constexpr const char* kMusicDefaultScript = R"LUA(
local root = hp_root()
local info_lbl

function on_open()
  local title = hp_label(root, "Home-Display Musik")
  hp_align(title, "top_mid", 0, 40)

  info_lbl = hp_label(root, "")
  hp_align(info_lbl, "center", 0, 0)

  local prev = hp_button(root, "<<")
  hp_align(prev, "bottom_mid", -150, -40)
  hp_on_click(prev, "on_prev")

  local play = hp_button(root, "Play/Pause")
  hp_align(play, "bottom_mid", 0, -40)
  hp_on_click(play, "on_play")

  local nxt = hp_button(root, ">>")
  hp_align(nxt, "bottom_mid", 150, -40)
  hp_on_click(nxt, "on_next")

  refresh()
end

function refresh()
  local s = hp_music_snapshot()
  local text = "Titel: " .. tostring(s.count) .. "\nLautstaerke: " .. tostring(s.volume) .. " %"
  if s.error ~= "" then text = text .. "\n\n" .. s.error end
  hp_set_text(info_lbl, text)
end

function on_tick() refresh() end
function on_play() hp_media_command("play_pause") end
function on_prev() hp_media_command("previous") end
function on_next() hp_media_command("next") end
function on_close() end
)LUA";

}
