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
#include <ctime>
#include <iomanip>
#include <sstream>
#include <opencv2/core/utils/logger.hpp>
#include <opencv2/opencv.hpp>
#include "IniReader/INIReader.h"
#include <clocale>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

///
/// Struktura przechowująca konfigurację programu odczytaną z pliku conf.ini
///
struct Config {
    std::string source;
    std::string output;
    int threads;
};

///
///Parser odczytuje plik conf.ini(ten sam katalog co program) i zwraca konfigurację
///
Config Parser()
{
	///Odczyt pliku ini za pomocą INIReader
    INIReader reader("conf.ini");
    Config cfg;
	/// W przypadku braku wpisu w pliku ini, użyj wartości domyślnych
    cfg.source = reader.GetString("", "source", "./input");
    cfg.output = reader.GetString("", "output", "./output");
    cfg.threads = static_cast<int>(reader.GetInteger("", "threads", 0));
	///Zwraca strukturę z konfiguracją
    return cfg;
}

/**
* Skanuje folder i zwraca wektor ścieżek do plików z rozszerzeniem.png
*/
std::vector<std::string> scanFolder(const std::string& folder)
{
    std::vector<std::string> result;
    namespace fs = std::filesystem;
    try {
        if (!fs::exists(folder) || !fs::is_directory(folder))
            return result;
		///szuka plików PNG w katalogu i podkatalogach
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
        /// w razie błędu zwracamy pustą listę
    }
    return result;
}

