#pragma once
#include <array>

// Tracks only pressed state for a configurable shortcut. No key text or
// history is retained. Raw Input works without relying on WM_HOTKEY delivery.
struct HotkeyState {
 std::array<bool,256> down{};
 bool latched=false;
 bool Update(unsigned key,bool pressed,unsigned modifiers,unsigned primary) {
  if(key>=down.size()) return false;
  down[key]=pressed;
  if(key==primary && !pressed) latched=false;
  if(key!=primary || !pressed || latched) return false;
  unsigned current=0;
  if(down[0x12] || down[0xA4] || down[0xA5]) current|=1; // Alt
  if(down[0x11] || down[0xA2] || down[0xA3]) current|=2; // Ctrl
  if(down[0x10] || down[0xA0] || down[0xA1]) current|=4; // Shift
  if(down[0x5B] || down[0x5C]) current|=8;              // Windows
  if(current!=modifiers) return false;
  latched=true; return true;
 }
};
