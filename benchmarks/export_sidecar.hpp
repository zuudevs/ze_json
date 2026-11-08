#pragma once

// Export metadata sidecar writer (AGENTS.md section 4).
//
// The CMake export helpers (cmake/modules/{Testing,Benchmark}.cmake) run the
// instrumented executables with environment variables:
//   benchmarks: METADATA_OUT, RUN_ID, GIT_COMMIT
//   tests:      ZE_METADATA_OUT, ZE_RUN_ID, ZE_GIT_COMMIT
// and the executable MUST write a metadata.json sidecar to that path so raw
// exhibits stay reconstructible (out/raw/<run_id>/ ...).
//
// Kept deliberately dependency-free (C stdio only): it is harness tooling,
// not core library code, but it must link into both the test and benchmark
// executables without dragging STL/iostream into any core header.

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

namespace zuu::testing {

inline const char *env_or_empty(const char *name) {
  const char *value = std::getenv(name);
  return (value != nullptr) ? value : "";
}

inline void json_escape_into(std::string &out, const char *raw) {
  for (const char *p = raw; *p != '\0'; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    switch (*p) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default:
      if (c < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
        out += buf;
      } else {
        out += *p;
      }
    }
  }
}

/**
 * @brief Write the export sidecar JSON when running under an export helper.
 *
 * No-op when the out-path env var is absent (plain local runs stay clean).
 * Exits non-zero when the sidecar cannot be written: a silent missing sidecar
 * would break exhibit provenance (AGENTS.md section 4), which is worse than a
 * failed export run.
 *
 * @param out_env   env var holding the sidecar path (METADATA_OUT / ZE_METADATA_OUT)
 * @param run_env   env var holding the run id (RUN_ID / ZE_RUN_ID)
 * @param git_env   env var holding the git commit (GIT_COMMIT / ZE_GIT_COMMIT)
 * @param target    executable name recorded in the sidecar
 */
inline void write_export_sidecar(const char *out_env, const char *run_env,
                                 const char *git_env, const char *target) {
  const char *out_path = std::getenv(out_env);
  if (out_path == nullptr || *out_path == '\0') {
    return; // not running under the export helper
  }

  const std::time_t now = std::time(nullptr);
  std::tm tm_utc{};
#ifdef _WIN32
  gmtime_s(&tm_utc, &now);
#else
  gmtime_r(&now, &tm_utc);
#endif
  char date_utc[32] = {};
  std::strftime(date_utc, sizeof(date_utc), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);

  std::string run_id, git_commit;
  json_escape_into(run_id, env_or_empty(run_env));
  json_escape_into(git_commit, env_or_empty(git_env));

  std::string json;
  json.reserve(256);
  json += "{\n  \"schema_version\": \"1.0.0\",\n  \"kind\": \"export_sidecar\",\n";
  json += "  \"target\": \"";
  json_escape_into(json, target);
  json += "\",\n  \"run_id\": \"";
  json += run_id;
  json += "\",\n  \"git_commit\": \"";
  json += git_commit;
  json += "\",\n  \"date_utc\": \"";
  json += date_utc;
  json += "\",\n  \"library\": \"ze_json\"\n}\n";

  std::FILE *file = nullptr;
#ifdef _WIN32
  fopen_s(&file, out_path, "wb");
#else
  file = std::fopen(out_path, "wb");
#endif
  if (file == nullptr) {
    std::fprintf(stderr, "[export_sidecar] cannot write %s\n", out_path);
    std::exit(1);
  }
  std::fputs(json.c_str(), file);
  std::fclose(file);
}

} // namespace zuu::testing
