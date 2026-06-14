#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <chrono>
#include <cmath>
#include <opencv2/opencv.hpp>
#include "IniReader/INIReader.h"
#include <clocale>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
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

// Tworzy mozaikę z zestawu obrazów i zapisuje do outFile
void createMosaic(const std::vector<std::string>& paths, const std::string& outFile, int cellW = 200, int cellH = 200)
{
    if (paths.empty()) return;
    size_t n = paths.size();
    int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
    int rows = static_cast<int>(std::ceil(static_cast<double>(n) / cols));

    int mosaicW = cols * cellW;
    int mosaicH = rows * cellH;

    cv::Mat mosaic(mosaicH, mosaicW, CV_8UC3, cv::Scalar(40,40,40));

    for (size_t i = 0; i < n; ++i) {
        cv::Mat img = cv::imread(paths[i], cv::IMREAD_UNCHANGED);
        if (img.empty()) continue;

        cv::Mat imgColor;
        if (img.channels() == 1) cv::cvtColor(img, imgColor, cv::COLOR_GRAY2BGR);
        else if (img.channels() == 4) cv::cvtColor(img, imgColor, cv::COLOR_BGRA2BGR);
        else imgColor = img;

        double scale = std::min(static_cast<double>(cellW) / imgColor.cols, static_cast<double>(cellH) / imgColor.rows);
        int newW = static_cast<int>(imgColor.cols * scale);
        int newH = static_cast<int>(imgColor.rows * scale);
        if (newW <= 0) newW = 1;
        if (newH <= 0) newH = 1;

        cv::Mat resized;
        cv::resize(imgColor, resized, cv::Size(newW, newH));

        int row = static_cast<int>(i) / cols;
        int col = static_cast<int>(i) % cols;
        int x = col * cellW + (cellW - newW) / 2;
        int y = row * cellH + (cellH - newH) / 2;

        cv::Rect roi(x, y, newW, newH);
        if (roi.x >= 0 && roi.y >= 0 && roi.x + roi.width <= mosaic.cols && roi.y + roi.height <= mosaic.rows) {
            resized.copyTo(mosaic(roi));
        }
    }

    try {
        cv::imwrite(outFile, mosaic);
    } catch (...) {}
}

int main()
{
    // Odczyt konfiguracji
    Config cfg = Parser();
    unsigned int actual_threads = cfg.threads > 0 ? static_cast<unsigned int>(cfg.threads) : std::thread::hardware_concurrency();
    if (actual_threads == 0) actual_threads = 1;
#ifdef _WIN32
    // Ustawienie konsoli na UTF-8 aby poprawnie wyświetlać polskie znaki
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::setlocale(LC_ALL, "");
    std::ios::sync_with_stdio(false);
    std::cout << "ścieżka plików wejściowych: " << cfg.source << std::endl;
    std::cout << "ścieżka plików wyjściowych: " << cfg.output << std::endl;
    std::cout << "Przetwarzanie na " << actual_threads << " wątków" << std::endl;

    // Skanowanie katalogu źródłowego
    auto files = scanFolder(cfg.source);

    // Przygotowanie kolejki zadań
    std::queue<std::string> tasks;
    for (const auto& f : files) tasks.push(f);
    std::mutex tasks_mutex;

    // Atomiczny licznik postępu
    std::atomic<size_t> processed_count{0};
    const size_t total = files.size();

    // Ustalenie liczby wątków
    unsigned int num_threads = actual_threads;

    // Utworzenie katalogu output jeśli nie istnieje
    namespace fs = std::filesystem;
    try {
        fs::create_directories(cfg.output);
    } catch (...) {}

    // Worker: pobiera ścieżkę z kolejki, przetwarza obraz i zapisuje wynik
    auto worker = [&](unsigned int id){
        while (true) {
            std::string task_path;
            {
                std::lock_guard<std::mutex> lock(tasks_mutex);
                if (tasks.empty()) break;
                task_path = tasks.front();
                tasks.pop();
            }

            try {
                cv::Mat img = cv::imread(task_path, cv::IMREAD_COLOR);
                if (img.empty()) {
                    std::cerr << "[worker " << id << "] nie mozna wczytac: " << task_path << std::endl;
                    processed_count.fetch_add(1);
                    continue;
                }

                cv::Mat gray, edges;
                cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
                cv::GaussianBlur(gray, gray, cv::Size(5,5), 1.5);
                cv::Canny(gray, edges, 30, 130);

                fs::path inPath(task_path);
                fs::path outPath = fs::path(cfg.output) / inPath.filename();
                // Zapisz jako PNG (edges to pojedynczy kanał) - cv::imwrite wykryje format po rozszerzeniu
                cv::imwrite(outPath.string(), edges);
            }
            catch (const std::exception& ex) {
                std::cerr << "[worker " << id << "] blad przetwarzania " << task_path << ": " << ex.what() << std::endl;
            }

            processed_count.fetch_add(1);
        }
    };

    // Uruchomienie workerów
    std::vector<std::thread> workers;
    for (unsigned int i = 0; i < num_threads; ++i) {
        workers.emplace_back(worker, i);
    }

    // Monitor postępu
    std::thread monitor([&]{
        while (processed_count.load() < total) {
            std::cout << "Postęp: " << processed_count.load() << " / " << total << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        std::cout << "Postęp: " << processed_count.load() << " / " << total << " (zakończono)" << std::endl;
    });

    // Czekamy na workerów
    for (auto& t : workers) if (t.joinable()) t.join();
    if (monitor.joinable()) monitor.join();

    // Po przetworzeniu stwórz mozaiki: z oryginalnych obrazów i z obrazów wyjściowych
    std::vector<std::string> outputFiles = scanFolder(cfg.output);
    // Zachowaj ten sam rozmiar komórki dla obu mozaik
    int cellW = 200;
    int cellH = 200;
    std::string mosaicIn = (fs::path(cfg.output) / "mosaic_input.png").string();
    std::string mosaicOut = (fs::path(cfg.output) / "mosaic_output.png").string();

    createMosaic(files, mosaicIn, cellW, cellH);
    createMosaic(outputFiles, mosaicOut, cellW, cellH);

    std::cout << "Wszystkie zadania wykonane." << std::endl;
    std::cout << "Mozaiki zapisane: " << mosaicIn << " , " << mosaicOut << std::endl;
    return 0;
}
