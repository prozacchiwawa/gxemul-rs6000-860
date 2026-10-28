#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

std::string gen_header = R"(/*
 *  Copyright (C) 2005-2009  Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE   
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 *   
 *
 *  Automatically register all machines in the src/machines/ subdir.
 *
 *  NOTE: automachine_head.c, plus a line for each machine, plus
 *  automachine_middle.c, plus another line (again) for each machine, plus
 *  automachine_tail.c should be combined into one. See makeautomachine.sh
 *  for more info.
 */

#include <stdio.h>

#include "machine.h"


)";

std::string gen_middle = R"(/*
 *  automachine_init():
 */
void automachine_init(void)
{
	/*  printf("automachine_init()\n");  */

	/*  automachine_middle.c ends here.  */

)";

std::vector<std::string> machine_names;

int main(int argc, char** argv)
{
    if (argc != 3)
        return -1;

    std::fstream outstream;

    outstream.open(argv[2], std::ios::out | std::ios::trunc);
    if (outstream.is_open())
    {
        try
        {
            for (const std::filesystem::directory_entry& dir_entry : std::filesystem::directory_iterator(argv[1])) {
                if (!dir_entry.is_directory() && dir_entry.path().extension() == ".cc" && std::string(dir_entry.path().filename().string()).substr(0, 8) == "machine_") {
                    std::fstream in(dir_entry.path(), std::ios_base::in);
                    if (in.is_open()) {
                        for (std::string line; std::getline(in, line, '\n');)
                        {
                            std::size_t pos;
                            if ((pos = line.find("MACHINE_REGISTER(")) != std::string::npos) {
                                std::string machine_name(line.begin() + pos + 17, line.begin() + line.rfind(')'));
                                machine_names.push_back(machine_name);
                            }
                        }
                    }
                }
            }
            std::sort(machine_names.begin(), machine_names.end());
            outstream << gen_header;
            for (auto& machine : machine_names)
            {
                outstream << "void machine_register_" << machine << "(void);\n";
            }
            outstream << gen_middle;
            for (auto& machine : machine_names)
            {
                outstream << "\tmachine_register_" << machine << "();\n";
            }
            outstream << "\n}\n\n";
            outstream.close();
            return 0;
        }
        catch (std::filesystem::filesystem_error)
        {
            return -1;
        }
    }
}