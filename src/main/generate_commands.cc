#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 4)
        return -1;

    std::fstream outstream;
    std::fstream outstream2;

    outstream.open(argv[2], std::ios::out | std::ios::trunc);
    outstream2.open(argv[3], std::ios::out | std::ios::trunc);
    if (outstream.is_open() && outstream2.is_open())
    {
        try
        {
            for (const std::filesystem::directory_entry& dir_entry : std::filesystem::directory_iterator(argv[1])) {
                if (!dir_entry.is_directory() && dir_entry.path().extension() == ".cc") {
                    std::size_t pos;
                    {
                        std::string component_cls(dir_entry.path().stem().string());
                        outstream << "#include \"commands/" << component_cls << ".h\"\n";
                        outstream2 << "\tAddCommand(new " << component_cls << ");\n";
                    }
                }
            }
        }
        catch (std::filesystem::filesystem_error)
        {
            return -1;
        }
    }
}