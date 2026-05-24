#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iostream>
#include <array>
#include <regex>

std::regex re("#define.SPR_");

std::array<std::string, 1024> spr_strs;

int main(int argc, char **argv)
{
    std::string array[3];
    if (argc != 3) return -1;

    std::fstream in(argv[1], std::ios_base::in);
    std::fstream out(argv[2], std::ios_base::out | std::ios_base::trunc);
    if (in.is_open() && out.is_open())
    {
        out << "/*\n *  AUTOMATICALLY GENERATED from ppc_spr.h! Do "
	    "not edit.\n */\n\nstatic const char *ppc_spr_names[1024] = {\n";
        for (std::string line; std::getline(in, line, '\n');)
        {
            if (std::regex_search(line, re)) {
                std::stringstream strm(line);
                strm >> array[0];
                strm >> array[1];
                strm >> array[2];
                if (array[0] == "#define"
                    && array[1].substr(0, 4) == "SPR_") {
                    spr_strs.at(std::strtoull(array[2].c_str(), nullptr, 0)) = array[1].substr(array[1].find('_') + 1);
                    auto& ref = spr_strs.at(std::strtoull(array[2].c_str(), nullptr, 0));
                    std::transform(ref.begin(), ref.end(), ref.begin(),
                        [](unsigned char c){ return std::tolower(c); });
                }
            }
        }
        for (int i = 0; i < 1024; i++) {
            if (i == 1023) {
                out << "\t" << "/* 0x" << std::hex << i << std::dec << " */ \"" << ((spr_strs[i].size() > 0) ? spr_strs[i] : "(unknown)") << "\"\n";
            } else {
                out << "\t" << "/* 0x" << std::hex << i << std::dec << " */ \"" << ((spr_strs[i].size() > 0) ? spr_strs[i] : "(unknown)") << "\",\n";
            }
        }
        out << "};\n";
        out.close();
    }
}