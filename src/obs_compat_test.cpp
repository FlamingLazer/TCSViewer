#include "obs_compat.hpp"
#include <iostream>

int main() {
 for (const auto* version : {"32.0.0", "32.0.4", "32.1.2", "32.2.2", "33.0.0", "33.0.1", "33.0.12"}) {
  if (!SupportsObsRuntime(version)) { std::cerr << "Unexpected rejection: " << version; return 1; }
 }
 for (const auto* version : {"31.1.2", "32.3.0", "33.1.0", "34.0.0", "33.0.1-beta1", "33.0.0-rc1",
                             "33.0.", "33.0.01", "33.0.-1", "33.0.1.0", "33.0.1junk", ""}) {
  if (SupportsObsRuntime(version)) { std::cerr << "Unexpected acceptance: " << version; return 1; }
 }
 std::cout << "OBS version policy checks passed\n";
 return 0;
}