/**
*Tworzy mozaikę z zestawu obrazów i zapisuje do outFile
*/
void createMosaic(const std::vector<std::string>& paths, const std::string& outFile, int cellW = 200, int cellH = 200)
{
	/// Jeżeli nie ma obrazów do przetworzenia, kończymy funkcję
    if (paths.empty()) return;
    size_t n = paths.size();
	///Zakładamy, że mozaika będzie kwadratowa lub prostokątna, obliczamy liczbę wierszy i kolumn
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
    /// Sprawdź czy plik conf.ini istnieje; jeśli plik nie istnieje program zakończy działanie
    namespace fs = std::filesystem;
    fs::path iniPath = "conf.ini";
    if (!fs::exists(iniPath)) {
        std::cerr << "Brak pliku conf.ini w katalogu programu. Zakonczono." << std::endl;
        return 1;
    }

    /// Odczyt konfiguracji i walidacja danych z pliku conf.ini
    /// Jeśli plik nie może być otwarty lub występuje błąd parsowania program się zakończy
    INIReader reader(iniPath.string());
    if (reader.ParseError() == -1) {
        std::cerr << "Nie mozna otworzyc pliku conf.ini. Zakonczono." << std::endl;
        return 1;
    }
    if (reader.ParseError() != 0) {
        std::cerr << "Blad parsowania conf.ini: " << reader.ParseErrorMessage() << std::endl;
        return 1;
    }

    /// Sprawdź obecność wymaganych kluczy (source, output, threads); brakujące wypisz i zakończ
    std::vector<std::string> missing;
    if (!reader.HasValue("", "source")) missing.push_back("source");
    if (!reader.HasValue("", "output")) missing.push_back("output");
    if (!reader.HasValue("", "threads")) missing.push_back("threads");
    if (!missing.empty()) {
        std::cerr << "Brakuje wpisow w conf.ini: ";
        for (size_t i = 0; i < missing.size(); ++i) {
            if (i) std::cerr << ", ";
            std::cerr << missing[i];
        }
        std::cerr << ". Zakonczono." << std::endl;
        return 1;
    }

    Config cfg;
    cfg.source = reader.GetString("", "source", "");
    cfg.output = reader.GetString("", "output", "");
    cfg.threads = static_cast<int>(reader.GetInteger("", "threads", 0));

    unsigned int hw_threads = std::thread::hardware_concurrency();
    if (hw_threads == 0) hw_threads = 1; //jeśli nieznane to 1

    unsigned int actual_threads = cfg.threads > 0 ? static_cast<unsigned int>(cfg.threads) : hw_threads;

    /// Ustawienie konsoli i locale dla poprawnego wyświetlania polskich znaków
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::setlocale(LC_ALL, "");
    std::ios::sync_with_stdio(false);

    /// Wyłącz logi informacyjne OpenCV, pozostaw tylko błędy
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_ERROR);

    /// Dopasowanie liczby wątków: jeśli w conf.ini jest więcej niż sprzęt obsługuje, ustaw na maksimum i poinformuj
    if (cfg.threads > 0 && hw_threads > 0 && static_cast<unsigned int>(cfg.threads) > hw_threads) {
        std::cout << "Ustawiona liczba wątków (" << cfg.threads << ") przekracza ilość dostępnych zasobów, przetwarzanie na (" << hw_threads << ") wątków" << std::endl;
        actual_threads = hw_threads;
    }

    std::cout << "ścieżka plików wejściowych: " << cfg.source << std::endl;
    std::cout << "ścieżka plików wyjściowych: " << cfg.output << std::endl;
    std::cout << "Przetwarzanie na " << actual_threads << " wątków" << std::endl;

    /// Skanowanie katalogu źródłowego i zebranie listy plików PNG do przetworzenia
    auto files = scanFolder(cfg.source);
    if (files.empty()) {
        std::cerr << "Brak plikow PNG w katalogu wejściowym: " << cfg.source << ". Zakonczono." << std::endl;
        return 1;
    }

    /// Przygotowanie kolejki zadań (ścieżki plików) oraz mutexu do bezpiecznego dostępu z wielu wątków
    std::queue<std::string> tasks;
    for (const auto& f : files) tasks.push(f);
    std::mutex tasks_mutex;

    ///Lista uszkodzonych plików (wielowątkowa)
    std::vector<std::string> corrupted_files;
    std::mutex corrupted_mutex;

    // Atomiczny licznik postępu
    std::atomic<size_t> processed_count{0};
    const size_t total = files.size();

    // Ustalenie liczby wątków
    unsigned int num_threads = actual_threads;

    /// Utworzenie katalogu output jeśli nie istnieje. Jeśli istnieje i nie jest pusty, zaproponuj akcję użytkownikowi
    namespace fs = std::filesystem;
    fs::path outPath(cfg.output);
    try {
        if (!fs::exists(outPath)) {
            fs::create_directories(outPath);
        } else if (!fs::is_directory(outPath)) {
            std::cerr << "Ścieżka output istnieje i nie jest katalogiem: " << cfg.output << ". Zakonczono." << std::endl;
            return 1;
        } else {
            /// Katalog istnieje — sprawdź czy pusty
            bool empty = fs::is_empty(outPath);
            if (!empty) {
                /// Jeśli katalog nie jest pusty, zapytaj użytkownika: wyczyścić, użyć podkatalogu z timestampem, lub przerwać
                std::cout << "Katalog output (" << cfg.output << ") nie jest pusty." << std::endl;
                std::cout << "  (y) - usunac pliki z katalogu\n  (n) - utworzyc podkatalog w output i zapisac tam wyniki\n  (x) - przerwac" << std::endl;
                std::cout << "Wybierz opcje [y/n/x]: ";
                char choice = '\0';
                std::cin >> choice;
                /// konsumuj reszte linii wejścia
                std::string rest; std::getline(std::cin, rest);
                choice = static_cast<char>(std::tolower(static_cast<unsigned char>(choice)));
                if (choice == 'y') {
                    /// Usuń wszystkie pliki i podkatalogi wewnątrz outPath
                    for (auto& entry : fs::directory_iterator(outPath)) {
                        try { fs::remove_all(entry.path()); } catch (...) {}
                    }
                    std::cout << "Katalog output wyczyszczony." << std::endl;
                } else if (choice == 'n') {
                    /// Utwórz podkatalog z nazwą opartą na bieżącym czasie i ustaw go jako docelowy output
                    auto now = std::chrono::system_clock::now();
                    std::time_t t = std::chrono::system_clock::to_time_t(now);
                    std::tm tm{};
#ifdef _WIN32
                    localtime_s(&tm, &t);
#else
                    localtime_r(&t, &tm);
#endif
                    std::ostringstream ss;
                    ss << std::put_time(&tm, "%Y%m%d_%H%M%S");
                    fs::path sub = outPath / ss.str();
                    try { fs::create_directories(sub); cfg.output = sub.string(); }
                    catch (...) { std::cerr << "Nie mozna utworzyc podkatalogu: " << sub.string() << std::endl; return 1; }
                    std::cout << "Utworzono podkatalog: " << cfg.output << std::endl;
                } else {
                    std::cout << "Przerwano." << std::endl;
                    return 1;
                }
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "Blad przy przygotowaniu katalogu output: " << ex.what() << std::endl;
        return 1;
    }

    /// Worker: pobiera ścieżkę z kolejki, przetwarza obraz (detekcja krawędzi) i zapisuje wynik wraz z miniaturami
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
                    {
                        std::lock_guard<std::mutex> lock(corrupted_mutex);
                        corrupted_files.push_back(std::filesystem::path(task_path).filename().string());
                    }
                    processed_count.fetch_add(1);
                    continue;
                }

                /// Tworzenie miniatury oryginalnego obrazu (pure B/W) o maksymalnym wymiarze 200px, zapis do katalogu output
                try {
                    namespace fs = std::filesystem;
                    fs::path inPathThumb(task_path);
                    std::string stem = inPathThumb.stem().string();
                    fs::path thumbIn = fs::path(cfg.output) / (stem + ".thumb_in.png");
                    if (!fs::exists(thumbIn)) {
                        cv::Mat thumb;
                        double scale = std::min(200.0 / img.cols, 200.0 / img.rows);
                        int newW = std::max(1, static_cast<int>(img.cols * scale));
                        int newH = std::max(1, static_cast<int>(img.rows * scale));
                        cv::resize(img, thumb, cv::Size(newW, newH), 0, 0, cv::INTER_AREA);
                        // Konwersja miniatury do odcieni szarości i binarizacja (brak odcieni szarości)
                        try {
                            cv::Mat thumbGray;
                            cv::cvtColor(thumb, thumbGray, cv::COLOR_BGR2GRAY);
                            cv::Mat thumbBin;
                            cv::threshold(thumbGray, thumbBin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
                            try { cv::imwrite(thumbIn.string(), thumbBin); } catch(...) {}
                        } catch(...) {
                            try { cv::imwrite(thumbIn.string(), thumb); } catch(...) {}
                        }
                    }
                } catch(...) {}

                cv::Mat gray, edges;
                cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
                cv::GaussianBlur(gray, gray, cv::Size(5,5), 1.5);
                cv::Canny(gray, edges, 30, 130);

                fs::path inPath(task_path);
                fs::path outPath = fs::path(cfg.output) / inPath.filename();
                /// Zapisz wynik (krawędzie) jako PNG; następnie utwórz miniaturę wyjściową i zapisz ją w formacie czarno-białym
                cv::imwrite(outPath.string(), edges);

                /// Tworzenie miniatury obrazu wynikowego (binarna) - ułatwia późniejsze tworzenie mozaiki bez wczytywania pełnych plików
                try {
                    namespace fs = std::filesystem;
                    std::string stemOut = outPath.stem().string();
                    fs::path thumbOut = fs::path(cfg.output) / (stemOut + ".thumb_out.png");
                    if (!fs::exists(thumbOut)) {
                        cv::Mat thumbEdges;
                        // edges is single-channel; resize and save (create small image)
                        double scaleOut = std::min(200.0 / std::max(1, edges.cols), 200.0 / std::max(1, edges.rows));
                        int newWout = std::max(1, static_cast<int>(edges.cols * scaleOut));
                        int newHout = std::max(1, static_cast<int>(edges.rows * scaleOut));
                        cv::resize(edges, thumbEdges, cv::Size(newWout, newHout), 0, 0, cv::INTER_AREA);
                        // Binarize to remove gray caused by resizing interpolation
                        try {
                            cv::Mat thumbBin;
                            cv::threshold(thumbEdges, thumbBin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
                            try { cv::imwrite(thumbOut.string(), thumbBin); } catch(...) {}
                        } catch(...) {
                            try { cv::imwrite(thumbOut.string(), thumbEdges); } catch(...) {}
                        }
                    }
                } catch(...) {}
            }
            catch (const std::exception& ex) {
                std::cerr << "[worker " << id << "] blad przetwarzania " << task_path << ": " << ex.what() << std::endl;
            }

            processed_count.fetch_add(1);
        }
    };

    /// Uruchomienie wątków workerów według ustalonej liczby num_threads
    std::vector<std::thread> workers;
    for (unsigned int i = 0; i < num_threads; ++i) {
        workers.emplace_back(worker, i);
    }

    /// Monitor postępu: w wątku drukuje liczbę przetworzonych plików co 500ms, aż wszystkie zostaną przetworzone
    std::thread monitor([&]{
        while (processed_count.load() < total) {
            std::cout << "Postęp: " << processed_count.load() << " / " << total << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        std::cout << "Postęp: " << processed_count.load() << " / " << total << " (zakończono)" << std::endl;
    });

    /// Poczekaj na zakończenie wszystkich workerów i monitora
    for (auto& t : workers) if (t.joinable()) t.join();
    if (monitor.joinable()) monitor.join();

    /// Po przetworzeniu: przygotuj listę plików wyjściowych do tworzenia mozaik (pomiń miniatury i pliki mozaik)
    std::vector<std::string> outputFilesAll = scanFolder(cfg.output);
    /// Filtruj pliki output, pomijając miniatury i pliki mozaik
    std::vector<std::string> outputFiles;
    for (const auto& f : outputFilesAll) {
        fs::path p(f);
        std::string name = p.filename().string();
        if (name.find(".thumb_in.png") != std::string::npos) continue;
        if (name.find(".thumb_out.png") != std::string::npos) continue;
        if (name == "mosaic_input.png" || name == "mosaic_output.png") continue;
        outputFiles.push_back(f);
    }
    
    // Zachowaj ten sam rozmiar komórki dla obu mozaik
    int cellW = 200;
    int cellH = 200;
    std::string mosaicIn = (fs::path(cfg.output) / "mosaic_input.png").string();
    std::string mosaicOut = (fs::path(cfg.output) / "mosaic_output.png").string();

    /// Preferuj miniatury przy tworzeniu mozaik, aby nie dekodować pełnych obrazów ponownie
    std::vector<std::string> thumbInputPaths;
    for (const auto& f : files) {
        fs::path p(f);
        fs::path thumb = fs::path(cfg.output) / (p.stem().string() + ".thumb_in.png");
        if (fs::exists(thumb)) thumbInputPaths.push_back(thumb.string());
        else thumbInputPaths.push_back(f);
    }

    std::vector<std::string> thumbOutputPaths;
    for (const auto& f : outputFiles) {
        fs::path p(f);
        fs::path thumb = fs::path(cfg.output) / (p.stem().string() + ".thumb_out.png");
        if (fs::exists(thumb)) thumbOutputPaths.push_back(thumb.string());
        else thumbOutputPaths.push_back(f);
    }

    createMosaic(thumbInputPaths, mosaicIn, cellW, cellH);
    createMosaic(thumbOutputPaths, mosaicOut, cellW, cellH);

    std::cout << "Wszystkie zadania wykonane." << std::endl;
    std::cout << "Mozaiki zapisane: " << mosaicIn << " , " << mosaicOut << std::endl;

    /// Usuń tymczasowe miniatury (.thumb_in.png i .thumb_out.png) utworzone w katalogu output aby pozostawić tylko finalne pliki
    try {
        int removedThumbs = 0;
        for (const auto& entry : fs::directory_iterator(cfg.output)) {
            try {
                if (!entry.is_regular_file()) continue;
                std::string fname = entry.path().filename().string();
                if (fname.find(".thumb_in.png") != std::string::npos || fname.find(".thumb_out.png") != std::string::npos) {
                    try { fs::remove(entry.path()); removedThumbs++; } catch(...) {}
                }
            } catch(...) {}
        }
        if (removedThumbs > 0) std::cout << "Usunieto miniatury (" << removedThumbs << ")" << std::endl;
    } catch(...) {}

    if (!corrupted_files.empty()) {
        std::cout << "Znaleziono " << corrupted_files.size() << " uszkodzonych obrazów: ";
        for (size_t i = 0; i < corrupted_files.size(); ++i) {
            if (i) std::cout << ", ";
            std::cout << corrupted_files[i];
        }
        std::cout << std::endl;
    }

    return 0;
}
