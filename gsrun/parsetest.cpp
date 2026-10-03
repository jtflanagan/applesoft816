// parsetest: compiler/parse.c (AppleSoft's tokenizer, ported) against the
// IIgs ROM's own PARSE.INPUT.LINE ($D559), run on emu816, line by line.
//
//   parsetest ROM FILE...     every line of every file, as typed; prints the
//                             lines that differ, and a count
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "cpu816.hpp"
extern "C" {
#include "parse.h"
}

static std::vector<uint8_t> slurp(const char* path)
{
  std::vector<uint8_t> v;
  FILE* f = fopen(path, "rb");
  if (!f) return v;
  uint8_t b[65536];
  size_t n;
  while ((n = fread(b, 1, sizeof b, f)) > 0) v.insert(v.end(), b, b + n);
  fclose(f);
  return v;
}

// where PARSE starts: past leading spaces and a line number, as CHRGET and
// LINGET leave TXTPTR (CHRGET skips spaces, between digits too)
static uint8_t start_of(const uint8_t* in)
{
  uint8_t x = 0;
  while (in[x] == ' ') x++;
  while ((in[x] >= '0' && in[x] <= '9') || in[x] == ' ') x++;
  return x;
}

int main(int argc, char** argv)
{
  if (argc < 3) {
    fprintf(stderr, "usage: parsetest ROM FILE...\n");
    return 2;
  }
  auto rom = slurp(argv[1]);
  if (rom.size() < 65536) { fprintf(stderr, "parsetest: %s: no ROM\n", argv[1]); return 2; }
  Cpu816 c;
  const uint8_t* ff = &rom[rom.size() - 65536];
  memcpy(&c.mem[0xD000], ff + 0xD000, 0x3000);     // AppleSoft and the monitor, in bank 0
  int lines = 0, bad = 0;
  for (int f = 2; f < argc; f++) {
    auto text = slurp(argv[f]);
    std::string s(text.begin(), text.end());
    size_t pos = 0;
    while (pos < s.size()) {
      size_t nl = s.find('\n', pos);
      std::string line = s.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
      pos = nl == std::string::npos ? s.size() : nl + 1;
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.size() > 239) line.resize(239);      // INLIN's limit
      uint8_t in[256] = {0};
      for (size_t i = 0; i < line.size(); i++) in[i] = uint8_t(line[i]) & 0x7F;
      uint8_t x0 = start_of(in);

      // the ROM: $0200 = the line, TXTPTR = $0200 + x0, LOCK clear; JSR $D559
      memset(&c.mem[0x0200], 0, 256);
      memcpy(&c.mem[0x0200], in, 256);
      c.mem[0xB8] = x0, c.mem[0xB9] = 0x02, c.mem[0xD6] = 0;
      c.mem[0x0300] = 0xDB;                         // STP: back from PARSE
      c.reset_regs();
      c.mem[0x01FF] = 0x02, c.mem[0x01FE] = 0xFF;   // return to $0300
      c.set_regs(0, 0, 0, 0x01FD, 0, 0, 0x34, true);
      c.set_pc(0xD559);
      for (int n = 0; n < 200000 && !c.halted(); n++) c.step();
      if (!c.halted()) { printf("%s: ROM did not return: %s\n", argv[f], line.c_str()); bad++; continue; }
      uint8_t ry = uint8_t(c.y());

      // ours
      uint8_t buf[256];
      memcpy(buf, in, 256);
      uint8_t y = as816_parse(buf, x0);
      lines++;
      bool same = y == ry && memcmp(buf, &c.mem[0x0200], size_t(y - 4)) == 0 &&
                  buf[uint8_t(y - 3)] == c.mem[0x01FD + ry];
      if (!same) {
        bad++;
        printf("%s: differs: %s\n  ROM:", argv[f], line.c_str());
        for (int i = 0; i < ry - 4; i++) printf(" %02X", c.mem[0x0200 + i]);
        printf("\n  ours:");
        for (int i = 0; i < y - 4; i++) printf(" %02X", buf[i]);
        printf("\n");
      }
    }
  }
  printf("parsetest: %d lines, %d differ\n", lines, bad);
  return bad ? 1 : 0;
}
