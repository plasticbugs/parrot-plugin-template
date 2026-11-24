-- leave.lua: the console's side of a plugin's way back to the menu when it
-- comes late, in MAME. The cart runs the plugin's exit function between the
-- console parking and the menu (cvk_on_exit, cvf_on_exit: up to 3 seconds),
-- so the console must wait that long, and longer, and still boot the menu.
-- This script is the cart, serving $8000-$FFFF:
--
--   1. the screen kernel (mhkernel.bin) until its first HELLO
--   2. half a second later the way back: LEAVE_HOW=reset presses RESET;
--      LEAVE_HOW=combo is the controllers' way back, RST 0 over the NMI
--      vector at $8021 (cvhome.h), so the next vertical blank restarts the
--      BIOS
--   3. in the BIOS's header read, handoff.bin, as core 1 switches to it,
--      until the console is parked (64 reads of the title at $8024)
--   4. LEAVE_GAP seconds of $00: the outputs off, while the exit function
--      runs and the menu starts. The console must not restart (no read of
--      $8000) and must keep reading the title
--   5. a ROM with a title ("TEST/MENU/2026", start $8100: jr $), as the
--      menu's: the console must boot it (a read of $8100)
--
--   LEAVE_KERNEL=mhkernel.bin LEAVE_HANDOFF=handoff.bin LEAVE_HOW=reset LEAVE_GAP=4 \
--   mame coleco ... -autoboot_script test/leave/leave.lua   (test/run.sh's leave)
--
-- It prints one line, "leave: ... ok", or "leave: ... WRONG: why".
if leave_loaded then return end        -- MAME runs it again at each reset
leave_loaded = true

local function slurp(name)
  local f = assert(io.open(assert(os.getenv(name), name), "rb"))
  local d = f:read("a")
  f:close()
  return d
end
local kernel = slurp("LEAVE_KERNEL")
local handoff = slurp("LEAVE_HANDOFF")
handoff = handoff .. string.rep("\xC7", 0x8000 - #handoff)    -- RST 0 past it, as the cart serves it
local START = 0x8100
local menu = "\x55\xAA" .. string.rep("\0", 8) .. string.char(START & 0xFF, START >> 8)
menu = menu .. string.rep("\xC9\0\0", 7) .. "\xED\x45\0" .. "TEST/MENU/2026\0"
menu = menu .. string.rep("\xC7", START - 0x8000 - #menu) .. "\x18\xFE"
menu = menu .. string.rep("\xC7", 0x8000 - #menu)
assert(#kernel == 0x8000 and #handoff == 0x8000 and #menu == 0x8000)
local how = os.getenv("LEAVE_HOW") or "reset"
local gap = tonumber(os.getenv("LEAVE_GAP") or "4")
local TITLE, NMI, PARKED = 0x8024, 0x8021, 64

local prog = manager.machine.devices[":maincpu"].spaces["program"]
local function now() return manager.machine.time:as_double() end

local step, t_step = 1, 0
local steps = {
  { name = "the kernel", serve = "kernel", until_hello = true },
  { name = "the plugin runs", serve = "kernel", wait = 0.5 },
  { name = "the way back: the header read", serve = "kernel", until_header = true },
  { name = "the handoff", serve = "handoff", until_parked = true },
  { name = "the exit function and the menu's start", serve = nil, gap = true },
  { name = "the menu", serve = "menu", until_start = true },
}
local img = { kernel = kernel, handoff = handoff, menu = menu }
local times, bad = {}, {}
local title_reads, gap_reads, combo = 0, 0, false

local function next_step(t)
  times[#times + 1] = string.format("%s %.0f ms", steps[step].name, (t - t_step) * 1000)
  step = step + 1
  t_step = t
  title_reads = 0
  if step > #steps then
    if gap_reads < 100 then bad[#bad + 1] = "the console stopped reading the title in the gap" end
    print(string.format("leave: %s, %.1f s of $00 (%d title reads in it): %s | %s", how, gap, gap_reads,
      table.concat(times, ", "), #bad == 0 and "ok" or ("WRONG: " .. table.concat(bad, "; "))))
    manager.machine:exit()
  end
end

tap = prog:install_read_tap(0x8000, 0xFFFF, "cart", function(offset, d, mask)
  if step > #steps then return 0xFF end
  local s, t = steps[step], now()
  if s.gap then
    if offset == 0x8000 then bad[#bad + 1] = "restarted in the gap" end
    if offset == TITLE then gap_reads = gap_reads + 1 end
    if t - t_step < gap then return 0x00 end
    next_step(t)
    s = steps[step]
  end
  if s.until_header and offset == 0x8000 then
    next_step(t)                         -- core 1 switches inside the read
    s = steps[step]
  end
  local b = img[s.serve]:byte(offset - 0x8000 + 1)
  if combo and s.serve == "kernel" and offset == NMI then b = 0xC7 end   -- RST 0 at the NMI vector
  if s.until_parked and offset == 0x8000 and title_reads > 0 then
    bad[#bad + 1] = "the handoff restarted on its own title"
  end
  if s.until_hello and offset >= 0xFFA0 and offset < 0xFFC0 then
    next_step(t)
  elseif s.until_parked and offset == TITLE then
    title_reads = title_reads + 1
    if title_reads >= PARKED then next_step(t) end
  elseif s.until_start and offset == START then
    next_step(t)
  end
  return b
end)

emu.register_frame_done(function()
  if step > #steps then return end
  local s, t = steps[step], now()
  if s.wait and t - t_step >= s.wait then
    next_step(t)
    if how == "combo" then combo = true else manager.machine:soft_reset() end
  elseif not s.gap and t - t_step > 3 then
    bad[#bad + 1] = "stuck at: " .. s.name
    step = #steps
    next_step(t)
  end
end)
