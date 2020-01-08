#include "mecab/utils.h"

#include <dirent.h>
#include <stdint.h>

#include <cstring>
#include <fstream>
#include <iostream>

#include "mecab.h"
#include "mecab/common.h"
#include "mecab/param.h"

namespace MeCab {

int decode_charset(const char* charset) {
  std::string tmp = charset;
  tmp = to_lower(tmp);
  if (tmp == "utf8" || tmp == "utf_8" || tmp == "utf-8")
    return UTF8;
  else if (tmp == "utf16" || tmp == "utf_16" || tmp == "utf-16")
    return UTF16;
  else if (tmp == "utf16be" || tmp == "utf_16be" || tmp == "utf-16be")
    return UTF16BE;
  else if (tmp == "utf16le" || tmp == "utf_16le" || tmp == "utf-16le")
    return UTF16LE;
  else if (tmp == "ascii")
    return ASCII;

  return UTF8;  // default is UTF8
}

std::string create_filename(const std::string& path, const std::string& file) {
  std::string s = path;
  if (s.size() && s[s.size() - 1] != '/')
    s += '/';
  s += file;
  return s;
}

std::string remove_filename(std::string path) {
  int len = static_cast<int>(path.size()) - 1;
  for (; len >= 0 && path.at(len) != '/'; --len)
    ;

  if (len > 0)
    return path.substr(0, len);

  return ".";
}

std::string remove_pathname(std::string path) {
  int len = static_cast<int>(path.size()) - 1;
  for (; len >= 0 && path.at(len) != '/'; --len)
    ;

  if (len > 0)
    return path.substr(len + 1, path.size() - len);

  return ".";
}

std::string replace_string(std::string string, const std::string& source, const std::string& destination) {
  const std::string::size_type pos = string.find(source);
  std::string result(string);
  if (pos != std::string::npos) {
    result.replace(pos, source.size(), destination);
  }
  return result;
}

void enum_csv_dictionaries(const char* path, std::vector<std::string>* dics) {
  dics->clear();

  DIR* dir = opendir(path);
  CHECK_DIE(dir) << "no such directory: " << path;

  for (struct dirent* dp = readdir(dir); dp; dp = readdir(dir)) {
    const std::string tmp = dp->d_name;
    if (tmp.size() >= 5) {
      std::string ext = tmp.substr(tmp.size() - 4, 4);
      if (to_lower(ext) == ".csv") {
        dics->push_back(create_filename(path, tmp));
      }
    }
  }
  closedir(dir);
}

std::string to_lower(std::string text) {
  std::string result{text};
  for (size_t i = 0; i < text.size(); ++i) {
    char c = result[i];
    if ((c >= 'A') && (c <= 'Z'))
      result[i] = c + 'a' - 'A';
  }
  return result;
}

bool escape_csv_element(std::string* w) {
  if (w->find(',') != std::string::npos || w->find('"') != std::string::npos) {
    std::string tmp = "\"";
    for (size_t j = 0; j < w->size(); j++) {
      if ((*w)[j] == '"')
        tmp += '"';
      tmp += (*w)[j];
    }
    tmp += '"';
    *w = tmp;
  }
  return true;
}

int progress_bar(const char* message, size_t current, size_t total) {
  static char bar[] = "###########################################";
  static int scale = sizeof(bar) - 1;
  static int prev = 0;

  int cur_percentage = static_cast<int>(100.0 * current / total);
  int bar_len = static_cast<int>(1.0 * current * scale / total);

  if (prev != cur_percentage) {
    printf("%s: %3d%% |%.*s%*s| ", message, cur_percentage, bar_len, bar, scale - bar_len, "");
    if (cur_percentage == 100)
      printf("\n");
    else
      printf("\r");
    fflush(stdout);
  }

  prev = cur_percentage;

  return 1;
}

int load_request_type(const Param& param) {
  int request_type = MECAB_ONE_BEST;

  if (param.get<bool>("allocate-sentence")) {
    request_type |= MECAB_ALLOCATE_SENTENCE;
  }

  if (param.get<bool>("partial")) {
    request_type |= MECAB_PARTIAL;
  }

  if (param.get<bool>("all-morphs")) {
    request_type |= MECAB_ALL_MORPHS;
  }

  if (param.get<bool>("marginal")) {
    request_type |= MECAB_MARGINAL_PROB;
  }

  const int nbest = param.get<int>("nbest");
  if (nbest >= 2) {
    request_type |= MECAB_NBEST;
  }

  // DEPRECATED:
  const int lattice_level = param.get<int>("lattice-level");
  if (lattice_level >= 1) {
    request_type |= MECAB_NBEST;
  }

  if (lattice_level >= 2) {
    request_type |= MECAB_MARGINAL_PROB;
  }

  return request_type;
}

bool load_dictionary_resource(Param* param) {
  std::string rcfile = param->get<std::string>("rcfile");

  if (rcfile.empty()) {
    const char* homedir = getenv("HOME");
    if (homedir) {
      const std::string s = MeCab::create_filename(std::string(homedir), ".mecabrc");
      std::ifstream ifs(s.c_str());
      if (ifs) {
        rcfile = s;
      }
    }
  }

  if (rcfile.empty()) {
    const char* rcenv = getenv("MECABRC");
    if (rcenv) {
      rcfile = rcenv;
    }
  }

  if (rcfile.empty()) {
    rcfile = MECAB_DEFAULT_RC;
  }

  if (!param->parseFile(rcfile.c_str())) {
    return false;
  }

  std::string dicdir = param->get<std::string>("dicdir");
  if (dicdir.empty()) {
    dicdir = ".";  // current
  }
  std::string rcpath = remove_filename(rcfile);
  dicdir = replace_string(dicdir, "$(rcpath)", rcpath);
  param->set("dicdir", dicdir, true);
  std::string dicrc = create_filename(dicdir, DICRC);

  return param->parseFile(dicrc);
}

namespace {

inline uint32_t rotl32(uint32_t x, uint8_t r) {
  return (x << r) | (x >> (32 - r));
}

#define ROTL32(x, y) rotl32(x, y)

//-----------------------------------------------------------------------------
// Block read - if your platform needs to do endian-swapping or can only
// handle aligned reads, do the conversion here

inline uint32_t getblock(const uint32_t* p, int i) {
  return p[i];
}

//-----------------------------------------------------------------------------
// Finalization mix - force all bits of a hash block to avalanche

inline uint32_t fmix(uint32_t h) {
  h ^= h >> 16;
  h *= 0x85ebca6b;
  h ^= h >> 13;
  h *= 0xc2b2ae35;
  h ^= h >> 16;

  return h;
}

void MurmurHash3_x86_128(const void* key, const int len, uint32_t seed, char* out) {
  const uint8_t* data = (const uint8_t*)key;
  const int nblocks = len / 16;

  uint32_t h1 = seed;
  uint32_t h2 = seed;
  uint32_t h3 = seed;
  uint32_t h4 = seed;

  uint32_t c1 = 0x239b961b;
  uint32_t c2 = 0xab0e9789;
  uint32_t c3 = 0x38b34ae5;
  uint32_t c4 = 0xa1e38b93;

  //----------
  // body

  const uint32_t* blocks = (const uint32_t*)(data + nblocks * 16);

  for (int i = -nblocks; i; i++) {
    uint32_t k1 = getblock(blocks, i * 4 + 0);
    uint32_t k2 = getblock(blocks, i * 4 + 1);
    uint32_t k3 = getblock(blocks, i * 4 + 2);
    uint32_t k4 = getblock(blocks, i * 4 + 3);

    k1 *= c1;
    k1 = ROTL32(k1, 15);
    k1 *= c2;
    h1 ^= k1;

    h1 = ROTL32(h1, 19);
    h1 += h2;
    h1 = h1 * 5 + 0x561ccd1b;

    k2 *= c2;
    k2 = ROTL32(k2, 16);
    k2 *= c3;
    h2 ^= k2;

    h2 = ROTL32(h2, 17);
    h2 += h3;
    h2 = h2 * 5 + 0x0bcaa747;

    k3 *= c3;
    k3 = ROTL32(k3, 17);
    k3 *= c4;
    h3 ^= k3;

    h3 = ROTL32(h3, 15);
    h3 += h4;
    h3 = h3 * 5 + 0x96cd1c35;

    k4 *= c4;
    k4 = ROTL32(k4, 18);
    k4 *= c1;
    h4 ^= k4;

    h4 = ROTL32(h4, 13);
    h4 += h1;
    h4 = h4 * 5 + 0x32ac3b17;
  }

  //----------
  // tail

  const uint8_t* tail = (const uint8_t*)(data + nblocks * 16);
  uint32_t k1 = 0;
  uint32_t k2 = 0;
  uint32_t k3 = 0;
  uint32_t k4 = 0;

  switch (len & 15) {
    case 15:
      k4 ^= tail[14] << 16;
    case 14:
      k4 ^= tail[13] << 8;
    case 13:
      k4 ^= tail[12] << 0;
      k4 *= c4;
      k4 = ROTL32(k4, 18);
      k4 *= c1;
      h4 ^= k4;

    case 12:
      k3 ^= tail[11] << 24;
    case 11:
      k3 ^= tail[10] << 16;
    case 10:
      k3 ^= tail[9] << 8;
    case 9:
      k3 ^= tail[8] << 0;
      k3 *= c3;
      k3 = ROTL32(k3, 17);
      k3 *= c4;
      h3 ^= k3;

    case 8:
      k2 ^= tail[7] << 24;
    case 7:
      k2 ^= tail[6] << 16;
    case 6:
      k2 ^= tail[5] << 8;
    case 5:
      k2 ^= tail[4] << 0;
      k2 *= c2;
      k2 = ROTL32(k2, 16);
      k2 *= c3;
      h2 ^= k2;

    case 4:
      k1 ^= tail[3] << 24;
    case 3:
      k1 ^= tail[2] << 16;
    case 2:
      k1 ^= tail[1] << 8;
    case 1:
      k1 ^= tail[0] << 0;
      k1 *= c1;
      k1 = ROTL32(k1, 15);
      k1 *= c2;
      h1 ^= k1;
  };

  //----------
  // finalization

  h1 ^= len;
  h2 ^= len;
  h3 ^= len;
  h4 ^= len;

  h1 += h2;
  h1 += h3;
  h1 += h4;
  h2 += h1;
  h3 += h1;
  h4 += h1;

  h1 = fmix(h1);
  h2 = fmix(h2);
  h3 = fmix(h3);
  h4 = fmix(h4);

  h1 += h2;
  h1 += h3;
  h1 += h4;
  h2 += h1;
  h3 += h1;
  h4 += h1;

  std::memcpy(out, reinterpret_cast<char*>(&h1), 4);
  std::memcpy(out + 4, reinterpret_cast<char*>(&h2), 4);
  std::memcpy(out + 8, reinterpret_cast<char*>(&h3), 4);
  std::memcpy(out + 12, reinterpret_cast<char*>(&h4), 4);
}
}  // namespace

uint64_t fingerprint(const char* str, size_t size) {
  uint64_t result[2] = {0};
  const uint32_t kFingerPrint32Seed = 0xfd14deff;
  MurmurHash3_x86_128(str, size, kFingerPrint32Seed, reinterpret_cast<char*>(result));
  return result[0];
}

uint64_t fingerprint(const std::string& str) {
  return fingerprint(str.data(), str.size());
}

bool file_exists(const char* filename) {
  std::ifstream ifs(filename);
  if (!ifs) {
    return false;
  }
  return true;
}
}  // namespace MeCab
