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

enum class PopupStage : int {
    ApiGate = 0,       // Окно проверки и ввода API-ключа
    ArtAgentMain = 1,  // Главное окно Art Agent (Чат + Превью Shape/Drawing/In GD)
    ModelSelector = 2  // Отдельное окошко выбора актуальной модели ИИ
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

struct AICustomDrawElement {
    float rx = 0.5f;
    float ry = 0.5f;
    float scaleX = 0.5f;
    float scaleY = 0.5f;
    float rot = 0.f;
    int shapeType = 0;
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
    int emblemType = 0;
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
        if ((c & 0xE0) == 0xC0) i += 2;
        else if ((c & 0xF0) == 0xE0) i += 3;
        else if ((c & 0xF8) == 0xF0) i += 4;
        else ++i;
    }
    return out;
}

static std::vector<std::string> wrapTextLines(std::string const& rawText, size_t maxCharsPerLine, size_t maxLines = 3) {
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
// ТОЛЬКО АКТУАЛЬНЫЕ ПОДДЕРЖИВАЕМЫЕ МОДЕЛИ ИИ (Проверены на ноябрь 2026,
// устаревшие/отключенные модели исключены!)
// ============================================================================
struct AIModelEntry {
    const char* modelId;
    const char* displayTitle;
    const char* badgeDesc;
};

struct ApiProviderConfig {
    const char* name;
    const wchar_t* hostW;
    const wchar_t* chatPathW;
    const wchar_t* modelsListPathW;
    bool isGeminiNative;
    AIModelEntry models[4];
};

static constexpr ApiProviderConfig kApiProviders[5] = {
    // 0: Google Gemini (актуальная линейка с новейшей Gemini 4 Hydrogen!)
    {
        "Gemini",
        L"generativelanguage.googleapis.com",
        L"/v1beta/models/",
        L"/v1beta/models",
        true,
        {
            { "gemini-4-hydrogen",     "Gemini 4 Hydrogen",       "Newest Gemini 4 fast agentic model (Recommended)" },
            { "gemini-3.8-flash",      "Gemini 3.8 Flash",        "High-speed Gemini 3.8 model" },
            { "gemini-3.5-flash-lite", "Gemini 3.5 Flash-Lite",   "Ultra-fast low-latency model" },
            { "gemini-2.5-flash-lite", "Gemini 2.5 Flash-Lite",   "Stable long-term supported model" }
        }
    },
    // 1: OpenAI (только актуальная линейка GPT-6 и активный GPT-4o Mini; отключенные GPT-5/o3-mini убраны)
    {
        "OpenAI",
        L"api.openai.com",
        L"/v1/chat/completions",
        L"/v1/models",
        false,
        {
            { "gpt-6-luna",    "GPT-6 Luna",    "Fastest & most efficient OpenAI model" },
            { "gpt-6.1-terra", "GPT-6.1 Terra", "Visual reasoning & design specialist" },
            { "gpt-6.1-sol",   "GPT-6.1 Sol",   "High intelligence balanced model" },
            { "gpt-4o-mini",   "GPT-4o Mini",   "Classic lightweight universal model" }
        }
    },
    // 2: DeepSeek (только актуальные DeepSeek V4; отключенные deepseek-chat / reasoner убраны)
    {
        "DeepSeek",
        L"api.deepseek.com",
        L"/chat/completions",
        L"/models",
        false,
        {
            { "deepseek-v4-flash", "DeepSeek V4 Flash", "1M context fast generation model" },
            { "deepseek-v4-pro",   "DeepSeek V4 Pro",   "1M context flagship coding/art model" },
            { "deepseek-v4-flash", "DeepSeek V4 Flash (Fast)", "Recommended for quick GD builds" },
            { "deepseek-v4-pro",   "DeepSeek V4 Pro (Max)",    "Recommended for complex structures" }
        }
    },
    // 3: Groq (только актуальные Production-модели; отключенные на бесплатном тарифе llama-3.3 убраны)
    {
        "Groq",
        L"api.groq.com",
        L"/openai/v1/chat/completions",
        L"/openai/v1/models",
        false,
        {
            { "openai/gpt-oss-120b", "GPT-OSS 120B",   "500 tok/s flagship open-weight model" },
            { "openai/gpt-oss-20b",  "GPT-OSS 20B",    "1000 tok/s ultra-fast instant model" },
            { "qwen/qwen3.8-27b",    "Qwen 3.8 27B",   "450 tok/s strong coding & JSON model" },
            { "openai/gpt-oss-120b", "GPT-OSS 120B+",  "Default production Groq model" }
        }
    },
    // 4: OpenRouter (актуальные бесплатные и топовые модели)
    {
        "OpenRouter",
        L"openrouter.ai",
        L"/api/v1/chat/completions",
        L"/api/v1/models",
        false,
        {
            { "openrouter/free",                        "OpenRouter Auto-Free", "Automatically picks active free model" },
            { "google/gemma-4-31b-it:free",             "Gemma 4 31B (Free)",   "Google Gemma 4 free model" },
            { "nvidia/nemotron-3-super-120b-a12b:free", "Nemotron 3 120B (Free)","NVIDIA 120B free model" },
            { "deepseek/deepseek-v4-flash",             "DeepSeek V4 Flash",    "Fast low-cost OpenRouter model" }
        }
    }
};

static bool g_apiConfigLoaded = false;
static bool g_apiVerifiedAndConnected = false;
static int g_apiProviderIndex = 0; // По умолчанию 0 = Gemini
static int g_selectedModelIndex[5] = { 0, 0, 0, 0, 0 };
static std::string g_connectedApiKey;
static std::string g_discoveredGeminiModel; // Модель, автоматически найденная у ключа пользователя

static const char* getCurrentModelID(int provIdx) {
    provIdx = std::clamp(provIdx, 0, 4);
    int mIdx = std::clamp(g_selectedModelIndex[provIdx], 0, 3);
    return kApiProviders[provIdx].models[mIdx].modelId;
}

static bool hasPlausibleKeyFormat(std::string const& rawKey) {
    std::string k = trimWhitespace(rawKey);
    if (k.size() < 14) return false;
    for (char c : k) {
        if (c <= 32 || c > 126) return false;
    }
    return true;
}

static int detectProviderFromKey(std::string const& rawKey, int currentIdx) {
    std::string key = trimWhitespace(rawKey);
    if (key.rfind("AIza", 0) == 0) return 0;   // Gemini
    if (key.rfind("gsk_", 0) == 0) return 3;   // Groq
    if (key.rfind("sk-or-", 0) == 0) return 4; // OpenRouter
    if (key.rfind("sk-", 0) == 0) return (currentIdx == 2) ? 2 : 1; // OpenAI или DeepSeek
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

static std::wstring utf8ToWideASCII(std::string const& s) {
    std::wstring w;
    w.reserve(s.size());
    for (unsigned char c : s) {
        w.push_back(static_cast<wchar_t>(c));
    }
    return w;
}

// Универсальный HTTPS-клиент через системный winhttp.dll (поддерживает GET и POST)
static bool performWinHttpRequest(
    const wchar_t* verbW,
    const wchar_t* hostW,
    std::wstring const& pathW,
    std::wstring const& extraHeadersW,
    std::string const& bodyData,
    int& outHttpStatus,
    std::string& outResponseBody,
    std::string& outErrorText
) {
    outHttpStatus = 0;
    outResponseBody.clear();
    outErrorText.clear();

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
        outErrorText = "WinHTTP exports missing";
        return false;
    }

    HINTERNET_T hSession = pOpen(L"GDArtAgent/2.0", 0, nullptr, nullptr, 0);
    if (!hSession) {
        FreeLibrary(hWinHttp);
        outErrorText = "Failed to open HTTP session";
        return false;
    }

    if (pSetTimeouts) {
        pSetTimeouts(hSession, 6000, 6000, 10000, 22000);
    }

    HINTERNET_T hConnect = pConnect(hSession, hostW, 443, 0);
    if (!hConnect) {
        pClose(hSession);
        FreeLibrary(hWinHttp);
        outErrorText = "Cannot connect to AI host";
        return false;
    }

    constexpr DWORD kWinHttpFlagSecure = 0x00800000;
    HINTERNET_T hRequest = pOpenReq(
        hConnect, verbW, pathW.c_str(),
        nullptr, nullptr, nullptr, kWinHttpFlagSecure
    );
    if (!hRequest) {
        pClose(hConnect);
        pClose(hSession);
        FreeLibrary(hWinHttp);
        outErrorText = "Failed to open HTTPS request";
        return false;
    }

    LPVOID bodyPtr = bodyData.empty() ? nullptr : const_cast<char*>(bodyData.data());
    DWORD bodyLen  = static_cast<DWORD>(bodyData.size());

    BOOL sent = pSendReq(
        hRequest,
        extraHeadersW.empty() ? nullptr : extraHeadersW.c_str(),
        static_cast<DWORD>(extraHeadersW.size()),
        bodyPtr,
        bodyLen,
        bodyLen,
        0
    );

    if (!sent || !pRecvResp(hRequest, nullptr)) {
        pClose(hRequest);
        pClose(hConnect);
        pClose(hSession);
        FreeLibrary(hWinHttp);
        outErrorText = "Network error contacting AI server";
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
    (void)verbW; (void)hostW; (void)pathW; (void)extraHeadersW; (void)bodyData;
    outErrorText = "Windows required for WinHTTP";
    return false;
#endif
}

// Извлекает первую доступную flash/pro модель из ответа GET /v1beta/models?key=...
static void extractAvailableGeminiModelFromList(std::string const& modelsJson) {
    for (const char* preferred : { "gemini-4-hydrogen", "gemini-3.8-flash", "gemini-3.5-flash-lite", "gemini-2.5-flash-lite" }) {
        std::string needle = std::string("\"models/") + preferred + "\"";
        if (modelsJson.find(needle) != std::string::npos) {
            g_discoveredGeminiModel = preferred;
            return;
        }
    }
    size_t pos = modelsJson.find("\"models/gemini-");
    if (pos != std::string::npos) {
        pos += 8; // пропускаем `"models/`
        size_t endQuote = modelsJson.find('\"', pos);
        if (endQuote != std::string::npos && endQuote > pos) {
            g_discoveredGeminiModel = modelsJson.substr(pos, endQuote - pos);
        }
    }
}

// Надежная проверка API-ключа через GET /models (НИКОГДА не выдает 404 из-за имени модели!)
static std::string sanitizeApiKeyString(std::string s) {
    s = trimWhitespace(s);
    while (!s.empty() && (s.front() == '"' || s.front() == '\'')) s.erase(s.begin());
    while (!s.empty() && (s.back() == '"' || s.back() == '\'' || s.back() == '\0')) s.pop_back();
    return trimWhitespace(s);
}

static bool verifyApiKeyWithServer(int providerIdx, std::string const& rawKey, std::string& outErrorMsg) {
    std::string key = sanitizeApiKeyString(rawKey);
    if (!hasPlausibleKeyFormat(key)) {
        outErrorMsg = "Key format invalid (too short or contains spaces)";
        return false;
    }

    providerIdx = std::clamp(providerIdx, 0, 4);
    int httpStatus = 0;
    std::string respBody;
    std::string netErr;

    if (kApiProviders[providerIdx].isGeminiNative) {
        // Способ 1: Официальный заголовок x-goog-api-key на GET /v1beta/models
        std::wstring hdrW = L"x-goog-api-key: " + utf8ToWideASCII(key) + L"\r\n";
        bool ok = performWinHttpRequest(
            L"GET",
            kApiProviders[providerIdx].hostW,
            kApiProviders[providerIdx].modelsListPathW,
            hdrW,
            "",
            httpStatus,
            respBody,
            netErr
        );
        if (ok && httpStatus == 200 && respBody.find("\"models\"") != std::string::npos) {
            extractAvailableGeminiModelFromList(respBody);
            return true;
        }

        // Способ 2: Проверка прямым запросом к выбранной модели (gemini-4-hydrogen) через x-goog-api-key
        std::string pingBody = "{\"contents\":[{\"parts\":[{\"text\":\"hi\"}]}],\"generationConfig\":{\"maxOutputTokens\":4}}";
        std::wstring genHdrW = L"Content-Type: application/json\r\nx-goog-api-key: " + utf8ToWideASCII(key) + L"\r\n";
        std::wstring genPathW = L"/v1beta/models/" + utf8ToWideASCII(getCurrentModelID(0)) + L":generateContent";
        if (performWinHttpRequest(L"POST", kApiProviders[providerIdx].hostW, genPathW, genHdrW, pingBody, httpStatus, respBody, netErr) && httpStatus == 200) {
            return true;
        }
    } else {
        std::wstring headersW = L"Authorization: Bearer " + utf8ToWideASCII(key) + L"\r\n";
        bool ok = performWinHttpRequest(
            L"GET",
            kApiProviders[providerIdx].hostW,
            kApiProviders[providerIdx].modelsListPathW,
            headersW,
            "",
            httpStatus,
            respBody,
            netErr
        );
        if (ok && httpStatus == 200) {
            return true;
        }
    }

    // Извлекаем точное сообщение ошибки от сервера, чтобы пользователь видел причину
    std::string serverMsg = extractJsonStringField(respBody, "message");
    if (!serverMsg.empty()) {
        if (serverMsg.size() > 64) serverMsg = serverMsg.substr(0, 61) + "...";
        outErrorMsg = fmt::format("HTTP {}: {}", httpStatus, serverMsg);
        return false;
    }
    if (httpStatus > 0) {
        outErrorMsg = fmt::format("HTTP {} rejected by {}", httpStatus, kApiProviders[providerIdx].name);
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
        out << g_apiProviderIndex << " " << g_selectedModelIndex[g_apiProviderIndex] << "\n"
            << g_connectedApiKey << "\n";
    }
}

static void loadAndVerifySavedApiFromDisk() {
    if (g_apiConfigLoaded) return;
    g_apiConfigLoaded = true;

    std::ifstream in("ai_decor_api.cfg");
    if (!in.is_open()) return;
    std::string firstLine;
    std::string key;
    if (std::getline(in, firstLine) && std::getline(in, key)) {
        std::istringstream iss(firstLine);
        int prov = 0;
        int modIdx = 0;
        iss >> prov >> modIdx;
        key = trimWhitespace(key);
        if (prov >= 0 && prov < 5 && hasPlausibleKeyFormat(key)) {
            g_selectedModelIndex[prov] = std::clamp(modIdx, 0, 3);
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
    return static_cast<int>(std::lround(extractJsonFloatField(json, key, static_cast<float>(defaultVal))));
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

// Запрос к реальной нейросети (с поддержкой нативного Gemini generateContent + автоматическим обходом 404!)
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

    int httpStatus = 0;
    std::string rawResp;
    std::string errText;
    std::string assistantContent;

    if (kApiProviders[g_apiProviderIndex].isGeminiNative) {
        // Формат запроса нативного Gemini API (generateContent)
        std::string combinedPrompt = systemRule + "\n\nConversation history:\n";
        size_t startHist = (g_chatHistory.size() > 6) ? (g_chatHistory.size() - 6) : 0;
        for (size_t i = startHist; i < g_chatHistory.size(); ++i) {
            combinedPrompt += g_chatHistory[i].role + ": " + g_chatHistory[i].content + "\n";
        }
        combinedPrompt += "User: " + userMessage;

        std::string geminiBody = fmt::format(
            "{{\"contents\":[{{\"role\":\"user\",\"parts\":[{{\"text\":\"{}\"}}]}}],\"generationConfig\":{{\"temperature\":0.85,\"maxOutputTokens\":900}}}}",
            jsonEscape(combinedPrompt)
        );

        // Пробуем выбранную модель, а если 404 — автоматически пробуем все активные модели Gemini!
        std::vector<std::string> candidateModels = { getCurrentModelID(0) };
        if (!g_discoveredGeminiModel.empty()) candidateModels.push_back(g_discoveredGeminiModel);
        for (int m = 0; m < 4; ++m) {
            candidateModels.push_back(kApiProviders[0].models[m].modelId);
        }

        bool ok = false;
        for (auto const& modName : candidateModels) {
            std::wstring pathW = L"/v1beta/models/" + utf8ToWideASCII(modName) + L":generateContent";
            std::wstring hdrW  = L"Content-Type: application/json\r\nx-goog-api-key: " + utf8ToWideASCII(g_connectedApiKey) + L"\r\n";
            if (performWinHttpRequest(L"POST", kApiProviders[0].hostW, pathW, hdrW, geminiBody, httpStatus, rawResp, errText)) {
                ok = true;
                break;
            }
            if (httpStatus != 404) break;
        }

        if (!ok) {
            res.httpSuccess = false;
            res.replyText = fmt::format("Gemini API Error (HTTP {}): {}", httpStatus, errText);
            return res;
        }
        assistantContent = extractJsonStringField(rawResp, "text");
    } else {
        // Формат OpenAI / DeepSeek / Groq / OpenRouter
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

        std::vector<std::string> candidateModels = { getCurrentModelID(g_apiProviderIndex) };
        for (int m = 0; m < 4; ++m) {
            candidateModels.push_back(kApiProviders[g_apiProviderIndex].models[m].modelId);
        }

        bool ok = false;
        std::wstring hdrW = L"Content-Type: application/json\r\nAuthorization: Bearer " + utf8ToWideASCII(g_connectedApiKey) + L"\r\n";

        for (auto const& modName : candidateModels) {
            std::string payload = fmt::format(
                "{{\"model\":\"{}\",\"messages\":[{}],\"temperature\":0.85,\"max_tokens\":900}}",
                modName,
                messagesJson
            );
            if (performWinHttpRequest(L"POST", kApiProviders[g_apiProviderIndex].hostW, kApiProviders[g_apiProviderIndex].chatPathW, hdrW, payload, httpStatus, rawResp, errText)) {
                ok = true;
                break;
            }
            if (httpStatus != 404 && httpStatus != 400) break;
        }

        if (!ok) {
            res.httpSuccess = false;
            res.replyText = fmt::format("API Error (HTTP {}): {}", httpStatus, errText);
            return res;
        }
        assistantContent = extractJsonStringField(rawResp, "content");
    }

    res.httpSuccess = true;
    if (assistantContent.empty()) {
        res.replyText = "AI response received.";
        return res;
    }

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

        res.want3D             = extractJsonBoolField(innerJson, "want_3d", true);
        res.wantNeon           = extractJsonBoolField(innerJson, "want_neon", true);
        res.wantGrass          = extractJsonBoolField(innerJson, "want_grass", false);
        res.wantSnowCap        = extractJsonBoolField(innerJson, "want_snow", false);
        res.wantBrickPattern   = extractJsonBoolField(innerJson, "want_brick", false);
        res.wantCrystalPattern = extractJsonBoolField(innerJson, "want_crystal", false);
        res.wantTreePattern    = extractJsonBoolField(innerJson, "want_tree", false);
        res.emblemType         = std::clamp(extractJsonIntField(innerJson, "emblem", 0), 0, 5);
        res.patternStyle       = std::clamp(extractJsonIntField(innerJson, "pattern", 0), 0, 5);
        res.suggestedCount     = std::clamp(extractJsonIntField(innerJson, "count", 130), 20, 450);

        parseCustomElementsFromAIJson(innerJson, res.customElements);
    } else {
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

            addSpec(result, 211, cx, cy, 1.00f, 1.00f, 0.f, LayerRole::Fill, ColorSlot::DarkFill, 1, 0, false);

            if (ai.wantBrickPattern) {
                addSpec(result, cat.idBrickBlock, cx, cy, 0.88f, 0.88f, 0.f, LayerRole::Fill, ColorSlot::MidTone, 2, 1, false, 160);
            } else if (((localCol + localRow + layoutStyle) % 2) == 0) {
                addSpec(result, 211, cx, cy, 0.86f, 0.86f, 0.f, LayerRole::Fill, ColorSlot::MidTone, 2, 1, false, 52);
            }

            if (topOpen && ai.wantSnowCap) {
                addSpec(result, 211, cx, cy + 10.5f, 1.00f, 0.30f, 0.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 7, 0, false, 235);
                addSpec(result, 211, cx - 6.f, cy + 6.5f, 0.42f, 0.16f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 7, 1, false, 210);
            }

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

    float spanW = static_cast<float>((std::max)(1, contour.maxGX - contour.minGX + 1)) * 30.f;
    float spanH = static_cast<float>((std::max)(1, contour.maxGY - contour.minGY + 1)) * 30.f;
    float originX = contour.minGX * 30.f;
    float originY = contour.minGY * 30.f;

    for (auto const& el : ai.customElements) {
        float worldX = originX + el.rx * spanW;
        float worldY = originY + el.ry * spanH;
        int cellGX = static_cast<int>(std::floor(worldX / 30.f));
        int cellGY = static_cast<int>(std::floor(worldY / 30.f));

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
            cs.priorityTier   = 0;
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
    return sanitizeApiKeyString(out);
}

// ============================================================================
// Помощник создания 100% НЕПРОЗРАЧНЫХ цветных плашек через CCLayerColor
// (чтобы фон, кнопки и превью никогда не были прозрачными!)
// ============================================================================
static CCLayerColor* createSolidPanel(float width, float height, ccColor3B color, GLubyte alpha = 255) {
    auto* panel = CCLayerColor::create(ccColor4B{ color.r, color.g, color.b, alpha }, width, height);
    panel->ignoreAnchorPointForPosition(false);
    panel->setAnchorPoint(CCPoint{ 0.5f, 0.5f });
    return panel;
}

static CCMenuItemSpriteExtra* createSolidButton(
    float width,
    float height,
    ccColor3B bgColor,
    const char* text,
    float textScale,
    ccColor3B textColor,
    CCObject* target,
    SEL_MenuHandler selector
) {
    auto* bg = createSolidPanel(width, height, bgColor, 255);
    auto* border = createSolidPanel(width + 2.f, height + 2.f, ccColor3B{ 62, 66, 78 }, 255);
    border->setPosition(CCPoint{ width * 0.5f, height * 0.5f });
    border->setZOrder(-1);
    bg->addChild(border);

    auto* lbl = CCLabelBMFont::create(text, "chatFont.fnt");
    lbl->setScale(textScale);
    lbl->setColor(textColor);
    lbl->setPosition(CCPoint{ width * 0.5f, height * 0.5f });
    bg->addChild(lbl);

    return CCMenuItemSpriteExtra::create(bg, target, selector);
}

// ============================================================================
// ГЛАВНОЕ ОКНО МОДА (3 ЭКРАНА: API Gate / Art Agent Main / AI Model Selector)
// ============================================================================
class AIDecorPopup;
static AIDecorPopup* g_activePopup = nullptr;

class AIDecorPopup : public geode::Popup {
protected:
    EditorUI* m_editorUI = nullptr;
    ContourAnalysis m_contour;
    PopupStage m_stage = PopupStage::ApiGate;
    PopupStage m_returnStageAfterModel = PopupStage::ApiGate;
    bool m_russianLayout = true;
    int m_providerIdx = 0;
    int m_previewTab = 0; // 0 = Shape, 1 = Drawing, 2 = In GD
    std::string m_inputBuffer;
    bool m_prevKeyStates[256]{};

    CCLabelBMFont* m_inputDisplayLabel = nullptr;
    CCLabelBMFont* m_langBadgeLabel = nullptr;
    CCLabelBMFont* m_apiGateStatusLabel = nullptr;
    CCLayerColor* m_checkBoxBg = nullptr;
    CCNode* m_checkTickContainer = nullptr;

    // Элементы интерфейса Art Agent
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
    CCLayerColor* m_tabShapeBg = nullptr;
    CCLayerColor* m_tabDrawingBg = nullptr;
    CCLayerColor* m_tabInGDBg = nullptr;
    CCNode* m_previewCanvasContent = nullptr;
    CCLabelBMFont* m_previewSubInfoLabel = nullptr;
    CCLabelBMFont* m_limitValueLabel = nullptr;
    CCLayerColor* m_insideTabBg = nullptr;
    CCLayerColor* m_overhangTabBg = nullptr;

    bool init(EditorUI* ui, ContourAnalysis contour, PopupStage requestedStage, PopupStage prevStage) {
        if (!Popup::init(492.f, 288.f)) return false;

        g_activePopup = this;
        m_editorUI = ui;
        m_contour = std::move(contour);
        m_returnStageAfterModel = prevStage;

        loadAndVerifySavedApiFromDisk();
        m_providerIdx = g_apiProviderIndex;

        if (requestedStage == PopupStage::ModelSelector) {
            m_stage = PopupStage::ModelSelector;
        } else if (!g_apiVerifiedAndConnected || requestedStage == PopupStage::ApiGate) {
            m_stage = PopupStage::ApiGate;
        } else {
            m_stage = PopupStage::ArtAgentMain;
        }

        m_russianLayout = (m_stage == PopupStage::ArtAgentMain);

        if (m_stage == PopupStage::ApiGate && !g_connectedApiKey.empty()) {
            m_inputBuffer = g_connectedApiKey;
        }

        auto size = m_mainLayer->getContentSize();
        auto center = CCPoint{ size.width / 2.f, size.height / 2.f };

        // Сплошной непрозрачный темно-серый корпус окна
        auto* outerBorder = createSolidPanel(size.width, size.height, ccColor3B{ 68, 72, 84 }, 255);
        outerBorder->setPosition(center);
        outerBorder->setZOrder(-3);
        m_mainLayer->addChild(outerBorder);

        auto* innerBody = createSolidPanel(size.width - 4.f, size.height - 4.f, ccColor3B{ 18, 19, 24 }, 255);
        innerBody->setPosition(center);
        innerBody->setZOrder(-2);
        m_mainLayer->addChild(innerBody);

        if (m_stage == PopupStage::ApiGate) {
            setupStrictApiGateUI(center);
        } else if (m_stage == PopupStage::ModelSelector) {
            setupModelSelectorWindowUI(center);
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
        m_checkBoxBg = createSolidPanel(20.f, 20.f, checked ? ccColor3B{ 42, 205, 95 } : ccColor3B{ 58, 62, 72 }, 255);
        m_checkBoxBg->setPosition(pos);
        m_mainLayer->addChild(m_checkBoxBg);

        m_checkTickContainer = CCNode::create();
        m_checkTickContainer->setPosition(pos);
        m_mainLayer->addChild(m_checkTickContainer);

        auto* shortLeg = createSolidPanel(3.4f, 7.8f, ccColor3B{ 255, 255, 255 }, 255);
        shortLeg->setRotation(-40.f);
        shortLeg->setPosition(CCPoint{ -2.8f, -1.4f });
        m_checkTickContainer->addChild(shortLeg);

        auto* longLeg = createSolidPanel(3.4f, 12.8f, ccColor3B{ 255, 255, 255 }, 255);
        longLeg->setRotation(38.f);
        longLeg->setPosition(CCPoint{ 2.4f, 1.0f });
        m_checkTickContainer->addChild(longLeg);

        setCheckmarkState(checked);
    }

    void setCheckmarkState(bool checked) {
        if (m_checkBoxBg) {
            m_checkBoxBg->setColor(checked ? ccColor3B{ 42, 205, 95 } : ccColor3B{ 58, 62, 72 });
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
    // ЭКРАН 1: СТРОГАЯ ПРОВЕРКА ДОСТОВЕРНОСТИ API-КЛЮЧА + КНОПКА ВЫБОРА МОДЕЛИ
    // ========================================================================
    void setupStrictApiGateUI(CCPoint center) {
        this->setTitle("ART AGENT — PODKLYUCHENIE II PO API");

        if (g_apiVerifiedAndConnected) {
            auto* backBtn = createSolidButton(
                76.f, 22.f, ccColor3B{ 46, 50, 62 },
                "Back", 0.48f, ccColor3B{ 235, 240, 250 },
                this, menu_selector(AIDecorPopup::onBackToArtAgent)
            );
            backBtn->setPosition(CCPoint{ center.x + 190.f, center.y + 118.f });
            m_buttonMenu->addChild(backBtn);
        }

        auto* cardBg = createSolidPanel(440.f, 122.f, ccColor3B{ 28, 30, 38 }, 255);
        cardBg->setPosition(CCPoint{ center.x, center.y + 28.f });
        m_mainLayer->addChild(cardBg);

        createCheckmarkBadge(CCPoint{ center.x - 196.f, center.y + 68.f }, g_apiVerifiedAndConnected);

        std::string initStatus = g_apiVerifiedAndConnected
            ? fmt::format("API Key Verified: {} ({})", kApiProviders[m_providerIdx].name, maskApiKeyPreview(g_connectedApiKey))
            : "Vvedite dostoverniy API-klyuch (proverka cherez GET /models bez oshibki 404!)";
        m_apiGateStatusLabel = CCLabelBMFont::create(initStatus.c_str(), "chatFont.fnt");
        m_apiGateStatusLabel->setScale(0.50f);
        m_apiGateStatusLabel->setColor(g_apiVerifiedAndConnected ? ccColor3B{ 110, 245, 155 } : ccColor3B{ 245, 195, 120 });
        m_apiGateStatusLabel->setPosition(CCPoint{ center.x + 10.f, center.y + 68.f });
        m_mainLayer->addChild(m_apiGateStatusLabel);

        // Две удобные кнопки выбора: [Провайдер] и [Окошко выбора модели ИИ]
        std::string provText = fmt::format("Provider: {} (Switch)", kApiProviders[m_providerIdx].name);
        auto* provBtn = createSolidButton(
            195.f, 26.f, ccColor3B{ 44, 48, 58 },
            provText.c_str(), 0.48f, ccColor3B{ 245, 248, 255 },
            this, menu_selector(AIDecorPopup::onCycleApiProvider)
        );
        provBtn->setPosition(CCPoint{ center.x - 108.f, center.y + 30.f });
        m_buttonMenu->addChild(provBtn);

        std::string modBtnText = fmt::format("Model: {} (Change)", getCurrentModelID(m_providerIdx));
        auto* openModWinBtn = createSolidButton(
            215.f, 26.f, ccColor3B{ 56, 62, 78 },
            modBtnText.c_str(), 0.45f, ccColor3B{ 165, 230, 255 },
            this, menu_selector(AIDecorPopup::onOpenModelSelectorFromGate)
        );
        openModWinBtn->setPosition(CCPoint{ center.x + 106.f, center.y + 30.f });
        m_buttonMenu->addChild(openModWinBtn);

        auto* hintLbl = CCLabelBMFont::create(
            "Nazhmite 'Model: ... (Change)' chtobi vibrat aktualnuyu model II v otdelnom okoshke!",
            "chatFont.fnt"
        );
        hintLbl->setScale(0.45f);
        hintLbl->setColor(ccColor3B{ 170, 178, 192 });
        hintLbl->setPosition(CCPoint{ center.x, center.y - 8.f });
        m_mainLayer->addChild(hintLbl);

        // Непрозрачное поле ввода API-ключа
        auto* inputBg = createSolidPanel(440.f, 34.f, ccColor3B{ 12, 13, 17 }, 255);
        inputBg->setPosition(CCPoint{ center.x, center.y - 54.f });
        m_mainLayer->addChild(inputBg);

        m_inputDisplayLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_inputDisplayLabel->setScale(0.55f);
        m_inputDisplayLabel->setPosition(CCPoint{ center.x, center.y - 54.f });
        m_mainLayer->addChild(m_inputDisplayLabel);
        refreshInputLabel();

        // Нижние кнопки с четкими непрозрачными плашками
        auto* pasteBtn = createSolidButton(
            130.f, 30.f, ccColor3B{ 44, 48, 58 },
            "Paste (Ctrl+V)", 0.50f, ccColor3B{ 240, 245, 255 },
            this, menu_selector(AIDecorPopup::onPasteApiClicked)
        );
        pasteBtn->setPosition(CCPoint{ center.x - 155.f, center.y - 104.f });
        m_buttonMenu->addChild(pasteBtn);

        auto* clearBtn = createSolidButton(
            90.f, 30.f, ccColor3B{ 44, 48, 58 },
            "Clear", 0.50f, ccColor3B{ 240, 245, 255 },
            this, menu_selector(AIDecorPopup::onClearApiClicked)
        );
        clearBtn->setPosition(CCPoint{ center.x - 35.f, center.y - 104.f });
        m_buttonMenu->addChild(clearBtn);

        auto* verifyBtn = createSolidButton(
            195.f, 30.f, ccColor3B{ 228, 58, 92 },
            "Verify & Connect API", 0.52f, ccColor3B{ 255, 255, 255 },
            this, menu_selector(AIDecorPopup::onVerifyAndConnectApi)
        );
        verifyBtn->setPosition(CCPoint{ center.x + 122.f, center.y - 104.f });
        m_buttonMenu->addChild(verifyBtn);
    }

    // ========================================================================
    // ЭКРАН 2: ОКОШКО ВЫБОРА АКТУАЛЬНОЙ МОДЕЛИ ИИ (Только поддерживаемые модели!)
    // ========================================================================
    void setupModelSelectorWindowUI(CCPoint center) {
        this->setTitle("VIBOR AKTUALNOY MODELI II");

        // Верхняя плашка выбора провайдера [< Provider: Gemini >] + кнопка [Назад]
        std::string provHeader = fmt::format("Provider: {} (Click to switch)", kApiProviders[m_providerIdx].name);
        auto* provSwitchBtn = createSolidButton(
            260.f, 26.f, ccColor3B{ 44, 48, 60 },
            provHeader.c_str(), 0.50f, ccColor3B{ 165, 230, 255 },
            this, menu_selector(AIDecorPopup::onCycleProviderInModelWindow)
        );
        provSwitchBtn->setPosition(CCPoint{ center.x - 50.f, center.y + 88.f });
        m_buttonMenu->addChild(provSwitchBtn);

        auto* backBtn = createSolidButton(
            110.f, 26.f, ccColor3B{ 62, 66, 78 },
            "Nazad / Done", 0.48f, ccColor3B{ 245, 248, 255 },
            this, menu_selector(AIDecorPopup::onCloseModelSelectorWindow)
        );
        backBtn->setPosition(CCPoint{ center.x + 155.f, center.y + 88.f });
        m_buttonMenu->addChild(backBtn);

        auto* subInfo = CCLabelBMFont::create(
            "Vse ustarevshie modeli ubrani. Nazhmite na lyubuyu podderzhivaemuyu model nizhe:",
            "chatFont.fnt"
        );
        subInfo->setScale(0.45f);
        subInfo->setColor(ccColor3B{ 175, 182, 195 });
        subInfo->setPosition(CCPoint{ center.x, center.y + 62.f });
        m_mainLayer->addChild(subInfo);

        int activeIdx = std::clamp(g_selectedModelIndex[m_providerIdx], 0, 3);
        SEL_MenuHandler const modelCallbacks[4] = {
            menu_selector(AIDecorPopup::onPickModel0),
            menu_selector(AIDecorPopup::onPickModel1),
            menu_selector(AIDecorPopup::onPickModel2),
            menu_selector(AIDecorPopup::onPickModel3)
        };

        for (int i = 0; i < 4; ++i) {
            float rowY = center.y + 28.f - i * 42.f;
            bool isSelected = (i == activeIdx);

            ccColor3B cardColor = isSelected ? ccColor3B{ 36, 68, 52 } : ccColor3B{ 28, 30, 38 };
            auto* rowBg = createSolidPanel(430.f, 34.f, cardColor, 255);
            auto* rowBorder = createSolidPanel(
                432.f, 36.f,
                isSelected ? ccColor3B{ 46, 215, 105 } : ccColor3B{ 54, 58, 68 },
                255
            );
            rowBorder->setPosition(CCPoint{ 215.f, 17.f });
            rowBorder->setZOrder(-1);
            rowBg->addChild(rowBorder);

            std::string titleLine = fmt::format(
                "{}{}  [{}]",
                isSelected ? "[ACTIVE] " : "",
                kApiProviders[m_providerIdx].models[i].displayTitle,
                kApiProviders[m_providerIdx].models[i].modelId
            );
            auto* titleLbl = CCLabelBMFont::create(titleLine.c_str(), "chatFont.fnt");
            titleLbl->setScale(0.50f);
            titleLbl->setColor(isSelected ? ccColor3B{ 125, 255, 165 } : ccColor3B{ 240, 245, 255 });
            titleLbl->setPosition(CCPoint{ 215.f, 22.f });
            rowBg->addChild(titleLbl);

            auto* descLbl = CCLabelBMFont::create(kApiProviders[m_providerIdx].models[i].badgeDesc, "chatFont.fnt");
            descLbl->setScale(0.42f);
            descLbl->setColor(ccColor3B{ 170, 178, 192 });
            descLbl->setPosition(CCPoint{ 215.f, 9.f });
            rowBg->addChild(descLbl);

            auto* itemBtn = CCMenuItemSpriteExtra::create(rowBg, this, modelCallbacks[i]);
            itemBtn->setPosition(CCPoint{ center.x, rowY });
            m_buttonMenu->addChild(itemBtn);
        }
    }

    // ========================================================================
    // ЭКРАН 3: ГЛАВНЫЙ ИНТЕРФЕЙС "ART AGENT" (1-в-1 по скриншотам!)
    // ========================================================================
    void setupArtAgentMainUI(CCPoint center) {
        this->setTitle("");

        // --- ВЕРХНЯЯ ПАНЕЛЬ (Шапка Art Agent) ---
        auto* headerTitle = CCLabelBMFont::create("Art Agent", "chatFont.fnt");
        headerTitle->setScale(0.58f);
        headerTitle->setColor(ccColor3B{ 245, 248, 255 });
        headerTitle->setPosition(CCPoint{ center.x + 15.f, center.y + 125.f });
        m_mainLayer->addChild(headerTitle);

        // Левая капсула API + зеленая галочка + розовая кнопка [+] смены API
        auto* balPill = createSolidPanel(106.f, 22.f, ccColor3B{ 28, 30, 38 }, 255);
        balPill->setPosition(CCPoint{ center.x - 180.f, center.y + 125.f });
        m_mainLayer->addChild(balPill);

        createCheckmarkBadge(CCPoint{ center.x - 221.f, center.y + 125.f }, true);

        std::string apiTag = fmt::format("API {}", kApiProviders[m_providerIdx].name);
        auto* apiTagLbl = CCLabelBMFont::create(apiTag.c_str(), "chatFont.fnt");
        apiTagLbl->setScale(0.44f);
        apiTagLbl->setColor(ccColor3B{ 230, 235, 245 });
        apiTagLbl->setPosition(CCPoint{ center.x - 182.f, center.y + 125.f });
        m_mainLayer->addChild(apiTagLbl);

        auto* changeApiBtn = createSolidButton(
            24.f, 20.f, ccColor3B{ 232, 65, 98 },
            "+", 0.55f, ccColor3B{ 255, 255, 255 },
            this, menu_selector(AIDecorPopup::onOpenApiGate)
        );
        changeApiBtn->setPosition(CCPoint{ center.x - 138.f, center.y + 125.f });
        m_buttonMenu->addChild(changeApiBtn);

        // Кнопка открытия окошка выбора модели ИИ прямо из шапки Art Agent!
        std::string shortMod = fmt::format("Model: {}", getCurrentModelID(m_providerIdx));
        if (shortMod.size() > 22) shortMod = shortMod.substr(0, 22);
        auto* modelBtn = createSolidButton(
            112.f, 20.f, ccColor3B{ 38, 42, 54 },
            shortMod.c_str(), 0.38f, ccColor3B{ 165, 230, 255 },
            this, menu_selector(AIDecorPopup::onOpenModelSelectorFromMain)
        );
        modelBtn->setPosition(CCPoint{ center.x - 64.f, center.y + 125.f });
        m_buttonMenu->addChild(modelBtn);

        // Кнопка переключения раскладки [RUS/ENG]
        auto* langBg = createSolidPanel(44.f, 20.f, ccColor3B{ 38, 42, 54 }, 255);
        m_langBadgeLabel = CCLabelBMFont::create("RUS", "chatFont.fnt");
        m_langBadgeLabel->setScale(0.45f);
        m_langBadgeLabel->setColor(ccColor3B{ 240, 245, 255 });
        m_langBadgeLabel->setPosition(CCPoint{ 22.f, 10.f });
        langBg->addChild(m_langBadgeLabel);
        auto* langBtn = CCMenuItemSpriteExtra::create(langBg, this, menu_selector(AIDecorPopup::onToggleLanguage));
        langBtn->setPosition(CCPoint{ center.x + 185.f, center.y + 125.f });
        m_buttonMenu->addChild(langBtn);

        // Разделительные линии
        auto* topDivider = createSolidPanel(484.f, 1.5f, ccColor3B{ 42, 45, 54 }, 255);
        topDivider->setPosition(CCPoint{ center.x, center.y + 109.f });
        m_mainLayer->addChild(topDivider);

        auto* midDivider = createSolidPanel(1.5f, 246.f, ccColor3B{ 42, 45, 54 }, 255);
        midDivider->setPosition(CCPoint{ center.x - 4.f, center.y - 14.f });
        m_mainLayer->addChild(midDivider);

        // ====================================================================
        // ЛЕВАЯ КОЛОНКА: Карточка ответа ИИ-Агента + Карточка выделения и шагов
        // ====================================================================
        float leftX = center.x - 122.f;

        auto* agentCard = createSolidPanel(222.f, 64.f, ccColor3B{ 26, 28, 35 }, 255);
        agentCard->setPosition(CCPoint{ leftX, center.y + 68.f });
        m_mainLayer->addChild(agentCard);

        auto* agentTitle = CCLabelBMFont::create("Agent", "chatFont.fnt");
        agentTitle->setScale(0.44f);
        agentTitle->setColor(ccColor3B{ 140, 146, 160 });
        agentTitle->setPosition(CCPoint{ leftX - 88.f, center.y + 90.f });
        m_mainLayer->addChild(agentTitle);

        std::string initMsg1 = fmt::format(
            "Tell me what to draw on the {} selected blocks.",
            m_contour.cellCount()
        );
        m_agentReplyLine1 = CCLabelBMFont::create(initMsg1.c_str(), "chatFont.fnt");
        m_agentReplyLine1->setScale(0.45f);
        m_agentReplyLine1->setColor(ccColor3B{ 240, 242, 248 });
        m_agentReplyLine1->setPosition(CCPoint{ leftX, center.y + 73.f });
        m_mainLayer->addChild(m_agentReplyLine1);

        m_agentReplyLine2 = CCLabelBMFont::create("You can write in any language.", "chatFont.fnt");
        m_agentReplyLine2->setScale(0.45f);
        m_agentReplyLine2->setColor(ccColor3B{ 210, 215, 225 });
        m_agentReplyLine2->setPosition(CCPoint{ leftX, center.y + 58.f });
        m_mainLayer->addChild(m_agentReplyLine2);

        m_agentReplyLine3 = CCLabelBMFont::create("", "chatFont.fnt");
        m_agentReplyLine3->setScale(0.43f);
        m_agentReplyLine3->setColor(ccColor3B{ 185, 192, 205 });
        m_agentReplyLine3->setPosition(CCPoint{ leftX, center.y + 44.f });
        m_mainLayer->addChild(m_agentReplyLine3);

        // Средняя карточка: Selection + чеклист (Shape / Plan / Drawing / Vector / Place)
        auto* stepsCard = createSolidPanel(222.f, 92.f, ccColor3B{ 24, 26, 32 }, 255);
        stepsCard->setPosition(CCPoint{ leftX, center.y - 16.f });
        m_mainLayer->addChild(stepsCard);

        auto* miniThumbBg = createSolidPanel(34.f, 22.f, ccColor3B{ 22, 92, 212 }, 255);
        miniThumbBg->setPosition(CCPoint{ leftX - 86.f, center.y + 14.f });
        m_mainLayer->addChild(miniThumbBg);

        auto* miniShapeIcon = createSolidPanel(16.f, 14.f, ccColor3B{ 38, 215, 50 }, 255);
        miniShapeIcon->setPosition(CCPoint{ leftX - 86.f, center.y + 14.f });
        m_mainLayer->addChild(miniShapeIcon);

        auto* selTitle = CCLabelBMFont::create("Selection", "chatFont.fnt");
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
        m_userPromptLine2->setScale(0.42f);
        m_userPromptLine2->setColor(ccColor3B{ 220, 225, 235 });
        m_userPromptLine2->setPosition(CCPoint{ leftX, center.y - 8.f });
        m_mainLayer->addChild(m_userPromptLine2);

        auto addStepRow = [&](float yPos, const char* stepName, std::string const& rightVal) -> CCLabelBMFont* {
            auto* dot = createSolidPanel(7.f, 7.f, ccColor3B{ 232, 65, 98 }, 255);
            dot->setPosition(CCPoint{ leftX - 98.f, yPos });
            m_mainLayer->addChild(dot);

            auto* nameL = CCLabelBMFont::create(stepName, "chatFont.fnt");
            nameL->setScale(0.41f);
            nameL->setColor(ccColor3B{ 215, 220, 230 });
            nameL->setPosition(CCPoint{ leftX - 68.f, yPos });
            m_mainLayer->addChild(nameL);

            auto* valL = CCLabelBMFont::create(rightVal.c_str(), "chatFont.fnt");
            valL->setScale(0.41f);
            valL->setColor(ccColor3B{ 145, 152, 168 });
            valL->setPosition(CCPoint{ leftX + 26.f, yPos });
            m_mainLayer->addChild(valL);
            return valL;
        };

        m_stepPlanLabel    = addStepRow(center.y - 23.f, "Plan",    "ready");
        m_stepDrawingLabel = addStepRow(center.y - 35.f, "Drawing", "waiting");
        m_stepVectorLabel  = addStepRow(center.y - 47.f, "Vector",  "waiting");
        m_stepPlaceLabel   = addStepRow(center.y - 59.f, "Place",   "waiting");

        // Примеры (For example)
        auto* exHeader = CCLabelBMFont::create("For example", "chatFont.fnt");
        exHeader->setScale(0.40f);
        exHeader->setColor(ccColor3B{ 135, 140, 155 });
        exHeader->setPosition(CCPoint{ leftX - 88.f, center.y - 72.f });
        m_mainLayer->addChild(exHeader);

        auto addExampleChip = [&](const char* label, CCPoint pos, SEL_MenuHandler cb) {
            auto* btn = createSolidButton(
                52.f, 15.f, ccColor3B{ 34, 36, 44 },
                label, 0.34f, ccColor3B{ 220, 225, 235 },
                this, cb
            );
            btn->setPosition(pos);
            m_buttonMenu->addChild(btn);
        };

        addExampleChip("Stone snow", CCPoint{ leftX - 83.f, center.y - 86.f }, menu_selector(AIDecorPopup::onPresetStoneSnow));
        addExampleChip("Tree glow",  CCPoint{ leftX - 28.f, center.y - 86.f }, menu_selector(AIDecorPopup::onPresetTreeGlow));
        addExampleChip("Crystal",    CCPoint{ leftX + 27.f, center.y - 86.f }, menu_selector(AIDecorPopup::onPresetCrystal));
        addExampleChip("Only 3D",    CCPoint{ leftX + 82.f, center.y - 86.f }, menu_selector(AIDecorPopup::onPresetOnly3D));

        // Нижняя строка ввода чата
        auto* glowStrip = createSolidPanel(165.f, 2.f, ccColor3B{ 110, 215, 255 }, 255);
        glowStrip->setPosition(CCPoint{ leftX - 15.f, center.y - 101.f });
        m_mainLayer->addChild(glowStrip);

        auto* inputBarBg = createSolidPanel(222.f, 28.f, ccColor3B{ 26, 28, 35 }, 255);
        inputBarBg->setPosition(CCPoint{ leftX, center.y - 116.f });
        m_mainLayer->addChild(inputBarBg);

        m_inputDisplayLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_inputDisplayLabel->setScale(0.45f);
        m_inputDisplayLabel->setPosition(CCPoint{ leftX - 14.f, center.y - 116.f });
        m_mainLayer->addChild(m_inputDisplayLabel);
        refreshInputLabel();

        auto* sendBtn = createSolidButton(
            26.f, 22.f, ccColor3B{ 215, 52, 85 },
            ">", 0.52f, ccColor3B{ 255, 255, 255 },
            this, menu_selector(AIDecorPopup::onSendToNeuralNet)
        );
        sendBtn->setPosition(CCPoint{ leftX + 94.f, center.y - 116.f });
        m_buttonMenu->addChild(sendBtn);

        // ====================================================================
        // ПРАВАЯ КОЛОНКА: Вкладки [Shape | Drawing | In GD] + Синее превью + Limit + Keep
        // ====================================================================
        float rightX = center.x + 118.f;

        auto* tabBarBg = createSolidPanel(224.f, 22.f, ccColor3B{ 26, 28, 35 }, 255);
        tabBarBg->setPosition(CCPoint{ rightX, center.y + 92.f });
        m_mainLayer->addChild(tabBarBg);

        auto createTabButton = [&](const char* title, CCPoint pos, SEL_MenuHandler cb, CCLayerColor*& outBg) {
            outBg = createSolidPanel(72.f, 18.f, ccColor3B{ 26, 28, 35 }, 255);
            auto* lbl = CCLabelBMFont::create(title, "chatFont.fnt");
            lbl->setScale(0.45f);
            lbl->setColor(ccColor3B{ 245, 248, 255 });
            lbl->setPosition(CCPoint{ 36.f, 9.f });
            outBg->addChild(lbl);
            auto* btn = CCMenuItemSpriteExtra::create(outBg, this, cb);
            btn->setPosition(pos);
            m_buttonMenu->addChild(btn);
        };

        createTabButton("Shape",   CCPoint{ rightX - 73.f, center.y + 92.f }, menu_selector(AIDecorPopup::onTabShape),   m_tabShapeBg);
        createTabButton("Drawing", CCPoint{ rightX,        center.y + 92.f }, menu_selector(AIDecorPopup::onTabDrawing), m_tabDrawingBg);
        createTabButton("In GD",   CCPoint{ rightX + 73.f, center.y + 92.f }, menu_selector(AIDecorPopup::onTabInGD),    m_tabInGDBg);

        // Сплошное синее превью-полотно в стиле редактора GD
        auto* canvasBg = createSolidPanel(224.f, 128.f, ccColor3B{ 18, 92, 212 }, 255);
        canvasBg->setPosition(CCPoint{ rightX, center.y + 10.f });
        m_mainLayer->addChild(canvasBg);

        auto* selectionFrame = createSolidPanel(166.f, 108.f, ccColor3B{ 95, 195, 255 }, 120);
        selectionFrame->setPosition(CCPoint{ rightX, center.y + 10.f });
        m_mainLayer->addChild(selectionFrame);

        auto* selectionInner = createSolidPanel(162.f, 104.f, ccColor3B{ 18, 92, 212 }, 255);
        selectionInner->setPosition(CCPoint{ rightX, center.y + 10.f });
        m_mainLayer->addChild(selectionInner);

        m_previewCanvasContent = CCNode::create();
        m_previewCanvasContent->setPosition(CCPoint{ rightX, center.y + 10.f });
        m_mainLayer->addChild(m_previewCanvasContent);

        std::string dimText = fmt::format(
            "{} x {} units  |  {} blocks",
            m_contour.widthUnits(), m_contour.heightUnits(), m_contour.cellCount()
        );
        m_previewSubInfoLabel = CCLabelBMFont::create(dimText.c_str(), "chatFont.fnt");
        m_previewSubInfoLabel->setScale(0.44f);
        m_previewSubInfoLabel->setColor(ccColor3B{ 175, 182, 195 });
        m_previewSubInfoLabel->setPosition(CCPoint{ rightX, center.y - 62.f });
        m_mainLayer->addChild(m_previewSubInfoLabel);

        // Кнопки [< Limit 16000 >] и [Inside | Overhang]
        auto* limitPill = createSolidPanel(108.f, 22.f, ccColor3B{ 26, 28, 35 }, 255);
        limitPill->setPosition(CCPoint{ rightX - 58.f, center.y - 84.f });
        m_mainLayer->addChild(limitPill);

        std::string limStr = fmt::format("Limit  {}", g_objectLimit);
        m_limitValueLabel = CCLabelBMFont::create(limStr.c_str(), "chatFont.fnt");
        m_limitValueLabel->setScale(0.42f);
        m_limitValueLabel->setColor(ccColor3B{ 235, 240, 250 });
        m_limitValueLabel->setPosition(CCPoint{ rightX - 58.f, center.y - 84.f });
        m_mainLayer->addChild(m_limitValueLabel);

        auto* decBtn = createSolidButton(18.f, 18.f, ccColor3B{ 42, 46, 56 }, "<", 0.45f, ccColor3B{ 245, 248, 255 }, this, menu_selector(AIDecorPopup::onLimitDec));
        decBtn->setPosition(CCPoint{ rightX - 101.f, center.y - 84.f });
        m_buttonMenu->addChild(decBtn);

        auto* incBtn = createSolidButton(18.f, 18.f, ccColor3B{ 42, 46, 56 }, ">", 0.45f, ccColor3B{ 245, 248, 255 }, this, menu_selector(AIDecorPopup::onLimitInc));
        incBtn->setPosition(CCPoint{ rightX - 15.f, center.y - 84.f });
        m_buttonMenu->addChild(incBtn);

        auto* modePill = createSolidPanel(108.f, 22.f, ccColor3B{ 26, 28, 35 }, 255);
        modePill->setPosition(CCPoint{ rightX + 58.f, center.y - 84.f });
        m_mainLayer->addChild(modePill);

        auto createModeHalf = [&](const char* txt, CCPoint pos, SEL_MenuHandler cb, CCLayerColor*& outBg) {
            outBg = createSolidPanel(51.f, 18.f, ccColor3B{ 26, 28, 35 }, 255);
            auto* l = CCLabelBMFont::create(txt, "chatFont.fnt");
            l->setScale(0.40f);
            l->setColor(ccColor3B{ 245, 248, 255 });
            l->setPosition(CCPoint{ 25.5f, 9.f });
            outBg->addChild(l);
            auto* b = CCMenuItemSpriteExtra::create(outBg, this, cb);
            b->setPosition(pos);
            m_buttonMenu->addChild(b);
        };
        createModeHalf("Inside",   CCPoint{ rightX + 32.f, center.y - 84.f }, menu_selector(AIDecorPopup::onSelectInside),   m_insideTabBg);
        createModeHalf("Overhang", CCPoint{ rightX + 84.f, center.y - 84.f }, menu_selector(AIDecorPopup::onSelectOverhang), m_overhangTabBg);

        // Нижние кнопки: [Another variant] и [Keep]
        std::string varStr = fmt::format("Another variant ({})", g_variantIndex);
        auto* varBtn = createSolidButton(
            108.f, 28.f, ccColor3B{ 30, 32, 40 },
            varStr.c_str(), 0.42f, ccColor3B{ 215, 220, 232 },
            this, menu_selector(AIDecorPopup::onAnotherVariantClicked)
        );
        varBtn->setPosition(CCPoint{ rightX - 58.f, center.y - 116.f });
        m_buttonMenu->addChild(varBtn);

        auto* keepBtn = createSolidButton(
            108.f, 28.f, ccColor3B{ 158, 38, 68 },
            "Keep", 0.50f, ccColor3B{ 255, 245, 248 },
            this, menu_selector(AIDecorPopup::onKeepClicked)
        );
        keepBtn->setPosition(CCPoint{ rightX + 58.f, center.y - 116.f });
        m_buttonMenu->addChild(keepBtn);

        refreshRightPanelTabsAndCanvas();
    }

    void refreshRightPanelTabsAndCanvas() {
        if (m_tabShapeBg)   m_tabShapeBg->setColor((m_previewTab == 0) ? ccColor3B{ 232, 65, 98 } : ccColor3B{ 26, 28, 35 });
        if (m_tabDrawingBg) m_tabDrawingBg->setColor((m_previewTab == 1) ? ccColor3B{ 232, 65, 98 } : ccColor3B{ 26, 28, 35 });
        if (m_tabInGDBg)    m_tabInGDBg->setColor((m_previewTab == 2) ? ccColor3B{ 232, 65, 98 } : ccColor3B{ 26, 28, 35 });

        if (m_insideTabBg)   m_insideTabBg->setColor(!g_overhangAllowed ? ccColor3B{ 232, 65, 98 } : ccColor3B{ 26, 28, 35 });
        if (m_overhangTabBg) m_overhangTabBg->setColor(g_overhangAllowed  ? ccColor3B{ 232, 65, 98 } : ccColor3B{ 26, 28, 35 });

        if (!m_previewCanvasContent) return;

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
            for (auto const& [gx, gy] : m_contour.occupiedCells) {
                float px = startX + (gx - m_contour.minGX) * cellPx;
                float py = startY + (gy - m_contour.minGY) * cellPx;
                auto* blockSpr = createSolidPanel(cellPx + 0.5f, cellPx + 0.5f, ccColor3B{ 245, 248, 255 }, 255);
                blockSpr->setPosition(CCPoint{ px, py });
                m_previewCanvasContent->addChild(blockSpr);
            }
        } else {
            for (auto const& [gx, gy] : m_contour.occupiedCells) {
                float px = startX + (gx - m_contour.minGX) * cellPx;
                float py = startY + (gy - m_contour.minGY) * cellPx;

                auto* fillSpr = createSolidPanel(
                    cellPx + 0.5f, cellPx + 0.5f,
                    g_lastPreviewPalette.slotColors[static_cast<int>(ColorSlot::DarkFill)],
                    255
                );
                fillSpr->setPosition(CCPoint{ px, py });
                m_previewCanvasContent->addChild(fillSpr);

                auto* texSpr = createSolidPanel(
                    cellPx * 0.72f, cellPx * 0.72f,
                    g_lastPreviewPalette.slotColors[static_cast<int>(ColorSlot::MidTone)],
                    255
                );
                texSpr->setPosition(CCPoint{ px, py });
                m_previewCanvasContent->addChild(texSpr);
            }

            float worldMinX = m_contour.minGX * 30.f;
            float worldMinY = m_contour.minGY * 30.f;
            float scaleRatio = cellPx / 30.f;

            size_t shownCount = 0;
            for (auto const& s : g_lastPreviewSpecs) {
                if (s.layerRole == LayerRole::Fill) continue;
                if (++shownCount > 55) break;

                float px = -totalW * 0.5f + (s.x - worldMinX) * scaleRatio;
                float py = -totalH * 0.5f + (s.y - worldMinY) * scaleRatio;

                float w = std::clamp(s.scaleX * cellPx * 0.65f, 2.5f, cellPx * 1.1f);
                float h = std::clamp(s.scaleY * cellPx * 0.65f, 2.5f, cellPx * 1.1f);
                ccColor3B col = (s.colorSlot == ColorSlot::CustomRGB)
                    ? s.customColor
                    : g_lastPreviewPalette.slotColors[std::clamp(static_cast<int>(s.colorSlot), 0, 5)];

                auto* detSpr = createSolidPanel(w, h, col, 255);
                detSpr->setRotation(s.rotation);
                detSpr->setPosition(CCPoint{ px, py });
                m_previewCanvasContent->addChild(detSpr);
            }
        }
    }

    void refreshInputLabel() {
        if (!m_inputDisplayLabel) return;
        if (m_inputBuffer.empty()) {
            if (m_stage == PopupStage::ApiGate) {
                m_inputDisplayLabel->setString("Paste valid API key (Ctrl+V) or type here...");
            } else {
                m_inputDisplayLabel->setString("Describe the art or ask AI...");
            }
            m_inputDisplayLabel->setColor(ccColor3B{ 140, 146, 162 });
        } else {
            std::string shown = m_inputBuffer;
            size_t maxLen = (m_stage == PopupStage::ApiGate) ? 48 : 30;
            if (shown.size() > maxLen) {
                shown = "..." + shown.substr(shown.size() - (maxLen - 3));
            }
            shown += "|";
            m_inputDisplayLabel->setString(shown.c_str());
            m_inputDisplayLabel->setColor(ccColor3B{ 245, 248, 255 });
        }
    }

    void reopenWithStage(PopupStage nextStage, PopupStage returnStage = PopupStage::ApiGate) {
        EditorUI* ui = m_editorUI;
        ContourAnalysis contourCopy = m_contour;
        restoreLockedContourPositions();
        g_activePopup = nullptr;
        this->onClose(nullptr);
        if (auto* next = AIDecorPopup::create(ui, std::move(contourCopy), nextStage, returnStage)) {
            next->show();
        }
    }

    // Обработчики переключения провайдера и окна выбора модели
    void onCycleApiProvider(CCObject*) {
        g_apiProviderIndex = (g_apiProviderIndex + 1) % 5;
        reopenWithStage(PopupStage::ApiGate);
    }

    void onOpenModelSelectorFromGate(CCObject*) {
        reopenWithStage(PopupStage::ModelSelector, PopupStage::ApiGate);
    }

    void onOpenModelSelectorFromMain(CCObject*) {
        reopenWithStage(PopupStage::ModelSelector, PopupStage::ArtAgentMain);
    }

    void onCycleProviderInModelWindow(CCObject*) {
        g_apiProviderIndex = (g_apiProviderIndex + 1) % 5;
        reopenWithStage(PopupStage::ModelSelector, m_returnStageAfterModel);
    }

    void onCloseModelSelectorWindow(CCObject*) {
        reopenWithStage(m_returnStageAfterModel);
    }

    void selectModelSlot(int slotIdx) {
        g_selectedModelIndex[m_providerIdx] = std::clamp(slotIdx, 0, 3);
        if (g_apiVerifiedAndConnected && !g_connectedApiKey.empty()) {
            saveVerifiedApiToDisk(m_providerIdx, g_connectedApiKey);
        }
        reopenWithStage(m_returnStageAfterModel);
    }

    void onPickModel0(CCObject*) { selectModelSlot(0); }
    void onPickModel1(CCObject*) { selectModelSlot(1); }
    void onPickModel2(CCObject*) { selectModelSlot(2); }
    void onPickModel3(CCObject*) { selectModelSlot(3); }

    void onPasteApiClicked(CCObject*) {
        std::string clip = readWindowsClipboardText();
        if (!clip.empty()) {
            m_inputBuffer = clip;
            if (m_inputBuffer.size() > 180) m_inputBuffer.resize(180);
            int det = detectProviderFromKey(m_inputBuffer, m_providerIdx);
            if (det != m_providerIdx) {
                g_apiProviderIndex = det;
                m_providerIdx = det;
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
        std::string key = sanitizeApiKeyString(m_inputBuffer);
        m_providerIdx = detectProviderFromKey(key, m_providerIdx);
        g_apiProviderIndex = m_providerIdx;

        std::string errMsg;
        if (!verifyApiKeyWithServer(m_providerIdx, key, errMsg)) {
            setCheckmarkState(false);
            if (m_apiGateStatusLabel) {
                m_apiGateStatusLabel->setString(utf8ToGDReadable(errMsg).c_str());
                m_apiGateStatusLabel->setColor(ccColor3B{ 255, 95, 105 });
            }
            return;
        }

        setCheckmarkState(true);
        saveVerifiedApiToDisk(m_providerIdx, key);
        reopenWithStage(PopupStage::ArtAgentMain);
    }

    void onOpenApiGate(CCObject*) {
        reopenWithStage(PopupStage::ApiGate);
    }

    void onBackToArtAgent(CCObject*) {
        if (g_apiVerifiedAndConnected) {
            reopenWithStage(PopupStage::ArtAgentMain);
        }
    }

    void onToggleLanguage(CCObject*) {
        m_russianLayout = !m_russianLayout;
        if (m_langBadgeLabel) {
            m_langBadgeLabel->setString(m_russianLayout ? "RUS" : "ENG");
        }
    }

    void onTabShape(CCObject*)   { m_previewTab = 0; refreshRightPanelTabsAndCanvas(); }
    void onTabDrawing(CCObject*) { m_previewTab = 1; refreshRightPanelTabsAndCanvas(); }
    void onTabInGD(CCObject*)    { m_previewTab = 2; refreshRightPanelTabsAndCanvas(); }

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
        onSendToNeuralNet(nullptr);
    }

    void onKeepClicked(CCObject*) {
        this->onClose(nullptr);
    }

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

        AIDecisionResult ai = queryRealNeuralNetwork(
            promptText,
            m_contour,
            g_objectLimit,
            g_overhangAllowed,
            g_variantIndex
        );

        auto replyLines = wrapTextLines(ai.replyText, 44, 3);
        if (m_agentReplyLine1) m_agentReplyLine1->setString(replyLines[0].c_str());
        if (m_agentReplyLine2) m_agentReplyLine2->setString(replyLines[1].c_str());
        if (m_agentReplyLine3) m_agentReplyLine3->setString(replyLines[2].c_str());

        if (!ai.httpSuccess) {
            if (m_stepPlanLabel) m_stepPlanLabel->setString("API request error");
            return;
        }

        if (ai.taskMode == AITaskMode::ChatOnly) {
            if (m_stepPlanLabel)    m_stepPlanLabel->setString("chat response");
            if (m_stepDrawingLabel) m_stepDrawingLabel->setString("idle");
            if (m_stepVectorLabel)  m_stepVectorLabel->setString("idle");
            if (m_stepPlaceLabel)   m_stepPlaceLabel->setString("no blocks placed");
            return;
        }

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
        m_previewTab = 1;
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
        if (m_stage == PopupStage::ModelSelector) return;

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
            if (m_stage == PopupStage::ApiGate) {
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
            if (m_stage == PopupStage::ApiGate) {
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
                        if (m_russianLayout && m_stage == PopupStage::ArtAgentMain) {
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

    static AIDecorPopup* create(
        EditorUI* ui,
        ContourAnalysis contour,
        PopupStage requestedStage = PopupStage::ArtAgentMain,
        PopupStage prevStage = PopupStage::ApiGate
    ) {
        auto ret = new AIDecorPopup();
        if (ret && ret->init(ui, std::move(contour), requestedStage, prevStage)) {
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
        if (auto* popup = AIDecorPopup::create(this, std::move(contour), PopupStage::ArtAgentMain)) {
            popup->show();
        }
    }
};
