#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

#include "src/d3d11/d3d11_shader_patch.h"
#include "src/dxvk/dxvk_shader_key.h"
#include <dxbc/dxbc_container.h>
#include <dxbc/dxbc_parser.h>

static void require(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}

static std::vector<uint8_t> readFile(const char* path) {
  std::ifstream file(path, std::ios::binary);
  require(bool(file), "Cannot open private shader input");
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static void rejects(const std::vector<uint8_t>& data) {
  try {
    dxvk::patchElexCloudShader(data.data(), data.size());
  } catch (const dxvk::DxvkError&) {
    return;
  }
  throw std::runtime_error("Corrupted target was not rejected");
}

int main(int argc, char** argv) {
  try {
    require(argc == 3, "Provide private original and independently corrected shader paths");
    auto original = readFile(argv[1]);
    auto expected = readFile(argv[2]);
    auto patched = dxvk::patchElexCloudShader(original.data(), original.size());
    require(patched == expected, "Port output differs from independently verified correction");
    size_t changes = 0;
    for (size_t i = 20; i < original.size(); i++) {
      if (original[i] != patched[i]) {
        require(i == 11577 && (original[i] ^ patched[i]) == 8, "Unexpected instruction change");
        changes++;
      }
    }
    require(changes == 1, "Expected exactly one instruction bit change");
    require(dxvk::patchElexCloudShader(nullptr, 0).empty(), "Null input not passed through");
    require(dxvk::patchElexCloudShader(patched.data(), patched.size()).empty(), "Not idempotent");
    auto corrupt = original;
    corrupt[1024] ^= 1;
    rejects(corrupt);
    corrupt = original;
    corrupt.pop_back();
    rejects(corrupt);
    corrupt = original;
    corrupt[4] ^= 1;
    require(dxvk::patchElexCloudShader(corrupt.data(), corrupt.size()).empty(),
      "Unrelated signature not passed through");

    dxbc_spv::dxbc::Container oldContainer(original.data(), original.size());
    dxbc_spv::dxbc::Container newContainer(patched.data(), patched.size());
    require(oldContainer.validateHash() && newContainer.validateHash(), "DXBC checksum invalid");
    auto oldHash = oldContainer.getHash();
    auto newHash = newContainer.getHash();
    dxvk::DxvkShaderHash oldKey(VK_SHADER_STAGE_COMPUTE_BIT,
      original.size(), oldHash.data.data(), oldHash.data.size());
    dxvk::DxvkShaderHash newKey(VK_SHADER_STAGE_COMPUTE_BIT,
      patched.size(), newHash.data.data(), newHash.data.size());
    require(oldKey.toString() != newKey.toString(), "Corrected shader aliases the old cache key");

    dxbc_spv::dxbc::Parser parser(newContainer.getCodeChunk());
    unsigned syncCount = 0;
    while (parser) {
      auto op = parser.parseInstruction();
      require(bool(op), "Parser rejected corrected instruction");
      if (op.getOpToken().getOpCode() == dxbc_spv::dxbc::OpCode::eSync) {
        require(bool(op.getOpToken().getSyncFlags() & dxbc_spv::dxbc::SyncFlag::eWorkgroupThreads),
          "A barrier still lacks workgroup execution synchronization");
        syncCount++;
      }
    }
    require(syncCount == 3, "Unexpected number of synchronization instructions");
    std::cout << "PASS: exact one-bit port, checksums, malformed input, unrelated pass-through, "
                 "idempotence, three execution barriers, distinct cache keys\n"
              << "Original key: " << oldKey.toString() << "\nCorrected key: " << newKey.toString() << '\n';
    return 0;
  } catch (const dxvk::DxvkError& e) {
    std::cerr << e.message() << '\n';
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
  }
  return 1;
}
