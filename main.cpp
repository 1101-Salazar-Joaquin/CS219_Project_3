#include "cpu.h"
#include <iostream>
#include <fstream>
#include <cstring>

using namespace std;

int main() {
    cout << "Simple Assembly Simulator (C-style strings, BEQ supported)\n";

    const char *filename = "PP3_input.txt";
    ifstream infile(filename);
    if (!infile) {
        cerr << "Error: Cannot open PP3_input.txt\n";
        return 1;
    }

    static char lines[MAX_PROGRAM_LINES][LINE_LEN];
    int lineCount = 0;
    string temp;
    while (lineCount < MAX_PROGRAM_LINES && std::getline(infile, temp)) {
        strncpy(lines[lineCount], temp.c_str(), LINE_LEN - 1);
        lines[lineCount][LINE_LEN - 1] = '\0';
        lineCount++;
    }
    infile.close();

    CPU_State cpu;
    CPU_init(&cpu);
    CPU_loadProgram(&cpu, lines, lineCount);
    CPU_run(&cpu);

    cout << "\nSimulation complete.\n";
    return 0;
}