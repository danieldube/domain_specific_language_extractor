#include <dsl/cli_exit_codes.h>

namespace dsl {

int CoherenceExitCode(const CoherenceResult &coherence) {
  switch (coherence.severity) {
  case CoherenceSeverity::kClean:
    return 0;
  case CoherenceSeverity::kIncoherent:
    return 2;
  }
  return 1;
}

} // namespace dsl
