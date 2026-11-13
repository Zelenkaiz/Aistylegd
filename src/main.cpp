#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/Popup.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#undef min
#undef max
#endif

using namespace geode::prelude;

// ============================================================================
// Слоты цветовой палитры и роли слоев редактора
// ============================================================================
enum class ColorSlot : int {
    DarkFill = 0,         // Слой +0: основной фон внутри контура
    Dark3D = 1,           // Слой +2: глубокий черный/темный тон для 3D-блоков
    MidTone = 2,          // Слой +1: средний тон (кладки, камни, ветви, рамки, узоры)
    BrightMain = 3,       // Слой +1/+3: главный акцент (снег, кристаллы, листва, глаз, череп)
    HighlightOutline = 4, // Слой +2: светлая обводка контура и блики
    AdditiveGlow = 5,     // Слой +3: неоновое свечение
    CustomRGB = 6         // Произвольный RGB-цвет, сгенерированный напрямую ИИ
};

enum class LayerRole : int {
    Fill = 0,        // firstFreeLayer + 0: Базовое заполнение
    InnerDetail = 1, // firstFreeLayer + 1: Внутренний арт и детали ИИ
    Outline3D = 2,   // firstFreeLayer + 2: Контур и 3D-блоки
    NeonAndGrass = 3 // firstFreeLayer + 3: Неон, снег, трава и внешние детали
};

enum class AITaskMode {
    ChatOnly = 0,   // ИИ просто общается с игроком (НЕ строит!)
    Only3D = 1,     // ИИ строит только 3D-объем и контур
    OnlyNeon = 2,   // ИИ строит только неон/свечение
    OnlyFill = 3,   // ИИ только заливает форму
    FullDecor = 4,  // ИИ генерирует полноценный арт/декор по запросу
    UndoLast = 5    // ИИ убирает последнюю постройку
};

struct StylePalette {
    ccColor3B masterColor{ 115, 255, 28 };
    ccColor3B slotColors[6];
    GLubyte slotOpacity[6];
    bool slotBlending[6];
};

struct SelectedSpikeInfo {
    float x = 0.f;
    float y = 0.f;
    float rotation = 0.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
    bool flipX = false;
    bool flipY = false;
    int objectId = 8;
    GameObject* sourceObj = nullptr;
};

struct LockedObjectState {
    Ref<GameObject> obj;
    CCPoint pos{ 0.f, 0.f };
    float rotation = 0.f;
};

struct ContourAnalysis {
    std::set<std::pair<int, int>> occupiedCells;
    std::vector<SelectedSpikeInfo> spikes;
    std::vector<GameObject*> solidObjects;
    std::vector<LockedObjectState> lockedStates;
    float minX = 0.f;
    float minY = 0.f;
    float maxX = 0.f;
    float maxY = 0.f;
    int minGX = 0;
    int maxGX = 0;
    int minGY = 0;
    int maxGY = 0;

    int cellCount() const { return static_cast<int>(occupiedCells.size()); }
    int spikeCount() const { return static_cast<int>(spikes.size()); }
    int widthUnits() const { return (std::max)(1, maxGX - minGX + 1) * 30; }
    int heightUnits() const { return (std::max)(1, maxGY - minGY + 1) * 30; }
};

struct GeneratedObjectSpec {
    int objectId = 211;
    float x = 0.f;
    float y = 0.f;
    float rotation = 0.f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    bool flipX = false;
    bool flipY = false;
    int zOrder = 2;
    LayerRole layerRole = LayerRole::Fill;
    ColorSlot colorSlot = ColorSlot::DarkFill;
    ccColor3B customColor{ 255, 255, 255 };
    GLubyte customOpacity = 255;
    bool customBlending = false;
    bool noTouch = true;
    bool noGlow = true;
    int priorityTier = 1;
    bool isOverhang = false;
};

// Элемент кастомного векторного/блочного рисунка, сгенерированный самой ИИ-нейросетью
struct AICustomDrawElement {
    float rx = 0.5f;     // Относительная позиция X внутри формы (0.0 .. 1.0)
    float ry = 0.5f;     // Относительная позиция Y внутри формы (0.0 .. 1.0)
    float scaleX = 0.5f; // Масштаб X
    float scaleY = 0.5f; // Масштаб Y
    float rot = 0.f;     // Поворот в градусах
    int shapeType = 0;   // 0=block(211), 1=circle, 2=triangle, 3=glow(1888), 4=grass(406), 5=brick, 6=eye, 7=skull, 8=arc, 9=line, 10=chain
    ccColor3B color{ 255, 255, 255 };
    GLubyte opacity = 255;
    bool blending = false;
    int zOrder = 6;
};

struct AIDecisionResult {
    bool httpSuccess = false;
    AITaskMode taskMode = AITaskMode::ChatOnly;
    std::string replyText;
    std::string planText;
    bool hasCustomPalette = false;
    ccColor3B fillRGB{ 32, 36, 44 };
    ccColor3B midRGB{ 90, 100, 118 };
    ccColor3B mainRGB{ 180, 215, 255 };
    ccColor3B outlineRGB{ 235, 245, 255 };
    ccColor3B glowRGB{ 120, 195, 255 };
    bool want3D = true;
    bool wantNeon = true;
    bool wantGrass = false;
    bool wantSnowCap = false;
    bool wantBrickPattern = false;
    bool wantCrystalPattern = false;
    bool wantTreePattern = false;
    int emblemType = 0; // 0=auto, 1=eye, 2=skull, 3=circle, 4=arch, 5=none
    int patternStyle = 0;
    int suggestedCount = 120;
    std::vector<AICustomDrawElement> customElements;
};

static int g_lastLayoutSeed = -1;
static int g_variantIndex = 1;
static int g_objectLimit = 16000;
static bool g_overhangAllowed = true;
static std::string g_lastBuildPrompt;
static std::vector<Ref<GameObject>> g_lastPlacedObjects;
static std::vector<GeneratedObjectSpec> g_lastPreviewSpecs;
static StylePalette g_lastPreviewPalette;
static bool g_hasGeneratedArt = false;

// История диалога для контекста нейросети
struct ChatTurnRecord {
    std::string role;
    std::string content;
};
static std::vector<ChatTurnRecord> g_chatHistory;

// ============================================================================
// Конвертация UTF-8 кириллицы в читаемый текст для шрифтов GD (chatFont.fnt)
// ============================================================================
static std::string trimWhitespace(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    size_t start = 0;
    while (start < s.size() && (s[start] == '\n' || s[start] == '\r' || s[start] == ' ' || s[start] == '\t')) {
        ++start;
    }
    return s.substr(start);
}

static std::string utf8ToGDReadable(std::string const& input) {
    std::string out;
    out.reserve(input.size() * 2);

    size_t i = 0;
    while (i < input.size()) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        if (c < 0x80) {
            if (c >= 32 || c == '\n') out.push_back(static_cast<char>(c));
            ++i;
            continue;
        }
        // Кириллица в UTF-8 (D0 80..BF, D1 80..BF)
        if ((c == 0xD0 || c == 0xD1) && (i + 1 < input.size())) {
            unsigned char c2 = static_cast<unsigned char>(input[i + 1]);
            int code = ((c & 0x1F) << 6) | (c2 & 0x3F);
            i += 2;

            switch (code) {
                case 0x0401: out += "Yo"; break; case 0x0451: out += "yo"; break;
                case 0x0410: out += "A";  break; case 0x0430: out += "a";  break;
                case 0x0411: out += "B";  break; case 0x0431: out += "b";  break;
                case 0x0412: out += "V";  break; case 0x0432: out += "v";  break;
                case 0x0413: out += "G";  break; case 0x0433: out += "g";  break;
                case 0x0414: out += "D";  break; case 0x0434: out += "d";  break;
                case 0x0415: out += "E";  break; case 0x0435: out += "e";  break;
                case 0x0416: out += "Zh"; break; case 0x0436: out += "zh"; break;
                case 0x0417: out += "Z";  break; case 0x0437: out += "z";  break;
                case 0x0418: out += "I";  break; case 0x0438: out += "i";  break;
                case 0x0419: out += "Y";  break; case 0x0439: out += "y";  break;
                case 0x041A: out += "K";  break; case 0x043A: out += "k";  break;
                case 0x041B: out += "L";  break; case 0x043B: out += "l";  break;
                case 0x041C: out += "M";  break; case 0x043C: out += "m";  break;
                case 0x041D: out += "N";  break; case 0x043D: out += "n";  break;
                case 0x041E: out += "O";  break; case 0x043E: out += "o";  break;
                case 0x041F: out += "P";  break; case 0x043F: out += "p";  break;
                case 0x0420: out += "R";  break; case 0x0440: out += "r";  break;
                case 0x0421: out += "S";  break; case 0x0441: out += "s";  break;
                case 0x0422: out += "T";  break; case 0x0442: out += "t";  break;
                case 0x0423: out += "U";  break; case 0x0443: out += "u";  break;
                case 0x0424: out += "F";  break; case 0x0444: out += "f";  break;
                case 0x0425: out += "H";  break; case 0x0445: out += "h";  break;
                case 0x0426: out += "Ts"; break; case 0x0446: out += "ts"; break;
                case 0x0427: out += "Ch"; break; case 0x0447: out += "ch"; break;
                case 0x0428: out += "Sh"; break; case 0x0448: out += "sh"; break;
                case 0x0429: out += "Sch";break; case 0x0449: out += "sch";break;
                case 0x042A: break;              case 0x044A: break;
                case 0x042B: out += "Y";  break; case 0x044B: out += "y";  break;
                case 0x042C: break;              case 0x044C: break;
                case 0x042D: out += "E";  break; case 0x044D: out += "e";  break;
                case 0x042E: out += "Yu"; break; case 0x044E: out += "yu"; break;
                case 0x042F: out += "Ya"; break; case 0x044F: out += "ya"; break;
                default: break;
            }
            continue;
        }
        // Пропускаем прочие многобайтовые последовательности
        if ((c & 0xE0) == 0xC0) i += 2;
        else if ((c & 0xF0) == 0xE0) i += 3;
        else if ((c & 0xF8) == 0xF0) i += 4;
        else ++i;
    }
    return out;
}

static std::vector<std::string> wrapTextLines(std::string const& rawText, size_t maxCharsPerLine, size_t maxLines = 4) {
    std::string clean = utf8ToGDReadable(rawText);
    for (char& c : clean) {
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    }

    std::vector<std::string> lines;
    std::istringstream iss(clean);
    std::string word;
    std::string current;

    while (iss >> word) {
        if (current.empty()) {
            current = word;
        } else if (current.size() + 1 + word.size() <= maxCharsPerLine) {
            current += " " + word;
        } else {
            lines.push_back(current);
            current = word;
            if (lines.size() >= maxLines) break;
        }
    }
    if (!current.empty() && lines.size() < maxLines) {
        lines.push_back(current);
    }
    while (lines.size() < maxLines) {
        lines.push_back("");
    }
    return lines;
}

// ============================================================================
// Провайдеры ИИ и РЕАЛЬНЫЕ HTTPS-запросы через системный winhttp.dll Windows
// ============================================================================
struct ApiProviderPreset {
    const char* name;
    const wchar_t* hostW;
    const wchar_t* pathW;
    const char* defaultModel;
};

static constexpr ApiProviderPreset kApiProviders[] = {
    { "OpenAI",     L"api.openai.com",                        L"/v1/chat/completions",              "gpt-4o-mini" },
    { "Gemini",     L"generativelanguage.googleapis.com",     L"/v1beta/openai/chat/completions",   "gemini-2.5-flash" },
    { "DeepSeek",   L"api.deepseek.com",                      L"/chat/completions",                 "deepseek-chat" },
    { "Groq",       L"api.groq.com",                          L"/openai/v1/chat/completions",       "llama-3.3-70b-versatile" },
    { "OpenRouter", L"openrouter.ai",                         L"/api/v1/chat/completions",          "openai/gpt-4o-mini" }
};

static bool g_apiConfigLoaded = false;
static bool g_apiVerifiedAndConnected = false;
static int g_apiProviderIndex = 0;
static std::string g_connectedApiKey;

static bool hasPlausibleKeyFormat(std::string const& rawKey) {
    std::string k = trimWhitespace(rawKey);
    if (k.size() < 16) return false;
    for (char c : k) {
        if (c <= 32 || c > 126) return false;
    }
    return true;
}

static int detectProviderFromKey(std::string const& rawKey, int currentIdx) {
    std::string key = trimWhitespace(rawKey);
    if (key.rfind("AIza", 0) == 0) return 1;   // Gemini
    if (key.rfind("gsk_", 0) == 0) return 3;   // Groq
    if (key.rfind("sk-or-", 0) == 0) return 4; // OpenRouter
    if (key.rfind("sk-", 0) == 0) return (currentIdx == 2) ? 2 : 0;
    return currentIdx;
}

static std::string maskApiKeyPreview(std::string const& key) {
    if (key.size() <= 10) return "not set";
    return key.substr(0, 5) + "..." + key.substr(key.size() - 4);
}

static std::string jsonEscape(std::string const& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c >= 32) out.push_back(static_cast<char>(c));
                break;
        }
    }
    return out;
}

