/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <exception>
#include <filesystem>
#include <iostream>

void runMp2kModuleTests();
void validateMp2kCorpus(const std::filesystem::path& path);
void exportMp2kCorpusSong(const std::filesystem::path& path, size_t song, const std::filesystem::path& directory);

int main(int argc, char** argv) {
  try {
    if (argc == 5 && std::string_view(argv[1]) == "--export-song") {
      exportMp2kCorpusSong(argv[2], std::stoul(argv[3]), argv[4]);
      return 0;
    }
    runMp2kModuleTests();
    for (int index = 1; index < argc; ++index) {
      validateMp2kCorpus(argv[index]);
      std::cout << "validated " << argv[index] << '\n';
    }
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
  return 0;
}
