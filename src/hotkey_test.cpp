#include "hotkey.hpp"
#include <cstdio>
int main() {
 HotkeyState state;
 auto require=[](bool condition,const char* message){ if(!condition){std::printf("FAIL: %s\n",message);return false;}return true; };
 if(!require(!state.Update(0x7B,true,6,0x7B),"F12 alone must not toggle"))return 1;
 state.Update(0x7B,false,6,0x7B);
 state.Update(0xA2,true,6,0x7B); // left Ctrl
 state.Update(0xA1,true,6,0x7B); // right Shift
 if(!require(state.Update(0x7B,true,6,0x7B),"Ctrl Shift F12 should toggle"))return 1;
 if(!require(!state.Update(0x7B,true,6,0x7B),"Held key must not repeatedly toggle"))return 1;
 state.Update(0x7B,false,6,0x7B);
 if(!require(state.Update(0x7B,true,6,0x7B),"Release must re-arm shortcut"))return 1;
 state.Update(0x7B,false,6,0x7B);state.Update(0xA4,true,6,0x7B);
 if(!require(!state.Update(0x7B,true,6,0x7B),"Extra modifier must not trigger another shortcut"))return 1;
 state.Update(0x7B,false,6,0x7B);state.Update(0xA4,false,6,0x7B);state.Update(0xA2,false,6,0x7B);
 if(!require(!state.Update(0x7B,true,6,0x7B),"Missing Ctrl must not toggle"))return 1;
 std::puts("PASS: shortcut modifiers, repeat suppression, release and re-arm");return 0;
}
