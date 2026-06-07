#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include "IniReader/INIReader.h"

struct Config {
    std::string source;
    std::string output;
    int threads;
};

// Parser odczytuje plik conf.ini (ten sam katalog co program) i zwraca konfigurację
Config Parser()
{
    INIReader reader("conf.ini");
    Config cfg;
    // brak sekcji w pliku, więc używamy pustego stringa jako section
    cfg.source = reader.GetString("", "source", "./input");
    cfg.output = reader.GetString("", "output", "./output");
    cfg.threads = static_cast<int>(reader.GetInteger("", "threads", 0));
    return cfg;
}

// Skanuje folder i zwraca wektor ścieżek do plików z rozszerzeniem .png
std::vector<std::string> scanFolder(const std::string& folder)
{
    std::vector<std::string> result;
    namespace fs = std::filesystem;
    try {
        if (!fs::exists(folder) || !fs::is_directory(folder))
            return result;

        for (const auto& entry : fs::recursive_directory_iterator(folder)) {
            if (!entry.is_regular_file())
                continue;
            fs::path p = entry.path();
            std::string ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
            if (ext == ".png") {
                result.push_back(p.string());
            }
        }
    }
    catch (const std::exception&) {
        // w razie błędu zwracamy pustą listę
    }
    return result;
}

int main()
{
    Config cfg = Parser();
    std::cout << "source = " << cfg.source << std::endl;
    std::cout << "output = " << cfg.output << std::endl;
    std::cout << "threads = " << cfg.threads << std::endl;

    auto files = scanFolder(cfg.source);
    for (const auto& f : files) {
        std::cout << f << std::endl;
    }

    return 0;
}
