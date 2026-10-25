#include "stdafx.h"
#include "platform/MCVulkanShaders.h"
#include "platform/MCFileSystem.h"
#if defined(__APPLE__)
#include "shaders/composite.msl.h"
#include "shaders/draw.msl.h"
#include "shaders/instance.msl.h"
#include "shaders/quad.msl.h"
#include "shaders/terrain.msl.h"
// A built-in shader's code in Format: the Mac build's MSL.
#define MC_BUILT_IN_SHADER(name) MC_##name##_msl
#else
#include "shaders/composite.spv.h"
#include "shaders/draw.spv.h"
#include "shaders/instance.spv.h"
#include "shaders/quad.spv.h"
#include "shaders/terrain.spv.h"
// A built-in shader's code in Format: dxc's SPIR-V.
#define MC_BUILT_IN_SHADER(name) MC_##name##_spirv
#endif

namespace MCVulkanShaders
{
    const MCShaderInfo Quad{MCShader::Quad, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1};
    const MCShaderInfo Composite{MCShader::Composite, SDL_GPU_SHADERSTAGE_FRAGMENT, 5, 1};
    const MCShaderInfo DrawVertex{MCShader::Instance, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, 1};
    const MCShaderInfo DrawFragment{MCShader::Draw, SDL_GPU_SHADERSTAGE_FRAGMENT, 6, 1};
    const MCShaderInfo TerrainVertex{MCShader::Terrain, SDL_GPU_SHADERSTAGE_VERTEX, 1, 1, 2};

    std::string_view Name(MCShader shader)
    {
        switch (shader)
        {
            case MCShader::Quad:
                return "quad";
            case MCShader::Composite:
                return "composite";
            case MCShader::Instance:
                return "instance";
            case MCShader::Draw:
                return "draw";
            case MCShader::Terrain:
                return "terrain";
        }

        return {};
    }

    std::span<const unsigned char> BuiltInCode(MCShader shader)
    {
        switch (shader)
        {
            case MCShader::Quad:
                return MC_BUILT_IN_SHADER(quad);
            case MCShader::Composite:
                return MC_BUILT_IN_SHADER(composite);
            case MCShader::Instance:
                return MC_BUILT_IN_SHADER(instance);
            case MCShader::Draw:
                return MC_BUILT_IN_SHADER(draw);
            case MCShader::Terrain:
                return MC_BUILT_IN_SHADER(terrain);
        }

        return {};
    }

    std::vector<std::filesystem::path> SearchFolders()
    {
        std::vector<std::filesystem::path> folders;

        // SDL keeps the base path (null when it can't tell).
        if (SDL_GetBasePath() != nullptr)
        {
            folders.push_back(std::filesystem::path(SDL_GetBasePath()) / "shaders");
        }

        if (const std::filesystem::path& data = MCFileSystem::GameRoot(); !data.empty())
        {
            folders.push_back(data / "shaders");
        }

        return folders;
    }

    namespace
    {
        /// <summary>The first replacement for <paramref name="name"/>, or empty.</summary>
        std::filesystem::path FindReplacement(std::string_view name)
        {
            // Replacements are SPIR-V, so only a SPIR-V build takes one.
            if (Format != SDL_GPU_SHADERFORMAT_SPIRV)
            {
                return {};
            }

            const std::string file = std::format("{}.spv", name);

            for (const std::filesystem::path& folder : SearchFolders())
            {
                std::error_code error;
                const std::filesystem::path path = folder / file;

                if (std::filesystem::is_regular_file(path, error))
                {
                    return path;
                }
            }

            return {};
        }

        std::vector<unsigned char> ReadFile(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            return std::vector<unsigned char>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        }
    }

    std::expected<MCGpuHandle<SDL_GPUShader>, std::string> Load(std::shared_ptr<SDL_GPUDevice> device,
                                                                const MCShaderInfo& info)
    {
        const std::string name(Name(info.Shader));
        const std::span<const unsigned char> builtIn = BuiltInCode(info.Shader);
        SDL_GPUShaderCreateInfo create{};
        create.code = builtIn.data();
        create.code_size = builtIn.size();
        create.entrypoint = EntryPoint;
        create.format = Format;
        create.stage = info.Stage;
        create.num_samplers = info.Samplers;
        create.num_uniform_buffers = info.UniformBuffers;
        create.num_storage_buffers = info.StorageBuffers;
        std::vector<unsigned char> replacement;

        if (const std::filesystem::path path = FindReplacement(name); !path.empty())
        {
            replacement = ReadFile(path);

            if (replacement.empty() || replacement.size() % 4 != 0)
            {
                SDL_Log("MCVulkanShaders: %s isn't SPIR-V; using the built-in %s", path.string().c_str(), name.c_str());
            }
            else
            {
                create.code = replacement.data();
                create.code_size = replacement.size();
                SDL_Log("MCVulkanShaders: %s replaced by %s", name.c_str(), path.string().c_str());
            }
        }

        MCGpuHandle<SDL_GPUShader> shader(SDL_CreateGPUShader(device.get(), &create), MCGpuRelease{device});

        if (shader == nullptr && create.code != builtIn.data())
        {
            SDL_Log("MCVulkanShaders: the replacement for %s failed (%s); using the built-in", name.c_str(),
                    SDL_GetError());
            create.code = builtIn.data();
            create.code_size = builtIn.size();
            shader.reset(SDL_CreateGPUShader(device.get(), &create));
        }

        if (shader == nullptr)
        {
            return std::unexpected(std::format("SDL_CreateGPUShader({}): {}", name, SDL_GetError()));
        }

        return shader;
    }
}
