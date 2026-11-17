#include "support/test_support.hpp"

#include "Graphics/ShaderProgram.h"

#include <filesystem>
#include <fstream>
#include <utility>

namespace fs = std::filesystem;

namespace tests
{

TEST_SUITE("ShaderProgram")
{
    TEST_CASE("get_file_contents reads a file")
    {
        const fs::path tempFile = fs::temp_directory_path() / "proceduralgeneration_shader_read.glsl";
        std::ofstream(tempFile.string()) << "void main() {}";
        AssertEqual(get_file_contents(tempFile.string().c_str()), std::string("void main() {}"),
                    "Shader program file contents should match");
        std::error_code ec;
        fs::remove(tempFile, ec);
    }

    TEST_CASE("get_file_contents throws on missing file")
    {
        bool thrown = false;
        try
        {
            (void)get_file_contents("__definitely_missing_shader__.glsl");
        }
        catch (int)
        {
            thrown = true;
        }
        Assert(thrown, "Reading a missing shader program file should throw");
    }

    TEST_CASE("default shader starts empty")
    {
        ShaderProgram shaderProgram;
        AssertEqual(shaderProgram.GetID(), static_cast<std::uint32_t>(0), "Default shader program id should be zero");
        Assert(!shaderProgram.IsCompiled(), "Default shader program should not be compiled");
    }

    TEST_CASE("moving default shader preserves zero id")
    {
        ShaderProgram source;
        ShaderProgram moved(std::move(source));
        AssertEqual(moved.GetID(), static_cast<std::uint32_t>(0), "Moved default shader program should still have zero id");
        Assert(!moved.IsCompiled(), "Moved default shader program should remain uncompiled");
    }
}

} // namespace tests