// Выполнение реального HTTPS POST запроса через WinHTTP (динамическая загрузка winhttp.dll)
static bool performWinHttpPost(
    int providerIdx,
    std::string const& apiKey,
    std::string const& jsonPayload,
    int& outHttpStatus,
    std::string& outResponseBody,
    std::string& outErrorText
) {
    outHttpStatus = 0;
    outResponseBody.clear();
    outErrorText.clear();

    if (providerIdx < 0 || providerIdx >= 5) providerIdx = 0;

#ifdef GEODE_IS_WINDOWS
    using HINTERNET_T = void*;
    using INTERNET_PORT_T = uint16_t;

    using FnWinHttpOpen = HINTERNET_T(WINAPI*)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
    using FnWinHttpSetTimeouts = BOOL(WINAPI*)(HINTERNET_T, int, int, int, int);
    using FnWinHttpConnect = HINTERNET_T(WINAPI*)(HINTERNET_T, LPCWSTR, INTERNET_PORT_T, DWORD);
    using FnWinHttpOpenRequest = HINTERNET_T(WINAPI*)(HINTERNET_T, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD);
    using FnWinHttpSendRequest = BOOL(WINAPI*)(HINTERNET_T, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
    using FnWinHttpReceiveResponse = BOOL(WINAPI*)(HINTERNET_T, LPVOID);
    using FnWinHttpQueryHeaders = BOOL(WINAPI*)(HINTERNET_T, DWORD, LPCWSTR, LPVOID, LPDWORD, LPDWORD);
    using FnWinHttpQueryDataAvailable = BOOL(WINAPI*)(HINTERNET_T, LPDWORD);
    using FnWinHttpReadData = BOOL(WINAPI*)(HINTERNET_T, LPVOID, DWORD, LPDWORD);
    using FnWinHttpCloseHandle = BOOL(WINAPI*)(HINTERNET_T);

    HMODULE hWinHttp = LoadLibraryA("winhttp.dll");
    if (!hWinHttp) {
        outErrorText = "WinHTTP library not available";
        return false;
    }

    auto pOpen = reinterpret_cast<FnWinHttpOpen>(GetProcAddress(hWinHttp, "WinHttpOpen"));
    auto pSetTimeouts = reinterpret_cast<FnWinHttpSetTimeouts>(GetProcAddress(hWinHttp, "WinHttpSetTimeouts"));
    auto pConnect = reinterpret_cast<FnWinHttpConnect>(GetProcAddress(hWinHttp, "WinHttpConnect"));
    auto pOpenReq = reinterpret_cast<FnWinHttpOpenRequest>(GetProcAddress(hWinHttp, "WinHttpOpenRequest"));
    auto pSendReq = reinterpret_cast<FnWinHttpSendRequest>(GetProcAddress(hWinHttp, "WinHttpSendRequest"));
    auto pRecvResp = reinterpret_cast<FnWinHttpReceiveResponse>(GetProcAddress(hWinHttp, "WinHttpReceiveResponse"));
    auto pQueryHdr = reinterpret_cast<FnWinHttpQueryHeaders>(GetProcAddress(hWinHttp, "WinHttpQueryHeaders"));
    auto pQueryData = reinterpret_cast<FnWinHttpQueryDataAvailable>(GetProcAddress(hWinHttp, "WinHttpQueryDataAvailable"));
    auto pReadData = reinterpret_cast<FnWinHttpReadData>(GetProcAddress(hWinHttp, "WinHttpReadData"));
    auto pClose = reinterpret_cast<FnWinHttpCloseHandle>(GetProcAddress(hWinHttp, "WinHttpCloseHandle"));

    if (!pOpen || !pConnect || !pOpenReq || !pSendReq || !pRecvResp || !pQueryHdr || !pQueryData || !pReadData || !pClose) {
        FreeLibrary(hWinHttp);
        outErrorText = "WinHTTP functions missing";
        return false;
    }

    HINTERNET_T hSession = pOpen(L"GDArtAgent/1.0", 0, nullptr, nullptr, 0);
    if (!hSession) {
        FreeLibrary(hWinHttp);
        outErrorText = "Failed to open HTTP session";
        return false;
    }

    if (pSetTimeouts) {
        pSetTimeouts(hSession, 6000, 6000, 10000, 20000);
    }

    HINTERNET_T hConnect = pConnect(hSession, kApiProviders[providerIdx].hostW, 443, 0);
    if (!hConnect) {
        pClose(hSession);
        FreeLibrary(hWinHttp);
        outErrorText = "Cannot connect to AI server";
        return false;
    }

    constexpr DWORD kWinHttpFlagSecure = 0x00800000;
    HINTERNET_T hRequest = pOpenReq(
        hConnect, L"POST", kApiProviders[providerIdx].pathW,
        nullptr, nullptr, nullptr, kWinHttpFlagSecure
    );
    if (!hRequest) {
        pClose(hConnect);
        pClose(hSession);
        FreeLibrary(hWinHttp);
        outErrorText = "Failed to create HTTPS request";
        return false;
    }

    std::wstring headersW = L"Content-Type: application/json\r\nAuthorization: Bearer ";
    for (unsigned char c : apiKey) {
        headersW.push_back(static_cast<wchar_t>(c));
    }
    headersW += L"\r\n";

    BOOL sent = pSendReq(
        hRequest,
        headersW.c_str(),
        static_cast<DWORD>(headersW.size()),
         const_cast<char*>(jsonPayload.data()),
        static_cast<DWORD>(jsonPayload.size()),
        static_cast<DWORD>(jsonPayload.size()),
        0
    );

    if (!sent || !pRecvResp(hRequest, nullptr)) {
        pClose(hRequest);
        pClose(hConnect);
        pClose(hSession);
        FreeLibrary(hWinHttp);
        outErrorText = "Network error contacting AI API";
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    constexpr DWORD kQueryStatusCodeNumber = 19 | 0x20000000;
    pQueryHdr(hRequest, kQueryStatusCodeNumber, nullptr, &statusCode, &statusSize, nullptr);
    outHttpStatus = static_cast<int>(statusCode);

    DWORD avail = 0;
    while (pQueryData(hRequest, &avail) && avail > 0) {
        std::vector<char> buf(avail + 1, 0);
        DWORD readBytes = 0;
        if (pReadData(hRequest, buf.data(), avail, &readBytes) && readBytes > 0) {
            outResponseBody.append(buf.data(), readBytes);
        } else {
            break;
        }
    }

    pClose(hRequest);
    pClose(hConnect);
    pClose(hSession);
    FreeLibrary(hWinHttp);
    return (outHttpStatus == 200);
#else
    (void)apiKey;
    (void)jsonPayload;
    outErrorText = "Windows required for WinHTTP";
    return false;
#endif
}

// Реальная проверка достоверности API-ключа на сервере провайдера!
static bool verifyApiKeyWithServer(int providerIdx, std::string const& rawKey, std::string& outErrorMsg) {
    std::string key = trimWhitespace(rawKey);
    if (!hasPlausibleKeyFormat(key)) {
        outErrorMsg = "Key format invalid (too short or has spaces)";
        return false;
    }

    std::string payload = fmt::format(
        "{{\"model\":\"{}\",\"messages\":[{{\"role\":\"user\",\"content\":\"ping\"}}],\"max_tokens\":5}}",
        kApiProviders[providerIdx].defaultModel
    );

    int httpStatus = 0;
    std::string respBody;
    std::string netErr;
    bool ok = performWinHttpPost(providerIdx, key, payload, httpStatus, respBody, netErr);

    if (ok && httpStatus == 200 && respBody.find("choices") != std::string::npos) {
        return true;
    }
    if (httpStatus == 401 || httpStatus == 403) {
        outErrorMsg = fmt::format("Invalid API Key (HTTP {}) — key rejected by {}", httpStatus, kApiProviders[providerIdx].name);
        return false;
    }
    if (httpStatus > 0) {
        outErrorMsg = fmt::format("API Error HTTP {} from {}", httpStatus, kApiProviders[providerIdx].name);
        return false;
    }
    outErrorMsg = netErr.empty() ? "Connection failed" : netErr;
    return false;
}

static void saveVerifiedApiToDisk(int provIdx, std::string const& rawKey) {
    std::string key = trimWhitespace(rawKey);
    g_apiConfigLoaded = true;
    g_apiProviderIndex = std::clamp(provIdx, 0, 4);
    g_connectedApiKey = key;
    g_apiVerifiedAndConnected = true;
    std::ofstream out("ai_decor_api.cfg", std::ios::trunc);
    if (out.is_open()) {
        out << g_apiProviderIndex << "\n" << g_connectedApiKey << "\n";
    }
}

static void loadAndVerifySavedApiFromDisk() {
    if (g_apiConfigLoaded) return;
    g_apiConfigLoaded = true;

    std::ifstream in("ai_decor_api.cfg");
    if (!in.is_open()) return;
    int prov = 0;
    std::string key;
    if (in >> prov) {
        std::getline(in >> std::ws, key);
        key = trimWhitespace(key);
        if (prov >= 0 && prov < 5 && hasPlausibleKeyFormat(key)) {
            std::string dummyErr;
            if (verifyApiKeyWithServer(prov, key, dummyErr)) {
                g_apiProviderIndex = prov;
                g_connectedApiKey = key;
                g_apiVerifiedAndConnected = true;
            }
        }
    }
}

// ============================================================================
// Парсер JSON-ответа от реальной ИИ-нейросети
// ============================================================================
static std::string extractJsonStringField(std::string const& json, std::string const& key) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return "";
    pos = json.find('\"', pos + 1);
    if (pos == std::string::npos) return "";
    ++pos;

    std::string out;
    while (pos < json.size()) {
        char c = json[pos++];
        if (c == '\\' && pos < json.size()) {
            char esc = json[pos++];
            if (esc == 'n') out.push_back(' ');
            else if (esc == '\"') out.push_back('\"');
            else if (esc == '\\') out.push_back('\\');
            else out.push_back(esc);
        } else if (c == '\"') {
            break;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

static float extractJsonFloatField(std::string const& json, std::string const& key, float defaultVal) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return defaultVal;
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return defaultVal;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) ++pos;
    char* endPtr = nullptr;
    float val = std::strtof(json.c_str() + pos, &endPtr);
    if (endPtr == json.c_str() + pos) return defaultVal;
    return val;
}

static int extractJsonIntField(std::string const& json, std::string const& key, int defaultVal) {
    return static_cast<int>( std::lround(extractJsonFloatField(json, key, static_cast<float>(defaultVal))) );
}

static bool extractJsonBoolField(std::string const& json, std::string const& key, bool defaultVal) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return defaultVal;
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return defaultVal;
    size_t subEnd = (std::min)(json.size(), pos + 16);
    std::string slice = json.substr(pos + 1, subEnd - (pos + 1));
    if (slice.find("true") != std::string::npos) return true;
    if (slice.find("false") != std::string::npos) return false;
    return defaultVal;
}

static bool extractJsonRGBArray(std::string const& json, std::string const& key, ccColor3B& outColor) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return false;
    pos = json.find('[', pos + pattern.size());
    if (pos == std::string::npos) return false;
    size_t closePos = json.find(']', pos);
    if (closePos == std::string::npos) return false;

    std::string arr = json.substr(pos + 1, closePos - pos - 1);
    for (char& c : arr) {
        if (c == ',') c = ' ';
    }
    std::istringstream iss(arr);
    int r = 255, g = 255, b = 255;
    if (iss >> r >> g >> b) {
        outColor = ccColor3B{
            static_cast<GLubyte>(std::clamp(r, 0, 255)),
            static_cast<GLubyte>(std::clamp(g, 0, 255)),
            static_cast<GLubyte>(std::clamp(b, 0, 255))
        };
        return true;
    }
    return false;
}

static void parseCustomElementsFromAIJson(std::string const& innerJson, std::vector<AICustomDrawElement>& outList) {
    size_t pos = innerJson.find("\"elements\"");
    if (pos == std::string::npos) return;
    pos = innerJson.find('[', pos);
    if (pos == std::string::npos) return;
    size_t endArr = innerJson.find(']', pos);
    if (endArr == std::string::npos) endArr = innerJson.size();

    size_t cur = pos;
    while (cur < endArr) {
        size_t objStart = innerJson.find('{', cur);
        if (objStart == std::string::npos || objStart >= endArr) break;
        size_t objEnd = innerJson.find('}', objStart);
        if (objEnd == std::string::npos) break;

        std::string item = innerJson.substr(objStart, objEnd - objStart + 1);
        AICustomDrawElement el;
        el.rx        = std::clamp(extractJsonFloatField(item, "rx", 0.5f), 0.0f, 1.0f);
        el.ry        = std::clamp(extractJsonFloatField(item, "ry", 0.5f), 0.0f, 1.0f);
        el.scaleX    = std::clamp(extractJsonFloatField(item, "sx", 0.5f), 0.08f, 2.5f);
        el.scaleY    = std::clamp(extractJsonFloatField(item, "sy", 0.5f), 0.08f, 2.5f);
        el.rot       = extractJsonFloatField(item, "rot", 0.0f);
        el.shapeType = std::clamp(extractJsonIntField(item, "type", 0), 0, 10);
        el.zOrder    = std::clamp(extractJsonIntField(item, "z", 6), -2, 20);
        el.opacity   = static_cast<GLubyte>(std::clamp(extractJsonIntField(item, "op", 255), 25, 255));
        el.blending  = extractJsonBoolField(item, "blend", false);

        int r = std::clamp(extractJsonIntField(item, "r", 220), 0, 255);
        int g = std::clamp(extractJsonIntField(item, "g", 230), 0, 255);
        int b = std::clamp(extractJsonIntField(item, "b", 245), 0, 255);
        el.color = ccColor3B{ static_cast<GLubyte>(r), static_cast<GLubyte>(g), static_cast<GLubyte>(b) };

        outList.push_back(el);
        cur = objEnd + 1;
    }
}

// Запрос к реальной подключенной нейросети (единственное системное правило: сразу общаться по GD и строить то, что просит игрок)
static AIDecisionResult queryRealNeuralNetwork(
    std::string const& userMessage,
    ContourAnalysis const& contour,
    int objectLimit,
    bool overhangAllowed,
    int variantNum
) {
    AIDecisionResult res;

    std::string systemRule = fmt::format(
        "You are Art Agent, an AI inside Geometry Dash 2.2 Level Editor. "
        "System Rule: Always communicate immediately about Geometry Dash level building and block design in the user's language (if the user writes in Russian or Russian translit, reply in Russian translit using Latin characters so the GD bitmap font displays every letter). "
        "Never use canned responses — answer the user's questions or greetings naturally as you wish. "
        "Selected contour: {} blocks ({}x{} units). Object limit: {}. Mode: {}. Variant #{}. "
        "Respond ONLY with a single valid JSON object (no markdown fences) with these fields: "
        "{{"
        "\"action\": \"chat\" | \"build\" | \"only_3d\" | \"only_neon\" | \"only_fill\" | \"undo\", "
        "\"reply\": \"your natural response to the player\", "
        "\"plan\": \"short description of the visual design plan\", "
        "\"fill_rgb\": [r,g,b], \"mid_rgb\": [r,g,b], \"main_rgb\": [r,g,b], \"outline_rgb\": [r,g,b], \"glow_rgb\": [r,g,b], "
        "\"want_3d\": true/false, \"want_neon\": true/false, \"want_grass\": true/false, "
        "\"want_snow\": true/false, \"want_brick\": true/false, \"want_crystal\": true/false, \"want_tree\": true/false, "
        "\"emblem\": 0..5, \"pattern\": 0..5, \"count\": 30..350, "
        "\"elements\": [{{\"rx\":0.0..1.0,\"ry\":0.0..1.0,\"sx\":0.1..1.5,\"sy\":0.1..1.5,\"rot\":0,\"type\":0..10,\"r\":0..255,\"g\":0..255,\"b\":0..255,\"op\":255,\"blend\":false,\"z\":6}}]"
        "}}. "
        "IMPORTANT: If the user is just greeting (e.g. 'privet', 'hi'), chatting, or asking a question without asking to build/draw yet, set \"action\": \"chat\" and answer naturally in \"reply\". "
        "If the user asks ONLY for 3D, set \"action\": \"only_3d\". "
        "If the user describes any art, material, object, or style to draw/build on the blocks, set \"action\": \"build\", choose accurate RGB colors for that exact material/theme, and generate up to 18 custom \"elements\" (types: 0=block, 1=circle, 2=triangle, 3=glow, 4=grass, 5=brick, 6=eye, 7=skull, 8=arc, 9=line, 10=chain) to draw the requested subject inside the contour.",
        contour.cellCount(),
        contour.widthUnits(),
        contour.heightUnits(),
        objectLimit,
        overhangAllowed ? "Overhang" : "Inside",
        variantNum
    );

    std::string messagesJson = fmt::format(
        "{{\"role\":\"system\",\"content\":\"{}\"}}",
        jsonEscape(systemRule)
    );

    size_t startHist = (g_chatHistory.size() > 6) ? (g_chatHistory.size() - 6) : 0;
    for (size_t i = startHist; i < g_chatHistory.size(); ++i) {
        messagesJson += fmt::format(
            ",{{\"role\":\"{}\",\"content\":\"{}\"}}",
            g_chatHistory[i].role,
            jsonEscape(g_chatHistory[i].content)
        );
    }
    messagesJson += fmt::format(
        ",{{\"role\":\"user\",\"content\":\"{}\"}}",
        jsonEscape(userMessage)
    );

    std::string payload = fmt::format(
        "{{\"model\":\"{}\",\"messages\":[{}],\"temperature\":0.85,\"max_tokens\":900}}",
        kApiProviders[g_apiProviderIndex].defaultModel,
        messagesJson
    );

    int httpStatus = 0;
    std::string rawResp;
    std::string errText;
    if (!performWinHttpPost(g_apiProviderIndex, g_connectedApiKey, payload, httpStatus, rawResp, errText)) {
        res.httpSuccess = false;
        res.replyText = fmt::format("API Error (HTTP {}): {}", httpStatus, errText);
        return res;
    }

    res.httpSuccess = true;
    std::string assistantContent = extractJsonStringField(rawResp, "content");
    if (assistantContent.empty()) {
        res.replyText = "AI response received.";
        return res;
    }

    // Если модель вернула JSON внутри content — разбираем поля решения ИИ
    size_t jsonStart = assistantContent.find('{');
    size_t jsonEnd   = assistantContent.rfind('}');
    if (jsonStart != std::string::npos && jsonEnd != std::string::npos && jsonEnd > jsonStart) {
        std::string innerJson = assistantContent.substr(jsonStart, jsonEnd - jsonStart + 1);

        std::string actionStr = extractJsonStringField(innerJson, "action");
        res.replyText = extractJsonStringField(innerJson, "reply");
        res.planText  = extractJsonStringField(innerJson, "plan");

        if (actionStr == "only_3d")        res.taskMode = AITaskMode::Only3D;
        else if (actionStr == "only_neon") res.taskMode = AITaskMode::OnlyNeon;
        else if (actionStr == "only_fill") res.taskMode = AITaskMode::OnlyFill;
        else if (actionStr == "undo")      res.taskMode = AITaskMode::UndoLast;
        else if (actionStr == "build")     res.taskMode = AITaskMode::FullDecor;
        else                               res.taskMode = AITaskMode::ChatOnly;

        bool c1 = extractJsonRGBArray(innerJson, "fill_rgb",    res.fillRGB);
        bool c2 = extractJsonRGBArray(innerJson, "mid_rgb",     res.midRGB);
        bool c3 = extractJsonRGBArray(innerJson, "main_rgb",    res.mainRGB);
        bool c4 = extractJsonRGBArray(innerJson, "outline_rgb", res.outlineRGB);
        bool c5 = extractJsonRGBArray(innerJson, "glow_rgb",    res.glowRGB);
        res.hasCustomPalette = (c1 || c2 || c3 || c4 || c5);

        res.want3D          = extractJsonBoolField(innerJson, "want_3d", true);
        res.wantNeon        = extractJsonBoolField(innerJson, "want_neon", true);
        res.wantGrass       = extractJsonBoolField(innerJson, "want_grass", false);
        res.wantSnowCap     = extractJsonBoolField(innerJson, "want_snow", false);
        res.wantBrickPattern= extractJsonBoolField(innerJson, "want_brick", false);
        res.wantCrystalPattern = extractJsonBoolField(innerJson, "want_crystal", false);
        res.wantTreePattern = extractJsonBoolField(innerJson, "want_tree", false);
        res.emblemType      = std::clamp(extractJsonIntField(innerJson, "emblem", 0), 0, 5);
        res.patternStyle    = std::clamp(extractJsonIntField(innerJson, "pattern", 0), 0, 5);
        res.suggestedCount  = std::clamp(extractJsonIntField(innerJson, "count", 130), 20, 450);

        parseCustomElementsFromAIJson(innerJson, res.customElements);
    } else {
        // Если нейросеть ответила свободным текстом без JSON — показываем её прямой ответ
        res.taskMode = AITaskMode::ChatOnly;
        res.replyText = assistantContent;
    }

    if (res.replyText.empty()) {
        res.replyText = "Done! Check the generated result.";
    }
    if (res.planText.empty()) {
        res.planText = userMessage;
    }

    g_chatHistory.push_back({ "user", userMessage });
    g_chatHistory.push_back({ "assistant", res.replyText });
    return res;
}

