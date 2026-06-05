// ImageProcessing.cpp : Ten plik zawiera funkcję „main”. W nim rozpoczyna się i kończy wykonywanie programu.
//

#include <iostream>
#include <string>
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

int main()
{
    Config cfg = Parser();
    std::cout << "source = " << cfg.source << std::endl;
    std::cout << "output = " << cfg.output << std::endl;
    std::cout << "threads = " << cfg.threads << std::endl;
    return 0;
}

// Uruchomienie programu: Ctrl + F5 lub menu Debugowanie > Uruchom bez debugowania
// Debugowanie programu: F5 lub menu Debugowanie > Rozpocznij debugowanie

// Porady dotyczące rozpoczynania pracy:
//   1. Użyj okna Eksploratora rozwiązań, aby dodać pliki i zarządzać nimi
//   2. Użyj okna programu Team Explorer, aby nawiązać połączenie z kontrolą źródła
//   3. Użyj okna Dane wyjściowe, aby sprawdzić dane wyjściowe kompilacji i inne komunikaty
//   4. Użyj okna Lista błędów, aby zobaczyć błędy
//   5. Wybierz pozycję Projekt > Dodaj nowy element, aby utworzyć nowe pliki kodu, lub wybierz pozycję Projekt > Dodaj istniejący element, aby dodać istniejące pliku kodu do projektu
//   6. Aby w przyszłości ponownie otworzyć ten projekt, przejdź do pozycji Plik > Otwórz > Projekt i wybierz plik sln
