// `Grid.exe --selftest` — runs the portable engine checks and reports them.
#include <windows.h>

#include <string>

#include "app.h"
#include "../tests/selftest_core.h"

namespace grid {
namespace ui {

int runSelfTest(std::string& report) {
  wchar_t tmpDir[MAX_PATH] = L"";
  GetTempPathW(MAX_PATH, tmpDir);
  std::string temp = narrow(tmpDir) + "grid-selftest.grid";
  int failures = selftest::runEngineSelfTest(report, temp);
  return failures;
}

}  // namespace ui
}  // namespace grid