// ============================================================================
// C++23 безопасные шаблоны для доступа к полям GameObject и EditorUI
// ============================================================================
template <typename ObjT>
static int readObjID(ObjT* obj) {
    if (!obj) return 0;
    if constexpr (requires { obj->m_objectID; }) {
        return static_cast<int>(obj->m_objectID);
    }
    return 0;
}

template <typename ObjT>
static int readObjEditorLayer(ObjT* obj) {
    if (!obj) return 0;
    if constexpr (requires { obj->m_editorLayer; }) {
        return static_cast<int>(obj->m_editorLayer);
    }
    return 0;
}

template <typename ObjT>
static int readObjEditorLayer2(ObjT* obj) {
    if (!obj) return 0;
    if constexpr (requires { obj->m_editorLayer2; }) {
        return static_cast<int>(obj->m_editorLayer2);
    }
    return 0;
}

template <typename ObjT>
static void writeObjEditorLayers(ObjT* obj, int layer1, int layer2 = 0) {
    if (!obj) return;
    if constexpr (requires { obj->m_editorLayer; }) {
        obj->m_editorLayer = static_cast<short>(layer1);
    }
    if constexpr (requires { obj->m_editorLayer2; }) {
        obj->m_editorLayer2 = static_cast<short>(layer2);
    }
}

template <typename ObjT>
static void writeObjScale(ObjT* obj, float sx, float sy) {
    if (!obj) return;
    obj->setScaleX(sx);
    obj->setScaleY(sy);
    if constexpr (requires { obj->m_scaleX; obj->m_scaleY; }) {
        obj->m_scaleX = sx;
        obj->m_scaleY = sy;
    }
    if constexpr (requires { obj->m_startScaleX; obj->m_startScaleY; }) {
        obj->m_startScaleX = sx;
        obj->m_startScaleY = sy;
    }
}

template <typename ObjT>
static void writeObjFlags(ObjT* obj, int zOrder, bool noTouch, bool noGlow) {
    if (!obj) return;
    obj->setZOrder(zOrder);
    if constexpr (requires { obj->m_zOrder; }) {
        obj->m_zOrder = zOrder;
    }
    if constexpr (requires { obj->m_isNoTouch; }) {
        obj->m_isNoTouch = noTouch;
    }
    if constexpr (requires { obj->m_isPassable; }) {
        obj->m_isPassable = noTouch;
    }
    if constexpr (requires { obj->m_hasNoGlow; }) {
        obj->m_hasNoGlow = noGlow;
    }
}

// ============================================================================
// Нативная окраска объектов через встроенный белый канал 1004 + HSV
// ============================================================================
static void rgbToHsv(ccColor3B c, float& outH, float& outS, float& outV) {
    float r = c.r / 255.0f;
    float g = c.g / 255.0f;
    float b = c.b / 255.0f;

    float maxC = (std::max)({ r, g, b });
    float minC = (std::min)({ r, g, b });
    float delta = maxC - minC;

    outV = maxC;
    outS = (maxC <= 0.0001f) ? 0.0f : (delta / maxC);

    if (delta <= 0.0001f) {
        outH = 0.0f;
    } else if (maxC == r) {
        outH = 60.0f * std::fmod(((g - b) / delta), 6.0f);
    } else if (maxC == g) {
        outH = 60.0f * (((b - r) / delta) + 2.0f);
    } else {
        outH = 60.0f * (((r - g) / delta) + 4.0f);
    }
    if (outH < 0.0f) outH += 360.0f;
}

template <typename SpriteColorT>
static void applyHSVOnGJSpriteColor(SpriteColorT* sc, ccColor3B color, GLubyte opacity, bool blending) {
    if (!sc) return;

    float H = 0.f, S = 0.f, V = 1.f;
    rgbToHsv(color, H, S, V);
    float hueSigned = (H > 180.0f) ? (H - 360.0f) : H;

    if constexpr (requires { sc->m_colorID; }) {
        sc->m_colorID = 1004;
    }
    if constexpr (requires { sc->m_defaultColorID; }) {
        sc->m_defaultColorID = 1004;
    }
    if constexpr (requires { sc->m_opacity; }) {
        sc->m_opacity = static_cast<float>(opacity) / 255.0f;
    }
    if constexpr (requires { sc->m_baseOpacity; }) {
        sc->m_baseOpacity = static_cast<float>(opacity) / 255.0f;
    }
    if constexpr (requires { sc->m_usesCustomBlend; }) {
        sc->m_usesCustomBlend = blending;
    }
    if constexpr (requires { sc->m_usesHSV; }) {
        sc->m_usesHSV = true;
    }
    if constexpr (requires { sc->m_hsv; }) {
        cocos2d::ccHSVValue hsvVal;
        hsvVal.h = hueSigned;
        hsvVal.s = S;
        hsvVal.v = V - 1.0f;
        hsvVal.absoluteSaturation = true;
        hsvVal.absoluteBrightness = true;
        sc->m_hsv = hsvVal;
    }
    if constexpr (requires { sc->m_customColor; }) {
        sc->m_customColor = color;
    }
}

template <typename ObjT>
static void bindObjectToColor(
    ObjT* obj,
    StylePalette const& palette,
    ColorSlot slot,
    ccColor3B customRGB,
    GLubyte overrideOpacity = 255,
    bool customBlend = false
) {
    if (!obj) return;
    ccColor3B col;
    bool blend = false;
    GLubyte finalOp = overrideOpacity;

    if (slot == ColorSlot::CustomRGB) {
        col = customRGB;
        blend = customBlend;
    } else {
        int idx = std::clamp(static_cast<int>(slot), 0, 5);
        col = palette.slotColors[idx];
        blend = palette.slotBlending[idx];
        if (overrideOpacity == 255) finalOp = palette.slotOpacity[idx];
    }

    if constexpr (requires { obj->m_baseColor; }) {
        applyHSVOnGJSpriteColor(obj->m_baseColor, col, finalOp, blend);
    }
    if constexpr (requires { obj->m_detailColor; }) {
        applyHSVOnGJSpriteColor(obj->m_detailColor, col, finalOp, blend);
    }

    obj->setColor(col);
    obj->setOpacity(finalOp);
    if (blend) {
        obj->setBlendFunc({ GL_SRC_ALPHA, GL_ONE });
    }
}

// ============================================================================
// Чтение готовых блоков из вкладок редактора (3D, Глаз, Череп, Декор, Неон)
// ============================================================================
struct EditorTabCatalog {
    int id3DTrapTop    = 211;
    int id3DHalfStrip  = 211;
    int id3DTriCorner  = 211;
    int id3DTriBR      = 211;

    int idEyeBlock      = 211;
    int idSkullBlock    = 211;
    int idSwirlSign     = 211;
    int idChainLink     = 211;
    int idBrickBlock    = 211;
    int idBracketArch   = 211;
    int idBracketCorner = 211;
    int idDiagShine1    = 211;

    int idThinLineLong  = 211;
    int idGradLine      = 1888;
    int idCornerMark    = 211;
    int idParallelLines = 211;
    int idArcThin       = 211;
    int idCircleRing    = 211;
    int idCircleDot     = 211;
    int idSmallCross    = 211;
    int idSmallTriangle = 211;
    int idChevron       = 211;
    int idHollowSquare  = 211;
    int idDiagThin      = 211;
    int idShortDash     = 211;
    int idDoubleDots    = 211;

    int idGlowEdgeSmall = 1888;
    int idGlowBeam      = 1888;
};

template <typename T>
static int extractCreateItemObjectID(T* item) {
    if (!item) return 0;
    if constexpr (requires { item->m_objectID; }) {
        if (item->m_objectID > 0) return static_cast<int>(item->m_objectID);
    }
    return item->getTag();
}

template <typename BarT>
static std::vector<int> extractBarObjectIDs(BarT* bar) {
    std::vector<int> ids;
    if (!bar) return ids;

    if constexpr (requires { bar->m_buttonArray; }) {
        if (bar->m_buttonArray) {
            for (unsigned int i = 0; i < bar->m_buttonArray->count(); ++i) {
                auto* obj = bar->m_buttonArray->objectAtIndex(i);
                if (auto* cmi = typeinfo_cast<CreateMenuItem*>(obj)) {
                    int id = extractCreateItemObjectID(cmi);
                    if (id > 0) ids.push_back(id);
                } else if (auto* node = typeinfo_cast<CCNode*>(obj)) {
                    if (node->getTag() > 0) ids.push_back(node->getTag());
                }
            }
        }
    }
    return ids;
}

static int getSlotObjectID(std::vector<int> const& barIDs, int pageZeroBased, int slotZeroBased, int fallbackID = 211) {
    int index = pageZeroBased * 12 + slotZeroBased;
    if (index >= 0 && index < static_cast<int>(barIDs.size()) && barIDs[index] > 0) {
        return barIDs[index];
    }
    return fallbackID;
}

static void findRealGlowObjectsInTab(std::vector<int> const& tabIDs, EditorTabCatalog& cat) {
    int idx1888 = -1;
    for (int i = 0; i < static_cast<int>(tabIDs.size()); ++i) {
        if (tabIDs[i] == 1888) {
            idx1888 = i;
            break;
        }
    }
    if (idx1888 >= 0) {
        int pageStart = (idx1888 / 12) * 12;
        if (pageStart + 4 < static_cast<int>(tabIDs.size()) && tabIDs[pageStart + 4] > 0) {
            cat.idGlowEdgeSmall = tabIDs[pageStart + 4];
        }
        if (pageStart + 1 < static_cast<int>(tabIDs.size()) && tabIDs[pageStart + 1] > 0) {
            cat.idGlowBeam = tabIDs[pageStart + 1];
        }
    } else if (cat.idGradLine != 211) {
        cat.idGlowEdgeSmall = cat.idGradLine;
    }
}

template <typename EditorUIType>
static EditorTabCatalog loadEditorTabCatalog(EditorUIType* ui) {
    EditorTabCatalog cat;
    if (!ui) return cat;

    if constexpr (requires { ui->m_createButtonBars; }) {
        if (!ui->m_createButtonBars) return cat;
        unsigned int barCount = ui->m_createButtonBars->count();

        std::vector<int> best3DBar;
        std::vector<int> bestDecorBar;

        for (unsigned int b = 0; b < barCount; ++b) {
            auto* barObj = ui->m_createButtonBars->objectAtIndex(b);
            auto* bar = typeinfo_cast<EditButtonBar*>(barObj);
            if (!bar) continue;

            std::vector<int> ids = extractBarObjectIDs(bar);
            if (b == 4 && ids.size() >= 24) {
                best3DBar = ids;
            }
            if ((b == 9 || b == 10 || b == 8) && ids.size() >= 110) {
                if (ids.size() > bestDecorBar.size()) {
                    bestDecorBar = ids;
                }
            }
        }

        if (best3DBar.size() >= 20) {
            cat.id3DTrapTop   = getSlotObjectID(best3DBar, 1, 0, 211);
            cat.id3DHalfStrip = getSlotObjectID(best3DBar, 1, 1, 211);
            cat.id3DTriCorner = getSlotObjectID(best3DBar, 1, 5, 211);
            cat.id3DTriBR     = getSlotObjectID(best3DBar, 1, 6, 211);
        }

        if (bestDecorBar.size() >= 115) {
            cat.idChainLink     = getSlotObjectID(bestDecorBar, 8, 1, 211);
            cat.idSwirlSign     = getSlotObjectID(bestDecorBar, 8, 4, 211);
            cat.idEyeBlock      = getSlotObjectID(bestDecorBar, 8, 5, 211);
            cat.idBrickBlock    = getSlotObjectID(bestDecorBar, 8, 6, 211);
            cat.idBracketArch   = getSlotObjectID(bestDecorBar, 8, 7, 211);
            cat.idBracketCorner = getSlotObjectID(bestDecorBar, 8, 8, 211);
            cat.idSkullBlock    = getSlotObjectID(bestDecorBar, 8, 10, 211);
            cat.idDiagShine1    = getSlotObjectID(bestDecorBar, 8, 11, 211);

            cat.idThinLineLong  = getSlotObjectID(bestDecorBar, 9, 0, 211);
            cat.idGradLine      = getSlotObjectID(bestDecorBar, 9, 2, 1888);
            cat.idCornerMark    = getSlotObjectID(bestDecorBar, 9, 3, 211);
            cat.idParallelLines = getSlotObjectID(bestDecorBar, 9, 4, 211);
            cat.idArcThin       = getSlotObjectID(bestDecorBar, 9, 6, 211);
            cat.idCircleRing    = getSlotObjectID(bestDecorBar, 9, 7, 211);
            cat.idCircleDot     = getSlotObjectID(bestDecorBar, 9, 8, 211);
            cat.idSmallCross    = getSlotObjectID(bestDecorBar, 9, 9, 211);
            cat.idSmallTriangle = getSlotObjectID(bestDecorBar, 9, 10, 211);
            cat.idChevron       = getSlotObjectID(bestDecorBar, 9, 11, 211);
        }
        if (bestDecorBar.size() >= 126) {
            cat.idHollowSquare  = getSlotObjectID(bestDecorBar, 10, 1, 211);
            cat.idDiagThin      = getSlotObjectID(bestDecorBar, 10, 2, 211);
            cat.idShortDash     = getSlotObjectID(bestDecorBar, 10, 3, 211);
            cat.idDoubleDots    = getSlotObjectID(bestDecorBar, 10, 5, 211);
        }

        findRealGlowObjectsInTab(bestDecorBar, cat);
    }

    return cat;
}

static int findFirstFreeEditorLayerBlock(LevelEditorLayer* editorLayer, int neededConsecutive = 4) {
    if (!editorLayer) return 1;

    std::set<int> usedLayers;
    if constexpr (requires { editorLayer->m_objects; }) {
        if (editorLayer->m_objects) {
            unsigned int count = editorLayer->m_objects->count();
            for (unsigned int i = 0; i < count; ++i) {
                auto* obj = typeinfo_cast<GameObject*>(editorLayer->m_objects->objectAtIndex(i));
                if (!obj) continue;
                int l1 = readObjEditorLayer(obj);
                int l2 = readObjEditorLayer2(obj);
                if (l1 > 0) usedLayers.insert(l1);
                if (l2 > 0) usedLayers.insert(l2);
            }
        }
    }

    for (int candidate = 1; candidate < 990; ++candidate) {
        bool allFree = true;
        for (int offset = 0; offset < neededConsecutive; ++offset) {
            if (usedLayers.count(candidate + offset) > 0) {
                allFree = false;
                break;
            }
        }
        if (allFree) return candidate;
    }
    return 1;
}

// ============================================================================
// Считывание и полное заполнение выделенного контура
// ============================================================================
static bool isSpikeObjectID(int id) {
    return id == 8 || id == 39 || id == 103 || id == 144 || id == 145 ||
           id == 177 || id == 178 || id == 179 || id == 205 || id == 216 ||
           id == 217 || id == 218 || id == 458 || id == 459;
}

struct IslandBounds {
    std::set<std::pair<int, int>> cells;
    int minGX = 0, maxGX = 0;
    int minGY = 0, maxGY = 0;
};

static std::vector<IslandBounds> splitIntoIslands(std::set<std::pair<int, int>> const& allCells) {
    std::vector<IslandBounds> islands;
    std::set<std::pair<int, int>> visited;

    for (auto const& startCell : allCells) {
        if (visited.count(startCell)) continue;

        IslandBounds isl;
        isl.minGX = isl.maxGX = startCell.first;
        isl.minGY = isl.maxGY = startCell.second;

        std::queue<std::pair<int, int>> q;
        q.push(startCell);
        visited.insert(startCell);

        while (!q.empty()) {
            auto [cx, cy] = q.front();
            q.pop();

            isl.cells.insert({ cx, cy });
            isl.minGX = (std::min)(isl.minGX, cx);
            isl.maxGX = (std::max)(isl.maxGX, cx);
            isl.minGY = (std::min)(isl.minGY, cy);
            isl.maxGY = (std::max)(isl.maxGY, cy);

            for (int dx = -2; dx <= 2; ++dx) {
                for (int dy = -2; dy <= 2; ++dy) {
                    if (dx == 0 && dy == 0) continue;
                    std::pair<int, int> nb{ cx + dx, cy + dy };
                    if (allCells.count(nb) && !visited.count(nb)) {
                        visited.insert(nb);
                        q.push(nb);
                    }
                }
            }
        }
        islands.push_back(std::move(isl));
    }
    return islands;
}

