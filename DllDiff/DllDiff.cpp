#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <cassert>
#include <map>
#include <windows.h>
#include <winnt.h>

namespace fs = std::filesystem;

static void lower(std::string& str) {
    std::transform(str.begin(), str.end(), str.begin(), [](char c){return std::tolower(c);});
}

static bool starts(const std::string& str, const char* pref) {
    return str.rfind(pref, 0) == 0;
}

static int get_export_count(const char* dllName) {
    auto lib = LoadLibraryExA(dllName, NULL, DONT_RESOLVE_DLL_REFERENCES);
    assert(((PIMAGE_DOS_HEADER)lib)->e_magic == IMAGE_DOS_SIGNATURE);
    auto header = (PIMAGE_NT_HEADERS)((BYTE *)lib + ((PIMAGE_DOS_HEADER)lib)->e_lfanew);
    assert(header->Signature == IMAGE_NT_SIGNATURE);
    assert(header->OptionalHeader.NumberOfRvaAndSizes > 0);
    auto exports = (PIMAGE_EXPORT_DIRECTORY)((BYTE *)lib + header->
        OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    assert(exports->NumberOfFunctions != 0);
    return exports->NumberOfFunctions;
}

struct STATS {
    int processCount;
    int threadCount;
    int dllCount;
    int outputCount;
    int exceptionCount;
};

void log2matlab(const std::string& directoryPath) {
    std::string line, lineLower;
    for (const auto& entry : fs::directory_iterator(directoryPath)) {
        if (fs::is_regular_file(entry.path())) {
            auto filename = entry.path().filename().string();
            if (starts(filename, "log_")) {
                std::cout << "log2matlab " << filename << std::endl;

                STATS stats{};
                std::ifstream infile(entry.path());
                auto outName = directoryPath + "\\matlab" + filename.substr(3);
                std::ofstream outfile(outName);
                int subCount = 0;

                while (std::getline(infile, line)) {
                    if (starts(line, "DLL")) {
                        ++stats.dllCount;
                        lineLower = line;
                        lower(lineLower);
                        if (lineLower.find("matlab") != std::string::npos) {
                            outfile << line << " " << subCount << std::endl;
                            subCount = 0;
                        } else
                            ++subCount;
                    } else if (starts(line, "THREAD"))
                        ++stats.threadCount;
                    else if (starts(line, "OUTPUT"))
                        ++stats.outputCount;
                    else if (starts(line, "EXCEPTION"))
                        ++stats.exceptionCount;
                    else if (starts(line, "PROCESS"))
                        ++stats.processCount;
                }

                auto statsName = directoryPath + "\\stats" + filename.substr(3);
                std::ofstream statsFile(statsName);
                statsFile << "processCount = " << stats.processCount << std::endl;
                statsFile << "threadCount = " << stats.threadCount << std::endl;
                statsFile << "dllCount = " << stats.dllCount << std::endl;
                statsFile << "outputCount = " << stats.outputCount << std::endl;
                statsFile << "exceptionCount = " << stats.exceptionCount << std::endl;
            }
        }
    }
}

void matlab2data(const std::string& directoryPath) {
    std::string line, type, name;
    float start, finish;
    int pid, tid, subCount, exportCount;
    size_t fileSize;

    for (const auto& entry : fs::directory_iterator(directoryPath)) {
        if (fs::is_regular_file(entry.path())) {
            auto filename = entry.path().filename().string();            
            if (starts(filename, "matlab_")) {
                std::cout << "matlab2data " << filename << std::endl;

                std::ifstream infile(entry.path());
                auto outName = directoryPath + "\\data" + filename.substr(6);
                std::ofstream outfile(outName);

                while (std::getline(infile, line)) {
                    std::istringstream ss(line);
                    ss >> type >> pid >> tid >> start >> finish >> std::quoted(name, '"', '\0') >> subCount;
                    fileSize = fs::file_size(name);
                    exportCount = get_export_count(name.c_str());
                    name = name.substr(name.rfind('\\') + 1);
                    outfile << start << " " << name << " " << subCount << " " << fileSize << " " << exportCount << std::endl;
                }
            }
        }
    }
}

void diffFiles(const std::string& directoryPath, const fs::path& f1, const fs::path& f2) {
    std::cout << "diffFiles " << f1.stem().string() << " " << f2.stem().string() << std::endl;

    auto mapFile = [](const fs::path& pathDataFile) {
        std::map<std::string, int> m;
        std::string line, name;
        float start;
        std::ifstream dataFile(pathDataFile);
        while (std::getline(dataFile, line)) {
            std::istringstream ss(line);
            ss >> start >> name;
            lower(name);
            m[name]++;
        }
        return m;
    };

    auto m1 = mapFile(f1);
    auto m2 = mapFile(f2);
    
    std::string diffName = directoryPath + "\\diff_" + f1.stem().string() + "_" + f2.stem().string() + ".txt";
    std::ofstream diffFile(diffName);

    // m1 - m2
    auto diff = [&diffFile](const auto& m1, const auto& m2) {
        int count{};
        for (const auto& e : m1) {
            if (m2.find(e.first) == m2.end()) {
                diffFile << e.first << std::endl;
                ++count;
            }
        }
        return count;
    };

    diffFile << "-------------" << std::endl;
    diffFile << "REMOVED DLLs:" << std::endl;
    diffFile << "-------------" << std::endl;
    int removed = diff(m1, m2);
    diffFile << std::endl;
    diffFile << "TOTAL REMOVED " << removed << std::endl;
    diffFile << std::endl;

    diffFile << "-------------" << std::endl;
    diffFile << "ADDED DLLs:" << std::endl;
    diffFile << "-------------" << std::endl;
    int added = diff(m2, m1);
    diffFile << std::endl;
    diffFile << "TOTAL ADDED " << added << std::endl;
    diffFile << std::endl;

    diffFile << "------------------------------" << std::endl;
    diffFile << "DIFF = ADDED - REMOVED = " << added - removed << std::endl;
    diffFile << "------------------------------" << std::endl;
}

void data2diff(const std::string& directoryPath) {
    std::vector<fs::path> files;

    for (const auto& e : fs::directory_iterator(directoryPath)) {
        if( starts(e.path().filename().string(), "data_"))
            files.push_back(e.path());
    }

    std::sort(files.begin(), files.end());

    for (size_t i=0; i<files.size()-1; ++i)
        diffFiles(directoryPath, files[i], files[i+1]);
}

int main() {
    std::string folder = "C:\\c++\\ProcStats\\x64\\Release"; 
    log2matlab(folder);
    matlab2data(folder);
    data2diff(folder);
    return 0;
}
