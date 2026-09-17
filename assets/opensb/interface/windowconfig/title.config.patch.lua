function patch(data)
  for i, v in pairs(data.mainMenuButtons) do
    if not v.rightAnchored then
      v.offset[2] = v.offset[2] + (v.key == "quit" and -5 or 15)
    end
  end

  -- The Mods menu is gone, the Workshop browser lists installed mods instead,
  -- so the Workshop button takes over the Mods button's corner.
  for i = #data.mainMenuButtons, 1, -1 do
    if data.mainMenuButtons[i].key == "mods" then
      table.remove(data.mainMenuButtons, i)
    end
  end
  -- A right anchored offset is the button's left edge measured from the right
  -- edge of the window, so it has to allow for the button's own width. The Mods
  -- cog was 18px wide at -22; the Workshop button is 86px wide, so -90 leaves it
  -- the same 4px gutter instead of hanging 64px off the screen.
  table.insert(data.mainMenuButtons, {
    key = "workshop",
    button = "/interface/modsmenu/workshopbutton.png",
    hover = "/interface/modsmenu/workshopbuttonhover.png",
    offset = jarray{-90, 2},
    rightAnchored = true
  })

  data.skyBackdropDarken = jarray{0, 0, 0, 64}
  local rng = sb.makeRandomSource(os.time())
  local barst = rng:randUInt(3000) == 0 and "barst"
  local terry = barst and rng:randUInt(5) == 0 and "starraria.png"
  local logo = terry or ((barst or "starb") .. "ound.png")
  data.backdropImages = jarray{
    jarray{
      jarray{0, 0}, 
      "/interface/title/" .. logo,
      terry and 0.75 or 0.5,
      jarray{0.5, terry and 0.75 or 0.5},
      { terry = terry }
    }
  }
  data.scripts = jarray{"/interface/title/title.lua"}
  return data
end
