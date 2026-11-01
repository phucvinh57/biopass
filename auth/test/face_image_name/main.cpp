// Checks biopass::hasImageExtension (core/auth_config.h) against cases.txt,
// the table the app's face.rs tests read too. Exits non-zero on any failure.

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "auth_config.h"

namespace {

int g_failures = 0;

void expect(bool cond, const std::string& what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
  if (!cond) {
    g_failures++;
  }
}

}  // namespace

int main() {
  std::ifstream in(CASES_FILE);
  expect(in.good(), std::string("open ") + CASES_FILE);

  int cases = 0;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#')
      continue;
    std::istringstream fields(line);
    std::string name;
    int want = -1;
    fields >> name >> want;
    expect(want == 0 || want == 1, "well-formed line: " + line);
    expect(biopass::hasImageExtension(name) == (want == 1),
           name + (want == 1 ? " accepted" : " rejected"));
    cases++;
  }
  expect(cases > 0, "table has cases");

  return g_failures == 0 ? 0 : 1;
}
