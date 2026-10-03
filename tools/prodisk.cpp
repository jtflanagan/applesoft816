// prodisk - ProDOS-order disk images (.po) for the live tests, over the
// strudelworks host's ProdosVolume (a sibling clone, ../../strudelworks-host).
//
//   prodisk IMAGE ls [DIR]
//   prodisk IMAGE get PATH OUTFILE
//   prodisk IMAGE put PATH TYPE AUX FILE      (TYPE, AUX hex; replaces)
//   prodisk IMAGE rm PATH
//   prodisk IMAGE mkdir PATH
//   prodisk IMAGE format VOLNAME [BLOCKS]     (default 1600 = 800K)
//   prodisk IMAGE boot FROMIMAGE              (blocks 0-1: the boot loader)
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "prodos_fs.hpp"

static bool slurp(const char* path, std::vector<uint8_t>* out)
{
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  uint8_t chunk[65536];
  size_t n;
  out->clear();
  while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) out->insert(out->end(), chunk, chunk + n);
  fclose(f);
  return true;
}
static bool spit(const char* path, const std::vector<uint8_t>& v)
{
  FILE* f = fopen(path, "wb");
  bool ok = f && fwrite(v.data(), 1, v.size(), f) == v.size();
  if (f) fclose(f);
  return ok;
}
static int fail(const std::string& what)
{
  fprintf(stderr, "prodisk: %s\n", what.c_str());
  return 1;
}
static void split(const std::string& path, std::string* dir, std::string* name)
{
  size_t k = path.rfind('/');
  *dir = k == std::string::npos ? "" : path.substr(0, k);
  *name = k == std::string::npos ? path : path.substr(k + 1);
}

int main(int argc, char** argv)
{
  if (argc < 3) {
    fprintf(stderr, "usage: prodisk IMAGE ls [DIR] | get PATH OUT | put PATH TYPE AUX FILE | rm PATH |\n"
                    "               mkdir PATH | format VOLNAME [BLOCKS] | boot FROMIMAGE\n");
    return 2;
  }
  const char* image = argv[1];
  std::string cmd = argv[2], err;
  if (cmd == "format" && argc >= 4) {
    swh::MemBlockDevice dev(argc >= 5 ? uint32_t(strtoul(argv[4], nullptr, 0)) : 1600u);
    if (!swh::ProdosVolume::format(dev, argv[3], &err)) return fail(err);
    return spit(image, dev.bytes()) ? 0 : fail("cannot write image");
  }
  std::vector<uint8_t> img;
  if (!slurp(image, &img) || img.empty() || img.size() % 512) return fail(std::string(image) + ": not a block image");
  swh::MemBlockDevice dev(std::move(img));
  swh::ProdosVolume vol(dev);
  if (!vol.open(&err)) return fail(err);
  if (cmd == "ls") {
    swh::ProdosEntry d = vol.root();
    if (argc >= 4 && !vol.find(argv[3], &d, &err)) return fail(std::string(argv[3]) + ": " + err);
    std::vector<swh::ProdosEntry> v;
    if (!vol.list(d, &v, &err)) return fail(err);
    for (auto& e : v) printf("%-15s $%02X $%04X %7u %5u blk\n", e.name.c_str(), e.type, e.aux, e.eof, e.blocks_used);
    return 0;
  }
  if (cmd == "get" && argc == 5) {
    swh::ProdosEntry e;
    std::vector<uint8_t> data;
    if (!vol.find(argv[3], &e, &err) || !vol.read_file(e, &data, &err)) return fail(std::string(argv[3]) + ": " + err);
    return spit(argv[4], data) ? 0 : fail("cannot write output");
  }
  if (cmd == "put" && argc == 7) {
    std::vector<uint8_t> data;
    if (!slurp(argv[6], &data)) return fail(std::string("cannot read ") + argv[6]);
    std::string dir, name;
    split(argv[3], &dir, &name);
    swh::ProdosEntry old;
    std::string ferr;
    if (vol.find(argv[3], &old, &ferr) && !vol.remove(argv[3], &err)) return fail(err);
    if (!vol.create_file(dir, name, uint8_t(strtoul(argv[4], nullptr, 16)), uint16_t(strtoul(argv[5], nullptr, 16)),
                         data, &err))
      return fail(std::string(argv[3]) + ": " + err);
  } else if (cmd == "mkdir" && argc == 4) {
    std::string dir, name;
    split(argv[3], &dir, &name);
    if (!vol.create_dir(dir, name, &err)) return fail(std::string(argv[3]) + ": " + err);
  } else if (cmd == "rm" && argc == 4) {
    if (!vol.remove(argv[3], &err)) return fail(std::string(argv[3]) + ": " + err);
  } else if (cmd == "boot" && argc == 4) {
    std::vector<uint8_t> from;
    if (!slurp(argv[3], &from) || from.size() < 1024) return fail("no boot blocks");
    std::copy(from.begin(), from.begin() + 1024, dev.bytes().begin());
  } else {
    return fail("bad command");
  }
  return spit(image, dev.bytes()) ? 0 : fail("cannot write image");
}
