// lmvideo entry point: the DSL host (`--dsl FILE.dslb`, `--dsl-schema`, `--dsl-signatures`) and the two small
// library demos (`demo OUT.png`, `render IN.svg OUT.png`) share one executable so `cli="lmvideo"` finds everything.
#include <cstring>
#include <iostream>

#ifndef LMVIDEO_VERSION
#define LMVIDEO_VERSION "0.0.0"
#endif

int host_main(int argc, char** argv);
int cli_main(int argc, char* argv[]);

int main(int argc, char** argv) {
  if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
    std::cout << "lmvideo " LMVIDEO_VERSION "\n";
    return std::cout ? 0 : 4;
  }
  if (argc >= 2 && (std::strcmp(argv[1], "demo") == 0 || std::strcmp(argv[1], "render") == 0)) return cli_main(argc, argv);
  return host_main(argc, argv);
}
