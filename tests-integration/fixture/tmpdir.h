#ifndef __TEST_INTEGRATION_TMPDIR_H__
#define __TEST_INTEGRATION_TMPDIR_H__

#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <string>

namespace fixture {

namespace {
bool is_exists(std::string path) {
  struct stat info;
  return stat(path.c_str(), &info) == 0;
}
}  // namespace

class TmpDir {
 public:
  TmpDir(std::string basePath = "../tmp") {
    if (is_exists(basePath) == false) {
      if (mkdir(basePath.c_str(), 0777)) {
        throw new std::runtime_error("Cannot create tmp folder");
      }
    }

    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    path = basePath + "/tmpdir-" + std::to_string(us);
    if (mkdir(path.c_str(), 0777)) {
      // if failed
      throw new std::runtime_error("Cannot create tmp folder");
    }
  }
  ~TmpDir() { rmdir(path.c_str()); }
  const std::string getPath() const { return path; }
  const std::string createPath(std::string path) const {
    auto expectedPath = this->path + "/" + path;

    if (mkdir(expectedPath.c_str(), 0777)) {
      // if failed
      throw new std::runtime_error("Cannot create tmp folder");
    }

    return expectedPath;
  }

 private:
  std::string path;
};

}  // namespace fixture

#endif  //__TEST_INTEGRATION_TMPDIR_H__
