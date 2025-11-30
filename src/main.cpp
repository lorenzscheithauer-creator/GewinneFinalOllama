#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

struct Config {
    std::string linksFilePath = "links.txt";
    std::string promptsFilePath = "prompts.txt";
    std::string progressFilePath = "progress.state";
    std::string resultFilePath = "result.txt";
    std::string tempJsonPath = "current_page.json";
    std::string modelName = "deepseek-r1:7b";
    int pollMinutes = 15;
};

struct PromptResult {
    std::string prompt;
    std::string thinking;
    std::string answer;
};

std::string trim(const std::string &input) {
    const auto start = input.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    const auto end = input.find_last_not_of(" \t\n\r");
    return input.substr(start, end - start + 1);
}

bool fileExists(const std::string &path) {
    return std::filesystem::exists(path);
}

std::vector<std::string> readLines(const std::string &path) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    if (!in) return lines;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

size_t readProgress(const std::string &path) {
    std::ifstream in(path);
    size_t progress = 0;
    if (in) {
        in >> progress;
    }
    return progress;
}

void writeProgress(const std::string &path, size_t progress) {
    std::ofstream out(path, std::ios::trunc);
    out << progress;
}

std::string runCommand(const std::string &command) {
    std::array<char, 4096> buffer{};
    std::string result;
    FILE *pipe = popen(command.c_str(), "r");
    if (!pipe) return result;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    pclose(pipe);
    return result;
}

std::string sanitizeForJson(const std::string &input) {
    std::ostringstream oss;
    for (char c : input) {
        switch (c) {
            case '\\': oss << "\\\\"; break;
            case '"': oss << "\\\""; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default: oss << c; break;
        }
    }
    return oss.str();
}

std::string cleanHtml(const std::string &html) {
    std::string cleaned = html;
    cleaned = std::regex_replace(cleaned, std::regex("<script[\\s\\S]*?</script>", std::regex::icase), "");
    cleaned = std::regex_replace(cleaned, std::regex("<style[\\s\\S]*?</style>", std::regex::icase), "");
    cleaned = std::regex_replace(cleaned, std::regex("<[^>]+>"), " ");
    cleaned = std::regex_replace(cleaned, std::regex(" +"), " ");
    return cleaned;
}

std::string downloadPage(const std::string &url) {
    std::string command = "curl -L --max-time 30 --silent --show-error " + url;
    return runCommand(command);
}

void writeJsonSnapshot(const std::string &path, const std::string &url, const std::string &pageText,
                       const std::vector<PromptResult> &results) {
    std::ofstream out(path, std::ios::trunc);
    out << "{\n";
    out << "  \"url\": \"" << sanitizeForJson(url) << "\",\n";
    out << "  \"page_text\": \"" << sanitizeForJson(pageText) << "\",\n";
    out << "  \"results\": [\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const auto &r = results[i];
        out << "    {\n";
        out << "      \"prompt\": \"" << sanitizeForJson(r.prompt) << "\",\n";
        out << "      \"thinking\": \"" << sanitizeForJson(r.thinking) << "\",\n";
        out << "      \"answer\": \"" << sanitizeForJson(r.answer) << "\"\n";
        out << "    }" << (i + 1 == results.size() ? "\n" : ",\n");
    }
    out << "  ]\n";
    out << "}\n";
}

struct OllamaResponse {
    std::string thinking;
    std::string answer;
    std::string raw;
    bool success{false};
};

OllamaResponse parseOllamaOutput(const std::string &raw) {
    OllamaResponse resp;
    resp.raw = raw;
    auto start = raw.find("<think>");
    auto end = raw.find("</think>");
    if (start != std::string::npos && end != std::string::npos && end > start) {
        resp.thinking = raw.substr(start + 7, end - start - 7);
        resp.answer = trim(raw.substr(end + 8));
    } else {
        resp.answer = trim(raw);
    }
    resp.success = !resp.answer.empty();
    return resp;
}

OllamaResponse callOllama(const std::string &userPrompt, const std::string &contextJson, const Config &config) {
    std::ostringstream oss;
    oss << "Du bist ein Assistent, der Gewinnspiel-Webseiten auswertet. Nutze ausschließlich den bereitgestellten JSON-Kontext.\n";
    oss << "JSON-Kontext:\n" << contextJson << "\n\n";
    oss << "Die Antwort muss nur den geforderten Wert im vorgegebenen Format enthalten, ohne Zusatztexte.\n";
    oss << "Aufgabe:\n" << userPrompt << "\n";

    const std::string prompt = oss.str();
    const std::string promptFile = ".ollama_prompt.tmp";
    {
        std::ofstream tmp(promptFile, std::ios::trunc);
        tmp << prompt;
    }

    std::string command = "cat " + promptFile + " | OLLAMA_NUM_GPU=1 ollama run " + config.modelName;
    std::string output = runCommand(command);
    std::filesystem::remove(promptFile);
    return parseOllamaOutput(output);
}

void appendResultsToFile(const std::string &path, size_t id, const std::vector<PromptResult> &results) {
    std::ofstream out(path, std::ios::app);
    out << "ID " << id << ":\n";
    if (results.empty()) {
        out << "Webseite nicht vorhanden\n\n\n";
        return;
    }
    for (const auto &r : results) {
        out << r.answer << "\n";
    }
    out << "\n\n";
}