static void fillAllStructureInteriorsCompletely(std::set<std::pair<int, int>>& occupied) {
    if (occupied.size() < 3) return;

    auto islands = splitIntoIslands(occupied);
    for (auto const& isl : islands) {
        if (isl.cells.size() < 3) continue;

        std::set<std::pair<int, int>> filled = isl.cells;

        for (int gy = isl.minGY; gy <= isl.maxGY; ++gy) {
            int leftX = 999999;
            int rightX = -999999;
            for (auto const& [x, y] : isl.cells) {
                if (y == gy) {
                    leftX  = (std::min)(leftX, x);
                    rightX = (std::max)(rightX, x);
                }
            }
            if (leftX < rightX && (rightX - leftX) <= 24) {
                for (int gx = leftX; gx <= rightX; ++gx) {
                    filled.insert({ gx, gy });
                }
            }
        }

        if (isl.minGY <= 2 && (isl.maxGX - isl.minGX) <= 20 && (isl.maxGY - isl.minGY) <= 20) {
            for (int gx = isl.minGX; gx <= isl.maxGX; ++gx) {
                int topY = -999999;
                for (auto const& [x, y] : filled) {
                    if (x == gx) topY = (std::max)(topY, y);
                }
                if (topY >= isl.minGY) {
                    for (int gy = isl.minGY; gy <= topY; ++gy) {
                        filled.insert({ gx, gy });
                    }
                }
            }
        }

        for (auto const& c : filled) {
            occupied.insert(c);
        }
    }
}

template <typename EditorUIType>
static ContourAnalysis analyzeSelectedContour(EditorUIType* ui) {
    ContourAnalysis info;
    if (!ui) return info;

    std::vector<GameObject*> rawObjects;

    if constexpr (requires { ui->m_selectedObjects; }) {
        if (ui->m_selectedObjects && ui->m_selectedObjects->count() > 0) {
            for (unsigned int i = 0; i < ui->m_selectedObjects->count(); ++i) {
                if (auto* go = typeinfo_cast<GameObject*>(ui->m_selectedObjects->objectAtIndex(i))) {
                    rawObjects.push_back(go);
                }
            }
        }
    }
    if (rawObjects.empty()) {
        if constexpr (requires { ui->m_selectedObject; }) {
            if (ui->m_selectedObject) {
                rawObjects.push_back(ui->m_selectedObject);
            }
        }
    }

    if (!rawObjects.empty()) {
        bool first = true;
        for (GameObject* obj : rawObjects) {
            if (!obj) continue;
            CCPoint pos = obj->getPosition();
            info.lockedStates.push_back(LockedObjectState{ obj, pos, obj->getRotation() });

            int id = readObjID(obj);
            if (first) {
                info.minX = info.maxX = pos.x;
                info.minY = info.maxY = pos.y;
                first = false;
            } else {
                info.minX = (std::min)(info.minX, pos.x);
                info.maxX = (std::max)(info.maxX, pos.x);
                info.minY = (std::min)(info.minY, pos.y);
                info.maxY = (std::max)(info.maxY, pos.y);
            }

            if (isSpikeObjectID(id)) {
                SelectedSpikeInfo sp;
                sp.x = pos.x;
                sp.y = pos.y;
                sp.rotation = obj->getRotation();
                sp.scaleX = std::fabs(obj->getScaleX()) > 0.01f ? std::fabs(obj->getScaleX()) : 1.f;
                sp.scaleY = std::fabs(obj->getScaleY()) > 0.01f ? std::fabs(obj->getScaleY()) : 1.f;
                sp.flipX = obj->isFlipX();
                sp.flipY = obj->isFlipY();
                sp.objectId = id;
                sp.sourceObj = obj;
                info.spikes.push_back(sp);
            } else {
                int gx = static_cast<int>(std::floor(pos.x / 30.0f));
                int gy = static_cast<int>(std::floor(pos.y / 30.0f));
                info.occupiedCells.insert({ gx, gy });
                info.solidObjects.push_back(obj);
            }
        }

        fillAllStructureInteriorsCompletely(info.occupiedCells);
    }

    if (info.occupiedCells.empty() && info.spikes.empty()) {
        CCPoint centerWorld{ 240.f, 150.f };
        if (ui->m_editorLayer && ui->m_editorLayer->m_objectLayer) {
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            centerWorld = ui->m_editorLayer->m_objectLayer->convertToNodeSpace(
                CCPoint{ winSize.width * 0.5f, winSize.height * 0.5f }
            );
        }
        int baseGX = static_cast<int>(std::floor(centerWorld.x / 30.0f));
        int baseGY = (std::max)(0, static_cast<int>(std::floor(centerWorld.y / 30.0f)) - 1);

        for (int dx = 0; dx < 3; ++dx) {
            for (int dy = 0; dy < 3; ++dy) {
                info.occupiedCells.insert({ baseGX + dx, baseGY + dy });
            }
        }
    }

    if (!info.occupiedCells.empty()) {
        info.minGX = info.maxGX = info.occupiedCells.begin()->first;
        info.minGY = info.maxGY = info.occupiedCells.begin()->second;
        for (auto const& [gx, gy] : info.occupiedCells) {
            info.minGX = (std::min)(info.minGX, gx);
            info.maxGX = (std::max)(info.maxGX, gx);
            info.minGY = (std::min)(info.minGY, gy);
            info.maxGY = (std::max)(info.maxGY, gy);
        }
    }

    return info;
}

// ============================================================================
// Построение палитры и объектов на основе решения реальной ИИ-нейросети
// ============================================================================
static StylePalette buildPaletteFromAIDecision(AIDecisionResult const& ai) {
    StylePalette pal;
    pal.masterColor = ai.mainRGB;

    pal.slotColors[static_cast<int>(ColorSlot::DarkFill)]         = ai.fillRGB;
    pal.slotColors[static_cast<int>(ColorSlot::Dark3D)]           = ccColor3B{ 4, 5, 7 };
    pal.slotColors[static_cast<int>(ColorSlot::MidTone)]          = ai.midRGB;
    pal.slotColors[static_cast<int>(ColorSlot::BrightMain)]       = ai.mainRGB;
    pal.slotColors[static_cast<int>(ColorSlot::HighlightOutline)] = (ai.taskMode == AITaskMode::Only3D)
        ? ccColor3B{ 255, 255, 255 }
        : ai.outlineRGB;
    pal.slotColors[static_cast<int>(ColorSlot::AdditiveGlow)]     = ai.glowRGB;

    pal.slotOpacity[static_cast<int>(ColorSlot::DarkFill)]         = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::Dark3D)]           = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::MidTone)]          = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::BrightMain)]       = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::HighlightOutline)] = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::AdditiveGlow)]     = 135;

    pal.slotBlending[static_cast<int>(ColorSlot::DarkFill)]         = false;
    pal.slotBlending[static_cast<int>(ColorSlot::Dark3D)]           = false;
    pal.slotBlending[static_cast<int>(ColorSlot::MidTone)]          = false;
    pal.slotBlending[static_cast<int>(ColorSlot::BrightMain)]       = false;
    pal.slotBlending[static_cast<int>(ColorSlot::HighlightOutline)] = false;
    pal.slotBlending[static_cast<int>(ColorSlot::AdditiveGlow)]     = true;

    return pal;
}

static void addSpec(
    std::vector<GeneratedObjectSpec>& out,
    int objectId,
    float x, float y,
    float scaleX, float scaleY,
    float rotation,
    LayerRole layerRole,
    ColorSlot colorSlot,
    int zOrder,
    int priorityTier = 1,
    bool isOverhang = false,
    GLubyte opacity = 255
) {
    GeneratedObjectSpec spec;
    spec.objectId = objectId;
    spec.x = x;
    spec.y = y;
    spec.scaleX = scaleX;
    spec.scaleY = scaleY;
    spec.rotation = rotation;
    spec.layerRole = layerRole;
    spec.colorSlot = colorSlot;
    spec.zOrder = zOrder;
    spec.priorityTier = priorityTier;
    spec.isOverhang = isOverhang;
    spec.customOpacity = opacity;
    spec.noTouch = true;
    spec.noGlow = true;
    out.push_back(spec);
}

static int resolveAIElementObjectID(int shapeType, EditorTabCatalog const& cat) {
    switch (shapeType) {
        case 0: return 211;
        case 1: return cat.idCircleDot;
        case 2: return cat.idSmallTriangle;
        case 3: return 1888;
        case 4: return 406;
        case 5: return cat.idBrickBlock;
        case 6: return cat.idEyeBlock;
        case 7: return cat.idSkullBlock;
        case 8: return cat.idArcThin;
        case 9: return cat.idParallelLines;
        case 10: return cat.idChainLink;
        default: return 211;
    }
}

static std::vector<GeneratedObjectSpec> generateArtFromAIDecision(
    ContourAnalysis const& contour,
    EditorTabCatalog const& cat,
    AIDecisionResult const& ai,
    bool allowOverhang,
    int maxObjectLimit,
    std::mt19937& rng
) {
    std::vector<GeneratedObjectSpec> result;

    auto islands = splitIntoIslands(contour.occupiedCells);
    int totalCells = (std::max)(1, contour.cellCount());

    // 1. РЕЖИМ "ТОЛЬКО 3D"
    if (ai.taskMode == AITaskMode::Only3D) {
        for (auto const& isl : islands) {
            for (auto const& [gx, gy] : isl.cells) {
                float cx = gx * 30.f + 15.f;
                float cy = gy * 30.f + 15.f;

                bool topOpen    = (contour.occupiedCells.count({ gx, gy + 1 }) == 0);
                bool bottomOpen = (contour.occupiedCells.count({ gx, gy - 1 }) == 0);
                bool leftOpen   = (contour.occupiedCells.count({ gx - 1, gy }) == 0);
                bool rightOpen  = (contour.occupiedCells.count({ gx + 1, gy }) == 0);

                if (topOpen)    addSpec(result, 211, cx, cy + 14.2f, 1.02f, 0.06f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11, 0, false);
                if (leftOpen)   addSpec(result, 211, cx - 14.2f, cy, 0.06f, 1.02f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11, 0, false);
                if (rightOpen)  addSpec(result, 211, cx + 14.2f, cy, 0.06f, 1.02f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11, 0, false);
                if (bottomOpen && gy > 0) addSpec(result, 211, cx, cy - 14.2f, 1.02f, 0.06f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11, 0, false);

                if (topOpen) {
                    int top3DId = leftOpen ? cat.id3DTrapTop : cat.id3DHalfStrip;
                    addSpec(result, top3DId, cx, cy + 22.5f, 1.00f, 1.00f, 0.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 0, true);
                }
                if (rightOpen) {
                    addSpec(result, cat.id3DHalfStrip, cx + 22.5f, cy, 1.00f, 1.00f, 90.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 0, true);
                }
                if (topOpen && rightOpen) {
                    addSpec(result, cat.id3DTriCorner, cx + 22.5f, cy + 22.5f, 1.00f, 1.00f, 0.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 0, true);
                    addSpec(result, cat.id3DTriBR,     cx + 22.5f, cy + 22.5f, 1.00f, 1.00f, 0.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 0, true);
                }
            }
        }
        return result;
    }

    // 2. РЕЖИМ "ТОЛЬКО НЕОН"
    if (ai.taskMode == AITaskMode::OnlyNeon) {
        for (auto const& isl : islands) {
            for (auto const& [gx, gy] : isl.cells) {
                float cx = gx * 30.f + 15.f;
                float cy = gy * 30.f + 15.f;
                bool topOpen   = (contour.occupiedCells.count({ gx, gy + 1 }) == 0);
                bool leftOpen  = (contour.occupiedCells.count({ gx - 1, gy }) == 0);
                bool rightOpen = (contour.occupiedCells.count({ gx + 1, gy }) == 0);

                if (topOpen) {
                    if (allowOverhang) {
                        addSpec(result, cat.idGlowEdgeSmall, cx - 7.5f, cy + 18.0f, 0.50f, 0.45f, 0.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 14, 0, true, 140);
                        addSpec(result, cat.idGlowEdgeSmall, cx + 7.5f, cy + 18.0f, 0.50f, 0.45f, 0.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 14, 0, true, 140);
                    }
                    addSpec(result, cat.idGlowEdgeSmall, cx - 7.5f, cy + 11.5f, 0.50f, 0.40f, 180.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 0, false, 115);
                    addSpec(result, cat.idGlowEdgeSmall, cx + 7.5f, cy + 11.5f, 0.50f, 0.40f, 180.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 0, false, 115);
                }
                if (leftOpen) {
                    addSpec(result, cat.idGlowEdgeSmall, cx - 11.5f, cy, 0.50f, 0.40f, 90.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 0, false, 110);
                }
                if (rightOpen) {
                    addSpec(result, cat.idGlowEdgeSmall, cx + 11.5f, cy, 0.50f, 0.40f, 270.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 0, false, 110);
                }
            }
        }
        return result;
    }

    // 3. РЕЖИМ "ТОЛЬКО ЗАЛИВКА"
    if (ai.taskMode == AITaskMode::OnlyFill) {
        for (auto const& isl : islands) {
            for (auto const& [gx, gy] : isl.cells) {
                float cx = gx * 30.f + 15.f;
                float cy = gy * 30.f + 15.f;
                addSpec(result, 211, cx, cy, 1.00f, 1.00f, 0.f, LayerRole::Fill, ColorSlot::DarkFill, 1, 0, false);
            }
        }
        return result;
    }

    // 4. ПОЛНОЦЕННАЯ ГЕНЕРАЦИЯ АРТА И ДЕКОРА ИИ (FullDecor)
    int effectiveCap = (std::min)(maxObjectLimit, (std::max)(40, ai.suggestedCount));
    float densityRatio = std::clamp(static_cast<float>(effectiveCap) / (totalCells * 8.f), 0.5f, 3.0f);
    float microScale = std::clamp(0.38f / std::sqrt(densityRatio), 0.16f, 0.38f);

    int layoutStyle = (ai.patternStyle + g_variantIndex + rng()) % 6;
    if (layoutStyle == g_lastLayoutSeed) {
        layoutStyle = (layoutStyle + 1) % 6;
    }
    g_lastLayoutSeed = layoutStyle;

    int const emblemPool[4] = { cat.idEyeBlock, cat.idSkullBlock, cat.idCircleRing, cat.idBracketArch };
    int chosenEmblem = emblemPool[rng() % 4];
    if (ai.emblemType == 1) chosenEmblem = cat.idEyeBlock;
    else if (ai.emblemType == 2) chosenEmblem = cat.idSkullBlock;
    else if (ai.emblemType == 3) chosenEmblem = cat.idCircleRing;
    else if (ai.emblemType == 4) chosenEmblem = cat.idBracketArch;

    int const cornerPool[4] = { cat.idBracketCorner, cat.idCornerMark, cat.idArcThin, cat.idSmallTriangle };
    int chosenCornerPiece = cornerPool[rng() % 4];

    for (auto const& isl : islands) {
        int cols = isl.maxGX - isl.minGX + 1;
        int rows = isl.maxGY - isl.minGY + 1;
        int focalCol = cols / 2;
        int focalRow = rows / 2;

        for (auto const& [gx, gy] : isl.cells) {
            float cx = gx * 30.f + 15.f;
            float cy = gy * 30.f + 15.f;

            bool topOpen    = (contour.occupiedCells.count({ gx, gy + 1 }) == 0);
            bool bottomOpen = (contour.occupiedCells.count({ gx, gy - 1 }) == 0);
            bool leftOpen   = (contour.occupiedCells.count({ gx - 1, gy }) == 0);
            bool rightOpen  = (contour.occupiedCells.count({ gx + 1, gy }) == 0);

            int localCol = gx - isl.minGX;
            int localRow = gy - isl.minGY;

            // Слой +0: Полная базовая заливка клетки + внутренний текстурный тон
            addSpec(result, 211, cx, cy, 1.00f, 1.00f, 0.f, LayerRole::Fill, ColorSlot::DarkFill, 1, 0, false);

            if (ai.wantBrickPattern) {
                addSpec(result, cat.idBrickBlock, cx, cy, 0.88f, 0.88f, 0.f, LayerRole::Fill, ColorSlot::MidTone, 2, 1, false, 160);
            } else if (((localCol + localRow + layoutStyle) % 2) == 0) {
                addSpec(result, 211, cx, cy, 0.86f, 0.86f, 0.f, LayerRole::Fill, ColorSlot::MidTone, 2, 1, false, 52);
            }

            // Снежная шапка / иней на верхних гранях (если ИИ включила wantSnowCap)
            if (topOpen && ai.wantSnowCap) {
                addSpec(result, 211, cx, cy + 10.5f, 1.00f, 0.30f, 0.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 7, 0, false, 235);
                addSpec(result, 211, cx - 6.f, cy + 6.5f, 0.42f, 0.16f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 7, 1, false, 210);
            }

            // Слой +2: Четкая обводка + 3D-блоки (с учетом переключателя Inside / Overhang)
            if (topOpen)    addSpec(result, 211, cx, cy + 14.2f, 1.02f, 0.06f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11, 0, false);
            if (leftOpen)   addSpec(result, 211, cx - 14.2f, cy, 0.06f, 1.02f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11, 0, false);
            if (rightOpen)  addSpec(result, 211, cx + 14.2f, cy, 0.06f, 1.02f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11, 0, false);
            if (bottomOpen && gy > 0) addSpec(result, 211, cx, cy - 14.2f, 1.02f, 0.06f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11, 0, false);

            if (ai.want3D && allowOverhang) {
                if (topOpen) {
                    int top3DId = leftOpen ? cat.id3DTrapTop : cat.id3DHalfStrip;
                    addSpec(result, top3DId, cx, cy + 22.5f, 1.00f, 1.00f, 0.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 0, true);
                }
                if (rightOpen) {
                    addSpec(result, cat.id3DHalfStrip, cx + 22.5f, cy, 1.00f, 1.00f, 90.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 0, true);
                }
                if (topOpen && rightOpen) {
                    addSpec(result, cat.id3DTriCorner, cx + 22.5f, cy + 22.5f, 1.00f, 1.00f, 0.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 0, true);
                    addSpec(result, cat.id3DTriBR,     cx + 22.5f, cy + 22.5f, 1.00f, 1.00f, 0.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 0, true);
                }
            }

            // Слой +3: Неон и трава
            if (ai.wantNeon) {
                if (topOpen) {
                    if (allowOverhang) {
                        addSpec(result, cat.idGlowEdgeSmall, cx - 7.5f, cy + 18.0f, 0.50f, 0.45f, 0.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 14, 0, true, 130);
                        addSpec(result, cat.idGlowEdgeSmall, cx + 7.5f, cy + 18.0f, 0.50f, 0.45f, 0.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 14, 0, true, 130);
                    }
                    addSpec(result, cat.idGlowEdgeSmall, cx - 7.5f, cy + 11.5f, 0.50f, 0.40f, 180.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 1, false, 105);
                    addSpec(result, cat.idGlowEdgeSmall, cx + 7.5f, cy + 11.5f, 0.50f, 0.40f, 180.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 1, false, 105);
                }
                if (leftOpen) {
                    addSpec(result, cat.idGlowEdgeSmall, cx - 11.5f, cy, 0.50f, 0.40f, 90.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 1, false, 100);
                }
                if (rightOpen) {
                    addSpec(result, cat.idGlowEdgeSmall, cx + 11.5f, cy, 0.50f, 0.40f, 270.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 1, false, 100);
                }
            }

            if (topOpen && ai.wantGrass && allowOverhang) {
                int const grassIDs[3] = { 406, 407, 408 };
                int gID = grassIDs[(gx + gy + layoutStyle) % 3];
                addSpec(result, gID, cx - 4.f, cy + 15.8f, 0.44f, 0.46f, 0.f, LayerRole::NeonAndGrass, ColorSlot::BrightMain, 15, 1, true);
            }

            // Внутренние структурные детали в каждой клетке
            if (ai.wantCrystalPattern) {
                addSpec(result, 211, cx, cy, 0.36f, 0.36f, 45.f, LayerRole::InnerDetail, ColorSlot::MidTone, 4, 1, false);
                addSpec(result, cat.idSmallTriangle, cx, cy, microScale, microScale, static_cast<float>((localCol * 90) % 360), LayerRole::InnerDetail, ColorSlot::BrightMain, 6, 1, false);
            } else if (localCol == focalCol && localRow == focalRow && ai.emblemType != 5) {
                addSpec(result, chosenEmblem, cx, cy, 0.52f, 0.52f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 6, 1, false);
            } else {
                addSpec(result, chosenCornerPiece, cx - 7.5f, cy + 7.5f, microScale, microScale, 0.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 5, 2, false);
                addSpec(result, chosenCornerPiece, cx + 7.5f, cy - 7.5f, microScale, microScale, 180.f, LayerRole::InnerDetail, ColorSlot::MidTone, 5, 2, false);
            }
        }
    }

    // 5. Кастомные элементы рисунка (elements), которые ИИ-нейросеть сгенерировала сама по координатам!
    float spanW = static_cast<float>((std::max)(1, contour.maxGX - contour.minGX + 1)) * 30.f;
    float spanH = static_cast<float>((std::max)(1, contour.maxGY - contour.minGY + 1)) * 30.f;
    float originX = contour.minGX * 30.f;
    float originY = contour.minGY * 30.f;

    for (auto const& el : ai.customElements) {
        float worldX = originX + el.rx * spanW;
        float worldY = originY + el.ry * spanH;
        int cellGX = static_cast<int>(std::floor(worldX / 30.f));
        int cellGY = static_cast<int>(std::floor(worldY / 30.f));

        // Если элемент попадает внутрь выделенного контура (или разрешен Overhang) — ставим его!
        if (contour.occupiedCells.count({ cellGX, cellGY }) > 0 || allowOverhang) {
            GeneratedObjectSpec cs;
            cs.objectId       = resolveAIElementObjectID(el.shapeType, cat);
            cs.x              = std::clamp(worldX, originX + 4.f, originX + spanW - 4.f);
            cs.y              = std::clamp(worldY, originY + 4.f, originY + spanH - 4.f);
            cs.scaleX         = std::clamp(el.scaleX, 0.12f, 1.25f);
            cs.scaleY         = std::clamp(el.scaleY, 0.12f, 1.25f);
            cs.rotation       = el.rot;
            cs.layerRole      = LayerRole::InnerDetail;
            cs.colorSlot      = ColorSlot::CustomRGB;
            cs.customColor    = el.color;
            cs.customOpacity  = el.opacity;
            cs.customBlending = el.blending;
            cs.zOrder         = el.zOrder;
            cs.priorityTier   = 0; // Кастомные элементы от самой ИИ имеют высший приоритет!
            cs.noTouch        = true;
            cs.noGlow         = true;
            result.push_back(cs);
        }
    }

    std::stable_sort(result.begin(), result.end(), [](GeneratedObjectSpec const& a, GeneratedObjectSpec const& b) {
        return a.priorityTier < b.priorityTier;
    });

    if (static_cast<int>(result.size()) > effectiveCap) {
        result.resize(effectiveCap);
    }

    return result;
}

