-- replay.lua: MAME's ColecoVision runs one of the SDK's two Z80 kernels, and
-- this script is the cart behind it: it serves the lists a plugin sent
-- (PC_LISTS: the lists.bin that test/host/host.c records), one after
-- another, the way the cart's core 1 does, then takes a snapshot and stops.
--
--   the screen kernel ('K' records)  a VDP write list at $A000 and the
--     mailbox at $FF00; the next list once the kernel reports the last one
--     run (ACK, or HELLO before its first: a read of $FF80 / $FFA0 | seq).
--     With PC_SOUND (the sound.bin beside lists.bin, which
--     tools/mame-replay.sh passes when it records a WAV), the mailbox's sound
--     too: each record's bytes at its time, by MAME's clock from the
--     kernel's first look at the mailbox, so the kernel sends the sound
--     chip what the cart would, when it would, for MAME's -wavwrite. The
--     run then lasts as long as the sound
--   the frame kernel ('F' records)   a list at $C000, a new one for each
--     read of $FFF0 | seq (one a TV frame); the controllers' reports
--     ($FFC0-$FFEF, and controller 2's at $FF80-$FFAF with CONTROLLER2)
--     are read and let go. Each list is checked for how the next one
--     started: straight after it, or after the kernel waited for the next
--     vertical blank. A list meant to run back to back (flag 1 in its
--     record) must be followed straight away, or the sound has a gap; any
--     other list that started in the blank must be done before the next
--     one, or it took longer than its TV frame.
--
-- tools/mame-replay.sh runs it; MAME never opens a window.

-- MAME runs an autoboot script again at each reset; once is enough
if replay_loaded then return end
replay_loaded = true

local f = assert(io.open(assert(os.getenv("PC_LISTS"), "PC_LISTS"), "rb"))
local data = f:read("a")
f:close()
local lists, p = {}, 1
while p + 8 <= #data do
  local kind, full, seq, len = string.unpack("<BBBI2", data, p)
  local flags = data:byte(p + 8)
  lists[#lists + 1] = { kind = string.char(kind), full = full, seq = seq, b2b = (flags & 1) ~= 0,
                        bytes = data:sub(p + 9, p + 8 + len) }
  p = p + 9 + len
end
assert(#lists > 0, "no lists in PC_LISTS")
local frames_kernel = lists[1].kind == "F"
print(string.format("replay: %d lists for the %s kernel", #lists, frames_kernel and "frame" or "screen"))

local prog = manager.machine.devices[":maincpu"].spaces["program"]
local at, cur = 0, nil          -- lists served so far, and the one being served
local LINGER = 20                -- TV frames between the last list's end and the snapshot: MAME
                                -- draws a frame as it goes, and skips some when it runs flat out
local linger = nil              -- what is left of them
local finished = false

local function finish()
  if finished then return end
  finished = true
  manager.machine.video:snapshot()
  print(string.format("replay: %d of %d lists run", at, #lists))
  manager.machine:exit()
end

if frames_kernel then
  local EMPTY = "\0\0"
  local bytes = EMPTY
  -- How the next list started: the kernel reads FRAMES ($7000) twice on its
  -- way from a list's end to the next ($FFF0 | seq), once more if the NMI
  -- comes in between (its handler reads it too), and only when it waited
  -- for the NMI, once more each time round the wait. So four or more reads
  -- since the list's last byte: it waited.
  local reads = 0
  local late, b2b, gaps = 0, 0, 0
  local prev, prev_in_blank = nil, true
  replay_frames = prog:install_read_tap(0x7000, 0x7000, "frames", function(offset, d, mask)
    reads = reads + 1
  end)
  replay_tap = prog:install_read_tap(0xC000, 0xFFFF, "cart", function(offset, d, mask)
    if offset >= 0xFFF0 then    -- a frame starts: its list
      local waited = reads >= 4
      if prev and at <= #lists then
        if prev.b2b then
          b2b = b2b + 1
          if waited then gaps = gaps + 1 end
        elseif prev_in_blank and not waited then
          late = late + 1       -- it started in the blank and ran into the next frame
        end
      end
      prev_in_blank = waited or not prev
      reads = 0
      if at < #lists then
        at = at + 1
        bytes = lists[at].bytes
        prev = lists[at]
      else
        if not linger then
          print(string.format("replay: %d lists took longer than their TV frame", late))
          if b2b > 0 then
            print(string.format("replay: %d lists back to back, %d of them followed by a wait for the next frame (a gap in the sound)", b2b, gaps))
          end
        end
        prev = nil
        bytes = EMPTY
        linger = (linger or LINGER) - 1
        if linger == 0 then finish() end
      end
      return 0xFF
    elseif offset >= 0xFF80 then
      return 0xFF               -- the controllers' reports
    end
    reads = 0
    return bytes:byte(offset - 0xC000 + 1) or 0
  end)
else
  -- the mailbox: silence (the PSG's own latch bytes), no list yet
  local mb = {}
  for i = 0, 0x7F do mb[i] = 0 end
  local silence = { 0x80, 0x00, 0x9F, 0xA0, 0x00, 0xBF, 0xC0, 0x00, 0xDF, 0xFF }
  for i = 1, 10 do mb[i] = silence[i] end
  -- the sound (sound.bin, test/host/host.c): "PCSOUND1", then a record each
  -- time the plugin published a change: 4 bytes of microseconds from its
  -- cvk_start() (little end first), and the mailbox's $FF00-$FF0C as
  -- published (SND_SEQ, the 10 SND bytes, NSEQ, NCTL)
  local snd, snd_at, snd_t0, snd_done = {}, 1, nil, true
  local snd_path = os.getenv("PC_SOUND")
  if snd_path and snd_path ~= "" then
    local sf = assert(io.open(snd_path, "rb"))
    local sd = sf:read("a")
    sf:close()
    assert(sd:sub(1, 8) == "PCSOUND1", "PC_SOUND: not a sound.bin")
    local q = 9
    while q + 16 <= #sd do
      snd[#snd + 1] = { t = string.unpack("<I4", sd, q), bytes = sd:sub(q + 4, q + 16) }
      q = q + 17
    end
    snd_done = #snd == 0
    print(string.format("replay: %d sound changes, %.1f s", #snd, #snd > 0 and snd[#snd].t / 1e6 or 0))
  end
  local lists_done = false
  local function sound_step()
    local now = manager.machine.time:as_double()
    if not snd_t0 then snd_t0 = now end
    local us = (now - snd_t0) * 1e6
    while snd_at <= #snd and snd[snd_at].t <= us do
      local b = snd[snd_at].bytes
      for i = 0, 12 do mb[i] = b:byte(i + 1) end
      snd_at = snd_at + 1
    end
    if snd_at > #snd and not snd_done then
      snd_done = true
      print(string.format("replay: %d sound changes played", #snd))
      if lists_done then finish() end
    end
  end
  replay_tap = prog:install_read_tap(0xA000, 0xFFFF, "cart", function(offset, d, mask)
    if offset >= 0xFF80 then
      local kind, seq = (offset >> 5) & 3, offset & 31
      if kind <= 1 and (cur == nil or seq == (cur.seq & 31)) then   -- ACK or HELLO: the last list is done with
        if at < #lists then
          at = at + 1
          cur = lists[at]
          mb[0x11] = cur.full   -- LIST_FULL, then LIST_SEQ
          mb[0x10] = cur.seq
        elseif not lists_done then
          linger = (linger or 2 * LINGER) - 1   -- (each report is read twice)
          if linger == 0 then
            lists_done = true
            if snd_done then finish() end
          end
        end
      end
      return 0xFF
    elseif offset >= 0xFF00 then
      if offset == 0xFF00 and #snd > 0 then sound_step() end   -- (the kernel reads SND_SEQ all the time)
      return mb[offset - 0xFF00]
    end
    return cur and cur.bytes:byte(offset - 0xA000 + 1) or 0xFF
  end)
end
