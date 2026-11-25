#pragma once

#include <cstdio>
#include <memory>
#include <string>

/// <summary>Closes a pipe to or from a command (popen's, _popen's on Windows), waiting for the command to end.</summary>
struct MCCommandPipeClose
{
    void operator()(FILE* pipe) const
    {
#ifdef _WIN32
        _pclose(pipe);
#else
        pclose(pipe);
#endif
    }
};

/// <summary>
/// A pipe to or from a command, closed as it goes. A test that wants the command's status from the close releases it
/// into <c>pclose</c> (<c>_pclose</c>) itself.
/// </summary>
using MCCommandPipe = std::unique_ptr<FILE, MCCommandPipeClose>;

/// <summary>Starts <paramref name="command"/> with a pipe to its input, written as bytes; none when it can't start.</summary>
inline MCCommandPipe MCPipeTo(const std::string& command)
{
#ifdef _WIN32
    return MCCommandPipe(_popen(command.c_str(), "wb"));
#else
    return MCCommandPipe(popen(command.c_str(), "w"));
#endif
}