// ============================================================================
// Спавн и отмена объектов в редакторе GD
// ============================================================================
static int clearLastPlacedObjects() {
    int removed = 0;
    for (auto& obj : g_lastPlacedObjects) {
        if (obj && obj->getParent()) {
            writeObjScale(static_cast<GameObject*>(obj), 0.f, 0.f);
            obj->setPosition(CCPoint{ -99999.f, -99999.f });
            obj->setOpacity(0);
            removed++;
        }
    }
    g_lastPlacedObjects.clear();
    return removed;
}

struct SpawnSummary {
    int createdCount = 0;
    int firstLayer = 1;
};

static SpawnSummary placeGeneratedArtInEditor(
    EditorUI* ui,
    ContourAnalysis const& contour,
    std::vector<GeneratedObjectSpec> const& specs,
    StylePalette const& palette,
    AITaskMode taskMode
) {
    SpawnSummary summary;
    if (!ui || !ui->m_editorLayer) return summary;

    clearLastPlacedObjects();

    auto* editorLayer = ui->m_editorLayer;
    summary.firstLayer = findFirstFreeEditorLayerBlock(editorLayer, 4);

    if (taskMode == AITaskMode::FullDecor || taskMode == AITaskMode::OnlyFill) {
        for (GameObject* baseObj : contour.solidObjects) {
            if (!baseObj) continue;
            writeObjEditorLayers(baseObj, summary.firstLayer + static_cast<int>(LayerRole::Fill), 0);
            writeObjFlags(baseObj, 0, false, true);
            bindObjectToColor(baseObj, palette, ColorSlot::DarkFill, ccColor3B{ 0, 0, 0 });
        }
    }

    for (auto const& s : specs) {
        GameObject* obj = editorLayer->createObject(s.objectId, CCPoint{ s.x, s.y }, false);
        if (!obj) continue;

        obj->setRotation(s.rotation);
        obj->setFlipX(s.flipX);
        obj->setFlipY(s.flipY);
        writeObjScale(obj, s.scaleX, s.scaleY);
        writeObjFlags(obj, s.zOrder, s.noTouch, s.noGlow);

        int assignedLayer = summary.firstLayer + static_cast<int>(s.layerRole);
        writeObjEditorLayers(obj, assignedLayer, 0);
        bindObjectToColor(obj, palette, s.colorSlot, s.customColor, s.customOpacity, s.customBlending);

        g_lastPlacedObjects.push_back(obj);
        summary.createdCount++;
    }

    return summary;
}

// ============================================================================
// Русская (ЙЦУКЕН) и английская раскладка + чтение буфера обмена Windows
// ============================================================================
struct RusKeyEntry {
    int vk;
    const char* rusPhonetic;
    char engLower;
    char engShift;
};

static constexpr RusKeyEntry kRusKeyTable[] = {
    { 'Q', "y",   'q', 'Q' }, { 'W', "ts",  'w', 'W' }, { 'E', "u",   'e', 'E' },
    { 'R', "k",   'r', 'R' }, { 'T', "e",   't', 'T' }, { 'Y', "n",   'y', 'Y' },
    { 'U', "g",   'u', 'U' }, { 'I', "sh",  'i', 'I' }, { 'O', "sch", 'o', 'O' },
    { 'P', "z",   'p', 'P' }, { 0xDB, "h",  '[', '{' }, { 0xDD, "",   ']', '}' },
    { 'A', "f",   'a', 'A' }, { 'S', "y",   's', 'S' }, { 'D', "v",   'd', 'D' },
    { 'F', "a",   'f', 'F' }, { 'G', "p",   'g', 'G' }, { 'H', "r",   'h', 'H' },
    { 'J', "o",   'j', 'J' }, { 'K', "l",   'k', 'K' }, { 'L', "d",   'd', 'D' },
    { 0xBA, "zh", ';', ':' }, { 0xDE, "e",  '\'', '"' },
    { 'Z', "ya",  'z', 'Z' }, { 'X', "ch",  'x', 'X' }, { 'C', "s",   'c', 'C' },
    { 'V', "m",   'v', 'V' }, { 'B', "i",   'b', 'B' }, { 'N', "t",   'n', 'N' },
    { 'M', "",    'm', 'M' }, { 0xBC, "b",  ',', '<' }, { 0xBE, "yu", '.', '>' },
    { 0xBF, "?",  '/', '?' }, { 0xBD, "-",  '-', '_' }
};

static std::string readWindowsClipboardText() {
    std::string out;
#ifdef GEODE_IS_WINDOWS
    if (OpenClipboard(nullptr)) {
        HANDLE hData = GetClipboardData(CF_TEXT);
        if (hData) {
            char const* pszText = static_cast<char const*>(GlobalLock(hData));
            if (pszText) {
                out = pszText;
                GlobalUnlock(hData);
            }
        }
        CloseClipboard();
    }
#endif
    return trimWhitespace(out);
}

// ============================================================================
// ИНТЕРФЕЙС "ART AGENT" (1-в-1 по скриншотам image_b9641c.png и image_b963fe.jpg)
// + Строгая блокировка входа без достоверного проверенного API-ключа!
// ============================================================================
class AIDecorPopup;
static AIDecorPopup* g_activePopup = nullptr;

class AIDecorPopup : public geode::Popup {
protected:
    EditorUI* m_editorUI = nullptr;
    ContourAnalysis m_contour;
    bool m_isApiGateStage = false;
    bool m_russianLayout = true;
    int m_providerIdx = 0;
    int m_previewTab = 0; // 0 = Shape, 1 = Drawing, 2 = In GD
    std::string m_inputBuffer;
    bool m_prevKeyStates[256]{};

    // Элементы экрана API Gate
    CCLabelBMFont* m_inputDisplayLabel = nullptr;
    CCLabelBMFont* m_langBadgeLabel = nullptr;
    CCLabelBMFont* m_providerBtnLabel = nullptr;
    CCLabelBMFont* m_apiGateStatusLabel = nullptr;
    CCScale9Sprite* m_checkBoxBg = nullptr;
    CCNode* m_checkTickContainer = nullptr;

    // Элементы интерфейса Art Agent (по скриншотам)
    CCLabelBMFont* m_agentReplyLine1 = nullptr;
    CCLabelBMFont* m_agentReplyLine2 = nullptr;
    CCLabelBMFont* m_agentReplyLine3 = nullptr;
    CCLabelBMFont* m_userPromptLine1 = nullptr;
    CCLabelBMFont* m_userPromptLine2 = nullptr;
    CCLabelBMFont* m_stepPlanLabel = nullptr;
    CCLabelBMFont* m_stepDrawingLabel = nullptr;
    CCLabelBMFont* m_stepVectorLabel = nullptr;
    CCLabelBMFont* m_stepPlaceLabel = nullptr;

    // Правая панель Art Agent (Shape | Drawing | In GD)
    CCScale9Sprite* m_tabShapeBg = nullptr;
    CCScale9Sprite* m_tabDrawingBg = nullptr;
    CCScale9Sprite* m_tabInGDBg = nullptr;
    CCNode* m_previewCanvasContent = nullptr;
    CCLabelBMFont* m_previewSubInfoLabel = nullptr;
    CCLabelBMFont* m_limitValueLabel = nullptr;
    CCScale9Sprite* m_insideTabBg = nullptr;
    CCScale9Sprite* m_overhangTabBg = nullptr;
    CCLabelBMFont* m_anotherVariantLabel = nullptr;

    bool init(EditorUI* ui, ContourAnalysis contour, bool forceApiGate) {
        if (!Popup::init(490.f, 286.f)) return false;

        g_activePopup = this;
        m_editorUI = ui;
        m_contour = std::move(contour);

        loadAndVerifySavedApiFromDisk();
        m_providerIdx = g_apiProviderIndex;

        // Строго не пускаем в мод, пока не введен и не проверен достоверный API-ключ!
        m_isApiGateStage = forceApiGate || !g_apiVerifiedAndConnected;
        m_russianLayout = !m_isApiGateStage;

        if (m_isApiGateStage && !g_connectedApiKey.empty()) {
            m_inputBuffer = g_connectedApiKey;
        }

        if constexpr (requires { this->m_bgSprite; }) {
            if (this->m_bgSprite) {
                this->m_bgSprite->setColor(ccColor3B{ 15, 16, 20 });
            }
        }

        auto size = m_mainLayer->getContentSize();
        auto center = CCPoint{ size.width / 2.f, size.height / 2.f };

        // Темно-угольный корпус окна в стиле Art Agent
        auto outerFrame = CCScale9Sprite::create("square02b_001.png");
        outerFrame->setContentSize(CCSize{ size.width, size.height });
        outerFrame->setColor(ccColor3B{ 42, 44, 52 });
        outerFrame->setPosition(center);
        outerFrame->setZOrder(-2);
        m_mainLayer->addChild(outerFrame);

        auto innerDark = CCScale9Sprite::create("square02b_001.png");
        innerDark->setContentSize(CCSize{ size.width - 4.f, size.height - 4.f });
        innerDark->setColor(ccColor3B{ 15, 16, 20 });
        innerDark->setPosition(center);
        innerDark->setZOrder(-1);
        m_mainLayer->addChild(innerDark);

        if (m_isApiGateStage) {
            setupStrictApiGateUI(center);
        } else {
            setupArtAgentMainUI(center);
        }

        for (int k = 0; k < 256; ++k) {
#ifdef GEODE_IS_WINDOWS
            m_prevKeyStates[k] = (GetAsyncKeyState(k) & 0x8000) != 0;
#else
            m_prevKeyStates[k] = false;
#endif
        }

        this->schedule(schedule_selector(AIDecorPopup::updateKeyboardAndLock), 0.0f);
        return true;
    }

    void createCheckmarkBadge(CCPoint pos, bool checked) {
        m_checkBoxBg = CCScale9Sprite::create("square02b_001.png");
        m_checkBoxBg->setContentSize(CCSize{ 20.f, 20.f });
        m_checkBoxBg->setPosition(pos);
        m_mainLayer->addChild(m_checkBoxBg);

        m_checkTickContainer = CCNode::create();
        m_checkTickContainer->setPosition(pos);
        m_mainLayer->addChild(m_checkTickContainer);

        auto shortLeg = CCScale9Sprite::create("square02b_001.png");
        shortLeg->setContentSize(CCSize{ 3.4f, 7.8f });
        shortLeg->setColor(ccColor3B{ 255, 255, 255 });
        shortLeg->setRotation(-40.f);
        shortLeg->setPosition(CCPoint{ -2.8f, -1.4f });
        m_checkTickContainer->addChild(shortLeg);

        auto longLeg = CCScale9Sprite::create("square02b_001.png");
        longLeg->setContentSize(CCSize{ 3.4f, 12.8f });
        longLeg->setColor(ccColor3B{ 255, 255, 255 });
        longLeg->setRotation(38.f);
        longLeg->setPosition(CCPoint{ 2.4f, 1.0f });
        m_checkTickContainer->addChild(longLeg);

        setCheckmarkState(checked);
    }

