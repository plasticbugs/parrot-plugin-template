-- controllers.lua: MAME's ColecoVision runs the frame kernel, and this
-- script is the cart behind it. It presses MAME's two hand controllers in
-- turn (each direction, the two fire buttons and every keypad key, on
-- controller 2 and then on controller 1) and decodes the kernel's reports
-- as the cart's bus loop does (cvfserve.S): a read of $FF80+n is report
-- n >> 4, nibble n & 15. Each step must come out as cvf_input() and
-- cvf_input2() encode it (low = pressed), with the other controller at
-- rest.
--
--   PC_CTRL2=1  the kernel was built with CONTROLLER2: both controllers
--               are reported. Otherwise only controller 1 is, and nothing
--               may read $FF80-$FFBF.
--
-- tools/mame-controllers.sh runs it; MAME never opens a window.

if controllers_loaded then return end   -- (MAME runs it again at a reset)
controllers_loaded = true

local ctrl2 = os.getenv("PC_CTRL2") == "1"
local prog = manager.machine.devices[":maincpu"].spaces["program"]
local ports = manager.machine.ioport.ports

local function field(port, name)
  local p = assert(ports[port], port)
  return assert(p.fields[name], port .. ": " .. name)
end

-- controller c's switches by name, in MAME's ports
local function switches(c)
  local j, k = ":STD_JOY" .. c, ":STD_KEYPAD" .. c
  local s = {
    up = field(j, "P" .. c .. " Up"), right = field(j, "P" .. c .. " Right"),
    down = field(j, "P" .. c .. " Down"), left = field(j, "P" .. c .. " Left"),
    fire = field(j, "P" .. c .. " Button 1"), fire2 = field(k, "P" .. c .. " Button 2"),
  }
  for d = 0, 9 do s[tostring(d)] = field(k, d .. " (pad " .. c .. ")") end
  s["*"] = field(k, "* (pad " .. c .. ")")
  s["#"] = field(k, "# (pad " .. c .. ")")
  return s
end
local sw = { switches(1), switches(2) }

-- what the cart should see for a step's switches: {joy, fire, key}
local DIRS = { up = 1, right = 2, down = 4, left = 8 }
local KEYS = { ["0"] = 10, ["1"] = 13, ["2"] = 7, ["3"] = 12, ["4"] = 2, ["5"] = 3,
               ["6"] = 14, ["7"] = 5, ["8"] = 1, ["9"] = 11, ["*"] = 9, ["#"] = 6 }   -- wired.h's lines
local function want(held)
  local joy, fire, key = 15, 3, 15
  for _, w in ipairs(held) do
    if DIRS[w] then joy = joy & ~DIRS[w] end
    if w == "fire" then fire = fire & ~1 end
    if w == "fire2" then fire = fire & ~2 end
    if KEYS[w] then key = KEYS[w] end
  end
  return { joy, fire, key }
end

local steps = {}
local function add(c, held) steps[#steps + 1] = { c = c, held = held } end
for _, c in ipairs(ctrl2 and { 2, 1 } or { 1 }) do
  for _, h in ipairs({ { "up" }, { "right" }, { "down" }, { "left" }, { "up", "right" }, { "down", "left" },
                       { "fire" }, { "fire2" }, { "fire", "fire2" }, { "left", "fire2" } }) do add(c, h) end
  for _, k in ipairs({ "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "#" }) do add(c, { k }) end
  add(c, {})
end

local HOLD, CHECK = 8, 4                -- frames a step is held, and the last few checked
local frame, step = 0, 0
local seen = { {}, {} }                 -- seen[c][slot]: the nibble last reported
local reads_low = 0                     -- reads of $FF80-$FFBF
local good, bad, fails = 0, 0, {}

local function press(s, on)
  if not s then return end
  for _, w in ipairs(s.held) do sw[s.c][w]:set_value(on and 1 or 0) end
end

local function check(s)
  for c = 1, 2 do
    local w = (c == s.c) and want(s.held) or want({})
    if c == 2 and not ctrl2 then goto next end
    for slot = 1, 3 do
      if seen[c][slot] == w[slot] then good = good + 1
      else
        bad = bad + 1
        if #fails < 8 then
          fails[#fails + 1] = string.format("controller %d holding %s: %s %s, wanted %s", s.c,
            #s.held > 0 and table.concat(s.held, "+") or "nothing", ({ "joystick", "fires", "keypad" })[slot] .. " of " .. c,
            seen[c][slot] and string.format("%X", seen[c][slot]) or "never reported", string.format("%X", w[slot]))
        end
      end
    end
    ::next::
  end
end

local EMPTY = "\0\0"
controllers_tap = prog:install_read_tap(0xC000, 0xFFFF, "cart", function(offset, d, mask)
  if offset >= 0xFF80 then
    local n = offset - 0xFF80
    local slot = n >> 4
    if slot == 7 then                   -- $FFF0+seq: a frame starts
      frame = frame + 1
      local s = steps[step]
      if s and (frame % HOLD) >= HOLD - CHECK then check(s) end
      if frame % HOLD == 0 then
        press(s, false)
        step = step + 1
        if step > #steps then
          print(string.format("controllers: %d steps on controller%s, %d nibbles right, %d wrong%s",
            #steps, ctrl2 and "s 2 and 1" or " 1", good, bad,
            ctrl2 and "" or string.format(", %d reads of $FF80-$FFBF", reads_low)))
          for _, f in ipairs(fails) do print("controllers:   " .. f) end
          print("controllers: " .. ((bad == 0 and good > 0 and (ctrl2 or reads_low == 0)) and "ok" or "FAILED"))
          manager.machine:exit()
        end
        press(steps[step], true)
      end
    elseif slot >= 4 then seen[1][slot - 3] = n & 15
    else
      reads_low = reads_low + 1
      if slot <= 2 then seen[2][slot + 1] = n & 15 end
    end
    return 0xFF
  end
  return EMPTY:byte(offset - 0xC000 + 1) or 0
end)