std::vector<std::string> defaultPrompts() {
    return {
        "Datum Anfang TXT : Dies ist das Anfangsdatum wann das Gewinnspiel gestartet hat. Format ADatum:<Tag.Monat(Zahl).jahr>",
        "Datum Ende TXT: Hier wird eingetragen wann das Gewinnspiel endet(Einseldeschluss/ teilnahmeschluss) Format EDatum:<Tag.Monat(Zahl).Jahr>",
        "Veranstalter TXT: Der veranstalter des Gewinnspiels Format Veranstalter:<Veranstaltername>",
        "Gewinne TXT: Hier werden alle Gewinne aufgelistet, welche man gwinnen kann. Also alle preise Format <Gewinn1, Gewinn2, ...>",
        "Gewinnsumme TXT: Hier wird dir totale Gewinnsumme ermittelt. Wenn es sachwerte gibt. Bitte recherschiere den Alltäglichen wert dieses genauen gegenstands online Format Gewinnsumme:<000.000.000€>",
        "Gewinnanzahl TXT : Hier wird aufgelistet, was wie oft gewonnen werden kann. Format Gewinnanzahl:<Gewinn1 x 000.000, Gewinn2 x 000.000>",
        "Loesung TXT : Hier sollst du nachgucken, ob es auf der Webseite in Form von kommentaren, etc eine loesung zum Rätsel gibt. Wenn es sowas wie ein Formular gibt, dann schreibe Format Loesung:<Formular ausfüllen> Andernfalls sowas wie Format Loesung:<Loesungswort> oder loesung<Nicht vorhanden>",
        "Keywords TXT : Hier sollen die mainkeywords rausgesucht werden, welche direkt ins auge stechen. Format Keywords:<Keyword1, Keyword2, ...>",
        "Kategorien TXT : Hier soll das Gewinnspiel in kategorien eingeordnet werden. Die vorgegebenen Kategorien aus denen du auswählen sollst sind : Austo, urlaub, Gutschein, Geld, Smartphone, Amazon Gutschein, Reisen. Wenn keine vorhanden ist mache ein strich - Format Kategorie:<Kategorie> oder Kategorie:<->",
        "Status TXT : Hier soll geguckt werden, ob das aktuelle Gewinnspiel noch aktiv ist entscheide zwischen True or false Format Status:<Aktiv> oder Status:<Abgelaufen>"
    };
}

std::vector<std::string> loadPrompts(const std::string &path) {
    auto prompts = readLines(path);
    if (!prompts.empty()) return prompts;
    return defaultPrompts();
}

void ensureResultFileExists(const std::string &path) {
    if (!fileExists(path)) {
        std::ofstream out(path, std::ios::app);
        (void)out;
    }
}

void processLink(size_t id, const std::string &url, const std::vector<std::string> &prompts, const Config &config) {
    std::cout << "\nVerarbeite Link " << id << ": " << url << "\n";
    std::vector<PromptResult> results;
    const std::string rawPage = downloadPage(url);
    if (rawPage.empty()) {
        std::cout << "Webseite nicht vorhanden oder konnte nicht geladen werden.\n";
        appendResultsToFile(config.resultFilePath, id, results);
        return;
    }

    const std::string pageText = cleanHtml(rawPage);
    writeJsonSnapshot(config.tempJsonPath, url, pageText, results);

    for (const auto &prompt : prompts) {
        std::string contextJson;
        {
            std::ifstream in(config.tempJsonPath);
            std::ostringstream oss;
            oss << in.rdbuf();
            contextJson = oss.str();
        }
        const auto response = callOllama(prompt, contextJson, config);
        PromptResult res{prompt, response.thinking, response.answer};
        results.push_back(res);
        writeJsonSnapshot(config.tempJsonPath, url, pageText, results);

        std::cout << "Prompt: " << prompt << "\n";
        std::cout << "OLLAMA denken: " << (response.thinking.empty() ? "(nicht bereitgestellt)" : response.thinking) << "\n";
        std::cout << "Ergebnis: " << response.answer << "\n\n";
    }

    appendResultsToFile(config.resultFilePath, id, results);
    std::filesystem::remove(config.tempJsonPath);
}

Config parseArgs(int argc, char *argv[]) {
    Config config;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::optional<std::string> {
            if (i + 1 < argc) return argv[++i];
            return std::nullopt;
        };

        if (arg == "--links") {
            if (auto value = next()) config.linksFilePath = *value;
        } else if (arg == "--prompts") {
            if (auto value = next()) config.promptsFilePath = *value;
        } else if (arg == "--progress") {
            if (auto value = next()) config.progressFilePath = *value;
        } else if (arg == "--result") {
            if (auto value = next()) config.resultFilePath = *value;
        } else if (arg == "--temp-json") {
            if (auto value = next()) config.tempJsonPath = *value;
        } else if (arg == "--model") {
            if (auto value = next()) config.modelName = *value;
        } else if (arg == "--poll-minutes") {
            if (auto value = next()) config.pollMinutes = std::stoi(*value);
        }
    }
    return config;
}

int main(int argc, char *argv[]) {
    Config config = parseArgs(argc, argv);
    ensureResultFileExists(config.resultFilePath);

    while (true) {
        const auto links = readLines(config.linksFilePath);
        const auto prompts = loadPrompts(config.promptsFilePath);
        size_t progress = readProgress(config.progressFilePath);

        if (progress >= links.size()) {
            std::cout << "Keine neuen Links. Warte " << config.pollMinutes << " Minuten..." << std::endl;
            std::this_thread::sleep_for(std::chrono::minutes(config.pollMinutes));
            continue;
        }

        for (size_t i = progress; i < links.size(); ++i) {
            processLink(i + 1, links[i], prompts, config);
            writeProgress(config.progressFilePath, i + 1);
        }
    }

    return 0;
}