    void setCheckmarkState(bool checked) {
        if (m_checkBoxBg) {
            m_checkBoxBg->setColor(checked ? ccColor3B{ 42, 205, 95 } : ccColor3B{ 58, 60, 68 });
        }
        if (m_checkTickContainer) {
            m_checkTickContainer->setScale(checked ? 1.0f : 0.0f);
        }
    }

    void restoreLockedContourPositions() {
        for (auto const& st : m_contour.lockedStates) {
            if (st.obj && st.obj->getParent()) {
                st.obj->setPosition(st.pos);
                st.obj->setRotation(st.rotation);
            }
        }
    }

    // ========================================================================
    // ЭКРАН 1: СТРОГАЯ ПРОВЕРКА ДОСТОВЕРНОСТИ API-КЛЮЧА ПЕРЕД ВХОДОМ В МОД
    // ========================================================================
    void setupStrictApiGateUI(CCPoint center) {
        this->setTitle("ART AGENT — PODKLYUCHENIE II PO API");

        if (g_apiVerifiedAndConnected) {
            auto backSpr = ButtonSprite::create("Back");
            backSpr->setScale(0.50f);
            auto backBtn = CCMenuItemSpriteExtra::create(backSpr, this, menu_selector(AIDecorPopup::onBackToArtAgent));
            backBtn->setPosition(CCPoint{ center.x + 190.f, center.y + 118.f });
            m_buttonMenu->addChild(backBtn);
        }

        auto cardBg = CCScale9Sprite::create("square02b_001.png");
        cardBg->setContentSize(CCSize{ 430.f, 110.f });
        cardBg->setColor(ccColor3B{ 24, 25, 31 });
        cardBg->setPosition(CCPoint{ center.x, center.y + 30.f });
        m_mainLayer->addChild(cardBg);

        createCheckmarkBadge(CCPoint{ center.x - 190.f, center.y + 62.f }, g_apiVerifiedAndConnected);

        std::string initStatus = g_apiVerifiedAndConnected
            ? fmt::format("Dostoverniy API podtverzhden: {} ({})", kApiProviders[m_providerIdx].name, maskApiKeyPreview(g_connectedApiKey))
            : "Vhod zakrit: Vvedite dostoverniy API-klyuch neyroseti dlya proverki!";
        m_apiGateStatusLabel = CCLabelBMFont::create(initStatus.c_str(), "chatFont.fnt");
        m_apiGateStatusLabel->setScale(0.54f);
        m_apiGateStatusLabel->setColor(g_apiVerifiedAndConnected ? ccColor3B{ 110, 245, 155 } : ccColor3B{ 240, 180, 115 });
        m_apiGateStatusLabel->setPosition(CCPoint{ center.x + 10.f, center.y + 62.f });
        m_mainLayer->addChild(m_apiGateStatusLabel);

        // Кнопка переключения провайдера (OpenAI / Gemini / DeepSeek / Groq / OpenRouter)
        auto provBg = CCScale9Sprite::create("square02b_001.png");
        provBg->setContentSize(CCSize{ 300.f, 26.f });
        provBg->setColor(ccColor3B{ 38, 40, 48 });
        std::string provText = fmt::format("Provider: {} (Click to switch)", kApiProviders[m_providerIdx].name);
        m_providerBtnLabel = CCLabelBMFont::create(provText.c_str(), "chatFont.fnt");
        m_providerBtnLabel->setScale(0.55f);
        m_providerBtnLabel->setColor(ccColor3B{ 245, 248, 255 });
        m_providerBtnLabel->setPosition(CCPoint{ 150.f, 13.f });
        provBg->addChild(m_providerBtnLabel);

        auto provBtn = CCMenuItemSpriteExtra::create(provBg, this, menu_selector(AIDecorPopup::onCycleApiProvider));
        provBtn->setPosition(CCPoint{ center.x, center.y + 24.f });
        m_buttonMenu->addChild(provBtn);

        auto hintLbl = CCLabelBMFont::create(
            "Mod proveryaet klyuch cherez realniy HTTPS-zapros k API pered otkritiem!",
            "chatFont.fnt"
        );
        hintLbl->setScale(0.48f);
        hintLbl->setColor(ccColor3B{ 155, 162, 175 });
        hintLbl->setPosition(CCPoint{ center.x, center.y - 8.f });
        m_mainLayer->addChild(hintLbl);

        // Поле ввода API-ключа
        auto inputBg = CCScale9Sprite::create("square02b_001.png");
        inputBg->setContentSize(CCSize{ 430.f, 34.f });
        inputBg->setColor(ccColor3B{ 22, 23, 28 });
        inputBg->setPosition(CCPoint{ center.x, center.y - 52.f });
        m_mainLayer->addChild(inputBg);

        m_inputDisplayLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_inputDisplayLabel->setScale(0.56f);
        m_inputDisplayLabel->setPosition(CCPoint{ center.x, center.y - 52.f });
        m_mainLayer->addChild(m_inputDisplayLabel);
        refreshInputLabel();

        // Кнопки [Paste Ctrl+V] [Clear] [Verify & Connect API]
        auto pasteBg = CCScale9Sprite::create("square02b_001.png");
        pasteBg->setContentSize(CCSize{ 125.f, 30.f });
        pasteBg->setColor(ccColor3B{ 38, 40, 48 });
        auto pasteLbl = CCLabelBMFont::create("Paste (Ctrl+V)", "chatFont.fnt");
        pasteLbl->setScale(0.52f);
        pasteLbl->setPosition(CCPoint{ 62.5f, 15.f });
        pasteBg->addChild(pasteLbl);
        auto pasteBtn = CCMenuItemSpriteExtra::create(pasteBg, this, menu_selector(AIDecorPopup::onPasteApiClicked));
        pasteBtn->setPosition(CCPoint{ center.x - 150.f, center.y - 102.f });
        m_buttonMenu->addChild(pasteBtn);

        auto clearBg = CCScale9Sprite::create("square02b_001.png");
        clearBg->setContentSize(CCSize{ 85.f, 30.f });
        clearBg->setColor(ccColor3B{ 38, 40, 48 });
        auto clearLbl = CCLabelBMFont::create("Clear", "chatFont.fnt");
        clearLbl->setScale(0.52f);
        clearLbl->setPosition(CCPoint{ 42.5f, 15.f });
        clearBg->addChild(clearLbl);
        auto clearBtn = CCMenuItemSpriteExtra::create(clearBg, this, menu_selector(AIDecorPopup::onClearApiClicked));
        clearBtn->setPosition(CCPoint{ center.x - 32.f, center.y - 102.f });
        m_buttonMenu->addChild(clearBtn);

        auto verifyBg = CCScale9Sprite::create("square02b_001.png");
        verifyBg->setContentSize(CCSize{ 185.f, 30.f });
        verifyBg->setColor(ccColor3B{ 225, 58, 92 });
        auto verifyLbl = CCLabelBMFont::create("Verify & Connect API", "chatFont.fnt");
        verifyLbl->setScale(0.54f);
        verifyLbl->setColor(ccColor3B{ 255, 255, 255 });
        verifyLbl->setPosition(CCPoint{ 92.5f, 15.f });
        verifyBg->addChild(verifyLbl);
        auto verifyBtn = CCMenuItemSpriteExtra::create(verifyBg, this, menu_selector(AIDecorPopup::onVerifyAndConnectApi));
        verifyBtn->setPosition(CCPoint{ center.x + 120.f, center.y - 102.f });
        m_buttonMenu->addChild(verifyBtn);
    }

    // ========================================================================
    // ЭКРАН 2: ОСНОВНОЙ ИНТЕРФЕЙС "ART AGENT" (1-в-1 по скриншотам!)
    // ========================================================================
    void setupArtAgentMainUI(CCPoint center) {
        this->setTitle("");

        // --- ВЕРХНЯЯ ПАНЕЛЬ (Шапка Art Agent) ---
        auto headerTitle = CCLabelBMFont::create("Art Agent", "chatFont.fnt");
        headerTitle->setScale(0.60f);
        headerTitle->setColor(ccColor3B{ 240, 242, 248 });
        headerTitle->setPosition(CCPoint{ center.x, center.y + 125.f });
        m_mainLayer->addChild(headerTitle);

        // Левая верхняя капсула: [✓ API: OpenAI] + розовая кнопка [+] для смены API!
        auto balPill = CCScale9Sprite::create("square02b_001.png");
        balPill->setContentSize(CCSize{ 118.f, 22.f });
        balPill->setColor(ccColor3B{ 26, 27, 33 });
        balPill->setPosition(CCPoint{ center.x - 174.f, center.y + 125.f });
        m_mainLayer->addChild(balPill);

        createCheckmarkBadge(CCPoint{ center.x - 220.f, center.y + 125.f }, true);

        std::string apiTag = fmt::format("API {}", kApiProviders[m_providerIdx].name);
        auto apiTagLbl = CCLabelBMFont::create(apiTag.c_str(), "chatFont.fnt");
        apiTagLbl->setScale(0.46f);
        apiTagLbl->setColor(ccColor3B{ 225, 230, 240 });
        apiTagLbl->setPosition(CCPoint{ center.x - 176.f, center.y + 125.f });
        m_mainLayer->addChild(apiTagLbl);

        auto plusBg = CCScale9Sprite::create("square02b_001.png");
        plusBg->setContentSize(CCSize{ 24.f, 18.f });
        plusBg->setColor(ccColor3B{ 232, 65, 98 });
        auto plusLbl = CCLabelBMFont::create("+", "bigFont.fnt");
        plusLbl->setScale(0.38f);
        plusLbl->setPosition(CCPoint{ 12.f, 10.f });
        plusBg->addChild(plusLbl);
        auto changeApiBtn = CCMenuItemSpriteExtra::create(plusBg, this, menu_selector(AIDecorPopup::onOpenApiGate));
        changeApiBtn->setPosition(CCPoint{ center.x - 129.f, center.y + 125.f });
        m_buttonMenu->addChild(changeApiBtn);

        // Кнопка раскладки [RUS/ENG] справа вверху
        auto langBg = CCScale9Sprite::create("square02b_001.png");
        langBg->setContentSize(CCSize{ 44.f, 18.f });
        langBg->setColor(ccColor3B{ 34, 36, 44 });
        m_langBadgeLabel = CCLabelBMFont::create("RUS", "chatFont.fnt");
        m_langBadgeLabel->setScale(0.45f);
        m_langBadgeLabel->setColor(ccColor3B{ 235, 240, 250 });
        m_langBadgeLabel->setPosition(CCPoint{ 22.f, 9.f });
        langBg->addChild(m_langBadgeLabel);
        auto langBtn = CCMenuItemSpriteExtra::create(langBg, this, menu_selector(AIDecorPopup::onToggleLanguage));
        langBtn->setPosition(CCPoint{ center.x + 188.f, center.y + 125.f });
        m_buttonMenu->addChild(langBtn);

        // Тонкая разделительная линия шапки и центральной колонки
        auto topDivider = CCScale9Sprite::create("square02b_001.png");
        topDivider->setContentSize(CCSize{ 482.f, 1.2f });
        topDivider->setColor(ccColor3B{ 34, 36, 42 });
        topDivider->setPosition(CCPoint{ center.x, center.y + 109.f });
        m_mainLayer->addChild(topDivider);

        auto midDivider = CCScale9Sprite::create("square02b_001.png");
        midDivider->setContentSize(CCSize{ 1.2f, 246.f });
        midDivider->setColor(ccColor3B{ 34, 36, 42 });
        midDivider->setPosition(CCPoint{ center.x - 4.f, center.y - 14.f });
        m_mainLayer->addChild(midDivider);

        // ====================================================================
        // ЛЕВАЯ КОЛОНКА: Живой ИИ-Агент + Карточка выделения + Статус шагов
        // ====================================================================
        float leftX = center.x - 122.f;

        // 1. Верхняя карточка ответа ИИ-Агента
        auto agentCard = CCScale9Sprite::create("square02b_001.png");
        agentCard->setContentSize(CCSize{ 222.f, 64.f });
        agentCard->setColor(ccColor3B{ 24, 25, 30 });
        agentCard->setPosition(CCPoint{ leftX, center.y + 68.f });
        m_mainLayer->addChild(agentCard);

        auto agentTitle = CCLabelBMFont::create("Agent", "chatFont.fnt");
        agentTitle->setScale(0.44f);
        agentTitle->setColor(ccColor3B{ 130, 135, 148 });
        agentTitle->setPosition(CCPoint{ leftX - 88.f, center.y + 90.f });
        m_mainLayer->addChild(agentTitle);

        std::string initMsg1 = fmt::format(
            "Tell me what to draw on the {} selected blocks.",
            m_contour.cellCount()
        );
        m_agentReplyLine1 = CCLabelBMFont::create(initMsg1.c_str(), "chatFont.fnt");
        m_agentReplyLine1->setScale(0.46f);
        m_agentReplyLine1->setColor(ccColor3B{ 235, 238, 245 });
        m_agentReplyLine1->setPosition(CCPoint{ leftX, center.y + 73.f });
        m_mainLayer->addChild(m_agentReplyLine1);

        m_agentReplyLine2 = CCLabelBMFont::create("You can write in Russian or English.", "chatFont.fnt");
        m_agentReplyLine2->setScale(0.46f);
        m_agentReplyLine2->setColor(ccColor3B{ 210, 215, 225 });
        m_agentReplyLine2->setPosition(CCPoint{ leftX, center.y + 58.f });
        m_mainLayer->addChild(m_agentReplyLine2);

        m_agentReplyLine3 = CCLabelBMFont::create("", "chatFont.fnt");
        m_agentReplyLine3->setScale(0.44f);
        m_agentReplyLine3->setColor(ccColor3B{ 185, 192, 205 });
        m_agentReplyLine3->setPosition(CCPoint{ leftX, center.y + 44.f });
        m_mainLayer->addChild(m_agentReplyLine3);

        // 2. Средняя карточка запроса пользователя и статуса шагов (Shape / Plan / Drawing / Vector / Place)
        auto stepsCard = CCScale9Sprite::create("square02b_001.png");
        stepsCard->setContentSize(CCSize{ 222.f, 92.f });
        stepsCard->setColor(ccColor3B{ 22, 23, 28 });
        stepsCard->setPosition(CCPoint{ leftX, center.y - 16.f });
        m_mainLayer->addChild(stepsCard);

        // Мини-иконка выделенной формы (синий фон + зеленая форма, как на скриншоте 2)
        auto miniThumbBg = CCScale9Sprite::create("square02b_001.png");
        miniThumbBg->setContentSize(CCSize{ 34.f, 22.f });
        miniThumbBg->setColor(ccColor3B{ 22, 92, 210 });
        miniThumbBg->setPosition(CCPoint{ leftX - 86.f, center.y + 14.f });
        m_mainLayer->addChild(miniThumbBg);

        auto miniShapeIcon = CCScale9Sprite::create("square02b_001.png");
        miniShapeIcon->setContentSize(CCSize{ 16.f, 14.f });
        miniShapeIcon->setColor(ccColor3B{ 35, 210, 45 });
        miniShapeIcon->setPosition(CCPoint{ leftX - 86.f, center.y + 14.f });
        m_mainLayer->addChild(miniShapeIcon);

        auto selTitle = CCLabelBMFont::create("Selection", "chatFont.fnt");
        selTitle->setScale(0.44f);
        selTitle->setColor(ccColor3B{ 232, 82, 105 });
        selTitle->setPosition(CCPoint{ leftX - 42.f, center.y + 19.f });
        m_mainLayer->addChild(selTitle);

        std::string selObjStr = fmt::format("{} objects ({}x{})", m_contour.cellCount(), m_contour.widthUnits(), m_contour.heightUnits());
        m_userPromptLine1 = CCLabelBMFont::create(selObjStr.c_str(), "chatFont.fnt");
        m_userPromptLine1->setScale(0.43f);
        m_userPromptLine1->setColor(ccColor3B{ 185, 190, 202 });
        m_userPromptLine1->setPosition(CCPoint{ leftX - 16.f, center.y + 8.f });
        m_mainLayer->addChild(m_userPromptLine1);

        m_userPromptLine2 = CCLabelBMFont::create("Prompt: waiting for your message...", "chatFont.fnt");
        m_userPromptLine2->setScale(0.43f);
        m_userPromptLine2->setColor(ccColor3B{ 220, 225, 235 });
        m_userPromptLine2->setPosition(CCPoint{ leftX, center.y - 8.f });
        m_mainLayer->addChild(m_userPromptLine2);

        // Чеклист прогресса (Shape / Plan / Drawing / Vector / Place) с розовыми индикаторами
        auto addStepRow = [&](float yPos, const char* stepName, std::string const& rightVal) -> CCLabelBMFont* {
            auto dot = CCScale9Sprite::create("square02b_001.png");
            dot->setContentSize(CCSize{ 7.f, 7.f });
            dot->setColor(ccColor3B{ 232, 65, 98 });
            dot->setPosition(CCPoint{ leftX - 98.f, yPos });
            m_mainLayer->addChild(dot);

            auto nameL = CCLabelBMFont::create(stepName, "chatFont.fnt");
            nameL->setScale(0.41f);
            nameL->setColor(ccColor3B{ 215, 220, 230 });
            nameL->setPosition(CCPoint{ leftX - 68.f, yPos });
            m_mainLayer->addChild(nameL);

            auto valL = CCLabelBMFont::create(rightVal.c_str(), "chatFont.fnt");
            valL->setScale(0.41f);
            valL->setColor(ccColor3B{ 140, 146, 160 });
            valL->setPosition(CCPoint{ leftX + 26.f, yPos });
            m_mainLayer->addChild(valL);
            return valL;
        };

        m_stepPlanLabel    = addStepRow(center.y - 23.f, "Plan",    "ready");
        m_stepDrawingLabel = addStepRow(center.y - 35.f, "Drawing", "waiting");
        m_stepVectorLabel  = addStepRow(center.y - 47.f, "Vector",  "waiting");
        m_stepPlaceLabel   = addStepRow(center.y - 59.f, "Place",   "waiting");

        // 3. Строка быстрых примеров (For example: [Stone with snow] [Tree with glow] [Crystal] [3D + Neon])
        auto exHeader = CCLabelBMFont::create("For example", "chatFont.fnt");
        exHeader->setScale(0.40f);
        exHeader->setColor(ccColor3B{ 130, 135, 148 });
        exHeader->setPosition(CCPoint{ leftX - 88.f, center.y - 72.f });
        m_mainLayer->addChild(exHeader);

        auto addExampleChip = [&](const char* label, CCPoint pos, SEL_MenuHandler cb) {
            auto chipBg = CCScale9Sprite::create("square02b_001.png");
            chipBg->setContentSize(CCSize{ 52.f, 15.f });
            chipBg->setColor(ccColor3B{ 30, 32, 38 });
            auto chipLbl = CCLabelBMFont::create(label, "chatFont.fnt");
            chipLbl->setScale(0.34f);
            chipLbl->setColor(ccColor3B{ 215, 220, 230 });
            chipLbl->setPosition(CCPoint{ 26.f, 7.5f });
            chipBg->addChild(chipLbl);
            auto btn = CCMenuItemSpriteExtra::create(chipBg, this, cb);
            btn->setPosition(pos);
            m_buttonMenu->addChild(btn);
        };

        addExampleChip("Stone snow", CCPoint{ leftX - 83.f, center.y - 86.f }, menu_selector(AIDecorPopup::onPresetStoneSnow));
        addExampleChip("Tree glow",  CCPoint{ leftX - 28.f, center.y - 86.f }, menu_selector(AIDecorPopup::onPresetTreeGlow));
        addExampleChip("Crystal",    CCPoint{ leftX + 27.f, center.y - 86.f }, menu_selector(AIDecorPopup::onPresetCrystal));
        addExampleChip("Only 3D",    CCPoint{ leftX + 82.f, center.y - 86.f }, menu_selector(AIDecorPopup::onPresetOnly3D));

        // 4. Нижняя строка ввода с голубым неоновым бликом сверху и розовой кнопкой [ > ]
        auto glowStrip = CCScale9Sprite::create("square02b_001.png");
        glowStrip->setContentSize(CCSize{ 160.f, 2.f });
        glowStrip->setColor(ccColor3B{ 110, 215, 255 });
        glowStrip->setPosition(CCPoint{ leftX - 15.f, center.y - 102.f });
        m_mainLayer->addChild(glowStrip);

        auto inputBarBg = CCScale9Sprite::create("square02b_001.png");
        inputBarBg->setContentSize(CCSize{ 222.f, 28.f });
        inputBarBg->setColor(ccColor3B{ 24, 25, 30 });
        inputBarBg->setPosition(CCPoint{ leftX, center.y - 116.f });
        m_mainLayer->addChild(inputBarBg);

        m_inputDisplayLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_inputDisplayLabel->setScale(0.46f);
        m_inputDisplayLabel->setPosition(CCPoint{ leftX - 14.f, center.y - 116.f });
        m_mainLayer->addChild(m_inputDisplayLabel);
        refreshInputLabel();

        auto sendArrowBg = CCScale9Sprite::create("square02b_001.png");
        sendArrowBg->setContentSize(CCSize{ 26.f, 22.f });
        sendArrowBg->setColor(ccColor3B{ 195, 48, 78 });
        auto sendArrowLbl = CCLabelBMFont::create(">", "bigFont.fnt");
        sendArrowLbl->setScale(0.42f);
        sendArrowLbl->setPosition(CCPoint{ 13.f, 12.f });
        sendArrowBg->addChild(sendArrowLbl);
        auto sendBtn = CCMenuItemSpriteExtra::create(sendArrowBg, this, menu_selector(AIDecorPopup::onSendToNeuralNet));
        sendBtn->setPosition(CCPoint{ leftX + 94.f, center.y - 116.f });
        m_buttonMenu->addChild(sendBtn);

        // ====================================================================
        // ПРАВАЯ КОЛОНКА: Вкладки [Shape | Drawing | In GD] + Синее превью + Limit + Keep
        // ====================================================================
        float rightX = center.x + 118.f;

        auto tabBarBg = CCScale9Sprite::create("square02b_001.png");
        tabBarBg->setContentSize(CCSize{ 224.f, 22.f });
        tabBarBg->setColor(ccColor3B{ 24, 25, 30 });
        tabBarBg->setPosition(CCPoint{ rightX, center.y + 92.f });
        m_mainLayer->addChild(tabBarBg);

        auto createTabButton = [&](const char* title, CCPoint pos, SEL_MenuHandler cb, CCScale9Sprite*& outBg) {
            outBg = CCScale9Sprite::create("square02b_001.png");
            outBg->setContentSize(CCSize{ 72.f, 18.f });
            auto lbl = CCLabelBMFont::create(title, "chatFont.fnt");
            lbl->setScale(0.45f);
            lbl->setColor(ccColor3B{ 245, 248, 255 });
            lbl->setPosition(CCPoint{ 36.f, 9.f });
            outBg->addChild(lbl);
            auto btn = CCMenuItemSpriteExtra::create(outBg, this, cb);
            btn->setPosition(pos);
            m_buttonMenu->addChild(btn);
        };

        createTabButton("Shape",   CCPoint{ rightX - 73.f, center.y + 92.f }, menu_selector(AIDecorPopup::onTabShape),   m_tabShapeBg);
        createTabButton("Drawing", CCPoint{ rightX,        center.y + 92.f }, menu_selector(AIDecorPopup::onTabDrawing), m_tabDrawingBg);
        createTabButton("In GD",   CCPoint{ rightX + 73.f, center.y + 92.f }, menu_selector(AIDecorPopup::onTabInGD),    m_tabInGDBg);

        // Синее превью-полотно в стиле редактора GD (как на обоих скриншотах!)
        auto canvasBg = CCScale9Sprite::create("square02b_001.png");
        canvasBg->setContentSize(CCSize{ 224.f, 128.f });
        canvasBg->setColor(ccColor3B{ 18, 92, 212 });
        canvasBg->setPosition(CCPoint{ rightX, center.y + 10.f });
        m_mainLayer->addChild(canvasBg);

        // Светящаяся голубая рамка выделения внутри синего превью (как на скриншоте 1)
        auto selectionFrame = CCScale9Sprite::create("square02b_001.png");
        selectionFrame->setContentSize(CCSize{ 166.f, 108.f });
        selectionFrame->setColor(ccColor3B{ 95, 195, 255 });
        selectionFrame->setOpacity(95);
        selectionFrame->setPosition(CCPoint{ rightX, center.y + 10.f });
        m_mainLayer->addChild(selectionFrame);

        auto selectionInner = CCScale9Sprite::create("square02b_001.png");
        selectionInner->setContentSize(CCSize{ 162.f, 104.f });
        selectionInner->setColor(ccColor3B{ 18, 92, 212 });
        selectionInner->setPosition(CCPoint{ rightX, center.y + 10.f });
        m_mainLayer->addChild(selectionInner);

        m_previewCanvasContent = CCNode::create();
        m_previewCanvasContent->setPosition(CCPoint{ rightX, center.y + 10.f });
        m_mainLayer->addChild(m_previewCanvasContent);

        // Строка размеров под превью: "180 x 180 units | 30 blocks"
        std::string dimText = fmt::format(
            "{} x {} units  |  {} blocks",
            m_contour.widthUnits(), m_contour.heightUnits(), m_contour.cellCount()
        );
        m_previewSubInfoLabel = CCLabelBMFont::create(dimText.c_str(), "chatFont.fnt");
        m_previewSubInfoLabel->setScale(0.44f);
        m_previewSubInfoLabel->setColor(ccColor3B{ 170, 176, 190 });
        m_previewSubInfoLabel->setPosition(CCPoint{ rightX, center.y - 62.f });
        m_mainLayer->addChild(m_previewSubInfoLabel);

        // Кнопки [< Limit 16000 >] и [Inside | Overhang]
        auto limitPill = CCScale9Sprite::create("square02b_001.png");
        limitPill->setContentSize(CCSize{ 108.f, 22.f });
        limitPill->setColor(ccColor3B{ 24, 25, 30 });
        limitPill->setPosition(CCPoint{ rightX - 58.f, center.y - 84.f });
        m_mainLayer->addChild(limitPill);

        std::string limStr = fmt::format("Limit  {}", g_objectLimit);
        m_limitValueLabel = CCLabelBMFont::create(limStr.c_str(), "chatFont.fnt");
        m_limitValueLabel->setScale(0.42f);
        m_limitValueLabel->setColor(ccColor3B{ 230, 235, 245 });
        m_limitValueLabel->setPosition(CCPoint{ rightX - 58.f, center.y - 84.f });
        m_mainLayer->addChild(m_limitValueLabel);

        auto makeSmallNavBtn = [&](const char* txt, CCPoint pos, SEL_MenuHandler cb) {
            auto bg = CCScale9Sprite::create("square02b_001.png");
            bg->setContentSize(CCSize{ 18.f, 18.f });
            bg->setColor(ccColor3B{ 38, 40, 48 });
            auto l = CCLabelBMFont::create(txt, "chatFont.fnt");
            l->setScale(0.45f);
            l->setPosition(CCPoint{ 9.f, 9.f });
            bg->addChild(l);
            auto b = CCMenuItemSpriteExtra::create(bg, this, cb);
            b->setPosition(pos);
            m_buttonMenu->addChild(b);
        };
        makeSmallNavBtn("<", CCPoint{ rightX - 101.f, center.y - 84.f }, menu_selector(AIDecorPopup::onLimitDec));
        makeSmallNavBtn(">", CCPoint{ rightX - 15.f,  center.y - 84.f }, menu_selector(AIDecorPopup::onLimitInc));

        // Переключатель [Inside | Overhang]
        auto modePill = CCScale9Sprite::create("square02b_001.png");
        modePill->setContentSize(CCSize{ 108.f, 22.f });
        modePill->setColor(ccColor3B{ 24, 25, 30 });
        modePill->setPosition(CCPoint{ rightX + 58.f, center.y - 84.f });
        m_mainLayer->addChild(modePill);

        auto createModeHalf = [&](const char* txt, CCPoint pos, SEL_MenuHandler cb, CCScale9Sprite*& outBg) {
            outBg = CCScale9Sprite::create("square02b_001.png");
            outBg->setContentSize(CCSize{ 51.f, 18.f });
            auto l = CCLabelBMFont::create(txt, "chatFont.fnt");
            l->setScale(0.40f);
            l->setColor(ccColor3B{ 245, 248, 255 });
            l->setPosition(CCPoint{ 25.5f, 9.f });
            outBg->addChild(l);
            auto b = CCMenuItemSpriteExtra::create(outBg, this, cb);
            b->setPosition(pos);
            m_buttonMenu->addChild(b);
        };
        createModeHalf("Inside",   CCPoint{ rightX + 32.f, center.y - 84.f }, menu_selector(AIDecorPopup::onSelectInside),   m_insideTabBg);
        createModeHalf("Overhang", CCPoint{ rightX + 84.f, center.y - 84.f }, menu_selector(AIDecorPopup::onSelectOverhang), m_overhangTabBg);

        // Нижние кнопки: [Another variant (1)] и [Keep]
        auto varBg = CCScale9Sprite::create("square02b_001.png");
        varBg->setContentSize(CCSize{ 108.f, 28.f });
        varBg->setColor(ccColor3B{ 26, 27, 33 });
        std::string varStr = fmt::format("Another variant ({})", g_variantIndex);
        m_anotherVariantLabel = CCLabelBMFont::create(varStr.c_str(), "chatFont.fnt");
        m_anotherVariantLabel->setScale(0.42f);
        m_anotherVariantLabel->setColor(ccColor3B{ 205, 210, 222 });
        m_anotherVariantLabel->setPosition(CCPoint{ 54.f, 14.f });
        varBg->addChild(m_anotherVariantLabel);
        auto varBtn = CCMenuItemSpriteExtra::create(varBg, this, menu_selector(AIDecorPopup::onAnotherVariantClicked));
        varBtn->setPosition(CCPoint{ rightX - 58.f, center.y - 116.f });
        m_buttonMenu->addChild(varBtn);

        auto keepBg = CCScale9Sprite::create("square02b_001.png");
        keepBg->setContentSize(CCSize{ 108.f, 28.f });
        keepBg->setColor(ccColor3B{ 148, 38, 66 });
        auto keepLbl = CCLabelBMFont::create("Keep", "chatFont.fnt");
        keepLbl->setScale(0.50f);
        keepLbl->setColor(ccColor3B{ 255, 240, 245 });
        keepLbl->setPosition(CCPoint{ 54.f, 14.f });
        keepBg->addChild(keepLbl);
        auto keepBtn = CCMenuItemSpriteExtra::create(keepBg, this, menu_selector(AIDecorPopup::onKeepClicked));
        keepBtn->setPosition(CCPoint{ rightX + 58.f, center.y - 116.f });
        m_buttonMenu->addChild(keepBtn);

        refreshRightPanelTabsAndCanvas();
    }

    // Отрисовка мини-превью выбранной фигуры (Shape) или готового арта (Drawing / In GD) на синем полотне!
    void refreshRightPanelTabsAndCanvas() {
        if (m_tabShapeBg)   m_tabShapeBg->setColor((m_previewTab == 0) ? ccColor3B{ 232, 65, 98 } : ccColor3B{ 24, 25, 30 });
        if (m_tabDrawingBg) m_tabDrawingBg->setColor((m_previewTab == 1) ? ccColor3B{ 232, 65, 98 } : ccColor3B{ 24, 25, 30 });
        if (m_tabInGDBg)    m_tabInGDBg->setColor((m_previewTab == 2) ? ccColor3B{ 232, 65, 98 } : ccColor3B{ 24, 25, 30 });

        if (m_insideTabBg)   m_insideTabBg->setColor(!g_overhangAllowed ? ccColor3B{ 232, 65, 98 } : ccColor3B{ 24, 25, 30 });
        if (m_overhangTabBg) m_overhangTabBg->setColor(g_overhangAllowed  ? ccColor3B{ 232, 65, 98 } : ccColor3B{ 24, 25, 30 });

        if (!m_previewCanvasContent) return;

        // Очищаем старый контент превью, заменяя контейнер новым узлом
        CCPoint canvasPos = m_previewCanvasContent->getPosition();
        m_previewCanvasContent->setScale(0.f);
        m_previewCanvasContent = CCNode::create();
        m_previewCanvasContent->setPosition(canvasPos);
        m_mainLayer->addChild(m_previewCanvasContent);

        int cols = (std::max)(1, m_contour.maxGX - m_contour.minGX + 1);
        int rows = (std::max)(1, m_contour.maxGY - m_contour.minGY + 1);
        float cellPx = (std::min)(136.f / cols, 88.f / rows);
        cellPx = std::clamp(cellPx, 8.f, 26.f);

        float totalW = cols * cellPx;
        float totalH = rows * cellPx;
        float startX = -totalW * 0.5f + cellPx * 0.5f;
        float startY = -totalH * 0.5f + cellPx * 0.5f;

        if (m_previewTab == 0 || !g_hasGeneratedArt) {
            // Вкладка "Shape": Белый силуэт выделенной игроком фигуры на синей сетке (как на скриншоте 1!)
            for (auto const& [gx, gy] : m_contour.occupiedCells) {
                float px = startX + (gx - m_contour.minGX) * cellPx;
                float py = startY + (gy - m_contour.minGY) * cellPx;
                auto blockSpr = CCScale9Sprite::create("square02b_001.png");
                blockSpr->setContentSize(CCSize{ cellPx + 0.8f, cellPx + 0.8f });
                blockSpr->setColor(ccColor3B{ 242, 245, 250 });
                blockSpr->setPosition(CCPoint{ px, py });
                m_previewCanvasContent->addChild(blockSpr);
            }
        } else {
            // Вкладка "Drawing" / "In GD": Показываем сгенерированный ИИ цветной арт внутри формы (как на скриншоте 2!)
            for (auto const& [gx, gy] : m_contour.occupiedCells) {
                float px = startX + (gx - m_contour.minGX) * cellPx;
                float py = startY + (gy - m_contour.minGY) * cellPx;

                auto fillSpr = CCScale9Sprite::create("square02b_001.png");
                fillSpr->setContentSize(CCSize{ cellPx + 0.5f, cellPx + 0.5f });
                fillSpr->setColor(g_lastPreviewPalette.slotColors[static_cast<int>(ColorSlot::DarkFill)]);
                fillSpr->setPosition(CCPoint{ px, py });
                m_previewCanvasContent->addChild(fillSpr);

                auto texSpr = CCScale9Sprite::create("square02b_001.png");
                texSpr->setContentSize(CCSize{ cellPx * 0.72f, cellPx * 0.72f });
                texSpr->setColor(g_lastPreviewPalette.slotColors[static_cast<int>(ColorSlot::MidTone)]);
                texSpr->setPosition(CCPoint{ px, py });
                m_previewCanvasContent->addChild(texSpr);
            }

            // Отображаем кастомные детали ИИ в превью
            float worldMinX = m_contour.minGX * 30.f;
            float worldMinY = m_contour.minGY * 30.f;
            float scaleRatio = cellPx / 30.f;

            size_t shownCount = 0;
            for (auto const& s : g_lastPreviewSpecs) {
                if (s.layerRole == LayerRole::Fill) continue;
                if (++shownCount > 55) break;

                float px = -totalW * 0.5f + (s.x - worldMinX) * scaleRatio;
                float py = -totalH * 0.5f + (s.y - worldMinY) * scaleRatio;

                auto detSpr = CCScale9Sprite::create("square02b_001.png");
                float w = std::clamp(s.scaleX * cellPx * 0.65f, 2.5f, cellPx * 1.1f);
                float h = std::clamp(s.scaleY * cellPx * 0.65f, 2.5f, cellPx * 1.1f);
                detSpr->setContentSize(CCSize{ w, h });
                detSpr->setRotation(s.rotation);

                ccColor3B col = (s.colorSlot == ColorSlot::CustomRGB)
                    ? s.customColor
                    : g_lastPreviewPalette.slotColors[std::clamp(static_cast<int>(s.colorSlot), 0, 5)];
                detSpr->setColor(col);
                detSpr->setPosition(CCPoint{ px, py });
                m_previewCanvasContent->addChild(detSpr);
            }
        }
    }

    void refreshInputLabel() {
        if (!m_inputDisplayLabel) return;
        if (m_inputBuffer.empty()) {
            if (m_isApiGateStage) {
                m_inputDisplayLabel->setString("Paste valid API key (Ctrl+V) or type here...");
            } else {
                m_inputDisplayLabel->setString("Describe the art or ask AI...");
            }
            m_inputDisplayLabel->setColor(ccColor3B{ 125, 130, 145 });
        } else {
            std::string shown = m_inputBuffer;
            size_t maxLen = m_isApiGateStage ? 48 : 30;
            if (shown.size() > maxLen) {
                shown = "..." + shown.substr(shown.size() - (maxLen - 3));
            }
            shown += "|";
            m_inputDisplayLabel->setString(shown.c_str());
            m_inputDisplayLabel->setColor(ccColor3B{ 245, 248, 255 });
        }
    }

    void reopenPopup(bool forceApiGate) {
        EditorUI* ui = m_editorUI;
        ContourAnalysis contourCopy = m_contour;
        restoreLockedContourPositions();
        g_activePopup = nullptr;
        this->onClose(nullptr);
        if (auto* next = AIDecorPopup::create(ui, std::move(contourCopy), forceApiGate)) {
            next->show();
        }
    }

    // Обработчики экрана проверки API-ключа
    void onCycleApiProvider(CCObject*) {
        m_providerIdx = (m_providerIdx + 1) % 5;
        if (m_providerBtnLabel) {
            std::string provText = fmt::format("Provider: {} (Click to switch)", kApiProviders[m_providerIdx].name);
            m_providerBtnLabel->setString(provText.c_str());
        }
    }

    void onPasteApiClicked(CCObject*) {
        std::string clip = readWindowsClipboardText();
        if (!clip.empty()) {
            m_inputBuffer = clip;
            if (m_inputBuffer.size() > 180) m_inputBuffer.resize(180);
            m_providerIdx = detectProviderFromKey(m_inputBuffer, m_providerIdx);
            if (m_providerBtnLabel) {
                std::string provText = fmt::format("Provider: {} (Auto-detected)", kApiProviders[m_providerIdx].name);
                m_providerBtnLabel->setString(provText.c_str());
            }
            refreshInputLabel();
        }
    }

    void onClearApiClicked(CCObject*) {
        m_inputBuffer.clear();
        setCheckmarkState(false);
        refreshInputLabel();
    }

    void onVerifyAndConnectApi(CCObject*) {
        std::string key = trimWhitespace(m_inputBuffer);
        m_providerIdx = detectProviderFromKey(key, m_providerIdx);

        std::string errMsg;
        if (!verifyApiKeyWithServer(m_providerIdx, key, errMsg)) {
            setCheckmarkState(false);
            if (m_apiGateStatusLabel) {
                m_apiGateStatusLabel->setString(utf8ToGDReadable(errMsg).c_str());
                m_apiGateStatusLabel->setColor(ccColor3B{ 255, 95, 105 });
            }
            return;
        }

        // Ключ реально проверен сервером — загорается зеленая галочка и открывается Art Agent!
        setCheckmarkState(true);
        saveVerifiedApiToDisk(m_providerIdx, key);
        reopenPopup(false);
    }

    void onOpenApiGate(CCObject*) {
        reopenPopup(true);
    }

    void onBackToArtAgent(CCObject*) {
        if (g_apiVerifiedAndConnected) {
            reopenPopup(false);
        }
    }

    void onToggleLanguage(CCObject*) {
        m_russianLayout = !m_russianLayout;
        if (m_langBadgeLabel) {
            m_langBadgeLabel->setString(m_russianLayout ? "RUS" : "ENG");
        }
    }

    // Вкладки превью справа
    void onTabShape(CCObject*)   { m_previewTab = 0; refreshRightPanelTabsAndCanvas(); }
    void onTabDrawing(CCObject*) { m_previewTab = 1; refreshRightPanelTabsAndCanvas(); }
    void onTabInGD(CCObject*)    { m_previewTab = 2; refreshRightPanelTabsAndCanvas(); }

    // Регулятор лимита и режима Inside / Overhang
    void onLimitDec(CCObject*) {
        g_objectLimit = (std::max)(50, g_objectLimit - 1000);
        if (m_limitValueLabel) {
            std::string s = fmt::format("Limit  {}", g_objectLimit);
            m_limitValueLabel->setString(s.c_str());
        }
    }

    void onLimitInc(CCObject*) {
        g_objectLimit = (std::min)(40000, g_objectLimit + 1000);
        if (m_limitValueLabel) {
            std::string s = fmt::format("Limit  {}", g_objectLimit);
            m_limitValueLabel->setString(s.c_str());
        }
    }

    void onSelectInside(CCObject*)   { g_overhangAllowed = false; refreshRightPanelTabsAndCanvas(); }
    void onSelectOverhang(CCObject*) { g_overhangAllowed = true;  refreshRightPanelTabsAndCanvas(); }

    // Быстрые чипы-примеры (For example)
    void onPresetStoneSnow(CCObject*) { m_inputBuffer = "Stone with snow and icy glow"; refreshInputLabel(); onSendToNeuralNet(nullptr); }
    void onPresetTreeGlow(CCObject*)  { m_inputBuffer = "Tree with neon glow and leaves"; refreshInputLabel(); onSendToNeuralNet(nullptr); }
    void onPresetCrystal(CCObject*)   { m_inputBuffer = "Glowing crystal structure"; refreshInputLabel(); onSendToNeuralNet(nullptr); }
    void onPresetOnly3D(CCObject*)    { m_inputBuffer = "sdelay 3d"; refreshInputLabel(); onSendToNeuralNet(nullptr); }

    void onAnotherVariantClicked(CCObject*) {
        if (g_lastBuildPrompt.empty()) {
            m_inputBuffer = "Create a unique new block design variant";
        } else {
            m_inputBuffer = g_lastBuildPrompt;
        }
        g_variantIndex++;
        if (m_anotherVariantLabel) {
            std::string s = fmt::format("Another variant ({})", g_variantIndex);
            m_anotherVariantLabel->setString(s.c_str());
        }
        onSendToNeuralNet(nullptr);
    }

    void onKeepClicked(CCObject*) {
        this->onClose(nullptr);
    }

    // Отправка сообщения реальной подключенной ИИ-нейросети
    void onSendToNeuralNet(CCObject*) {
        restoreLockedContourPositions();

        std::string promptText = trimWhitespace(m_inputBuffer);
        if (promptText.empty()) return;

        m_inputBuffer.clear();
        refreshInputLabel();

        if (m_userPromptLine2) {
            std::string shortP = utf8ToGDReadable(promptText);
            if (shortP.size() > 42) shortP = shortP.substr(0, 39) + "...";
            m_userPromptLine2->setString(shortP.c_str());
        }

        // Вызываем реальную нейросеть по HTTPS!
        AIDecisionResult ai = queryRealNeuralNetwork(
            promptText,
            m_contour,
            g_objectLimit,
            g_overhangAllowed,
            g_variantIndex
        );

        // Выводим свободный ответ нейросети в карточку Agent (3 строки)
        auto replyLines = wrapTextLines(ai.replyText, 44, 3);
        if (m_agentReplyLine1) m_agentReplyLine1->setString(replyLines[0].c_str());
        if (m_agentReplyLine2) m_agentReplyLine2->setString(replyLines[1].c_str());
        if (m_agentReplyLine3) m_agentReplyLine3->setString(replyLines[2].c_str());

        if (!ai.httpSuccess) {
            if (m_stepPlanLabel) m_stepPlanLabel->setString("API request error");
            return;
        }

        // Если нейросеть решила, что это обычный диалог ("action": "chat") — ничего не строим!
        if (ai.taskMode == AITaskMode::ChatOnly) {
            if (m_stepPlanLabel)    m_stepPlanLabel->setString("chat response");
            if (m_stepDrawingLabel) m_stepDrawingLabel->setString("idle");
            if (m_stepVectorLabel)  m_stepVectorLabel->setString("idle");
            if (m_stepPlaceLabel)   m_stepPlaceLabel->setString("no blocks placed");
            return;
        }

        // Если нейросеть решила отменить постройку ("action": "undo")
        if (ai.taskMode == AITaskMode::UndoLast) {
            int rem = clearLastPlacedObjects();
            g_hasGeneratedArt = false;
            m_previewTab = 0;
            refreshRightPanelTabsAndCanvas();
            if (m_stepPlaceLabel) {
                std::string uStr = fmt::format("removed {} objects", rem);
                m_stepPlaceLabel->setString(uStr.c_str());
            }
            return;
        }

        // Иначе нейросеть генерирует постройку / арт!
        g_lastBuildPrompt = promptText;
        std::mt19937 rng(std::random_device{}());
        EditorTabCatalog catalog = loadEditorTabCatalog(m_editorUI);
        StylePalette palette = buildPaletteFromAIDecision(ai);

        auto specs = generateArtFromAIDecision(
            m_contour,
            catalog,
            ai,
            g_overhangAllowed,
            g_objectLimit,
            rng
        );

        SpawnSummary summary = placeGeneratedArtInEditor(m_editorUI, m_contour, specs, palette, ai.taskMode);

        g_lastPreviewSpecs = specs;
        g_lastPreviewPalette = palette;
        g_hasGeneratedArt = true;
        m_previewTab = 1; // Переключаем правое превью на вкладку "Drawing", как на скриншоте 2!
        refreshRightPanelTabsAndCanvas();

        if (m_stepPlanLabel) {
            std::string pStr = utf8ToGDReadable(ai.planText);
            if (pStr.size() > 26) pStr = pStr.substr(0, 23) + "...";
            m_stepPlanLabel->setString(pStr.c_str());
        }
        if (m_stepDrawingLabel) m_stepDrawingLabel->setString("done");
        if (m_stepVectorLabel)  m_stepVectorLabel->setString("done");
        if (m_stepPlaceLabel) {
            std::string plStr = fmt::format("{} objects (L{}-L{})", summary.createdCount, summary.firstLayer, summary.firstLayer + 3);
            m_stepPlaceLabel->setString(plStr.c_str());
        }
        if (m_previewSubInfoLabel) {
            std::string subStr = fmt::format("Covers 100% of the shape  |  {} objects", summary.createdCount);
            m_previewSubInfoLabel->setString(subStr.c_str());
        }
    }

    void updateKeyboardAndLock(float) {
        restoreLockedContourPositions();

#ifdef GEODE_IS_WINDOWS
        bool shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        bool ctrlDown  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool altDown   = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

        auto wasJustPressed = [&](int vk) -> bool {
            bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
            bool just = down && !m_prevKeyStates[vk];
            m_prevKeyStates[vk] = down;
            return just;
        };

        if (altDown && wasJustPressed(VK_SHIFT)) {
            onToggleLanguage(nullptr);
            return;
        }

        if (ctrlDown && wasJustPressed('V')) {
            if (m_isApiGateStage) {
                onPasteApiClicked(nullptr);
            } else {
                std::string clip = readWindowsClipboardText();
                if (!clip.empty()) {
                    m_inputBuffer += utf8ToGDReadable(clip);
                    if (m_inputBuffer.size() > 160) m_inputBuffer.resize(160);
                    refreshInputLabel();
                }
            }
            return;
        }

        if (wasJustPressed(VK_BACK)) {
            if (!m_inputBuffer.empty()) {
                m_inputBuffer.pop_back();
                refreshInputLabel();
            }
            return;
        }

        if (wasJustPressed(VK_RETURN)) {
            if (m_isApiGateStage) {
                onVerifyAndConnectApi(nullptr);
            } else {
                onSendToNeuralNet(nullptr);
            }
            return;
        }

        if (wasJustPressed(VK_SPACE)) {
            if (m_inputBuffer.size() < 160) {
                m_inputBuffer.push_back(' ');
                refreshInputLabel();
            }
            return;
        }

        for (int d = 0; d <= 9; ++d) {
            if (wasJustPressed('0' + d) || wasJustPressed(VK_NUMPAD0 + d)) {
                if (m_inputBuffer.size() < 160) {
                    m_inputBuffer.push_back(static_cast<char>('0' + d));
                    refreshInputLabel();
                }
            }
        }

        if (!ctrlDown && !altDown) {
            for (auto const& entry : kRusKeyTable) {
                if (wasJustPressed(entry.vk)) {
                    if (m_inputBuffer.size() < 160) {
                        if (m_russianLayout && !m_isApiGateStage) {
                            m_inputBuffer += entry.rusPhonetic;
                        } else {
                            m_inputBuffer.push_back(shiftDown ? entry.engShift : entry.engLower);
                        }
                        refreshInputLabel();
                    }
                }
            }
        }
#endif
    }

    void onClose(CCObject* sender) override {
        restoreLockedContourPositions();
        if (g_activePopup == this) {
            g_activePopup = nullptr;
        }
        Popup::onClose(sender);
    }

public:
    void closeByRightAlt() {
        this->onClose(nullptr);
    }

    static AIDecorPopup* create(EditorUI* ui, ContourAnalysis contour, bool forceApiGate = false) {
        auto ret = new AIDecorPopup();
        if (ret && ret->init(ui, std::move(contour), forceApiGate)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

// ============================================================================
// Открытие и закрытие окна Art Agent по нажатию Правого Alt (VK_RMENU)
// ============================================================================
class $modify(AIDecorEditorUI, EditorUI) {
    struct Fields {
        bool m_rightAltWasDown = false;
    };

    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;
        this->schedule(schedule_selector(AIDecorEditorUI::checkRightAltKey), 0.0f);
        return true;
    }

    void checkRightAltKey(float) {
#ifdef GEODE_IS_WINDOWS
        bool isDown = (GetAsyncKeyState(VK_RMENU) & 0x8000) != 0;
        if (isDown && !m_fields->m_rightAltWasDown) {
            m_fields->m_rightAltWasDown = true;
            toggleAIDecorMenu();
        } else if (!isDown) {
            m_fields->m_rightAltWasDown = false;
        }
#endif
    }

    void toggleAIDecorMenu() {
        if (g_activePopup) {
            g_activePopup->closeByRightAlt();
            return;
        }

        ContourAnalysis contour = analyzeSelectedContour(this);
        if (auto* popup = AIDecorPopup::create(this, std::move(contour), false)) {
            popup->show();
        }
    }
};
