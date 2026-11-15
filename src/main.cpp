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
#include <thread>
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

struct SelectedOrbPortalInfo {
    float x = 0.f;
    float y = 0.f;
    float rotation = 0.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
    bool flipX = false;
    bool flipY = false;
    int objectId = 36;
    bool isDashOrb = false;
    bool isPortal = false;
    GameObject* sourceObj = nullptr;
};

struct LockedObjectState {
    Ref<GameObject> obj;
    CCPoint pos{ 0.f, 0.f };
    float rotation = 0.f;
};

struct ContourAnalysis {
    std::set<std::pair<int, int>> rawCells;      // Только исходный каркас выделенной структуры
    std::set<std::pair<int, int>> occupiedCells; // Полностью заполненная структура (каркас + внутренность)
    std::vector<SelectedSpikeInfo> spikes;
    std::vector<SelectedOrbPortalInfo> orbsAndPortals; // Орбы, дэш-орбы и порталы (декорятся ТОЛЬКО с разрешения игрока!)
    std::vector<GameObject*> solidObjects;
    std::vector<LockedObjectState> lockedStates;
    bool hasAnySelection = false;
    CCPoint cameraCenterWorld{ 240.f, 150.f };
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
    int orbPortalCount() const { return static_cast<int>(orbsAndPortals.size()); }
    int widthUnits() const { return hasAnySelection ? (std::max)(1, maxGX - minGX + 1) * 30 : 0; }
    int heightUnits() const { return hasAnySelection ? (std::max)(1, maxGY - minGY + 1) * 30 : 0; }
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
    int zLayerCode = 0; // -3=B4, -1=B3, 1=B2, 3=B1, 5=T1, 7=T2, 9=T3 (внутренние и внешние слои GD)
    int editorLayerOffset = 0; // 0..5 (свободный слой редактора)
    LayerRole layerRole = LayerRole::Fill;
    ColorSlot colorSlot = ColorSlot::CustomRGB;
    ccColor3B customColor{ 255, 255, 255 };
    GLubyte customOpacity = 255;
    bool customBlending = false;
    ccColor3B detailColor{ 255, 255, 255 };
    GLubyte detailOpacity = 255;
    bool detailBlending = false;
    bool hasSeparateDetailColor = false;
    bool noTouch = true;
    bool noGlow = true;
    int priorityTier = 1;
    bool isOverhang = false;
};

struct AICustomDrawElement {
    int directObjectId = 211; // ИИ сам решает, какой ID объекта из редактора ставить!
    float rx = 0.5f;
    float ry = 0.5f;
    float scaleX = 0.5f;
    float scaleY = 0.5f;
    float rot = 0.f;
    int shapeType = 0;
    ccColor3B color{ 255, 255, 255 };
    GLubyte opacity = 255;
    bool blending = false;
    ccColor3B detailColor{ 255, 255, 255 };
    GLubyte detailOpacity = 255;
    bool detailBlending = false;
    bool hasDetailColor = false;
    int zOrder = 6;
    int zLayerCode = 1;     // Внутренний/внешний Z-Layer в блоке (-3..9)
    int editorLayerOff = 1; // Смещение слоя редактора (0..5)
};

struct AIDecisionResult {
    bool httpSuccess = false;
    AITaskMode taskMode = AITaskMode::ChatOnly;
    std::string replyText;
    std::string planText;
    std::string styleThemeName; // Уникальное название стиля, чтобы ИИ никогда не повторялся
    int selfMatchScore = 96;    // Самооценка ИИ (0..100%) совпадения с референсом/задачей
    int refinementPasses = 1;   // Сколько итераций самопроверки и переделки выполнил ИИ
    std::string critiqueNotes;  // Заметки самопроверки ИИ
    bool hasCustomPalette = false;
    ccColor3B fillRGB{ 22, 26, 34 };
    ccColor3B midRGB{ 78, 92, 118 };
    ccColor3B mainRGB{ 165, 215, 255 };
    ccColor3B outlineRGB{ 235, 245, 255 };
    ccColor3B glowRGB{ 110, 190, 255 };
    ccColor3B gradTopRGB{ 75, 115, 185 };
    ccColor3B gradBotRGB{ 18, 22, 34 };
    bool hasGradient = false;
    bool wantRealisticPBR = false; // Включается ТОЛЬКО по запросу (или с референсом), отключается если просят "не делай реалистик/пбр"!
    bool wantBricks = false;       // Сборка кирпичной кладки из кубиков 211
    bool wantWindows = false;      // Сборка реалистичных окон из кубиков 211 (рама, стекла, переплет, блики)
    bool wantVines = false;        // Сборка свисающих лиан и листвы из кубиков 211 на каждом парте
    bool wantNeon = false;         // Неоновый стиль и односторонний неон (только когда просят неон/glow!)
    int suggestedCount = 140;

    // Прямые команды управления редактором GD (сдвиг, поворот, масштаб, дублирование, удаление, чистая покраска)
    float directMoveX = 0.0f;
    float directMoveY = 0.0f;
    float directRotateDeg = 0.0f;
    float directScaleMulX = 1.0f;
    float directScaleMulY = 1.0f;
    int directDuplicateCount = 0;
    float directDuplicateStepX = 0.0f;
    float directDuplicateStepY = 0.0f;
    bool directDeleteSelected = false;
    bool directRecolorOnly = false;

    bool wantCustomSpikes = false;      // Кастомные шипы от ИИ (собираются внутри треугольника шипа или спавнятся с нуля)
    bool allowOrbsAndPortals = false;   // Разрешение трогать орбы, дэш-орбы и порталы
    bool wantCustomOrbTexture = false;  // Кастомная текстура внутри орбов ТОЛЬКО по явной просьбе (иначе на орбах только неон!)
    bool wantBackground = false;        // Создание заднего фона (BG), если ничего не выделено или попросили фон

    // cellElements: микро-дизайн ИИ внутри КАЖДОГО блока 30x30
    std::vector<AICustomDrawElement> cellElements;
    // customElements: макро-арт ИИ на КАЖДОМ парте/острове или на заднем фоне (BG)
    std::vector<AICustomDrawElement> customElements;
    // spikeElements: кастомные элементы ИИ внутри треугольника шипа (rx: -0.48..0.48, ry: 0.0..0.95)
    std::vector<AICustomDrawElement> spikeElements;
    // orbElements: кастомные элементы ИИ внутри орбов, дэш-орбов и порталов (только по запросу игрока!)
    std::vector<AICustomDrawElement> orbElements;
};

// Состояние загруженной картинки-референса и память прошлых уникальных стилей
static bool g_hasReferenceImage = false;
static std::string g_referenceFilePath;
static std::string g_referenceFileName;
static std::string g_referenceMimeType = "image/png";
static std::string g_referenceBase64;
static std::vector<ccColor3B> g_referenceSampleGrid; // Сетка цветов референса 12x8
static int g_referenceGridW = 12;
static int g_referenceGridH = 8;
static std::vector<ccColor3B> g_referenceHighResGrid; // Высокоточная попиксельная карта 48x48 выбранной структуры из референса!
static int g_refHighResW = 96;
static int g_refHighResH = 96;
static bool g_showFilledInShapeTab = false; // Кнопка переключения "Каркас / Заполнение" во вкладке Shape
static std::vector<std::string> g_pastGeneratedThemes; // Список всех прошлых декоров (запрет повторов!)
static int g_lastSelfScore = 0;
static int g_lastRefinementPasses = 1;

static int g_lastLayoutSeed = -1;
static int g_variantIndex = 1;
static int g_objectLimit = 25000; // Никаких искусственных урезаний: ИИ сам решает сколько блоков нужно (или берет число из чата игрока!)
static bool g_overhangAllowed = true;
static std::string g_lastBuildPrompt;
static std::vector<Ref<GameObject>> g_lastPlacedObjects;
static std::vector<GeneratedObjectSpec> g_lastPreviewSpecs;
static StylePalette g_lastPreviewPalette;
static AITaskMode g_lastPreviewTaskMode = AITaskMode::FullDecor;
static bool g_hasGeneratedArt = false;
static bool g_hasPendingDrawingApproval = false; // ИИ нарисовал в Drawing и ждет согласия (Keep/In GD) или переделки!

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
    // 0: Google Gemini (актуальная линейка Gemini 4: Hydrogen, Argon, Carbon + 3.8 Flash!)
    {
        "Gemini",
        L"generativelanguage.googleapis.com",
        L"/v1beta/models/",
        L"/v1beta/models",
        true,
        {
            { "gemini-4-hydrogen", "Gemini 4 Hydrogen", "Fast multimodal & coding Gemini 4 (Recommended)" },
            { "gemini-4-argon",    "Gemini 4 Argon",    "Frontier reasoning & visual precision Gemini 4" },
            { "gemini-4-carbon",   "Gemini 4 Carbon",   "Deepest reasoning & self-verification Gemini 4" },
            { "gemini-3.8-flash",  "Gemini 3.8 Flash",  "High-speed multimodal Gemini 3.8 model" }
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
            { "deepseek-chat",     "DeepSeek V3/V4 Chat", "Fast universal DeepSeek chat model (Recommended)" },
            { "deepseek-v4-flash", "DeepSeek V4 Flash",   "1M context fast generation model" },
            { "deepseek-v4-pro",   "DeepSeek V4 Pro",     "1M context flagship coding/art model" },
            { "deepseek-reasoner", "DeepSeek Reasoner",   "Deep reasoning R1/V4 model" }
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
static int g_apiProviderIndex = 0;
static int g_selectedModelIndex[5] = { 0, 0, 0, 0, 0 };
static std::string g_connectedApiKey;
static std::string g_discoveredGeminiModel;

// Настраиваемые пользователем Base URL, Endpoint и Имя модели (принимаются ЛЮБЫЕ API-ключи и прокси!)
static std::string g_customBaseUrl      = "https://api.openai.com";
static std::string g_customEndpointPath = "/v1/chat/completions";
static std::string g_customModelName    = "gpt-4o-mini";

static const char* getCurrentModelID(int /*provIdx*/ = 0) {
    return g_customModelName.c_str();
}

static std::string stripBearerPrefix(std::string s) {
    s = trimWhitespace(s);
    while (!s.empty() && (s.front() == '"' || s.front() == '\'')) s.erase(s.begin());
    while (!s.empty() && (s.back() == '"' || s.back() == '\'' || s.back() == 0)) s.pop_back();
    s = trimWhitespace(s);
    if (s.size() > 7) {
        std::string pref = s.substr(0, 7);
        for (char& c : pref) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (pref == "bearer ") {
            s = trimWhitespace(s.substr(7));
        }
    }
    return s;
}

// Мод принимает ЛЮБЫЕ виды API-ключей (любой длины и любого формата!)
static bool hasPlausibleKeyFormat(std::string const& rawKey) {
    std::string k = stripBearerPrefix(rawKey);
    return !k.empty();
}

static std::string maskApiKeyPreview(std::string const& key) {
    if (key.empty()) return "not set";
    if (key.size() <= 8) return key;
    return key.substr(0, 4) + "..." + key.substr(key.size() - 4);
}

// Разбор пользовательского Base URL и Endpoint в (hostW, pathW, isGeminiNative)
static void resolveCustomBaseUrlAndEndpoint(
    std::string const& modelName,
    std::wstring& outHostW,
    std::wstring& outPathW,
    bool& outIsGeminiNative
) {
    std::string base = trimWhitespace(g_customBaseUrl);
    std::string ep   = trimWhitespace(g_customEndpointPath);
    if (base.empty()) base = "https://api.openai.com";
    if (ep.empty())   ep   = "/v1/chat/completions";

    // Убираем протокол https:// или http://
    std::string hostAndPrefix = base;
    size_t protoPos = hostAndPrefix.find("://");
    if (protoPos != std::string::npos) {
        hostAndPrefix = hostAndPrefix.substr(protoPos + 3);
    }
    while (!hostAndPrefix.empty() && hostAndPrefix.back() == '/') {
        hostAndPrefix.pop_back();
    }

    std::string hostOnly = hostAndPrefix;
    std::string basePathPrefix;
    size_t slashPos = hostAndPrefix.find('/');
    if (slashPos != std::string::npos) {
        hostOnly = hostAndPrefix.substr(0, slashPos);
        basePathPrefix = hostAndPrefix.substr(slashPos);
    }

    if (!ep.empty() && ep.front() != '/') {
        ep = "/" + ep;
    }

    // Если в Base URL уже написан суффикс /v1 (например https://my-proxy.ru/v1), а Endpoint начинается с /v1/..., не дублируем /v1!
    std::string fullPath = basePathPrefix + ep;
    if (basePathPrefix.size() >= 3 &&
        basePathPrefix.substr(basePathPrefix.size() - 3) == "/v1" &&
        ep.rfind("/v1/", 0) == 0) {
        fullPath = basePathPrefix + ep.substr(3);
    }

    std::string lowerHost = hostOnly;
    for (char& c : lowerHost) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string lowerPath = fullPath;
    for (char& c : lowerPath) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    outIsGeminiNative = (
        (lowerHost.find("generativelanguage.googleapis.com") != std::string::npos ||
         lowerPath.find(":generatecontent") != std::string::npos ||
         lowerPath.find("/v1beta/models") != std::string::npos) &&
        lowerPath.find("/chat/completions") == std::string::npos
    );

    if (outIsGeminiNative) {
        std::string mName = modelName.empty() ? "gemini-4-hydrogen" : modelName;
        size_t placeholder = fullPath.find("{model}");
        if (placeholder != std::string::npos) {
            fullPath.replace(placeholder, 7, mName);
        } else if (fullPath.find(":generateContent") == std::string::npos) {
            if (!fullPath.empty() && fullPath.back() != '/') fullPath += "/";
            if (fullPath.find("/models/") == std::string::npos) {
                fullPath = "/v1beta/models/" + mName + ":generateContent";
            } else {
                fullPath += mName + ":generateContent";
            }
        }
    }

    outHostW.clear();
    for (unsigned char c : hostOnly) outHostW.push_back(static_cast<wchar_t>(c));
    outPathW.clear();
    for (unsigned char c : fullPath) outPathW.push_back(static_cast<wchar_t>(c));
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

static std::string decodeJsonUnicodeEscapes(std::string const& s) {
    std::string utf8;
    utf8.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 5 < s.size() && s[i + 1] == 'u') {
            unsigned int cp = 0;
            bool valid = true;
            for (int k = 0; k < 4; ++k) {
                char h = s[i + 2 + k];
                cp <<= 4;
                if (h >= '0' && h <= '9') cp |= (h - '0');
                else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                else { valid = false; break; }
            }
            if (valid) {
                if (cp < 0x80) {
                    utf8.push_back(static_cast<char>(cp));
                } else if (cp < 0x800) {
                    utf8.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                    utf8.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else {
                    utf8.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                    utf8.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    utf8.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                i += 5;
                continue;
            }
        }
        utf8.push_back(s[i]);
    }
    return utf8ToGDReadable(utf8);
}

static std::string extractHttpErrorDetails(std::string const& body) {
    if (body.empty()) return "";
    for (const char* key : { "\"message\"", "\"detail\"", "\"error\"" }) {
        size_t pos = body.find(key);
        if (pos != std::string::npos) {
            size_t colon = body.find(':', pos + std::strlen(key));
            if (colon != std::string::npos) {
                size_t q1 = body.find('\"', colon + 1);
                if (q1 != std::string::npos) {
                    size_t q2 = body.find('\"', q1 + 1);
                    if (q2 != std::string::npos && q2 > q1 + 1) {
                        std::string msg = decodeJsonUnicodeEscapes(body.substr(q1 + 1, q2 - q1 - 1));
                        if (msg.size() > 90) msg = msg.substr(0, 87) + "...";
                        return msg;
                    }
                }
            }
        }
    }
    if (body.find("Cloudflare") != std::string::npos || body.find("cf-ray") != std::string::npos || body.find("<html") != std::string::npos) {
        return "Blocked by Cloudflare/WAF (check Base URL or VPN)";
    }
    std::string clean = decodeJsonUnicodeEscapes(body);
    if (clean.size() > 80) clean = clean.substr(0, 77) + "...";
    return clean;
}

// Автоматический резервный HTTPS-клиент через встроенный системный curl.exe Windows 10/11
// (срабатывает мгновенно, если WinHTTP блокируется прокси/VPN/файрволом или TLS-сертификатами!)
static bool performCurlFallbackRequest(
    const wchar_t* verbW,
    const wchar_t* hostW,
    std::wstring const& pathW,
    std::wstring const& extraHeadersW,
    std::string const& bodyData,
    int& outHttpStatus,
    std::string& outResponseBody,
    std::string& outErrorText
) {
#ifdef GEODE_IS_WINDOWS
    auto wideToAscii = [](std::wstring const& w) -> std::string {
        std::string s;
        s.reserve(w.size());
        for (wchar_t ch : w) {
            if (ch > 0 && ch < 128) s.push_back(static_cast<char>(ch));
        }
        return s;
    };

    std::string verb = wideToAscii(verbW);
    std::string host = wideToAscii(hostW);
    std::string path = wideToAscii(pathW);
    std::string hdrs = wideToAscii(extraHeadersW);
    std::string url  = "https://" + host + path;

    std::string reqBodyFile = "ai_agent_req_body.tmp";
    if (!bodyData.empty()) {
        std::ofstream bf(reqBodyFile, std::ios::binary | std::ios::trunc);
        bf.write(bodyData.data(), static_cast<std::streamsize>(bodyData.size()));
    }

    std::string cmd = "curl.exe -s -S -k -L --connect-timeout 12 --max-time 45 -A \"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\" -H \"Accept: application/json\" -X " + verb;
    std::istringstream hss(hdrs);
    std::string hLine;
    while (std::getline(hss, hLine)) {
        while (!hLine.empty() && (hLine.back() == '\r' || hLine.back() == '\n' || hLine.back() == ' ')) hLine.pop_back();
        if (!hLine.empty()) {
            cmd += " -H \"" + hLine + "\"";
        }
    }
    if (!bodyData.empty()) {
        cmd += " --data-binary @\"" + reqBodyFile + "\"";
    }
    cmd += " -w \"\\nHTTP_STATUS_CODE:%{http_code}\" \"" + url + "\"";

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        outErrorText = "Network error contacting AI server";
        return false;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError  = hWrite;

    PROCESS_INFORMATION pi{};
    std::vector<char> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back('\0');

    BOOL started = CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);

    if (!started) {
        CloseHandle(hRead);
        outErrorText = "Network error contacting AI server";
        return false;
    }

    std::string output;
    char buf[1024];
    DWORD readBytes = 0;
    while (ReadFile(hRead, buf, sizeof(buf), &readBytes, nullptr) && readBytes > 0) {
        output.append(buf, readBytes);
    }
    WaitForSingleObject(pi.hProcess, 45000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hRead);

    size_t marker = output.rfind("HTTP_STATUS_CODE:");
    if (marker != std::string::npos) {
        std::string codeStr = output.substr(marker + 17);
        outHttpStatus = std::atoi(codeStr.c_str());
        outResponseBody = output.substr(0, marker);
        while (!outResponseBody.empty() && (outResponseBody.back() == '\n' || outResponseBody.back() == '\r')) {
            outResponseBody.pop_back();
        }
        if (outHttpStatus == 200) return true;
        if (outHttpStatus > 0) {
            std::string det = extractHttpErrorDetails(outResponseBody);
            outErrorText = det.empty() ? fmt::format("HTTP {}", outHttpStatus) : det;
            return false;
        }
    }
#else
    (void)verbW; (void)hostW; (void)pathW; (void)extraHeadersW; (void)bodyData; (void)outHttpStatus; (void)outResponseBody;
#endif
    outErrorText = "Network error contacting AI server";
    return false;
}

// Универсальный HTTPS-клиент через системный winhttp.dll (с поддержкой системного прокси/VPN, TLS 1.2/1.3 и авто-фолбэком на curl.exe!)
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
    using FnWinHttpSetOption = BOOL(WINAPI*)(HINTERNET_T, DWORD, LPVOID, DWORD);
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
        return performCurlFallbackRequest(verbW, hostW, pathW, extraHeadersW, bodyData, outHttpStatus, outResponseBody, outErrorText);
    }

    auto pOpen = reinterpret_cast<FnWinHttpOpen>(GetProcAddress(hWinHttp, "WinHttpOpen"));
    auto pSetTimeouts = reinterpret_cast<FnWinHttpSetTimeouts>(GetProcAddress(hWinHttp, "WinHttpSetTimeouts"));
    auto pSetOption = reinterpret_cast<FnWinHttpSetOption>(GetProcAddress(hWinHttp, "WinHttpSetOption"));
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
        return performCurlFallbackRequest(verbW, hostW, pathW, extraHeadersW, bodyData, outHttpStatus, outResponseBody, outErrorText);
    }

    // Пробуем сначала автоматический системный прокси/VPN Windows (4 = AUTOMATIC_PROXY), затем прямое соединение (1 = NO_PROXY)
    constexpr DWORD kAccessTypes[2] = { 4, 1 };
    for (DWORD accessType : kAccessTypes) {
        HINTERNET_T hSession = pOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36", accessType, nullptr, nullptr, 0);
        if (!hSession) continue;

        if (pSetTimeouts) {
            pSetTimeouts(hSession, 8000, 8000, 15000, 45000);
        }
        // Включаем поддержку TLS 1.2 + TLS 1.3 (WINHTTP_OPTION_SECURE_PROTOCOLS = 84)
        if (pSetOption) {
            DWORD tlsFlags = 0x00000800 | 0x00002000 | 0x00000200;
            pSetOption(hSession, 84, &tlsFlags, sizeof(tlsFlags));
        }

        HINTERNET_T hConnect = pConnect(hSession, hostW, 443, 0);
        if (!hConnect) {
            pClose(hSession);
            continue;
        }

        constexpr DWORD kWinHttpFlagSecure = 0x00800000;
        HINTERNET_T hRequest = pOpenReq(
            hConnect, verbW, pathW.c_str(),
            nullptr, nullptr, nullptr, kWinHttpFlagSecure
        );
        if (!hRequest) {
            pClose(hConnect);
            pClose(hSession);
            continue;
        }

        // Отключаем ошибки проверки локальных сертификатов / CRL при работе через VPN или прокси (WINHTTP_OPTION_SECURITY_FLAGS = 31)
        if (pSetOption) {
            DWORD secFlags = 0x00000100 | 0x00000200 | 0x00001000 | 0x00002000;
            pSetOption(hRequest, 31, &secFlags, sizeof(secFlags));
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

        if (sent && pRecvResp(hRequest, nullptr)) {
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
            // Если WinHTTP получил 403 (частая блокировка TLS-отпечатка со стороны Cloudflare/WAF на шлюзах),
            // пробуем обойти через системный curl.exe!
            if (outHttpStatus == 403) {
                int curlStatus = 0;
                std::string curlResp, curlErr;
                if (performCurlFallbackRequest(verbW, hostW, pathW, extraHeadersW, bodyData, curlStatus, curlResp, curlErr)) {
                    FreeLibrary(hWinHttp);
                    outHttpStatus = curlStatus;
                    outResponseBody = std::move(curlResp);
                    outErrorText.clear();
                    return true;
                }
                if (curlStatus > 0) {
                    outHttpStatus = curlStatus;
                    outResponseBody = std::move(curlResp);
                }
            }
            FreeLibrary(hWinHttp);
            if (outHttpStatus != 200) {
                outErrorText = extractHttpErrorDetails(outResponseBody);
            }
            return (outHttpStatus == 200);
        }

        pClose(hRequest);
        pClose(hConnect);
        pClose(hSession);
    }

    FreeLibrary(hWinHttp);
    // Если WinHTTP не смог достучаться — автоматически используем системный curl.exe!
    return performCurlFallbackRequest(verbW, hostW, pathW, extraHeadersW, bodyData, outHttpStatus, outResponseBody, outErrorText);
#else
    (void)verbW; (void)hostW; (void)pathW; (void)extraHeadersW; (void)bodyData;
    outErrorText = "Windows required for WinHTTP";
    return false;
#endif
}

// Извлекает первую доступную flash/pro модель из ответа GET /v1beta/models?key=...
static void extractAvailableGeminiModelFromList(std::string const& modelsJson) {
    for (const char* preferred : { "gemini-4-hydrogen", "gemini-4-argon", "gemini-4-carbon", "gemini-3.8-flash" }) {
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
    return stripBearerPrefix(s);
}

static std::string extractJsonStringField(std::string const& json, std::string const& key);

// Настоящая строгая проверка API-ключа, Base URL, Endpoint и Model через реальный тестовый запрос к серверу!
// Рандомный или нерабочий ключ ГАРАНТИРОВАННО отклоняется.
static bool verifyApiKeyWithServer(int /*providerIdx*/, std::string const& rawKey, std::string& outErrorMsg) {
    std::string key = sanitizeApiKeyString(rawKey);
    if (key.empty() || key.size() < 8) {
        outErrorMsg = "Key format invalid (empty or too short)";
        return false;
    }
    if (trimWhitespace(g_customBaseUrl).empty()) {
        g_customBaseUrl = "https://api.openai.com";
    }
    if (trimWhitespace(g_customEndpointPath).empty()) {
        g_customEndpointPath = "/v1/chat/completions";
    }
    if (trimWhitespace(g_customModelName).empty()) {
        g_customModelName = "gpt-4o-mini";
    }

    std::wstring hostW;
    std::wstring pathW;
    bool isGeminiNative = false;
    resolveCustomBaseUrlAndEndpoint(g_customModelName, hostW, pathW, isGeminiNative);

    int httpStatus = 0;
    std::string respBody;
    std::string netErr;
    bool ok = false;

    if (isGeminiNative) {
        std::string testBody = "{\"contents\":[{\"role\":\"user\",\"parts\":[{\"text\":\"ping\"}]}],\"generationConfig\":{\"maxOutputTokens\":8}}";
        std::wstring hdrW = L"Content-Type: application/json\r\nAccept: application/json\r\nx-goog-api-key: " + utf8ToWideASCII(key) + L"\r\n";
        ok = performWinHttpRequest(L"POST", hostW.c_str(), pathW, hdrW, testBody, httpStatus, respBody, netErr);
        if (!ok && (httpStatus == 401 || httpStatus == 403 || httpStatus == 400)) {
            std::wstring pathWithKeyW = pathW;
            if (pathWithKeyW.find(L"key=") == std::wstring::npos) {
                pathWithKeyW += (pathWithKeyW.find(L'?') == std::wstring::npos ? L"?key=" : L"&key=") + utf8ToWideASCII(key);
            }
            ok = performWinHttpRequest(L"POST", hostW.c_str(), pathWithKeyW, L"Content-Type: application/json\r\nAccept: application/json\r\n", testBody, httpStatus, respBody, netErr);
        }
        if (ok && httpStatus == 200 && (respBody.find("\"candidates\"") != std::string::npos || respBody.find("\"text\"") != std::string::npos)) {
            outErrorMsg.clear();
            return true;
        }
    } else {
        std::string activeModel = g_customModelName.empty() ? "gpt-4o-mini" : g_customModelName;
        std::string testPayload = fmt::format(
            "{{\"model\":\"{}\",\"messages\":[{{\"role\":\"user\",\"content\":\"ping\"}}],\"max_tokens\":8}}",
            jsonEscape(activeModel)
        );
        std::wstring hdrBearerW =
            L"Content-Type: application/json\r\n"
            L"Accept: application/json\r\n"
            L"Authorization: Bearer " + utf8ToWideASCII(key) + L"\r\n";
        std::wstring hdrXApiKeyW =
            L"Content-Type: application/json\r\n"
            L"Accept: application/json\r\n"
            L"x-api-key: " + utf8ToWideASCII(key) + L"\r\n";
        std::wstring hdrApiKeyW =
            L"Content-Type: application/json\r\n"
            L"Accept: application/json\r\n"
            L"api-key: " + utf8ToWideASCII(key) + L"\r\n";

        ok = performWinHttpRequest(L"POST", hostW.c_str(), pathW, hdrBearerW, testPayload, httpStatus, respBody, netErr);
        if (!ok && (httpStatus == 401 || httpStatus == 403)) {
            ok = performWinHttpRequest(L"POST", hostW.c_str(), pathW, hdrXApiKeyW, testPayload, httpStatus, respBody, netErr);
        }
        if (!ok && (httpStatus == 401 || httpStatus == 403)) {
            ok = performWinHttpRequest(L"POST", hostW.c_str(), pathW, hdrApiKeyW, testPayload, httpStatus, respBody, netErr);
        }
        if (ok && httpStatus == 200 && (respBody.find("\"choices\"") != std::string::npos || respBody.find("\"content\"") != std::string::npos || respBody.find("\"text\"") != std::string::npos)) {
            outErrorMsg.clear();
            return true;
        }
    }

    if (httpStatus > 0) {
        std::string det = netErr.empty() ? extractHttpErrorDetails(respBody) : netErr;
        outErrorMsg = det.empty()
            ? fmt::format("Rejected (HTTP {})", httpStatus)
            : fmt::format("HTTP {}: {}", httpStatus, det);
        return false;
    }
    outErrorMsg = netErr.empty() ? "Network error contacting AI server" : netErr;
    return false;
}

static void saveVerifiedApiToDisk(int /*provIdx*/, std::string const& rawKey) {
    std::string key = sanitizeApiKeyString(rawKey);
    g_apiConfigLoaded = true;
    g_connectedApiKey = key;
    g_apiVerifiedAndConnected = !key.empty();
    std::ofstream out("ai_decor_api.cfg", std::ios::trunc);
    if (out.is_open()) {
        out << g_connectedApiKey << "\n"
            << g_customBaseUrl << "\n"
            << g_customEndpointPath << "\n"
            << g_customModelName << "\n";
    }
}

static void loadAndVerifySavedApiFromDisk() {
    if (g_apiConfigLoaded) return;
    g_apiConfigLoaded = true;

    std::ifstream in("ai_decor_api.cfg");
    if (!in.is_open()) return;
    std::string line1, line2, line3, line4;
    if (std::getline(in, line1)) {
        line1 = trimWhitespace(line1);
        if (std::getline(in, line2) && std::getline(in, line3) && std::getline(in, line4)) {
            g_connectedApiKey    = sanitizeApiKeyString(line1);
            g_customBaseUrl      = trimWhitespace(line2);
            g_customEndpointPath = trimWhitespace(line3);
            g_customModelName    = trimWhitespace(line4);
            std::string dummyErr;
            if (!g_connectedApiKey.empty() && verifyApiKeyWithServer(0, g_connectedApiKey, dummyErr)) {
                g_apiVerifiedAndConnected = true;
            } else {
                g_apiVerifiedAndConnected = false;
            }
        } else if (!line2.empty()) {
            // Совместимость со старым форматом конфига
            g_connectedApiKey = sanitizeApiKeyString(line2);
            if (!g_connectedApiKey.empty()) {
                if (g_connectedApiKey.rfind("AIza", 0) == 0) {
                    g_customBaseUrl = "https://generativelanguage.googleapis.com";
                    g_customEndpointPath = "/v1beta/models/{model}:generateContent";
                    g_customModelName = "gemini-4-hydrogen";
                }
                g_apiVerifiedAndConnected = true;
            }
        }
    }
}

// ============================================================================
// Парсер JSON-ответа от реальной ИИ-нейросети
// ============================================================================
static std::string extractGeminiFinalTextPart(std::string const& rawResp) {
    std::string bestJsonPart;
    std::string lastNonEmptyPart;
    size_t searchPos = 0;

    while (searchPos < rawResp.size()) {
        size_t pos = rawResp.find("\"text\"", searchPos);
        if (pos == std::string::npos) break;
        size_t colon = rawResp.find(':', pos + 6);
        if (colon == std::string::npos) break;

        size_t valPos = colon + 1;
        while (valPos < rawResp.size() && (rawResp[valPos] == ' ' || rawResp[valPos] == '\t' || rawResp[valPos] == '\r' || rawResp[valPos] == '\n')) {
            ++valPos;
        }
        if (valPos >= rawResp.size() || rawResp[valPos] != '\"') {
            searchPos = valPos + 1;
            continue;
        }

        size_t cur = valPos + 1;
        std::string val;
        while (cur < rawResp.size()) {
            char c = rawResp[cur++];
            if (c == '\\' && cur < rawResp.size()) {
                char esc = rawResp[cur++];
                if (esc == 'n' || esc == 'r' || esc == 't') val.push_back(' ');
                else if (esc == '\"') val.push_back('\"');
                else if (esc == '\\') val.push_back('\\');
                else val.push_back(esc);
            } else if (c == '\"') {
                break;
            } else {
                val.push_back(c);
            }
        }

        if (!val.empty()) {
            lastNonEmptyPart = val;
            if (val.find("\"action\"") != std::string::npos || val.find("\"fill_rgb\"") != std::string::npos || val.find('{') != std::string::npos) {
                bestJsonPart = val;
            }
        }
        searchPos = cur;
    }

    return !bestJsonPart.empty() ? bestJsonPart : lastNonEmptyPart;
}

// Извлекает последнее непустое строковое значение поля key (корректно пропуская "content": null!)
static std::string extractJsonStringField(std::string const& json, std::string const& key) {
    std::string pattern = "\"" + key + "\"";
    std::string bestOut;
    size_t searchFrom = 0;

    while (searchFrom < json.size()) {
        size_t pos = json.find(pattern, searchFrom);
        if (pos == std::string::npos) break;
        size_t colon = json.find(':', pos + pattern.size());
        if (colon == std::string::npos) break;

        size_t valPos = colon + 1;
        while (valPos < json.size() && (json[valPos] == ' ' || json[valPos] == '\t' || json[valPos] == '\r' || json[valPos] == '\n')) {
            ++valPos;
        }
        if (valPos >= json.size() || json[valPos] != '\"') {
            // Значение не строка (например, null, число или объект) — ищем следующее вхождение ключа!
            searchFrom = valPos + 1;
            continue;
        }

        size_t cur = valPos + 1;
        std::string out;
        while (cur < json.size()) {
            char c = json[cur++];
            if (c == '\\' && cur < json.size()) {
                char esc = json[cur++];
                if (esc == 'n') out.push_back(' ');
                else if (esc == 'r' || esc == 't') out.push_back(' ');
                else if (esc == '\"') out.push_back('\"');
                else if (esc == '\\') out.push_back('\\');
                else out.push_back(esc);
            } else if (c == '\"') {
                break;
            } else {
                out.push_back(c);
            }
        }
        if (!out.empty()) {
            bestOut = out;
            // Если внутри уже есть JSON с "action", сразу возвращаем его!
            if (out.find("\"action\"") != std::string::npos || out.find("{") != std::string::npos) {
                return out;
            }
        }
        searchFrom = cur;
    }
    return bestOut;
}

// Извлекает контент ответа из OpenAI / DeepSeek / Groq / OpenRouter (поддерживает content, reasoning_content и обрезанные ответы!)
static std::string extractOpenAICompatibleContent(std::string const& rawResp) {
    std::string c = extractJsonStringField(rawResp, "content");
    if (!c.empty()) return c;
    std::string r = extractJsonStringField(rawResp, "reasoning_content");
    if (!r.empty()) return r;
    std::string t = extractJsonStringField(rawResp, "text");
    return t;
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

// ============================================================================
// Каталог реальных ID объектов из вкладок редактора GD
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

    int idGlowEdgeSmall = 211;
    int idGlowBeam      = 211;
};

// Кодирование бинарного файла картинки в Base64 для отправки в Multimodal Vision API (Gemini / OpenAI)
static std::string encodeBytesToBase64(std::vector<uint8_t> const& data) {
    static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < data.size()) {
        uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                     (static_cast<uint32_t>(data[i + 1]) << 8) |
                     static_cast<uint32_t>(data[i + 2]);
        out.push_back(kTable[(n >> 18) & 63]);
        out.push_back(kTable[(n >> 12) & 63]);
        out.push_back(kTable[(n >> 6) & 63]);
        out.push_back(kTable[n & 63]);
        i += 3;
    }
    if (i < data.size()) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < data.size()) n |= (static_cast<uint32_t>(data[i + 1]) << 8);
        out.push_back(kTable[(n >> 18) & 63]);
        out.push_back(kTable[(n >> 12) & 63]);
        out.push_back((i + 1 < data.size()) ? kTable[(n >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

// Загрузка картинки-референса с диска + извлечение реальной сетки цветов 12x8 через CCImage
static bool loadReferenceImageFromPath(std::string const& filePath, std::string& outStatus) {
    std::ifstream in(filePath, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        outStatus = "Cannot open image file";
        return false;
    }
    std::streamsize sz = in.tellg();
    if (sz <= 0 || sz > 12 * 1024 * 1024) {
        outStatus = "Image file must be between 1B and 12MB";
        return false;
    }
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(sz));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), sz)) {
        outStatus = "Failed to read image bytes";
        return false;
    }

    std::string lower = filePath;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.find(".jpg") != std::string::npos || lower.find(".jpeg") != std::string::npos) {
        g_referenceMimeType = "image/jpeg";
    } else if (lower.find(".webp") != std::string::npos) {
        g_referenceMimeType = "image/webp";
    } else {
        g_referenceMimeType = "image/png";
    }

    g_referenceBase64 = encodeBytesToBase64(bytes);
    g_referenceFilePath = filePath;

    size_t slashPos = filePath.find_last_of("/\\");
    g_referenceFileName = (slashPos == std::string::npos) ? filePath : filePath.substr(slashPos + 1);
    if (g_referenceFileName.size() > 20) {
        g_referenceFileName = g_referenceFileName.substr(0, 17) + "...";
    }

    // Снимаем попиксельную карту 48x48 ОДНОЙ структуры из референса (автоматически отсекая однородный фон по краям!)
    g_referenceSampleGrid.assign(g_referenceGridW * g_referenceGridH, ccColor3B{ 120, 140, 180 });
    g_referenceHighResGrid.assign(g_refHighResW * g_refHighResH, ccColor3B{ 120, 140, 180 });
    CCImage img;
    if (img.initWithImageData(bytes.data(), static_cast<int>(bytes.size()))) {
        unsigned char* raw = img.getData();
        int w = img.getWidth();
        int h = img.getHeight();
        bool hasAlpha = img.hasAlpha();
        int bpp = hasAlpha ? 4 : 3;
        if (raw && w > 0 && h > 0) {
            // Определяем цвет фона по углам изображения, чтобы выделить ОДНУ главную структуру с мелкими текстурами
            ccColor3B bgCol{ raw[0], raw[1], raw[2] };
            int cropX0 = 0, cropX1 = w - 1;
            int cropY0 = 0, cropY1 = h - 1;

            int minX = w, maxX = 0, minY = h, maxY = 0;
            int fgCount = 0;
            for (int y = 0; y < h; y += (std::max)(1, h / 120)) {
                for (int x = 0; x < w; x += (std::max)(1, w / 120)) {
                    size_t idx = static_cast<size_t>((y * w + x) * bpp);
                    int dr = std::abs(static_cast<int>(raw[idx + 0]) - static_cast<int>(bgCol.r));
                    int dg = std::abs(static_cast<int>(raw[idx + 1]) - static_cast<int>(bgCol.g));
                    int db = std::abs(static_cast<int>(raw[idx + 2]) - static_cast<int>(bgCol.b));
                    bool notBg = (dr + dg + db > 42) && (!hasAlpha || raw[idx + 3] > 30);
                    if (notBg) {
                        minX = (std::min)(minX, x);
                        maxX = (std::max)(maxX, x);
                        minY = (std::min)(minY, y);
                        maxY = (std::max)(maxY, y);
                        fgCount++;
                    }
                }
            }
            if (fgCount > 20 && (maxX - minX) >= w / 8 && (maxY - minY) >= h / 8) {
                cropX0 = minX;
                cropX1 = maxX;
                cropY0 = minY;
                cropY1 = maxY;
            }
            int cw = (std::max)(1, cropX1 - cropX0 + 1);
            int ch = (std::max)(1, cropY1 - cropY0 + 1);

            for (int gy = 0; gy < g_referenceGridH; ++gy) {
                for (int gx = 0; gx < g_referenceGridW; ++gx) {
                    int px = std::clamp(cropX0 + (gx * cw + cw / 2) / g_referenceGridW, 0, w - 1);
                    int py = std::clamp(cropY0 + ((g_referenceGridH - 1 - gy) * ch + ch / 2) / g_referenceGridH, 0, h - 1);
                    size_t idx = static_cast<size_t>((py * w + px) * bpp);
                    g_referenceSampleGrid[gy * g_referenceGridW + gx] = ccColor3B{ raw[idx + 0], raw[idx + 1], raw[idx + 2] };
                }
            }

            for (int gy = 0; gy < g_refHighResH; ++gy) {
                for (int gx = 0; gx < g_refHighResW; ++gx) {
                    int px = std::clamp(cropX0 + (gx * cw + cw / 2) / g_refHighResW, 0, w - 1);
                    int py = std::clamp(cropY0 + ((g_refHighResH - 1 - gy) * ch + ch / 2) / g_refHighResH, 0, h - 1);
                    size_t idx = static_cast<size_t>((py * w + px) * bpp);
                    g_referenceHighResGrid[gy * g_refHighResW + gx] = ccColor3B{ raw[idx + 0], raw[idx + 1], raw[idx + 2] };
                }
            }
        }
    }

    g_hasReferenceImage = true;
    outStatus = "Ref loaded: " + g_referenceFileName;
    return true;
}

// Чтение скопированной картинки (CF_DIB / скриншот Win+Shift+S), файла (CF_HDROP) или пути из буфера обмена Windows
static bool tryLoadReferenceImageFromClipboard(std::string& outStatus) {
#ifdef GEODE_IS_WINDOWS
    if (!OpenClipboard(nullptr)) return false;

    // 1. Проверяем CF_HDROP (если игрок скопировал файл картинки через Ctrl+C в Проводнике)
    constexpr uint32_t kCF_HDROP = 15;
    HANDLE hDrop = GetClipboardData(kCF_HDROP);
    if (hDrop) {
        HMODULE hShell = LoadLibraryA("shell32.dll");
        if (hShell) {
            using FnDragQueryFileA = uint32_t(WINAPI*)( void*, uint32_t, char*, uint32_t );
            auto pDragQuery = reinterpret_cast<FnDragQueryFileA>(GetProcAddress(hShell, "DragQueryFileA"));
            if (pDragQuery) {
                char pathBuf[1024]{};
                if (pDragQuery(hDrop, 0, pathBuf, sizeof(pathBuf)) > 0 && pathBuf[0] != 0) {
                    CloseClipboard();
                    FreeLibrary(hShell);
                    return loadReferenceImageFromPath(std::string(pathBuf), outStatus);
                }
            }
            FreeLibrary(hShell);
        }
    }

    // 2. Проверяем CF_DIB (если игрок сделал скриншот Win+Shift+S или нажал "Копировать картинку" в браузере!)
    constexpr uint32_t kCF_DIB = 8;
    HANDLE hDib = GetClipboardData(kCF_DIB);
    if (hDib) {
        size_t dibSize = static_cast<size_t>(GlobalSize(hDib));
        const uint8_t* dibPtr = static_cast<const uint8_t*>(GlobalLock(hDib));
        if (dibPtr && dibSize >= 40 && dibSize <= 12 * 1024 * 1024) {
            uint32_t headerSize = 0;
            std::memcpy(&headerSize, dibPtr, 4);
            uint32_t fileSize = static_cast<uint32_t>(14 + dibSize);
            uint32_t pixelOffset = 14 + headerSize;
            std::vector<uint8_t> bmpBytes(fileSize, 0);
            bmpBytes[0] = 'B';
            bmpBytes[1] = 'M';
            std::memcpy(bmpBytes.data() + 2, &fileSize, 4);
            std::memcpy(bmpBytes.data() + 10, &pixelOffset, 4);
            std::memcpy(bmpBytes.data() + 14, dibPtr, dibSize);
            GlobalUnlock(hDib);
            CloseClipboard();

            g_referenceMimeType = "image/bmp";
            g_referenceBase64 = encodeBytesToBase64(bmpBytes);
            g_referenceFilePath = "clipboard_image.bmp";
            g_referenceFileName = "Clipboard Image";
            g_referenceSampleGrid.assign(g_referenceGridW * g_referenceGridH, ccColor3B{ 120, 140, 180 });
            CCImage img;
            g_referenceHighResGrid.assign(g_refHighResW * g_refHighResH, ccColor3B{ 120, 140, 180 });
            if (img.initWithImageData(bmpBytes.data(), static_cast<int>(bmpBytes.size()))) {
                unsigned char* raw = img.getData();
                int w = img.getWidth();
                int h = img.getHeight();
                int bpp = img.hasAlpha() ? 4 : 3;
                if (raw && w > 0 && h > 0) {
                    for (int gy = 0; gy < g_referenceGridH; ++gy) {
                        for (int gx = 0; gx < g_referenceGridW; ++gx) {
                            int px = std::clamp((gx * w + w / 2) / g_referenceGridW, 0, w - 1);
                            int py = std::clamp(((g_referenceGridH - 1 - gy) * h + h / 2) / g_referenceGridH, 0, h - 1);
                            size_t idx = static_cast<size_t>((py * w + px) * bpp);
                            g_referenceSampleGrid[gy * g_referenceGridW + gx] = ccColor3B{
                                raw[idx + 0], raw[idx + 1], raw[idx + 2]
                            };
                        }
                    }
                    for (int gy = 0; gy < g_refHighResH; ++gy) {
                        for (int gx = 0; gx < g_refHighResW; ++gx) {
                            int px = std::clamp((gx * w + w / 2) / g_refHighResW, 0, w - 1);
                            int py = std::clamp(((g_refHighResH - 1 - gy) * h + h / 2) / g_refHighResH, 0, h - 1);
                            size_t idx = static_cast<size_t>((py * w + px) * bpp);
                            g_referenceHighResGrid[gy * g_refHighResW + gx] = ccColor3B{
                                raw[idx + 0], raw[idx + 1], raw[idx + 2]
                            };
                        }
                    }
                }
            }
            g_hasReferenceImage = true;
            outStatus = "Ref loaded from clipboard";
            return true;
        }
        if (dibPtr) GlobalUnlock(hDib);
    }

    // 3. Проверяем текстовый путь в буфере обмена
    HANDLE hText = GetClipboardData(CF_TEXT);
    if (hText) {
        char const* rawTxt = static_cast<char const*>(GlobalLock(hText));
        if (rawTxt) {
            std::string candidate = sanitizeApiKeyString(rawTxt);
            GlobalUnlock(hText);
            std::string lower = candidate;
            for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower.find(".png") != std::string::npos ||
                lower.find(".jpg") != std::string::npos ||
                lower.find(".jpeg") != std::string::npos ||
                lower.find(".webp") != std::string::npos ||
                lower.find(".bmp") != std::string::npos) {
                CloseClipboard();
                return loadReferenceImageFromPath(candidate, outStatus);
            }
        }
    }

    CloseClipboard();
#endif
    (void)outStatus;
    return false;
}

// Асинхронный выбор файла через отдельный изолированный STA-процесс PowerShell OpenFileDialog:
// гарантированно открывается поверх полноэкранного GD, не зависает и мгновенно закрывается!
static bool g_fileDialogThreadActive = false;
static bool g_fileDialogHasResult = false;
static bool g_fileDialogPickedSuccess = false;
static std::string g_fileDialogPickedPath;

static void startAsyncReferenceImageDialog() {
    if (g_fileDialogThreadActive) return;
    g_fileDialogThreadActive = true;
    g_fileDialogHasResult = false;
    g_fileDialogPickedSuccess = false;
    g_fileDialogPickedPath.clear();

#ifdef GEODE_IS_WINDOWS
    std::thread([]() {
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = nullptr;

        HANDLE hReadPipe = nullptr;
        HANDLE hWritePipe = nullptr;
        if (CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
            SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOA si{};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
            si.wShowWindow = SW_HIDE;
            si.hStdOutput = hWritePipe;
            si.hStdError  = hWritePipe;

            PROCESS_INFORMATION pi{};
            std::string cmd =
                "powershell.exe -NoProfile -NonInteractive -STA -ExecutionPolicy Bypass -Command \""
                "Add-Type -AssemblyName System.Windows.Forms; "
                "$f = New-Object System.Windows.Forms.OpenFileDialog; "
                "$f.Title = 'Select Reference Image for AI Art Agent'; "
                "$f.Filter = 'Images (*.png;*.jpg;*.jpeg;*.webp;*.bmp)|*.png;*.jpg;*.jpeg;*.webp;*.bmp|All Files (*.*)|*.*'; "
                "$f.TopMost = $true; "
                "if ($f.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { Write-Output $f.FileName }"
                "\"";

            std::vector<char> cmdBuf(cmd.begin(), cmd.end());
            cmdBuf.push_back('\0');

            BOOL ok = CreateProcessA(
                nullptr,
                cmdBuf.data(),
                nullptr,
                nullptr,
                TRUE,
                CREATE_NO_WINDOW,
                nullptr,
                nullptr,
                &si,
                &pi
            );

            CloseHandle(hWritePipe);

            if (ok) {
                std::string output;
                char buf[512];
                DWORD readBytes = 0;
                while (ReadFile(hReadPipe, buf, sizeof(buf) - 1, &readBytes, nullptr) && readBytes > 0) {
                    output.append(buf, readBytes);
                }
                WaitForSingleObject(pi.hProcess, 5000);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);

                output = trimWhitespace(output);
                // Берем последнюю непустую строку (путь к выбранному файлу)
                size_t lastNl = output.find_last_of("\r\n");
                if (lastNl != std::string::npos) {
                    output = trimWhitespace(output.substr(lastNl + 1));
                }
                if (!output.empty() && output.find(":\\") != std::string::npos) {
                    g_fileDialogPickedPath = output;
                    g_fileDialogPickedSuccess = true;
                }
            }
            CloseHandle(hReadPipe);
        }

        g_fileDialogHasResult = true;
        g_fileDialogThreadActive = false;
    }).detach();
#else
    g_fileDialogHasResult = true;
    g_fileDialogThreadActive = false;
#endif
}

static std::string buildReferenceGridSummaryForPrompt() {
    if (!g_hasReferenceImage || g_referenceSampleGrid.size() < static_cast<size_t>(g_referenceGridW * g_referenceGridH)) {
        return "No reference image attached.";
    }
    std::ostringstream oss;
    oss << "REFERENCE IMAGE ATTACHED (" << g_referenceFileName << "). "
        << "Sampled 6x4 region RGB map from bottom (ry=0.0) to top (ry=1.0), left (rx=0.0) to right (rx=1.0): ";
    for (int ry = 0; ry < 4; ++ry) {
        for (int rx = 0; rx < 6; ++rx) {
            int gx = rx * 2;
            int gy = ry * 2;
            ccColor3B c = g_referenceSampleGrid[gy * g_referenceGridW + gx];
            oss << fmt::format("[rx={:.2f},ry={:.2f}:RGB({},{},{})] ",
                (rx + 0.5f) / 6.0f, (ry + 0.5f) / 4.0f, c.r, c.g, c.b);
        }
    }
    return oss.str();
}

static void parseNamedElementsArrayFromAIJson(
    std::string const& innerJson,
    const char* arrayKey,
    std::vector<AICustomDrawElement>& outList
) {
    std::string keyPattern = std::string("\"") + arrayKey + "\"";
    size_t pos = innerJson.find(keyPattern);
    if (pos == std::string::npos) return;
    pos = innerJson.find('[', pos + keyPattern.size());
    if (pos == std::string::npos) return;

    // Находим закрывающую скобку ']' именно этого массива
    size_t endArr = innerJson.find(']', pos);
    if (endArr == std::string::npos || endArr <= pos) endArr = innerJson.size();

    size_t cur = pos;
    while (cur < endArr) {
        size_t objStart = innerJson.find('{', cur);
        if (objStart == std::string::npos || objStart >= endArr) break;
        size_t objEnd = innerJson.find('}', objStart);
        if (objEnd == std::string::npos || objEnd > endArr) break;

        std::string item = innerJson.substr(objStart, objEnd - objStart + 1);
        AICustomDrawElement el;
        el.directObjectId = extractJsonIntField(item, "id", 211);
        // Разрешаем широкий диапазон rx/ry (-0.45..1.45), чтобы ИИ мог свободно строить свисающие лианы, сосульки, траву, огонь, крыши и выступы!
        el.rx             = std::clamp(extractJsonFloatField(item, "rx", 0.5f), -0.45f, 1.45f);
        el.ry             = std::clamp(extractJsonFloatField(item, "ry", 0.5f), -0.60f, 1.60f);
        // Разрешаем масштабы от микро-пикселей 0.005 до крупных балок/фонов 12.0!
        el.scaleX         = std::clamp(extractJsonFloatField(item, "sx", 0.18f), 0.005f, 12.0f);
        el.scaleY         = std::clamp(extractJsonFloatField(item, "sy", 0.18f), 0.005f, 12.0f);
        el.rot            = extractJsonFloatField(item, "rot", 0.0f);
        el.shapeType      = std::clamp(extractJsonIntField(item, "type", 0), 0, 12);
        el.zOrder         = std::clamp(extractJsonIntField(item, "z", 18), -15, 60);
        el.zLayerCode     = std::clamp(extractJsonIntField(item, "zl", 3), -3, 9);
        el.editorLayerOff = std::clamp(extractJsonIntField(item, "el", 2), 0, 5);
        el.opacity        = static_cast<GLubyte>(std::clamp(extractJsonIntField(item, "op", 255), 10, 255));
        el.blending       = extractJsonBoolField(item, "blend", false);

        int r = std::clamp(extractJsonIntField(item, "r", 220), 0, 255);
        int g = std::clamp(extractJsonIntField(item, "g", 230), 0, 255);
        int b = std::clamp(extractJsonIntField(item, "b", 245), 0, 255);
        el.color = ccColor3B{ static_cast<GLubyte>(r), static_cast<GLubyte>(g), static_cast<GLubyte>(b) };

        int dr = extractJsonIntField(item, "dr", -1);
        int dg = extractJsonIntField(item, "dg", -1);
        int db = extractJsonIntField(item, "db", -1);
        if (dr >= 0 && dg >= 0 && db >= 0) {
            el.hasDetailColor = true;
            el.detailColor = ccColor3B{
                static_cast<GLubyte>(std::clamp(dr, 0, 255)),
                static_cast<GLubyte>(std::clamp(dg, 0, 255)),
                static_cast<GLubyte>(std::clamp(db, 0, 255))
            };
            el.detailOpacity = static_cast<GLubyte>(std::clamp(extractJsonIntField(item, "dop", el.opacity), 10, 255));
            el.detailBlending = extractJsonBoolField(item, "dblend", el.blending);
        }

        outList.push_back(el);
        cur = objEnd + 1;
    }
}

// Самооценка качества попиксельной детализации и точности выбранной структуры из референса (0..100%)
static int evaluateReferenceSimilarityScore(AIDecisionResult const& res, std::string& outWeakRegions) {
    outWeakRegions.clear();
    if (res.customElements.empty()) {
        outWeakRegions = "No pixel-by-pixel micro-elements placed; build 35..80 fine elements (sx/sy 0.02..0.30) matching ONE chosen structure.";
        return 30;
    }

    int microPixelCount = 0;
    for (auto const& el : res.customElements) {
        if (el.scaleX <= 0.35f || el.scaleY <= 0.35f) {
            microPixelCount++;
        }
    }

    int score = res.selfMatchScore;
    if (res.customElements.size() < 24) {
        score = (std::min)(score, 78);
        outWeakRegions += fmt::format("Too few elements ({}); add 35..80 pixel-by-pixel micro-textures! ", res.customElements.size());
    }
    if (microPixelCount < 10) {
        score = (std::min)(score, 75);
        outWeakRegions += fmt::format("Only {} micro-elements <= 0.35 scale; use tiny pixel scales (0.02..0.28) for fine textures! ", microPixelCount);
    }
    return std::clamp(score, 10, 100);
}

// Смешивание двух RGB-цветов
static ccColor3B lerpColor(ccColor3B a, ccColor3B b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return ccColor3B{
        static_cast<GLubyte>(std::lround(a.r + (b.r - a.r) * t)),
        static_cast<GLubyte>(std::lround(a.g + (b.g - a.g) * t)),
        static_cast<GLubyte>(std::lround(a.b + (b.b - a.b) * t))
    };
}

// Одиночный вызов реальной нейросети (с поддержкой Multimodal Vision для референс-картинок!)
static bool executeSingleNeuralNetPass(
    std::string const& systemRule,
    std::string const& userTurnPrompt,
    bool includeImage,
    AIDecisionResult& outRes
) {
    int httpStatus = 0;
    std::string rawResp;
    std::string errText;
    std::string assistantContent;

    std::wstring hostW;
    std::wstring pathW;
    bool isGeminiNative = false;
    resolveCustomBaseUrlAndEndpoint(g_customModelName, hostW, pathW, isGeminiNative);

    if (isGeminiNative) {
        std::string combinedPrompt = systemRule + "\n\nConversation history:\n";
        size_t startHist = (g_chatHistory.size() > 6) ? (g_chatHistory.size() - 6) : 0;
        for (size_t i = startHist; i < g_chatHistory.size(); ++i) {
            combinedPrompt += g_chatHistory[i].role + ": " + g_chatHistory[i].content + "\n";
        }
        combinedPrompt += "User request: " + userTurnPrompt;

        std::string partsJson;
        if (includeImage && g_hasReferenceImage && !g_referenceBase64.empty()) {
            partsJson = fmt::format(
                "{{\"inline_data\":{{\"mime_type\":\"{}\",\"data\":\"{}\"}}}},{{\"text\":\"{}\"}}",
                g_referenceMimeType,
                g_referenceBase64,
                jsonEscape(combinedPrompt)
            );
        } else {
            partsJson = fmt::format("{{\"text\":\"{}\"}}", jsonEscape(combinedPrompt));
        }

        std::string geminiBody = fmt::format(
            "{{\"contents\":[{{\"role\":\"user\",\"parts\":[{}]}}],\"generationConfig\":{{\"temperature\":0.92,\"maxOutputTokens\":4096,\"responseMimeType\":\"application/json\"}}}}",
            partsJson
        );

        std::wstring hdrW = L"Content-Type: application/json\r\nx-goog-api-key: " + utf8ToWideASCII(g_connectedApiKey) + L"\r\n";
        std::wstring pathWithKeyW = pathW;
        if (pathWithKeyW.find(L"key=") == std::wstring::npos) {
            pathWithKeyW += (pathWithKeyW.find(L'?') == std::wstring::npos ? L"?key=" : L"&key=") + utf8ToWideASCII(g_connectedApiKey);
        }

        bool ok = performWinHttpRequest(L"POST", hostW.c_str(), pathW, hdrW, geminiBody, httpStatus, rawResp, errText);
        if (!ok) {
            ok = performWinHttpRequest(L"POST", hostW.c_str(), pathWithKeyW, L"Content-Type: application/json\r\n", geminiBody, httpStatus, rawResp, errText);
        }
        if (!ok) {
            outRes.httpSuccess = false;
            outRes.replyText = fmt::format("API Error (HTTP {}): {}", httpStatus, errText);
            return false;
        }
        assistantContent = extractGeminiFinalTextPart(rawResp);
        if (assistantContent.empty()) {
            assistantContent = extractOpenAICompatibleContent(rawResp);
        }
    } else {
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

        std::wstring hdrW =
            L"Content-Type: application/json\r\n"
            L"Accept: application/json\r\n"
            L"Authorization: Bearer " + utf8ToWideASCII(g_connectedApiKey) + L"\r\n";
        std::wstring hdrXApiW =
            L"Content-Type: application/json\r\n"
            L"Accept: application/json\r\n"
            L"x-api-key: " + utf8ToWideASCII(g_connectedApiKey) + L"\r\n";
        std::wstring hdrAltW =
            L"Content-Type: application/json\r\n"
            L"Accept: application/json\r\n"
            L"api-key: " + utf8ToWideASCII(g_connectedApiKey) + L"\r\n";

        std::string activeModel = g_customModelName.empty() ? "gpt-4o-mini" : g_customModelName;
        bool sentOk = false;

        if (includeImage && g_hasReferenceImage && !g_referenceBase64.empty()) {
            std::string dataUri = "data:" + g_referenceMimeType + ";base64," + g_referenceBase64;
            std::string visionMessages = messagesJson + fmt::format(
                ",{{\"role\":\"user\",\"content\":[{{\"type\":\"text\",\"text\":\"{}\"}},{{\"type\":\"image_url\",\"image_url\":{{\"url\":\"{}\"}}}}]}}",
                jsonEscape(userTurnPrompt),
                dataUri
            );
            std::string visionPayload = fmt::format(
                "{{\"model\":\"{}\",\"messages\":[{}],\"temperature\":0.92,\"max_tokens\":4096}}",
                jsonEscape(activeModel),
                visionMessages
            );
            if (performWinHttpRequest(L"POST", hostW.c_str(), pathW, hdrW, visionPayload, httpStatus, rawResp, errText)) {
                sentOk = true;
            }
        }

        if (!sentOk) {
            std::string textMessages = messagesJson + fmt::format(
                ",{{\"role\":\"user\",\"content\":\"{}\"}}",
                jsonEscape(userTurnPrompt)
            );
            std::string payload = fmt::format(
                "{{\"model\":\"{}\",\"messages\":[{}],\"temperature\":0.92,\"max_tokens\":4096}}",
                jsonEscape(activeModel),
                textMessages
            );
            if (performWinHttpRequest(L"POST", hostW.c_str(), pathW, hdrW, payload, httpStatus, rawResp, errText)) {
                sentOk = true;
            } else if (httpStatus == 401 || httpStatus == 403) {
                if (performWinHttpRequest(L"POST", hostW.c_str(), pathW, hdrXApiW, payload, httpStatus, rawResp, errText)) {
                    sentOk = true;
                } else if (httpStatus == 401 || httpStatus == 403) {
                    if (performWinHttpRequest(L"POST", hostW.c_str(), pathW, hdrAltW, payload, httpStatus, rawResp, errText)) {
                        sentOk = true;
                    }
                }
            }
        }

        if (!sentOk) {
            outRes.httpSuccess = false;
            outRes.replyText = fmt::format("API Error (HTTP {}): {}", httpStatus, errText);
            return false;
        }
        assistantContent = extractOpenAICompatibleContent(rawResp);
        if (assistantContent.empty()) {
            assistantContent = extractGeminiFinalTextPart(rawResp);
        }
    }

    // Строгое определение запрошенного игроком цвета и флагов ТОЛЬКО по текущему запросу игрока!
    std::string lp = userTurnPrompt;
    for (char& ch : lp) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (lp.find("self-critique refinement pass") != std::string::npos) {
        size_t q1 = lp.find('\'');
        size_t q2 = (q1 != std::string::npos) ? lp.find('\'', q1 + 1) : std::string::npos;
        if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1 + 1) {
            lp = lp.substr(q1 + 1, q2 - q1 - 1);
        }
    }

    auto detectUserColorOverride = [&](ccColor3B& fillC, ccColor3B& midC, ccColor3B& mainC, ccColor3B& outlC, ccColor3B& glowC) -> bool {
        if (lp.find("krasn") != std::string::npos || lp.find("red") != std::string::npos || lp.find("bordov") != std::string::npos) {
            fillC = ccColor3B{ 46, 10, 14 }; midC = ccColor3B{ 125, 22, 30 }; mainC = ccColor3B{ 220, 42, 54 }; outlC = ccColor3B{ 255, 185, 190 }; glowC = ccColor3B{ 255, 48, 64 };
            return true;
        }
        if (lp.find("sin") != std::string::npos || lp.find("blue") != std::string::npos) {
            fillC = ccColor3B{ 10, 20, 52 }; midC = ccColor3B{ 26, 62, 145 }; mainC = ccColor3B{ 52, 122, 245 }; outlC = ccColor3B{ 185, 220, 255 }; glowC = ccColor3B{ 65, 145, 255 };
            return true;
        }
        if (lp.find("golub") != std::string::npos || lp.find("cyan") != std::string::npos || lp.find("biryuz") != std::string::npos || lp.find("ice") != std::string::npos || lp.find("led") != std::string::npos) {
            fillC = ccColor3B{ 10, 38, 48 }; midC = ccColor3B{ 24, 108, 132 }; mainC = ccColor3B{ 58, 205, 238 }; outlC = ccColor3B{ 205, 250, 255 }; glowC = ccColor3B{ 75, 235, 255 };
            return true;
        }
        if (lp.find("fiolet") != std::string::npos || lp.find("purp") != std::string::npos || lp.find("violet") != std::string::npos || lp.find("siren") != std::string::npos) {
            fillC = ccColor3B{ 32, 12, 52 }; midC = ccColor3B{ 88, 34, 142 }; mainC = ccColor3B{ 162, 68, 245 }; outlC = ccColor3B{ 228, 195, 255 }; glowC = ccColor3B{ 185, 85, 255 };
            return true;
        }
        if (lp.find("rozov") != std::string::npos || lp.find("pink") != std::string::npos || lp.find("magent") != std::string::npos) {
            fillC = ccColor3B{ 48, 12, 34 }; midC = ccColor3B{ 138, 32, 92 }; mainC = ccColor3B{ 242, 68, 162 }; outlC = ccColor3B{ 255, 200, 230 }; glowC = ccColor3B{ 255, 82, 178 };
            return true;
        }
        if (lp.find("zhelt") != std::string::npos || lp.find("yellow") != std::string::npos || lp.find("zolot") != std::string::npos || lp.find("gold") != std::string::npos) {
            fillC = ccColor3B{ 48, 38, 10 }; midC = ccColor3B{ 142, 112, 22 }; mainC = ccColor3B{ 245, 202, 42 }; outlC = ccColor3B{ 255, 245, 190 }; glowC = ccColor3B{ 255, 215, 55 };
            return true;
        }
        if (lp.find("oranzh") != std::string::npos || lp.find("orange") != std::string::npos || lp.find("yantar") != std::string::npos) {
            fillC = ccColor3B{ 52, 24, 8 }; midC = ccColor3B{ 152, 68, 18 }; mainC = ccColor3B{ 248, 128, 32 }; outlC = ccColor3B{ 255, 215, 175 }; glowC = ccColor3B{ 255, 140, 42 };
            return true;
        }
        if (lp.find("bel") != std::string::npos || lp.find("white") != std::string::npos || lp.find("svetl") != std::string::npos) {
            fillC = ccColor3B{ 145, 152, 165 }; midC = ccColor3B{ 195, 202, 215 }; mainC = ccColor3B{ 238, 242, 250 }; outlC = ccColor3B{ 255, 255, 255 }; glowC = ccColor3B{ 225, 238, 255 };
            return true;
        }
        if (lp.find("chern") != std::string::npos || lp.find("black") != std::string::npos || lp.find("temn") != std::string::npos || lp.find("dark") != std::string::npos) {
            fillC = ccColor3B{ 8, 9, 12 }; midC = ccColor3B{ 22, 25, 32 }; mainC = ccColor3B{ 48, 54, 68 }; outlC = ccColor3B{ 165, 175, 195 }; glowC = ccColor3B{ 95, 115, 155 };
            return true;
        }
        if (lp.find("korichn") != std::string::npos || lp.find("brown") != std::string::npos || lp.find("derev") != std::string::npos || lp.find("wood") != std::string::npos) {
            fillC = ccColor3B{ 38, 22, 12 }; midC = ccColor3B{ 92, 54, 28 }; mainC = ccColor3B{ 148, 92, 48 }; outlC = ccColor3B{ 215, 175, 135 }; glowC = ccColor3B{ 195, 128, 68 };
            return true;
        }
        if (lp.find("seruy") != std::string::npos || lp.find("seriy") != std::string::npos || lp.find("seroe") != std::string::npos ||
            lp.find("sera") != std::string::npos || lp.find("serogo") != std::string::npos || lp.find("grey") != std::string::npos || lp.find("gray") != std::string::npos) {
            fillC = ccColor3B{ 36, 39, 44 }; midC = ccColor3B{ 82, 88, 98 }; mainC = ccColor3B{ 138, 146, 158 }; outlC = ccColor3B{ 205, 212, 224 }; glowC = ccColor3B{ 155, 175, 205 };
            return true;
        }
        if (lp.find("zelen") != std::string::npos || lp.find("green") != std::string::npos || lp.find("izumrud") != std::string::npos || lp.find("laym") != std::string::npos) {
            fillC = ccColor3B{ 12, 42, 20 }; midC = ccColor3B{ 28, 112, 52 }; mainC = ccColor3B{ 58, 210, 92 }; outlC = ccColor3B{ 190, 255, 205 }; glowC = ccColor3B{ 65, 245, 110 };
            return true;
        }
        return false;
    };

    auto applyUserDirectivesAndFallbacks = [&](bool forceBuildIfRequested) {
        bool isGreetingOnly = (lp == "privet" || lp == "hi" || lp == "hello" || lp == "zdarova" || lp == "kak dela");

        bool hasBrickWord = (lp.find("kirpich") != std::string::npos || lp.find("brick") != std::string::npos || lp.find("kladk") != std::string::npos);
        bool hasVineWord  = (lp.find("lian") != std::string::npos || lp.find("vine") != std::string::npos ||
                             lp.find("plyusch") != std::string::npos || lp.find("plyush") != std::string::npos ||
                             lp.find("trav") != std::string::npos || lp.find("moh") != std::string::npos ||
                             lp.find("listv") != std::string::npos || lp.find("kust") != std::string::npos);
        bool hasNeonWord  = (lp.find("neon") != std::string::npos || lp.find("glow") != std::string::npos ||
                             lp.find("svech") != std::string::npos || lp.find("svet") != std::string::npos ||
                             lp.find("kiber") != std::string::npos || lp.find("cyber") != std::string::npos ||
                             lp.find("lazer") != std::string::npos);
        bool hasWinWord   = (lp.find("okn") != std::string::npos || lp.find("okon") != std::string::npos ||
                             lp.find("window") != std::string::npos || lp.find("dom") != std::string::npos ||
                             lp.find("zdani") != std::string::npos || lp.find("fasad") != std::string::npos);
        bool hasOrbPortalWord = (lp.find("orb") != std::string::npos || lp.find("orm") != std::string::npos ||
                                 lp.find("sfer") != std::string::npos || lp.find("dash") != std::string::npos ||
                                 lp.find("desh") != std::string::npos || lp.find("portal") != std::string::npos ||
                                 lp.find("ring") != std::string::npos || lp.find("kolts") != std::string::npos);
        bool hasCustomOrbTexWord = hasOrbPortalWord && (
            lp.find("kastom") != std::string::npos || lp.find("custom") != std::string::npos ||
            lp.find("tekstur") != std::string::npos || lp.find("texture") != std::string::npos ||
            lp.find("uzor") != std::string::npos || lp.find("vnutr") != std::string::npos ||
            lp.find("risun") != std::string::npos || lp.find("art") != std::string::npos
        );
        bool hasSpikeWord = (lp.find("ship") != std::string::npos || lp.find("spike") != std::string::npos ||
                             lp.find("kolyuch") != std::string::npos || lp.find("thorn") != std::string::npos);
        bool hasBgWord    = (lp.find("fon") != std::string::npos || lp.find("zadn") != std::string::npos ||
                             lp.find("bg") != std::string::npos || lp.find("background") != std::string::npos ||
                             lp.find("nebo") != std::string::npos || lp.find("kosmos") != std::string::npos ||
                             lp.find("peyzazh") != std::string::npos);

        // Если игрок просит просто "добавь лианы / пусть спускается лиана" к прошлой кирпичной постройке (и не просит неон/новый стиль)
        std::string prevLower = g_lastBuildPrompt;
        for (char& c : prevLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        bool prevHadBricks = (prevLower.find("kirpich") != std::string::npos || prevLower.find("brick") != std::string::npos);
        bool asksNewStyle  = (hasNeonWord || lp.find("stil") != std::string::npos || lp.find("style") != std::string::npos ||
                              lp.find("kamen") != std::string::npos || lp.find("derev") != std::string::npos ||
                              lp.find("metall") != std::string::npos || lp.find("stekl") != std::string::npos);

        bool wantsBuild = !isGreetingOnly && (
            hasBrickWord || hasVineWord || hasNeonWord || hasWinWord || hasOrbPortalWord || hasSpikeWord || hasBgWord || asksNewStyle ||
            lp.find("sdelay") != std::string::npos || lp.find("postroy") != std::string::npos ||
            lp.find("narisuy") != std::string::npos || lp.find("ukras") != std::string::npos ||
            lp.find("dekor") != std::string::npos || lp.find("stenu") != std::string::npos ||
            lp.find("strukt") != std::string::npos || lp.find("ship") != std::string::npos ||
            lp.find("spike") != std::string::npos || lp.find("build") != std::string::npos ||
            lp.find("create") != std::string::npos || lp.find("draw") != std::string::npos ||
            lp.find("recreate") != std::string::npos || lp.find("invent") != std::string::npos ||
            lp.find("refine") != std::string::npos || lp.find("dobav") != std::string::npos ||
            lp.find("spusk") != std::string::npos || g_hasReferenceImage
        );

        if (forceBuildIfRequested && wantsBuild && outRes.taskMode == AITaskMode::ChatOnly) {
            outRes.taskMode = AITaskMode::FullDecor;
            outRes.selfMatchScore = 96;
        }

        // Проверяем запрет на PBR/реалистик
        bool forbidPBR = (lp.find("bez pbr") != std::string::npos || lp.find("ne delay pbr") != std::string::npos ||
                          lp.find("no pbr") != std::string::npos || lp.find("bez real") != std::string::npos ||
                          lp.find("ne delay real") != std::string::npos || lp.find("ne nado pbr") != std::string::npos ||
                          lp.find("ne nado real") != std::string::npos || lp.find("uberi pbr") != std::string::npos ||
                          lp.find("prost") != std::string::npos || lp.find("minimal") != std::string::npos);

        bool askPBR = (lp.find("pbr") != std::string::npos || lp.find("realist") != std::string::npos ||
                       lp.find("detal") != std::string::npos || g_hasReferenceImage);

        if (forbidPBR) {
            outRes.wantRealisticPBR = false;
        } else if (askPBR) {
            outRes.wantRealisticPBR = true;
        } else {
            outRes.wantRealisticPBR = false;
        }

        // СТРОГОЕ ВЫПОЛНЕНИЕ ТОЛЬКО ТОГО СТИЛЯ, КОТОРЫЙ ПОПРОСИЛ ИГРОК:
        // Если игрок просит неон ("сделай стиль неон") — кирпичи гарантированно выключаются!
        outRes.wantBricks          = hasBrickWord || (hasVineWord && prevHadBricks && !asksNewStyle);
        outRes.wantVines           = hasVineWord || outRes.wantVines;
        outRes.wantWindows         = hasWinWord;
        outRes.wantNeon            = hasNeonWord || (outRes.wantNeon && !outRes.wantBricks && !outRes.wantWindows && !hasVineWord);
        outRes.hasGradient         = (lp.find("grad") != std::string::npos);
        outRes.wantCustomSpikes    = hasSpikeWord || outRes.wantCustomSpikes || !outRes.spikeElements.empty();
        // На орбах по умолчанию делается ТОЛЬКО НЕОН (без кастомной текстуры!), а кастомная текстура внутри орба — ТОЛЬКО по явной просьбе!
        outRes.allowOrbsAndPortals  = hasOrbPortalWord || outRes.wantNeon;
        outRes.wantCustomOrbTexture = hasCustomOrbTexWord;
        outRes.wantBackground       = hasBgWord || outRes.wantBackground;

        // Проверяем, написал ли сам игрок в чат желаемое количество блоков/объектов ("на 300 блоков", "около 500 объектов" и т.д.)
        outRes.suggestedCount = 0;
        if (lp.find("blok") != std::string::npos || lp.find("block") != std::string::npos ||
            lp.find("obj") != std::string::npos || lp.find("obekt") != std::string::npos ||
            lp.find("obyekt") != std::string::npos || lp.find("sht") != std::string::npos) {
            for (size_t idx = 0; idx < lp.size(); ++idx) {
                if (lp[idx] >= '0' && lp[idx] <= '9') {
                    int val = 0;
                    while (idx < lp.size() && lp[idx] >= '0' && lp[idx] <= '9') {
                        val = val * 10 + (lp[idx] - '0');
                        ++idx;
                    }
                    if (val >= 15 && val <= 25000) {
                        outRes.suggestedCount = val;
                        break;
                    }
                }
            }
        }

        ccColor3B uFill, uMid, uMain, uOutl, glowC;
        if (detectUserColorOverride(uFill, uMid, uMain, uOutl, glowC)) {
            if (outRes.wantNeon && !outRes.wantBricks) {
                outRes.fillRGB    = lerpColor(ccColor3B{ 6, 8, 14 }, uFill, 0.40f);
                outRes.midRGB     = uMid;
                outRes.mainRGB    = uMain;
                outRes.outlineRGB = glowC;
                outRes.glowRGB    = glowC;
            } else {
                outRes.fillRGB    = uFill;
                outRes.midRGB     = uMid;
                outRes.mainRGB    = uMain;
                outRes.outlineRGB = uOutl;
                outRes.glowRGB    = glowC;
            }
            outRes.gradTopRGB = uMain;
            outRes.gradBotRGB = outRes.fillRGB;
            outRes.hasCustomPalette = true;

            // НЕ перезаписываем цвета элементов ИИ (cellElements и customElements), чтобы ИИ мог рисовать
            // любые детали (лаву, лианы, цветы, глаза, огонь, воду, радугу, звезды) именно теми цветами, которые задумал!
        } else if (outRes.wantNeon && !outRes.hasCustomPalette) {
            outRes.fillRGB    = ccColor3B{ 8, 10, 22 };
            outRes.midRGB     = ccColor3B{ 24, 64, 155 };
            outRes.mainRGB    = ccColor3B{ 45, 185, 255 };
            outRes.outlineRGB = ccColor3B{ 85, 235, 255 };
            outRes.glowRGB    = ccColor3B{ 55, 220, 255 };
            outRes.hasCustomPalette = true;
        }
    };

    outRes.httpSuccess = true;
    if (assistantContent.empty()) {
        outRes.replyText = "Postroil strukturu tochno po vashemu zaprosu!";
        applyUserDirectivesAndFallbacks(true);
        return true;
    }

    size_t jsonStart = assistantContent.find('{');
    if (jsonStart != std::string::npos) {
        std::string innerJson = assistantContent.substr(jsonStart);
        if (innerJson.find("\"elements\"") != std::string::npos && innerJson.rfind(']') < innerJson.find("\"elements\"")) {
            size_t lastBrace = innerJson.rfind('}');
            if (lastBrace != std::string::npos && lastBrace > innerJson.find("\"elements\"")) {
                innerJson = innerJson.substr(0, lastBrace + 1) + "]}";
            } else {
                innerJson += "]}";
            }
        }

        std::string actionStr = extractJsonStringField(innerJson, "action");
        outRes.replyText      = extractJsonStringField(innerJson, "reply");
        outRes.planText       = extractJsonStringField(innerJson, "plan");
        outRes.styleThemeName = extractJsonStringField(innerJson, "style_name");
        outRes.critiqueNotes  = extractJsonStringField(innerJson, "critique");
        outRes.selfMatchScore = std::clamp(extractJsonIntField(innerJson, "match_score", 95), 0, 100);

        if (actionStr == "only_3d")        outRes.taskMode = AITaskMode::Only3D;
        else if (actionStr == "only_neon") outRes.taskMode = AITaskMode::OnlyNeon;
        else if (actionStr == "only_fill") outRes.taskMode = AITaskMode::OnlyFill;
        else if (actionStr == "undo")      outRes.taskMode = AITaskMode::UndoLast;
        else if (actionStr == "build")     outRes.taskMode = AITaskMode::FullDecor;
        else                               outRes.taskMode = AITaskMode::ChatOnly;

        bool c1 = extractJsonRGBArray(innerJson, "fill_rgb",    outRes.fillRGB);
        bool c2 = extractJsonRGBArray(innerJson, "mid_rgb",     outRes.midRGB);
        bool c3 = extractJsonRGBArray(innerJson, "main_rgb",    outRes.mainRGB);
        bool c4 = extractJsonRGBArray(innerJson, "outline_rgb", outRes.outlineRGB);
        bool c5 = extractJsonRGBArray(innerJson, "glow_rgb",    outRes.glowRGB);
        bool c6 = extractJsonRGBArray(innerJson, "grad_top",    outRes.gradTopRGB);
        bool c7 = extractJsonRGBArray(innerJson, "grad_bot",    outRes.gradBotRGB);
        if (!c6) outRes.gradTopRGB = outRes.mainRGB;
        if (!c7) outRes.gradBotRGB = outRes.fillRGB;
        outRes.hasGradient      = extractJsonBoolField(innerJson, "want_gradient", false);
        outRes.wantRealisticPBR = extractJsonBoolField(innerJson, "want_pbr", false);
        outRes.wantBricks       = extractJsonBoolField(innerJson, "want_bricks", false);
        outRes.wantWindows      = extractJsonBoolField(innerJson, "want_windows", false);
        outRes.wantVines        = extractJsonBoolField(innerJson, "want_vines", false);
        outRes.hasCustomPalette = (c1 || c2 || c3 || c4 || c5 || c6 || c7);

        outRes.wantNeon       = extractJsonBoolField(innerJson, "want_neon", false);
        outRes.suggestedCount = 0; // Определяется только если игрок сам написал в чат желаемое количество блоков!

        outRes.directMoveX          = extractJsonFloatField(innerJson, "move_x", 0.0f);
        outRes.directMoveY          = extractJsonFloatField(innerJson, "move_y", 0.0f);
        outRes.directRotateDeg      = extractJsonFloatField(innerJson, "rotate_deg", 0.0f);
        outRes.directScaleMulX      = std::clamp(extractJsonFloatField(innerJson, "scale_x", 1.0f), 0.05f, 10.0f);
        outRes.directScaleMulY      = std::clamp(extractJsonFloatField(innerJson, "scale_y", 1.0f), 0.05f, 10.0f);
        outRes.directDuplicateCount = std::clamp(extractJsonIntField(innerJson, "duplicate_count", 0), 0, 20);
        outRes.directDuplicateStepX = extractJsonFloatField(innerJson, "duplicate_dx", 0.0f);
        outRes.directDuplicateStepY = extractJsonFloatField(innerJson, "duplicate_dy", 0.0f);
        outRes.directDeleteSelected = extractJsonBoolField(innerJson, "delete_selected", false);
        outRes.directRecolorOnly    = extractJsonBoolField(innerJson, "recolor_only", false);

        outRes.wantCustomSpikes    = extractJsonBoolField(innerJson, "want_custom_spikes", false);
        outRes.allowOrbsAndPortals = extractJsonBoolField(innerJson, "decorate_orbs_portals", false);
        outRes.wantBackground      = extractJsonBoolField(innerJson, "want_background", false);

        outRes.cellElements.clear();
        outRes.customElements.clear();
        outRes.spikeElements.clear();
        outRes.orbElements.clear();
        parseNamedElementsArrayFromAIJson(innerJson, "cell_elements", outRes.cellElements);
        parseNamedElementsArrayFromAIJson(innerJson, "elements", outRes.customElements);
        parseNamedElementsArrayFromAIJson(innerJson, "spike_elements", outRes.spikeElements);
        parseNamedElementsArrayFromAIJson(innerJson, "orb_elements", outRes.orbElements);

        bool hasDirectTransform = (std::fabs(outRes.directMoveX) > 0.01f || std::fabs(outRes.directMoveY) > 0.01f ||
                                   std::fabs(outRes.directRotateDeg) > 0.01f ||
                                   std::fabs(outRes.directScaleMulX - 1.0f) > 0.01f || std::fabs(outRes.directScaleMulY - 1.0f) > 0.01f ||
                                   outRes.directDuplicateCount > 0 || outRes.directDeleteSelected || outRes.directRecolorOnly);

        if (outRes.taskMode == AITaskMode::ChatOnly &&
            (!outRes.cellElements.empty() || !outRes.customElements.empty() || outRes.hasCustomPalette || hasDirectTransform)) {
            outRes.taskMode = AITaskMode::FullDecor;
        }
        applyUserDirectivesAndFallbacks(true);
    } else {
        outRes.taskMode = AITaskMode::ChatOnly;
        outRes.replyText = assistantContent;
        applyUserDirectivesAndFallbacks(true);
    }
    return true;
}

// Состояние фонового запроса и авто-повтора каждые 2 секунды при ошибке (503 / 429 / нет сети)
static bool g_aiRequestThreadRunning = false;
static bool g_aiRequestHasCompleted = false;
static AIDecisionResult g_aiThreadResult;
static std::string g_pendingRetryPrompt;
static bool g_autoRetryTimerActive = false;
static float g_autoRetryCountdown = 0.0f;
static int g_autoRetryAttemptCount = 0;
static float g_floatingDotsTimer = 0.0f;
static int g_floatingDotsPhase = 0;

// Запрос к реальной нейросети с АВТОМАТИЧЕСКИМ ЦИКЛОМ САМООЦЕНКИ И ПЕРЕДЕЛКИ ПОД РЕФЕРЕНС
static AIDecisionResult queryRealNeuralNetwork(
    std::string const& userMessage,
    ContourAnalysis const& contour,
    EditorTabCatalog const& /*cat*/,
    int objectLimit,
    bool overhangAllowed,
    int variantNum
) {
    std::string forbiddenThemes = "None yet";
    if (!g_pastGeneratedThemes.empty()) {
        forbiddenThemes.clear();
        size_t startIdx = (g_pastGeneratedThemes.size() > 12) ? (g_pastGeneratedThemes.size() - 12) : 0;
        for (size_t i = startIdx; i < g_pastGeneratedThemes.size(); ++i) {
            if (!forbiddenThemes.empty()) forbiddenThemes += ", ";
            forbiddenThemes += g_pastGeneratedThemes[i];
        }
    }

    std::string refInfo = g_hasReferenceImage
        ? fmt::format("REFERENCE IMAGE ATTACHED ({}). Inspect ALL structures and the fine micro-textures inside each structure. PICK ONLY ONE SINGLE STRUCTURE from the reference (ignore background, sky, UI, and all other structures!), name it in \"style_name\", and recreate ONLY that ONE structure's style, colors, opacities, and fine micro-textures pixel-by-pixel!", g_referenceFileName)
        : "No reference image attached. Invent a brand-new, non-repeating artistic style with pixel-by-pixel micro-textures.";

    std::string systemRule = fmt::format(
        "You are Art Agent, an autonomous Geometry Dash 2.2 Level Editor AI with DIRECT control over the editor. "
        "You MUST fulfill ANY user request directly in the editor — even things that have no preset in code (custom spikes, orb/dash/portal interiors, full backgrounds when nothing is selected, lava, ice icicles, wood planks, honeycomb, skulls, eyes, trees, flowers, fire, water, gears, chains, saws, stars, planets, or direct editor transforms!). "
        "STRICT RULES: "
        "1. Always reply in the user's language (if Russian, write in Russian translit with Latin letters so GD font displays it). "
        "2. DIRECT EDITOR ARRAYS: "
        "   - \"cell_elements\": Repeating pattern inside EVERY 30x30 block (rx: 0.0..1.0, ry: 0.0..1.0, sx: 0.02..1.0, sy: 0.02..1.0, rot: 0..360). "
        "   - \"elements\": Macro-art, overhang decor, or Background (BG) layers across EACH selected part or background canvas (rx: -0.35..1.35, ry: -0.50..1.50, sx: 0.02..8.0, sy: 0.02..8.0, rot: 0..360). "
        "   - \"spike_elements\": Custom spike design inside the spike triangle (rx: -0.45..0.45 across base, ry: 0.0..0.95 from base to tip, sx: 0.02..0.8, sy: 0.02..0.8, rot: -180..180). Populate whenever custom spikes are requested! "
        "   - \"orb_elements\": Custom design placed INSIDE selected orbs, dash orbs, and portals (rx: 0.0..1.0, ry: 0.0..1.0, sx: 0.02..0.9, sy: 0.02..0.9, rot: 0..360). ONLY populate when the user explicitly asks to decorate orbs, dash orbs, or portals! "
        "3. REAL GD OBJECT IDs: 211 (universal colorable square/rectangle/line/pixel block), 8 (spike), 39 (small spike), 88/89/98 (saws), 406/407/408 (foliage/grass), 1888 (glow). "
        "4. DIRECT EDITOR TRANSFORMS: \"move_x\", \"move_y\", \"rotate_deg\", \"scale_x\", \"scale_y\", \"duplicate_count\", \"duplicate_dx\", \"duplicate_dy\", \"delete_selected\": true/false, \"recolor_only\": true/false. "
        "5. Selection: {} blocks ({}x{} units, {} spikes, {} orbs/portals). Limit: {}. Mode: {}. Variant #{}. Previously built: [{}]. {} "
        "Respond ONLY with a single valid JSON object (no markdown fences): "
        "{{"
        "\"action\": \"build\" | \"only_3d\" | \"only_neon\" | \"only_fill\" | \"undo\" | \"chat\", "
        "\"reply\": \"natural response explaining what you built or changed in the editor\", "
        "\"style_name\": \"name of the custom design\", "
        "\"plan\": \"short summary of editor actions\", "
        "\"match_score\": 98, "
        "\"fill_rgb\": [r,g,b], \"mid_rgb\": [r,g,b], \"main_rgb\": [r,g,b], \"outline_rgb\": [r,g,b], \"glow_rgb\": [r,g,b], \"grad_top\": [r,g,b], \"grad_bot\": [r,g,b], "
        "\"want_bricks\": false, \"want_vines\": false, \"want_windows\": false, \"want_pbr\": false, \"want_gradient\": false, \"want_neon\": false, \"want_custom_spikes\": false, \"decorate_orbs_portals\": false, \"want_background\": false, "
        "\"move_x\": 0, \"move_y\": 0, \"rotate_deg\": 0, \"scale_x\": 1.0, \"scale_y\": 1.0, \"duplicate_count\": 0, \"duplicate_dx\": 0, \"duplicate_dy\": 0, \"delete_selected\": false, \"recolor_only\": false, "
        "\"cell_elements\": [{{\"id\":211,\"rx\":0.0..1.0,\"ry\":0.0..1.0,\"sx\":0.02..1.0,\"sy\":0.02..1.0,\"rot\":0..360,\"r\":0..255,\"g\":0..255,\"b\":0..255,\"op\":15..255,\"blend\":false,\"el\":1..3,\"zl\":1..5,\"z\":6..16}}], "
        "\"elements\": [{{\"id\":211,\"rx\":-0.35..1.35,\"ry\":-0.50..1.50,\"sx\":0.02..8.0,\"sy\":0.02..8.0,\"rot\":0..360,\"r\":0..255,\"g\":0..255,\"b\":0..255,\"op\":15..255,\"blend\":false,\"el\":2..5,\"zl\":3..7,\"z\":18..35}}], "
        "\"spike_elements\": [{{\"id\":211,\"rx\":-0.42..0.42,\"ry\":0.05..0.90,\"sx\":0.03..0.65,\"sy\":0.03..0.65,\"rot\":0..360,\"r\":0..255,\"g\":0..255,\"b\":0..255,\"op\":40..255,\"blend\":false,\"el\":2..4,\"zl\":1..5,\"z\":10..22}}], "
        "\"orb_elements\": [{{\"id\":211,\"rx\":0.1..0.9,\"ry\":0.1..0.9,\"sx\":0.04..0.75,\"sy\":0.04..0.75,\"rot\":0..360,\"r\":0..255,\"g\":0..255,\"b\":0..255,\"op\":40..255,\"blend\":false,\"el\":3..5,\"zl\":3..7,\"z\":14..28}}]"
        "}}.",
        contour.cellCount(),
        contour.widthUnits(),
        contour.heightUnits(),
        contour.spikes.size(),
        contour.orbsAndPortals.size(),
        objectLimit,
        overhangAllowed ? "Overhang" : "Inside",
        variantNum,
        forbiddenThemes,
        refInfo
    );

    AIDecisionResult bestRes;
    if (!executeSingleNeuralNetPass(systemRule, userMessage, true, bestRes)) {
        return bestRes;
    }

    bestRes.refinementPasses = 1;

    // АВТОМАТИЧЕСКИЙ ЦИКЛ САМОПРОВЕРКИ И ИСПРАВЛЕНИЯ (Self-Critique & Refinement Loop):
    if (bestRes.taskMode == AITaskMode::FullDecor) {
        int maxPasses = g_hasReferenceImage ? 3 : 2;
        for (int pass = 1; pass < maxPasses; ++pass) {
            std::string weakZones;
            int actualScore = evaluateReferenceSimilarityScore(bestRes, weakZones);
            bestRes.selfMatchScore = actualScore;

            bool needsRefinement = (bestRes.cellElements.empty() && bestRes.customElements.empty() && !bestRes.directRecolorOnly && !bestRes.directDeleteSelected);
            if (!needsRefinement) break;

            std::string critiquePrompt = fmt::format(
                "SELF-CRITIQUE REFINEMENT PASS #{}: Your previous draft for '{}' scored {}% similarity ({} elements). "
                "Critique notes: {} {} "
                "REBUILD and IMPROVE the JSON now: fix every mismatched color region, add multi-layered internal block shading (el=0..5, zl=-3..9), "
                "use accurate transparency (op=30..255) and additive glow (blend=true where bright), and output 28..45 precise 'elements' so match_score reaches 96..100%!",
                pass + 1,
                userMessage,
                actualScore,
                bestRes.customElements.size(),
                bestRes.critiqueNotes,
                weakZones
            );

            AIDecisionResult refinedRes;
            if (executeSingleNeuralNetPass(systemRule, critiquePrompt, false, refinedRes) &&
                refinedRes.httpSuccess &&
                refinedRes.taskMode == AITaskMode::FullDecor &&
                !refinedRes.customElements.empty()) {
                std::string newWeak;
                int newScore = evaluateReferenceSimilarityScore(refinedRes, newWeak);
                refinedRes.selfMatchScore = (std::max)(newScore, refinedRes.selfMatchScore);
                refinedRes.refinementPasses = pass + 1;
                bestRes = std::move(refinedRes);
                if (bestRes.selfMatchScore >= 94 && bestRes.customElements.size() >= 20) {
                    break;
                }
            } else {
                break;
            }
        }
    }

    if (bestRes.replyText.empty()) {
        bestRes.replyText = "Done! Built custom multi-layered art.";
    }
    if (bestRes.planText.empty()) {
        bestRes.planText = userMessage;
    }
    if (!bestRes.styleThemeName.empty()) {
        g_pastGeneratedThemes.push_back(bestRes.styleThemeName);
    } else if (bestRes.taskMode == AITaskMode::FullDecor) {
        g_pastGeneratedThemes.push_back(fmt::format("Style#{}_{}x{}", g_pastGeneratedThemes.size() + 1, bestRes.mainRGB.r, bestRes.glowRGB.b));
    }

    g_lastSelfScore = bestRes.selfMatchScore;
    g_lastRefinementPasses = bestRes.refinementPasses;

    std::string finalBotMsg = bestRes.replyText;
    if (bestRes.taskMode == AITaskMode::FullDecor) {
        finalBotMsg += fmt::format(" [Match: {}% | Passes: {}]", bestRes.selfMatchScore, bestRes.refinementPasses);
    }

    bestRes.replyText = finalBotMsg;
    return bestRes;
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
static void writeObjFlags(ObjT* obj, int zOrder, int zLayerCode, bool noTouch, bool noGlow) {
    if (!obj) return;
    obj->setZOrder(zOrder);
    if constexpr (requires { obj->m_zOrder; }) {
        obj->m_zOrder = zOrder;
    }
    if (zLayerCode != 0) {
        if constexpr (requires { obj->m_zLayer; }) {
            obj->m_zLayer = static_cast<decltype(obj->m_zLayer)>(zLayerCode);
        }
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
    bool customBlend = false,
    bool hasDetailOverride = false,
    ccColor3B detailRGB = ccColor3B{ 255, 255, 255 },
    GLubyte detailOpacity = 255,
    bool detailBlend = false
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
        if (hasDetailOverride) {
            applyHSVOnGJSpriteColor(obj->m_detailColor, detailRGB, detailOpacity, detailBlend);
        } else {
            applyHSVOnGJSpriteColor(obj->m_detailColor, col, finalOp, blend);
        }
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

static void findRealGlowObjectsInTab(std::vector<int> const& /*tabIDs*/, EditorTabCatalog& cat) {
    // Никогда не берем слоты из панели редактора наугад (в GD 2.2 там лежат косые 3D-параллелограммы!).
    // Односторонний неон строится из чистых градиентных полосок 211 с Blending=true.
    cat.idGlowBeam = 211;
    cat.idGlowEdgeSmall = 211;
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
            // Ищем страницу с 1888 во ВСЕХ вкладках редактора, чтобы на 100% найти односторонний неон!
            findRealGlowObjectsInTab(ids, cat);

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
    return id == 8 || id == 9 || id == 39 || id == 61 || id == 103 ||
           id == 133 || id == 136 || id == 144 || id == 145 ||
           id == 177 || id == 178 || id == 179 || id == 191 || id == 205 ||
           id == 216 || id == 217 || id == 218 || id == 392 || id == 421 ||
           id == 422 || id == 458 || id == 459;
}

// Распознавание Орбов, Дэш-орбов и Порталов (декорятся ТОЛЬКО если игрок явно попросил!)
static bool isDashOrbID(int id) {
    return id == 1704 || id == 1751;
}

static bool isOrbID(int id) {
    return id == 36 || id == 84 || id == 141 || id == 1022 || id == 1330 ||
           id == 1333 || id == 1594 || id == 1704 || id == 1751 || id == 3004 || id == 3027;
}

static bool isPortalID(int id) {
    return id == 10 || id == 11 || id == 12 || id == 13 || id == 45 || id == 46 || id == 47 ||
           id == 99 || id == 101 || id == 111 || id == 286 || id == 287 || id == 660 ||
           id == 745 || id == 747 || id == 749 || id == 1331 || id == 1933 || id == 2902 || id == 2926;
}

template <typename ObjT>
static bool isOrbDashOrPortalObject(ObjT* obj, bool& outIsDashOrb, bool& outIsPortal) {
    outIsDashOrb = false;
    outIsPortal = false;
    if (!obj) return false;
    int id = readObjID(obj);
    if (isDashOrbID(id)) {
        outIsDashOrb = true;
        return true;
    }
    if (isOrbID(id) || typeinfo_cast<RingObject*>(obj) != nullptr) {
        return true;
    }
    if (isPortalID(id) || typeinfo_cast<TeleportPortalObject*>(obj) != nullptr) {
        outIsPortal = true;
        return true;
    }
    if constexpr (requires { obj->m_objectType; }) {
        int t = static_cast<int>(obj->m_objectType);
        if (t == 36 || t == 37) { outIsDashOrb = true; return true; }
        if (t == 11 || t == 12 || t == 13 || t == 28 || t == 31 || t == 34 || t == 35 || t == 41 || t == 44) {
            return true;
        }
        if ((t >= 3 && t <= 6) || (t >= 14 && t <= 19) || (t >= 24 && t <= 27) || t == 32 || t == 40) {
            outIsPortal = true;
            return true;
        }
    }
    return false;
}

// Фильтр триггеров, падов, ускорителей, монеток, пил и уже созданного ИИ-декора (не трогаем никогда!)
static bool isTriggerPadOrHazardID(int id) {
    if (id <= 0) return true;
    // Пады / Батуты и Ускорители
    if (id == 35 || id == 67 || id == 140 || id == 1332 || id == 3005 ||
        id == 200 || id == 201 || id == 202 || id == 203 || id == 1334) {
        return true;
    }
    // Триггеры
    if ((id >= 29 && id <= 34) || (id >= 55 && id <= 59) || id == 104 || id == 105 || id == 221 ||
        id == 717 || id == 718 || id == 743 || id == 744 || id == 899 || id == 900 || id == 901 ||
        id == 915 || id == 1006 || id == 1007 || id == 1049 || id == 1268 || id == 1346 || id == 1347 ||
        id == 1520 || id == 1585 || id == 1595 || id == 1611 || id == 1612 || id == 1613 || id == 1616 ||
        (id >= 1811 && id <= 1817) || (id >= 1912 && id <= 1917) || id == 1932 || id == 1934 || id == 1935 ||
        id == 2015 || id == 2016 || id == 2062 || id == 2067 || id == 2068 ||
        (id >= 2899 && id <= 3035) || (id >= 3600 && id <= 3660)) {
        return true;
    }
    // Монетки, ключи, чекпоинты, StartPos, предметы и пилы
    if (id == 44 || id == 88 || id == 89 || id == 98 || id == 142 || (id >= 183 && id <= 188) ||
        (id >= 397 && id <= 399) || (id >= 675 && id <= 677) || (id >= 740 && id <= 742) ||
        id == 1275 || id == 1329 || (id >= 1587 && id <= 1598) || id == 1614 || id == 1619 || id == 1620 ||
        (id >= 1705 && id <= 1710) || (id >= 1734 && id <= 1736) || id == 2063) {
        return true;
    }
    return false;
}

template <typename ObjT>
static bool isTriggerPadOrAlreadyPlacedDecor(ObjT* obj) {
    if (!obj) return true;
    for (auto const& placed : g_lastPlacedObjects) {
        if (placed.data() == obj) return true;
    }
    int id = readObjID(obj);
    if (isSpikeObjectID(id)) return false;
    bool dummyDash = false, dummyPortal = false;
    if (isOrbDashOrPortalObject(obj, dummyDash, dummyPortal)) return false;
    if (isTriggerPadOrHazardID(id)) return true;

    if (typeinfo_cast<StartPosObject*>(obj) != nullptr) return true;
    if (typeinfo_cast<EffectGameObject*>(obj) != nullptr) return true;

    if constexpr (requires { obj->m_isTrigger; }) {
        if (obj->m_isTrigger) return true;
    }
    if constexpr (requires { obj->m_objectType; }) {
        int t = static_cast<int>(obj->m_objectType);
        if (t >= 3 && t != 7 && t != 23) {
            return true;
        }
    }
    return false;
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

            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
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
    if (occupied.size() < 4) return;

    auto islands = splitIntoIslands(occupied);
    for (auto const& isl : islands) {
        if (isl.cells.size() < 4) continue;
        int w = isl.maxGX - isl.minGX + 1;
        int h = isl.maxGY - isl.minGY + 1;
        if (w <= 2 || h <= 2 || w > 48 || h > 48) continue;

        std::set<std::pair<int, int>> boundary = isl.cells;
        if (isl.minGY <= 1) {
            int leftBottomX = 999999;
            int rightBottomX = -999999;
            for (auto const& [x, y] : isl.cells) {
                if (y <= isl.minGY + 1) {
                    leftBottomX  = (std::min)(leftBottomX, x);
                    rightBottomX = (std::max)(rightBottomX, x);
                }
            }
            if (leftBottomX < rightBottomX && (rightBottomX - leftBottomX) <= 16) {
                for (int gx = leftBottomX; gx <= rightBottomX; ++gx) {
                    boundary.insert({ gx, isl.minGY });
                }
            }
        }

        int padMinX = isl.minGX - 1;
        int padMaxX = isl.maxGX + 1;
        int padMinY = isl.minGY - 1;
        int padMaxY = isl.maxGY + 1;

        std::set<std::pair<int, int>> outsideVisited;
        std::queue<std::pair<int, int>> q;
        std::pair<int, int> start{ padMinX, padMinY };
        outsideVisited.insert(start);
        q.push(start);

        while (!q.empty()) {
            auto [cx, cy] = q.front();
            q.pop();
            const int dx[4] = { 1, -1, 0, 0 };
            const int dy[4] = { 0, 0, 1, -1 };
            for (int k = 0; k < 4; ++k) {
                int nx = cx + dx[k];
                int ny = cy + dy[k];
                if (nx < padMinX || nx > padMaxX || ny < padMinY || ny > padMaxY) continue;
                std::pair<int, int> np{ nx, ny };
                if (boundary.count(np) > 0 || outsideVisited.count(np) > 0) continue;
                outsideVisited.insert(np);
                q.push(np);
            }
        }

        for (int gy = isl.minGY; gy <= isl.maxGY; ++gy) {
            for (int gx = isl.minGX; gx <= isl.maxGX; ++gx) {
                std::pair<int, int> cell{ gx, gy };
                if (outsideVisited.count(cell) == 0) {
                    occupied.insert(cell);
                }
            }
        }
    }
}

template <typename EditorUIType>
static ContourAnalysis analyzeSelectedContour(EditorUIType* ui) {
    ContourAnalysis info;
    if (!ui) return info;

    if (ui->m_editorLayer && ui->m_editorLayer->m_objectLayer) {
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        info.cameraCenterWorld = ui->m_editorLayer->m_objectLayer->convertToNodeSpace(
            CCPoint{ winSize.width * 0.5f, winSize.height * 0.5f }
        );
    }

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
            if (isTriggerPadOrAlreadyPlacedDecor(obj)) continue;

            CCPoint pos = obj->getPosition();
            int id = readObjID(obj);

            bool isDash = false, isPort = false;
            if (isOrbDashOrPortalObject(obj, isDash, isPort)) {
                SelectedOrbPortalInfo op;
                op.x = pos.x;
                op.y = pos.y;
                op.rotation = obj->getRotation();
                op.scaleX = std::fabs(obj->getScaleX()) > 0.01f ? std::fabs(obj->getScaleX()) : 1.f;
                op.scaleY = std::fabs(obj->getScaleY()) > 0.01f ? std::fabs(obj->getScaleY()) : 1.f;
                op.flipX = obj->isFlipX();
                op.flipY = obj->isFlipY();
                op.objectId = id;
                op.isDashOrb = isDash;
                op.isPortal = isPort;
                op.sourceObj = obj;
                info.orbsAndPortals.push_back(op);
                info.lockedStates.push_back(LockedObjectState{ obj, pos, obj->getRotation() });
                continue;
            }

            info.lockedStates.push_back(LockedObjectState{ obj, pos, obj->getRotation() });

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
                info.rawCells.insert({ gx, gy });
                info.occupiedCells.insert({ gx, gy });
                info.solidObjects.push_back(obj);
            }
        }

        fillAllStructureInteriorsCompletely(info.occupiedCells);
    }

    info.hasAnySelection = (!info.occupiedCells.empty() || !info.spikes.empty() || !info.orbsAndPortals.empty());

    // Если НИЧЕГО не выделено — НЕ добавляем 9 фейковых блоков в occupiedCells (оставляем 0 блоков!),
    // а задаем только рабочие границы камеры 16x9 клеток на случай генерации заднего фона (Background)!
    if (!info.hasAnySelection) {
        int baseGX = static_cast<int>(std::floor(info.cameraCenterWorld.x / 30.0f));
        int baseGY = (std::max)(2, static_cast<int>(std::floor(info.cameraCenterWorld.y / 30.0f)));
        info.minGX = baseGX - 8;
        info.maxGX = baseGX + 7;
        info.minGY = baseGY - 4;
        info.maxGY = baseGY + 4;
        return info;
    }

    bool boundsInit = false;
    auto expandBounds = [&](int gx, int gy) {
        if (!boundsInit) {
            info.minGX = info.maxGX = gx;
            info.minGY = info.maxGY = gy;
            boundsInit = true;
        } else {
            info.minGX = (std::min)(info.minGX, gx);
            info.maxGX = (std::max)(info.maxGX, gx);
            info.minGY = (std::min)(info.minGY, gy);
            info.maxGY = (std::max)(info.maxGY, gy);
        }
    };

    for (auto const& [gx, gy] : info.occupiedCells) expandBounds(gx, gy);
    for (auto const& sp : info.spikes) expandBounds(static_cast<int>(std::floor(sp.x / 30.0f)), static_cast<int>(std::floor(sp.y / 30.0f)));
    for (auto const& op : info.orbsAndPortals) expandBounds(static_cast<int>(std::floor(op.x / 30.0f)), static_cast<int>(std::floor(op.y / 30.0f)));

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
    GLubyte opacity = 255,
    int zLayerCode = 1,
    int editorLayerOffset = 0,
    ccColor3B customCol = ccColor3B{ 255, 255, 255 },
    bool customBlend = false
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
    spec.zLayerCode = zLayerCode;
    spec.editorLayerOffset = editorLayerOffset;
    spec.priorityTier = priorityTier;
    spec.isOverhang = isOverhang;
    spec.customColor = customCol;
    spec.customOpacity = opacity;
    spec.customBlending = customBlend;
    spec.noTouch = true;
    spec.noGlow = true;
    out.push_back(spec);
}

static bool isPremade3DOrForbiddenTemplateID(int id, EditorTabCatalog const& cat) {
    if (id <= 0) return true;
    if (id == cat.id3DTrapTop || id == cat.id3DHalfStrip || id == cat.id3DTriCorner || id == cat.id3DTriBR) {
        return (id != 211);
    }
    return false;
}

// Никаких готовых наклонных текстур! ИИ собирает весь декор, кирпичи, окна и PBR исключительно из чистых кубиков 211
// (плюс односторонний неон idGlowEdgeSmall только для световой обводки)
static int resolveAIElementObjectID(AICustomDrawElement const& el, EditorTabCatalog const& cat, bool forbid3DBlocks) {
    if (el.directObjectId <= 0) return 211;
    if (forbid3DBlocks && isPremade3DOrForbiddenTemplateID(el.directObjectId, cat)) {
        return 211;
    }
    return el.directObjectId;
}

static float hash2DNoise(int x, int y, int seed = 1337) {
    uint32_t n = static_cast<uint32_t>(x * 374761393 + y * 668265263 + seed * 1442695041);
    n = (n ^ (n >> 13)) * 1274126177u;
    n = n ^ (n >> 16);
    return static_cast<float>(n & 0xFFFFu) / 65535.0f;
}

static float sampleRealisticFBM(float wx, float wy, int seed) {
    int ix0 = static_cast<int>(std::floor(wx * 0.22f));
    int iy0 = static_cast<int>(std::floor(wy * 0.22f));
    float n0 = hash2DNoise(ix0, iy0, seed);
    int ix1 = static_cast<int>(std::floor(wx * 0.55f));
    int iy1 = static_cast<int>(std::floor(wy * 0.55f));
    float n1 = hash2DNoise(ix1, iy1, seed + 7919);
    return n0 * 0.65f + n1 * 0.35f;
}

static ccColor3B computeRealisticTexelColor(
    float worldX,
    float worldY,
    float originX,
    float originY,
    float spanW,
    float spanH,
    bool topOpen,
    bool bottomOpen,
    bool leftOpen,
    bool rightOpen,
    float localSubU,
    float localSubV,
    AIDecisionResult const& ai,
    int styleSeed
) {
    float u = std::clamp((worldX - originX) / spanW, 0.0f, 0.999f);
    float v = std::clamp((worldY - originY) / spanH, 0.0f, 0.999f);

    ccColor3B baseCol = ai.fillRGB;
    bool hasRef = g_hasReferenceImage &&
                  g_referenceHighResGrid.size() >= static_cast<size_t>(g_refHighResW * g_refHighResH);

    if (hasRef) {
        float fx = u * (g_refHighResW - 1);
        float fy = v * (g_refHighResH - 1);
        int x0 = std::clamp(static_cast<int>(fx), 0, g_refHighResW - 1);
        int y0 = std::clamp(static_cast<int>(fy), 0, g_refHighResH - 1);
        int x1 = std::clamp(x0 + 1, 0, g_refHighResW - 1);
        int y1 = std::clamp(y0 + 1, 0, g_refHighResH - 1);
        float tx = fx - x0;
        float ty = fy - y0;

        ccColor3B c00 = g_referenceHighResGrid[y0 * g_refHighResW + x0];
        ccColor3B c10 = g_referenceHighResGrid[y0 * g_refHighResW + x1];
        ccColor3B c01 = g_referenceHighResGrid[y1 * g_refHighResW + x0];
        ccColor3B c11 = g_referenceHighResGrid[y1 * g_refHighResW + x1];
        ccColor3B topMix = lerpColor(c00, c10, tx);
        ccColor3B botMix = lerpColor(c01, c11, tx);
        baseCol = lerpColor(topMix, botMix, ty);
    } else if (ai.wantRealisticPBR) {
        float n = sampleRealisticFBM(worldX, worldY, styleSeed);
        baseCol = lerpColor(ai.fillRGB, ai.midRGB, std::clamp(n * 0.85f + v * 0.25f, 0.0f, 1.0f));
        if (n > 0.72f) {
            baseCol = lerpColor(baseCol, ai.mainRGB, (n - 0.72f) * 1.8f);
        }
        if (ai.hasGradient) {
            ccColor3B gradCol = lerpColor(ai.gradBotRGB, ai.gradTopRGB, v);
            baseCol = lerpColor(baseCol, gradCol, 0.55f);
        }
    } else {
        baseCol = ai.hasGradient ? lerpColor(ai.gradBotRGB, ai.gradTopRGB, v) : ai.fillRGB;
        return baseCol;
    }

    float shadeMul = 1.0f;
    if (topOpen && localSubV > 0.74f)    shadeMul += 0.14f;
    if (leftOpen && localSubU < 0.24f)   shadeMul += 0.08f;
    if (bottomOpen && localSubV < 0.26f) shadeMul -= 0.20f;
    if (rightOpen && localSubU > 0.76f)  shadeMul -= 0.10f;

    float grain = (hash2DNoise(static_cast<int>(worldX * 2.f), static_cast<int>(worldY * 2.f), styleSeed + 99) - 0.5f) * 0.09f;
    shadeMul = std::clamp(shadeMul + grain, 0.45f, 1.35f);

    return ccColor3B{
        static_cast<GLubyte>(std::clamp(static_cast<int>(std::lround(baseCol.r * shadeMul)), 0, 255)),
        static_cast<GLubyte>(std::clamp(static_cast<int>(std::lround(baseCol.g * shadeMul)), 0, 255)),
        static_cast<GLubyte>(std::clamp(static_cast<int>(std::lround(baseCol.b * shadeMul)), 0, 255))
    };
}

// Сборка реалистичной КИРПИЧНОЙ КЛАДКИ внутри блока 30x30 исключительно из ровных кубиков 211 (со швами раствора и PBR-оттенками каждого кирпича!)
static void appendCubeBrickworkInCell(
    std::vector<GeneratedObjectSpec>& result,
    float cx,
    float cy,
    int gx,
    int gy,
    AIDecisionResult const& ai,
    int styleSeed
) {
    // 3 ряда кирпичей по высоте блока (каждый ряд высотой 9.0 ед. + 1.0 ед. темный шов раствора)
    // Четные ряды: 2 целых кирпича по 13.8 ед.; Нечетные ряды (вразбежку): половинка + целый + половинка!
    ccColor3B mortarCol = lerpColor(ai.fillRGB, ccColor3B{ 4, 5, 8 }, 0.62f);
    addSpec(
        result, 211, cx, cy, 0.98f, 0.98f, 0.f,
        LayerRole::InnerDetail, ColorSlot::CustomRGB, 3, 0, false,
        250, -1, 1, mortarCol, false
    );

    for (int row = 0; row < 3; ++row) {
        float rowY = (cy - 10.0f) + row * 10.0f;
        float brickHScale = 8.4f / 30.0f; // оставляем ровный горизонтальный шов
        bool staggered = (((gy * 3 + row) % 2) != 0);

        if (!staggered) {
            for (int b = 0; b < 2; ++b) {
                float bx = cx + (b == 0 ? -7.2f : 7.2f);
                float brickWScale = 13.4f / 30.0f;
                float n = hash2DNoise(gx * 7 + b, gy * 11 + row, styleSeed);
                ccColor3B bCol = lerpColor(ai.midRGB, ai.mainRGB, n * 0.75f);
                addSpec(
                    result, 211, bx, rowY, brickWScale, brickHScale, 0.f,
                    LayerRole::InnerDetail, ColorSlot::CustomRGB, 5, 0, false,
                    255, 1, 2, bCol, false
                );
                // Верхний тонкий световой блик на каждом кирпичике (если включен PBR)
                if (ai.wantRealisticPBR) {
                    addSpec(
                        result, 211, bx, rowY + 3.4f, brickWScale * 0.94f, 0.035f, 0.f,
                        LayerRole::InnerDetail, ColorSlot::CustomRGB, 6, 0, false,
                        145, 1, 2, lerpColor(bCol, ai.outlineRGB, 0.45f), false
                    );
                }
            }
        } else {
            // Перевязка швов: левая половинка (-11.2), центральный кирпич (0.0), правая половинка (+11.2)
            float const offsets[3] = { -11.1f, 0.0f, 11.1f };
            float const widths[3]  = {  6.4f, 13.4f,  6.4f };
            for (int b = 0; b < 3; ++b) {
                float bx = cx + offsets[b];
                float brickWScale = widths[b] / 30.0f;
                float n = hash2DNoise(gx * 13 + b, gy * 17 + row, styleSeed + 33);
                ccColor3B bCol = lerpColor(ai.midRGB, ai.mainRGB, n * 0.80f);
                addSpec(
                    result, 211, bx, rowY, brickWScale, brickHScale, 0.f,
                    LayerRole::InnerDetail, ColorSlot::CustomRGB, 5, 0, false,
                    255, 1, 2, bCol, false
                );
                if (ai.wantRealisticPBR) {
                    addSpec(
                        result, 211, bx, rowY + 3.4f, brickWScale * 0.92f, 0.035f, 0.f,
                        LayerRole::InnerDetail, ColorSlot::CustomRGB, 6, 0, false,
                        140, 1, 2, lerpColor(bCol, ai.outlineRGB, 0.45f), false
                    );
                }
            }
        }
    }
}

// Сборка РЕАЛИСТИЧНОГО ОКНА из кубиков 211 внутри выбранных клеток структуры
// (каменный подоконник, глубокая коробка рамы, 4 стеклянных секции с отражением неба/света и переплет!)
static void appendCubeRealisticWindowInCell(
    std::vector<GeneratedObjectSpec>& result,
    float cx,
    float cy,
    AIDecisionResult const& ai
) {
    ccColor3B frameCol = lerpColor(ai.fillRGB, ccColor3B{ 10, 12, 16 }, 0.55f);
    ccColor3B sillCol  = lerpColor(ai.midRGB, ai.outlineRGB, 0.45f);
    ccColor3B glassDark = lerpColor(ccColor3B{ 14, 24, 38 }, ai.glowRGB, 0.28f);
    ccColor3B glassLit  = lerpColor(ai.glowRGB, ccColor3B{ 225, 245, 255 }, 0.55f);

    // 1. Внешняя каменная/металлическая ниша окна (22x24 ед.)
    addSpec(result, 211, cx, cy, 0.74f, 0.80f, 0.f, LayerRole::InnerDetail, ColorSlot::CustomRGB, 8, 0, false, 255, 1, 2, frameCol, false);

    // 2. Четыре секции стекла (2x2) с градиентом отражения и бликом
    for (int wy = 0; wy < 2; ++wy) {
        for (int wx = 0; wx < 2; ++wx) {
            float gxPos = cx + (wx == 0 ? -4.6f : 4.6f);
            float gyPos = cy + (wy == 0 ? -4.8f : 4.8f);
            ccColor3B paneCol = (wy == 1) ? lerpColor(glassDark, glassLit, 0.45f) : glassDark;
            addSpec(result, 211, gxPos, gyPos, 0.25f, 0.27f, 0.f, LayerRole::InnerDetail, ColorSlot::CustomRGB, 9, 0, false, 235, 3, 3, paneCol, false);
            // Тонкий световой блик на стекле (прямой вертикальный/горизонтальный из кубика 211, без кривых наклонов!)
            addSpec(result, 211, gxPos - 1.5f, gyPos + 1.8f, 0.08f, 0.12f, 0.f, LayerRole::InnerDetail, ColorSlot::CustomRGB, 10, 0, false, 150, 3, 3, glassLit, true);
        }
    }

    // 3. Крестовина оконного переплета (импост + горбыльки)
    addSpec(result, 211, cx, cy, 0.05f, 0.76f, 0.f, LayerRole::InnerDetail, ColorSlot::CustomRGB, 11, 0, false, 255, 3, 3, frameCol, false);
    addSpec(result, 211, cx, cy, 0.70f, 0.05f, 0.f, LayerRole::InnerDetail, ColorSlot::CustomRGB, 11, 0, false, 255, 3, 3, frameCol, false);

    // 4. Выступающий верхний наличник и нижний подоконник (Window Sill)
    addSpec(result, 211, cx, cy + 12.2f, 0.82f, 0.07f, 0.f, LayerRole::InnerDetail, ColorSlot::CustomRGB, 11, 0, false, 255, 3, 3, sillCol, false);
    addSpec(result, 211, cx, cy - 12.2f, 0.84f, 0.08f, 0.f, LayerRole::InnerDetail, ColorSlot::CustomRGB, 11, 0, false, 255, 3, 3, sillCol, false);
}

// ОДНОСТОРОННИЙ НЕОН ПО КРАЯМ СТРУКТУРЫ (тонкий и мягкий, чтобы НИКОГДА не перекрывать внутреннюю текстуру!)
static void appendOneSidedEdgeNeon(
    std::vector<GeneratedObjectSpec>& result,
    float cx,
    float cy,
    bool topOpen,
    bool bottomOpen,
    bool leftOpen,
    bool rightOpen,
    bool allowOverhang,
    ccColor3B glowRGB
) {
    // 3 тонкие деликатные ступени затухания прямо у самого края (zOrder=7, низкая прозрачность 110..32)
    constexpr float kOffsets[3] = { 14.4f, 13.5f, 12.2f };
    constexpr float kWidths[3]  = { 0.04f, 0.08f, 0.13f };
    constexpr GLubyte kOps[3]   = { 110,   65,    32 };

    if (topOpen) {
        for (int i = 0; i < 3; ++i) {
            addSpec(result, 211, cx, cy + kOffsets[i], 1.00f, kWidths[i], 0.f, LayerRole::NeonAndGrass, ColorSlot::CustomRGB, 7, 0, false, kOps[i], 3, 3, glowRGB, true);
        }
        if (allowOverhang) {
            addSpec(result, 211, cx, cy + 16.0f, 1.00f, 0.09f, 0.f, LayerRole::NeonAndGrass, ColorSlot::CustomRGB, 7, 0, true, 95, 5, 4, glowRGB, true);
        }
    }
    if (leftOpen) {
        for (int i = 0; i < 3; ++i) {
            addSpec(result, 211, cx - kOffsets[i], cy, kWidths[i], 1.00f, 0.f, LayerRole::NeonAndGrass, ColorSlot::CustomRGB, 7, 0, false, kOps[i], 3, 3, glowRGB, true);
        }
    }
    if (rightOpen) {
        for (int i = 0; i < 3; ++i) {
            addSpec(result, 211, cx + kOffsets[i], cy, kWidths[i], 1.00f, 0.f, LayerRole::NeonAndGrass, ColorSlot::CustomRGB, 7, 0, false, kOps[i], 3, 3, glowRGB, true);
        }
    }
    if (bottomOpen) {
        for (int i = 0; i < 3; ++i) {
            addSpec(result, 211, cx, cy - kOffsets[i], 1.00f, kWidths[i], 0.f, LayerRole::NeonAndGrass, ColorSlot::CustomRGB, 7, 0, false, kOps[i], 3, 3, ccColor3B{ 0, 0, 0 }, false);
        }
    }
}

// СБОРКА СВИСАЮЩИХ ЛИАН (2-4 детализированные пиксельные лианы с листьями из кубиков 211 НА КАЖДОМ ПАРТЕ!)
static void appendCubeHangingVinesOnIsland(
    std::vector<GeneratedObjectSpec>& result,
    IslandBounds const& isl,
    ContourAnalysis const& contour,
    bool allowOverhang,
    int styleSeed
) {
    int islCols = (std::max)(1, isl.maxGX - isl.minGX + 1);
    int vineCount = std::clamp((islCols + 1) / 2, 2, 4);

    ccColor3B vineDark = ccColor3B{ 18, 72, 28 };
    ccColor3B vineMid  = ccColor3B{ 42, 156, 58 };
    ccColor3B vineLit  = ccColor3B{ 102, 232, 88 };

    for (auto const& [gx, gy] : isl.cells) {
        if (contour.occupiedCells.count({ gx, gy + 1 }) == 0) {
            float cx = gx * 30.f + 15.f;
            float cy = gy * 30.f + 15.f;
            addSpec(result, 211, cx, cy + 12.8f, 0.96f, 0.14f, 0.f, LayerRole::NeonAndGrass, ColorSlot::CustomRGB, 21, 0, false, 255, 5, 4, vineDark, false);
            addSpec(result, 211, cx, cy + 13.6f, 0.88f, 0.09f, 0.f, LayerRole::NeonAndGrass, ColorSlot::CustomRGB, 22, 0, false, 255, 5, 4, vineMid, false);
        }
    }

    float islMinX = isl.minGX * 30.f;
    float islSpanW = islCols * 30.f;

    for (int v = 0; v < vineCount; ++v) {
        float fracX = (v + 0.5f) / static_cast<float>(vineCount);
        float baseVineX = islMinX + fracX * islSpanW;
        int colGX = std::clamp(static_cast<int>(std::floor(baseVineX / 30.f)), isl.minGX, isl.maxGX);

        int topGY = isl.minGY;
        int botGY = isl.maxGY;
        bool foundCol = false;
        for (int gy = isl.minGY; gy <= isl.maxGY; ++gy) {
            if (isl.cells.count({ colGX, gy })) {
                if (!foundCol) {
                    topGY = botGY = gy;
                    foundCol = true;
                } else {
                    topGY = (std::max)(topGY, gy);
                    botGY = (std::min)(botGY, gy);
                }
            }
        }
        if (!foundCol) continue;

        float startY = (topGY + 1) * 30.f - 2.0f;
        float endY   = botGY * 30.f - (allowOverhang ? (12.0f + (v % 2) * 8.0f) : -2.0f);
        float stepY  = 4.2f;
        int segIdx   = 0;

        for (float curY = startY; curY >= endY; curY -= stepY, ++segIdx) {
            float waveX = std::sin((segIdx * 0.65f) + v * 1.9f + styleSeed * 0.1f) * 2.8f;
            float vx = baseVineX + waveX;
            bool isHangingBelow = (curY < botGY * 30.f);

            addSpec(result, 211, vx + 0.9f, curY - 0.5f, 0.11f, 0.16f, 0.f, LayerRole::NeonAndGrass, ColorSlot::CustomRGB, 22, 0, isHangingBelow, 245, 5, 4, vineDark, false);
            addSpec(result, 211, vx,        curY,        0.10f, 0.16f, 0.f, LayerRole::NeonAndGrass, ColorSlot::CustomRGB, 23, 0, isHangingBelow, 255, 5, 4, vineMid,  false);

            if ((segIdx % 2) == 0) {
                float leafSide = ((segIdx / 2 + v) % 2 == 0) ? -3.4f : 3.4f;
                addSpec(result, 211, vx + leafSide, curY + 0.4f, 0.16f, 0.09f, 0.f, LayerRole::NeonAndGrass, ColorSlot::CustomRGB, 24, 0, isHangingBelow, 255, 5, 4, vineMid, false);
                addSpec(result, 211, vx + leafSide * 1.25f, curY + 0.8f, 0.10f, 0.06f, 0.f, LayerRole::NeonAndGrass, ColorSlot::CustomRGB, 25, 0, isHangingBelow, 255, 5, 4, vineLit, false);
            }
        }
    }
}

// КАСТОМНЫЕ И КЛАССИЧЕСКИЕ ШИПЫ ОТ ИИ:
// 1) Ставит 2-й внутренний шип меньшего размера с единым цветом Base и Detail.
// 2) Если попросили кастомные шипы (или ИИ передал spike_elements) — строит кастомный многослойный дизайн внутри треугольника шипа!
static void appendSpikeDecoration(
    std::vector<GeneratedObjectSpec>& result,
    SelectedSpikeInfo const& sp,
    EditorTabCatalog const& cat,
    AIDecisionResult const& ai,
    bool spawnBaseSpikeToo = false
) {
    float rad = -sp.rotation * (3.14159265f / 180.0f);
    float cosA = std::cos(rad);
    float sinA = std::sin(rad);

    auto localToWorld = [&](float lx, float ly) -> std::pair<float, float> {
        float sx = lx * sp.scaleX * (sp.flipX ? -1.f : 1.f);
        float sy = ly * sp.scaleY * (sp.flipY ? -1.f : 1.f);
        return { sp.x + (sx * cosA - sy * sinA), sp.y + (sx * sinA + sy * cosA) };
    };

    // Если игрок попросил создать новые кастомные шипы на блоках с нуля — спавним сам базовый шип
    if (spawnBaseSpikeToo) {
        GeneratedObjectSpec baseSp;
        baseSp.objectId               = (sp.objectId > 0) ? sp.objectId : 8;
        baseSp.x                      = sp.x;
        baseSp.y                      = sp.y;
        baseSp.scaleX                 = sp.scaleX;
        baseSp.scaleY                 = sp.scaleY;
        baseSp.rotation               = sp.rotation;
        baseSp.flipX                  = sp.flipX;
        baseSp.flipY                  = sp.flipY;
        baseSp.layerRole              = LayerRole::Outline3D;
        baseSp.colorSlot              = ColorSlot::CustomRGB;
        baseSp.customColor            = ai.fillRGB;
        baseSp.customOpacity          = 255;
        baseSp.hasSeparateDetailColor = true;
        baseSp.detailColor            = ai.outlineRGB;
        baseSp.detailOpacity          = 240;
        baseSp.zOrder                 = 6;
        baseSp.zLayerCode             = 1;
        baseSp.editorLayerOffset      = 1;
        baseSp.priorityTier           = 0;
        baseSp.noTouch                = false;
        baseSp.noGlow                 = true;
        result.push_back(baseSp);
    }

    // Внутренний меньший шип того же типа с монолитной заливкой Base и Detail одного цвета
    auto [inX, inY] = localToWorld(0.f, -2.5f);
    ccColor3B innerCol = ai.wantNeon
        ? lerpColor(ai.mainRGB, ai.glowRGB, 0.5f)
        : lerpColor(ai.midRGB, ai.mainRGB, 0.55f);

    GeneratedObjectSpec innerSpike;
    innerSpike.objectId               = (sp.objectId > 0) ? sp.objectId : 8;
    innerSpike.x                      = inX;
    innerSpike.y                      = inY;
    innerSpike.scaleX                 = sp.scaleX * 0.62f;
    innerSpike.scaleY                 = sp.scaleY * 0.62f;
    innerSpike.rotation               = sp.rotation;
    innerSpike.flipX                  = sp.flipX;
    innerSpike.flipY                  = sp.flipY;
    innerSpike.layerRole              = LayerRole::InnerDetail;
    innerSpike.colorSlot              = ColorSlot::CustomRGB;
    innerSpike.customColor            = innerCol;
    innerSpike.customOpacity          = 255;
    innerSpike.customBlending         = false;
    innerSpike.hasSeparateDetailColor = true;
    innerSpike.detailColor            = innerCol;
    innerSpike.detailOpacity          = 255;
    innerSpike.detailBlending         = false;
    innerSpike.zOrder                 = 8;
    innerSpike.zLayerCode             = 1;
    innerSpike.editorLayerOffset      = 2;
    innerSpike.priorityTier           = 0;
    innerSpike.noTouch                = true;
    innerSpike.noGlow                 = true;
    result.push_back(innerSpike);

    // Если попросили КАСТОМНЫЕ ШИПЫ или ИИ сгенерировал spike_elements — строим кастомную начинку внутри треугольника шипа!
    if (ai.wantCustomSpikes || !ai.spikeElements.empty()) {
        float spikeH = (sp.objectId == 39 || sp.objectId == 103) ? 15.0f : 30.0f;
        float baseY  = -15.0f;

        // Кастомный внутренний энергетический клинок и основание шипа
        auto [coreX, coreY] = localToWorld(0.f, baseY + spikeH * 0.36f);
        addSpec(
            result, 211, coreX, coreY, 0.08f * sp.scaleX, (spikeH / 30.0f) * 0.56f * sp.scaleY, sp.rotation,
            LayerRole::InnerDetail, ColorSlot::CustomRGB, 11, 0, false,
            235, 3, 3, ai.outlineRGB, ai.wantNeon
        );
        auto [baseBarX, baseBarY] = localToWorld(0.f, baseY + 1.8f);
        addSpec(
            result, 211, baseBarX, baseBarY, 0.86f * sp.scaleX, 0.07f * sp.scaleY, sp.rotation,
            LayerRole::InnerDetail, ColorSlot::CustomRGB, 12, 0, false,
            245, 3, 3, lerpColor(ai.fillRGB, ai.mainRGB, 0.4f), false
        );

        // Все кастомные элементы шипа от ИИ (spike_elements), аккуратно вписанные в треугольник шипа
        for (auto const& sel : ai.spikeElements) {
            float ry = std::clamp(sel.ry, 0.04f, 0.90f);
            float maxHalfFrac = (1.0f - ry) * 0.44f;
            float rx = std::clamp(sel.rx, -maxHalfFrac, maxHalfFrac);
            float lx = rx * 30.0f;
            float ly = baseY + ry * spikeH;
            auto [wx, wy] = localToWorld(lx, ly);

            float maxScaleW = (std::max)(0.03f, (1.0f - ry) * 0.72f);
            GeneratedObjectSpec cs;
            cs.objectId               = resolveAIElementObjectID(sel, cat, true);
            cs.x                      = wx;
            cs.y                      = wy;
            cs.scaleX                 = std::clamp(sel.scaleX, 0.02f, maxScaleW) * sp.scaleX;
            cs.scaleY                 = std::clamp(sel.scaleY, 0.02f, 0.65f) * sp.scaleY;
            cs.rotation               = sp.rotation + sel.rot;
            cs.layerRole              = LayerRole::InnerDetail;
            cs.editorLayerOffset      = std::clamp(sel.editorLayerOff, 2, 5);
            cs.zLayerCode             = (std::max)(3, sel.zLayerCode);
            cs.zOrder                 = (std::max)(13, sel.zOrder);
            cs.colorSlot              = ColorSlot::CustomRGB;
            cs.customColor            = sel.color;
            cs.customOpacity          = sel.opacity;
            cs.customBlending         = sel.blending;
            cs.hasSeparateDetailColor = sel.hasDetailColor;
            cs.detailColor            = sel.detailColor;
            cs.detailOpacity          = sel.detailOpacity;
            cs.detailBlending         = sel.detailBlending;
            cs.priorityTier           = 0;
            cs.noTouch                = true;
            cs.noGlow                 = true;
            result.push_back(cs);
        }
    }
}

// ДЕКОРИРОВАНИЕ ОРБОВ, ДЭШ-ОРБОВ И ПОРТАЛОВ:
// По умолчанию делает ТОЛЬКО мягкое неоновое свечение (Blending = true, прозрачность 60-75%),
// а кастомную текстуру внутри ставит СТРОГО только по явной просьбе игрока (ai.wantCustomOrbTexture)!
static void appendCustomOrbDashPortalDecoration(
    std::vector<GeneratedObjectSpec>& result,
    SelectedOrbPortalInfo const& op,
    EditorTabCatalog const& cat,
    AIDecisionResult const& ai
) {
    float rad = -op.rotation * (3.14159265f / 180.0f);
    float cosA = std::cos(rad);
    float sinA = std::sin(rad);

    float spanX = 26.0f * op.scaleX;
    float spanY = (op.isPortal ? 68.0f : 26.0f) * op.scaleY;

    auto localToWorld = [&](float lx, float ly) -> std::pair<float, float> {
        return { op.x + (lx * cosA - ly * sinA), op.y + (lx * sinA + ly * cosA) };
    };

    // 1. ЧИСТЫЙ НЕОН НА ОРБАХ / ДЭШ-ОРБАХ / ПОРТАЛАХ (мягкое свечение с Blending = true без перекрытия орба текстурой!)
    if (op.isPortal) {
        addSpec(result, 211, op.x, op.y, 0.28f * op.scaleX, 1.75f * op.scaleY, op.rotation, LayerRole::NeonAndGrass, ColorSlot::CustomRGB, 7, 0, false, 155, -1, 3, ai.glowRGB, true);
        addSpec(result, 211, op.x, op.y, 0.14f * op.scaleX, 1.55f * op.scaleY, op.rotation, LayerRole::NeonAndGrass, ColorSlot::CustomRGB, 8, 0, false, 185, 3, 4, ai.outlineRGB, true);
    } else {
        // Неоновый ореол по краям орба (4 тонкие неоновые полоски с Blending = true и прозрачностью ~65-75%)
        constexpr float kEdgeDist = 11.2f;
        auto [topX, topY] = localToWorld(0.f,  kEdgeDist);
        auto [botX, botY] = localToWorld(0.f, -kEdgeDist);
        auto [lftX, lftY] = localToWorld(-kEdgeDist, 0.f);
        auto [rgtX, rgtY] = localToWorld( kEdgeDist, 0.f);

        addSpec(result, 211, topX, topY, 0.56f * op.scaleX, 0.08f * op.scaleY, op.rotation, LayerRole::NeonAndGrass, ColorSlot::CustomRGB, 7, 0, false, 175, 3, 3, ai.glowRGB, true);
        addSpec(result, 211, botX, botY, 0.56f * op.scaleX, 0.08f * op.scaleY, op.rotation, LayerRole::NeonAndGrass, ColorSlot::CustomRGB, 7, 0, false, 175, 3, 3, ai.glowRGB, true);
        addSpec(result, 211, lftX, lftY, 0.08f * op.scaleX, 0.56f * op.scaleY, op.rotation, LayerRole::NeonAndGrass, ColorSlot::CustomRGB, 7, 0, false, 175, 3, 3, ai.glowRGB, true);
        addSpec(result, 211, rgtX, rgtY, 0.08f * op.scaleX, 0.56f * op.scaleY, op.rotation, LayerRole::NeonAndGrass, ColorSlot::CustomRGB, 7, 0, false, 175, 3, 3, ai.glowRGB, true);
        // Мягкое центральное неоновое свечение на заднем подслое (B3, Blending = true)
        addSpec(result, 211, op.x, op.y, 0.42f * op.scaleX, 0.42f * op.scaleY, op.rotation + 45.f, LayerRole::NeonAndGrass, ColorSlot::CustomRGB, 6, 0, false, 145, -1, 2, ai.glowRGB, true);
    }

    // 2. КАСТОМНАЯ ТЕКСТУРА ВНУТРИ ОРБА — СТРОГО ТОЛЬКО ПО ЯВНОЙ ПРОСЬБЕ ИГРОКА!
    if (!ai.wantCustomOrbTexture) {
        return;
    }

    if (op.isDashOrb) {
        addSpec(result, 211, op.x, op.y, 0.38f * op.scaleX, 0.38f * op.scaleY, op.rotation + 45.f, LayerRole::InnerDetail, ColorSlot::CustomRGB, 14, 0, false, 220, 3, 3, ai.fillRGB, false);
        addSpec(result, 211, op.x, op.y, 0.44f * op.scaleX, 0.12f * op.scaleY, op.rotation,        LayerRole::InnerDetail, ColorSlot::CustomRGB, 15, 0, false, 245, 5, 4, ai.mainRGB, true);
    } else if (!op.isPortal) {
        addSpec(result, 211, op.x, op.y, 0.40f * op.scaleX, 0.40f * op.scaleY, op.rotation,        LayerRole::InnerDetail, ColorSlot::CustomRGB, 14, 0, false, 220, 3, 3, ai.fillRGB, false);
        addSpec(result, 211, op.x, op.y, 0.22f * op.scaleX, 0.22f * op.scaleY, op.rotation + 45.f, LayerRole::InnerDetail, ColorSlot::CustomRGB, 15, 0, false, 245, 5, 4, ai.mainRGB, true);
    }

    auto const& srcList = !ai.orbElements.empty() ? ai.orbElements : ai.customElements;
    for (auto const& oel : srcList) {
        float lx = (std::clamp(oel.rx, 0.05f, 0.95f) - 0.5f) * spanX;
        float ly = (std::clamp(oel.ry, 0.05f, 0.95f) - 0.5f) * spanY;
        auto [wx, wy] = localToWorld(lx, ly);

        GeneratedObjectSpec cs;
        cs.objectId               = resolveAIElementObjectID(oel, cat, true);
        cs.x                      = wx;
        cs.y                      = wy;
        cs.scaleX                 = std::clamp(oel.scaleX, 0.02f, 0.65f) * op.scaleX;
        cs.scaleY                 = std::clamp(oel.scaleY, 0.02f, 0.65f) * op.scaleY;
        cs.rotation               = op.rotation + oel.rot;
        cs.layerRole              = LayerRole::InnerDetail;
        cs.editorLayerOffset      = std::clamp(oel.editorLayerOff, 3, 5);
        cs.zLayerCode             = (std::max)(3, oel.zLayerCode);
        cs.zOrder                 = (std::max)(16, oel.zOrder);
        cs.colorSlot              = ColorSlot::CustomRGB;
        cs.customColor            = oel.color;
        cs.customOpacity          = oel.opacity;
        cs.customBlending         = oel.blending;
        cs.hasSeparateDetailColor = oel.hasDetailColor;
        cs.detailColor            = oel.detailColor;
        cs.detailOpacity          = oel.detailOpacity;
        cs.detailBlending         = oel.detailBlending;
        cs.priorityTier           = 0;
        cs.noTouch                = true;
        cs.noGlow                 = true;
        result.push_back(cs);
    }
}

// СБОРКА КАСТОМНОГО ЗАДНЕГО ФОНА (BACKGROUND / BG) В УРОВНЕ, КОГДА НИЧЕГО НЕ ВЫДЕЛЕНО ИЛИ ПОПРОСИЛИ ФОН!
static void appendCustomBackgroundInEditor(
    std::vector<GeneratedObjectSpec>& result,
    ContourAnalysis const& contour,
    EditorTabCatalog const& cat,
    AIDecisionResult const& ai,
    int styleSeed
) {
    int bgCols = (std::max)(12, contour.maxGX - contour.minGX + 1);
    int bgRows = (std::max)(8,  contour.maxGY - contour.minGY + 1);
    float bgOriginX = contour.minGX * 30.f;
    float bgOriginY = contour.minGY * 30.f;
    float bgSpanW   = bgCols * 30.f;
    float bgSpanH   = bgRows * 30.f;
    float centerX   = bgOriginX + bgSpanW * 0.5f;

    // 1. Многослойный плавный градиент неба / фона на глубоком слое B4 (zLayerCode = -3, zOrder = -15..-10)
    constexpr int kSkyBands = 9;
    for (int b = 0; b < kSkyBands; ++b) {
        float t = (b + 0.5f) / static_cast<float>(kSkyBands);
        float bandY = bgOriginY + t * bgSpanH;
        float bandHScale = (bgSpanH / static_cast<float>(kSkyBands) + 2.0f) / 30.0f;
        float bandWScale = bgSpanW / 30.0f;
        ccColor3B skyCol = lerpColor(ai.fillRGB, lerpColor(ai.midRGB, ai.gradTopRGB, t), t);
        addSpec(result, 211, centerX, bandY, bandWScale, bandHScale, 0.f, LayerRole::Fill, ColorSlot::CustomRGB, -15 + b, 0, true, 255, -3, 0, skyCol, false);
    }

    // 2. Дальний и ближний силуэты горизонта (горы / башни / кибер-силуэты по всей ширине фона)
    ccColor3B farSilCol  = lerpColor(ai.fillRGB, ai.midRGB, 0.45f);
    ccColor3B nearSilCol = lerpColor(ai.fillRGB, ccColor3B{ 4, 6, 12 }, 0.55f);
    for (int col = 0; col < bgCols; ++col) {
        float cx = bgOriginX + (col + 0.5f) * 30.f;
        float nFar  = hash2DNoise(col * 3, 11, styleSeed);
        float nNear = hash2DNoise(col * 5, 29, styleSeed + 77);

        float farH  = (1.8f + nFar * 3.2f) * 30.f;
        float nearH = (1.0f + nNear * 2.1f) * 30.f;

        addSpec(result, 211, cx, bgOriginY + farH * 0.5f,  1.04f, farH / 30.f,  0.f, LayerRole::InnerDetail, ColorSlot::CustomRGB, -6, 0, true, 235, -3, 1, farSilCol, false);
        addSpec(result, 211, cx, bgOriginY + nearH * 0.5f, 1.04f, nearH / 30.f, 0.f, LayerRole::InnerDetail, ColorSlot::CustomRGB, -5, 0, true, 250, -3, 1, nearSilCol, false);
    }

    // 3. Атмосферное свечение / звезды / частицы на фоне
    for (int s = 0; s < 18; ++s) {
        float u = hash2DNoise(s, 101, styleSeed + 303);
        float v = 0.35f + hash2DNoise(s, 202, styleSeed + 505) * 0.60f;
        float sx = bgOriginX + u * bgSpanW;
        float sy = bgOriginY + v * bgSpanH;
        float sc = 0.06f + (s % 4) * 0.04f;
        addSpec(result, 211, sx, sy, sc, sc, 45.f, LayerRole::InnerDetail, ColorSlot::CustomRGB, -4, 0, true, 185, -3, 1, ai.glowRGB, true);
    }

    // 4. Все кастомные элементы фона от ИИ (elements и cell_elements), масштабированные на весь экран фона!
    for (auto const& el : ai.customElements) {
        float wx = bgOriginX + std::clamp(el.rx, -0.1f, 1.1f) * bgSpanW;
        float wy = bgOriginY + std::clamp(el.ry, -0.1f, 1.1f) * bgSpanH;

        GeneratedObjectSpec cs;
        cs.objectId               = resolveAIElementObjectID(el, cat, true);
        cs.x                      = wx;
        cs.y                      = wy;
        cs.scaleX                 = std::clamp(el.scaleX, 0.02f, 12.0f);
        cs.scaleY                 = std::clamp(el.scaleY, 0.02f, 12.0f);
        cs.rotation               = el.rot;
        cs.layerRole              = LayerRole::InnerDetail;
        cs.editorLayerOffset      = 1;
        cs.zLayerCode             = -3; // Строго на слое заднего фона B4!
        cs.zOrder                 = -3;
        cs.colorSlot              = ColorSlot::CustomRGB;
        cs.customColor            = el.color;
        cs.customOpacity          = el.opacity;
        cs.customBlending         = el.blending;
        cs.hasSeparateDetailColor = el.hasDetailColor;
        cs.detailColor            = el.detailColor;
        cs.detailOpacity          = el.detailOpacity;
        cs.detailBlending         = el.detailBlending;
        cs.priorityTier           = 0;
        cs.isOverhang             = true;
        cs.noTouch                = true;
        cs.noGlow                 = true;
        result.push_back(cs);
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
    if (ai.directDeleteSelected || ai.directRecolorOnly) {
        return result;
    }

    (void)maxObjectLimit;
    // Если игрок сам написал в чат сколько примерно хочет блоков (ai.suggestedCount > 0) — ориентируемся на его число,
    // а если НЕ написал — ИИ сам решает сколько достаточно и строит 100% структуры без урезаний!
    int userChatRequestedCount = ai.suggestedCount;
    int styleSeed = static_cast<int>(ai.fillRGB.r * 73 + ai.mainRGB.g * 151 + g_variantIndex * 997);

    // ЕСЛИ НИЧЕГО НЕ ВЫДЕЛЕНО (0 блоков) — ИИ СТРОИТ ЗАДНИЙ ФОН (BACKGROUND) ИЛИ СВОБОДНЫЙ АРТ НА ЭКРАНЕ КАМЕРЫ!
    if (!contour.hasAnySelection || (ai.wantBackground && contour.occupiedCells.empty())) {
        appendCustomBackgroundInEditor(result, contour, cat, ai, styleSeed);
        if (userChatRequestedCount > 0 && static_cast<int>(result.size()) > userChatRequestedCount) {
            result.resize(userChatRequestedCount);
        }
        return result;
    }

    auto islands = splitIntoIslands(contour.occupiedCells);

    if (ai.taskMode == AITaskMode::Only3D) {
        for (auto const& isl : islands) {
            for (auto const& [gx, gy] : isl.cells) {
                float cx = gx * 30.f + 15.f;
                float cy = gy * 30.f + 15.f;

                bool topOpen    = (contour.occupiedCells.count({ gx, gy + 1 }) == 0);
                bool bottomOpen = (contour.occupiedCells.count({ gx, gy - 1 }) == 0);
                bool leftOpen   = (contour.occupiedCells.count({ gx - 1, gy }) == 0);
                bool rightOpen  = (contour.occupiedCells.count({ gx + 1, gy }) == 0);

                if (topOpen)    addSpec(result, 211, cx, cy + 14.68f, 1.00f, 0.022f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 10, 0, false, 200, 3, 2);
                if (leftOpen)   addSpec(result, 211, cx - 14.68f, cy, 0.022f, 1.00f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 10, 0, false, 200, 3, 2);
                if (rightOpen)  addSpec(result, 211, cx + 14.68f, cy, 0.022f, 1.00f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 10, 0, false, 200, 3, 2);
                if (bottomOpen && gy > 0) addSpec(result, 211, cx, cy - 14.68f, 1.00f, 0.022f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 10, 0, false, 200, 3, 2);

                if (topOpen) {
                    int top3DId = leftOpen ? cat.id3DTrapTop : cat.id3DHalfStrip;
                    addSpec(result, top3DId, cx, cy + 22.5f, 1.00f, 1.00f, 0.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 0, true, 255, -1, 2);
                }
                if (rightOpen) {
                    addSpec(result, cat.id3DHalfStrip, cx + 22.5f, cy, 1.00f, 1.00f, 90.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 0, true, 255, -1, 2);
                }
                if (topOpen && rightOpen) {
                    addSpec(result, cat.id3DTriCorner, cx + 22.5f, cy + 22.5f, 1.00f, 1.00f, 0.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 0, true, 255, -1, 2);
                    addSpec(result, cat.id3DTriBR,     cx + 22.5f, cy + 22.5f, 1.00f, 1.00f, 0.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 0, true, 255, -1, 2);
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
                bool topOpen    = (contour.occupiedCells.count({ gx, gy + 1 }) == 0);
                bool bottomOpen = (contour.occupiedCells.count({ gx, gy - 1 }) == 0);
                bool leftOpen   = (contour.occupiedCells.count({ gx - 1, gy }) == 0);
                bool rightOpen  = (contour.occupiedCells.count({ gx + 1, gy }) == 0);

                appendOneSidedEdgeNeon(result, cx, cy, topOpen, bottomOpen, leftOpen, rightOpen, allowOverhang, ai.glowRGB);
            }
        }
        for (auto const& sp : contour.spikes) {
            appendSpikeDecoration(result, sp, cat, ai, false);
        }
        return result;
    }

    if (ai.taskMode == AITaskMode::OnlyFill) {
        for (auto const& isl : islands) {
            for (auto const& [gx, gy] : isl.cells) {
                float cx = gx * 30.f + 15.f;
                float cy = gy * 30.f + 15.f;
                addSpec(result, 211, cx, cy, 1.00f, 1.00f, 0.f, LayerRole::Fill, ColorSlot::DarkFill, 1, 0, false, 255, -3, 0);
            }
        }
        return result;
    }

    float spanW = static_cast<float>((std::max)(1, contour.maxGX - contour.minGX + 1)) * 30.f;
    float spanH = static_cast<float>((std::max)(1, contour.maxGY - contour.minGY + 1)) * 30.f;
    float originX = contour.minGX * 30.f;
    float originY = contour.minGY * 30.f;

    (void)rng;
    for (auto const& isl : islands) {
        int islCols = isl.maxGX - isl.minGX + 1;
        int islRows = isl.maxGY - isl.minGY + 1;

        for (auto const& [gx, gy] : isl.cells) {
            float cx = gx * 30.f + 15.f;
            float cy = gy * 30.f + 15.f;

            bool topOpen    = (contour.occupiedCells.count({ gx, gy + 1 }) == 0);
            bool bottomOpen = (contour.occupiedCells.count({ gx, gy - 1 }) == 0);
            bool leftOpen   = (contour.occupiedCells.count({ gx - 1, gy }) == 0);
            bool rightOpen  = (contour.occupiedCells.count({ gx + 1, gy }) == 0);

            // 1. Базовая заливка клетки (с градиентом только по просьбе!)
            if (ai.hasGradient) {
                float normYMid = std::clamp((cy - originY) / spanH, 0.0f, 1.0f);
                ccColor3B baseCol = lerpColor(ai.gradBotRGB, ai.gradTopRGB, normYMid);
                addSpec(result, 211, cx, cy, 1.00f, 1.00f, 0.f, LayerRole::Fill, ColorSlot::CustomRGB, 1, 0, false, 255, -3, 0, baseCol, false);
                for (int band = 0; band < 4; ++band) {
                    float bandFrac = (band + 0.5f) / 4.0f;
                    float bandY = (cy - 15.f) + bandFrac * 30.f;
                    float globalT = std::clamp((bandY - originY) / spanH, 0.0f, 1.0f);
                    ccColor3B bandCol = lerpColor(ai.gradBotRGB, ai.gradTopRGB, globalT);
                    addSpec(result, 211, cx, bandY, 1.00f, 0.27f, 0.f, LayerRole::InnerDetail, ColorSlot::CustomRGB, 2 + band, 0, false, 220, -1, 1, bandCol, false);
                }
            } else {
                addSpec(result, 211, cx, cy, 1.00f, 1.00f, 0.f, LayerRole::Fill, ColorSlot::CustomRGB, 1, 0, false, 255, -3, 0, ai.fillRGB, false);
            }

            // 2. СБОРКА КИРПИЧЕЙ ИЗ КУБИКОВ 211 (строго только когда попросили кирпичи!)
            if (ai.wantBricks) {
                appendCubeBrickworkInCell(result, cx, cy, gx, gy, ai, styleSeed);
            }
            // 3. СБОРКА НЕОНОВОГО СТИЛЯ (тонкие неоновые линии без перекрытия текстуры!)
            else if (ai.wantNeon && !ai.wantRealisticPBR && !g_hasReferenceImage) {
                if (topOpen)    addSpec(result, 211, cx, cy + 11.5f, 0.90f, 0.025f, 0.f, LayerRole::InnerDetail, ColorSlot::CustomRGB, 8, 0, false, 185, 3, 2, ai.glowRGB, true);
                if (bottomOpen) addSpec(result, 211, cx, cy - 11.5f, 0.90f, 0.025f, 0.f, LayerRole::InnerDetail, ColorSlot::CustomRGB, 8, 0, false, 165, 3, 2, ai.mainRGB, true);
                if (leftOpen)   addSpec(result, 211, cx - 11.5f, cy, 0.025f, 0.90f, 0.f, LayerRole::InnerDetail, ColorSlot::CustomRGB, 8, 0, false, 185, 3, 2, ai.glowRGB, true);
                if (rightOpen)  addSpec(result, 211, cx + 11.5f, cy, 0.025f, 0.90f, 0.f, LayerRole::InnerDetail, ColorSlot::CustomRGB, 8, 0, false, 185, 3, 2, ai.glowRGB, true);

                addSpec(result, 211, cx, cy, 0.36f, 0.025f, 0.f, LayerRole::InnerDetail, ColorSlot::CustomRGB, 7, 0, false, 125, 1, 2, ai.midRGB, true);
                addSpec(result, 211, cx, cy, 0.025f, 0.36f, 0.f, LayerRole::InnerDetail, ColorSlot::CustomRGB, 7, 0, false, 125, 1, 2, ai.midRGB, true);
            }
            // 4. ПОПИКСЕЛЬНЫЙ PBR ИЗ КУБИКОВ 211 (ТОЛЬКО если есть референс ИЛИ игрок попросил реалистик/PBR!)
            else if (ai.wantRealisticPBR || g_hasReferenceImage) {
                constexpr int kSubDiv = 6;
                float stepPx = 30.0f / static_cast<float>(kSubDiv);
                float baseScaleY = 1.02f / static_cast<float>(kSubDiv);

                for (int sy = 0; sy < kSubDiv; ++sy) {
                    float pyWorld = (cy - 15.f) + (sy + 0.5f) * stepPx;
                    float localV = (sy + 0.5f) / static_cast<float>(kSubDiv);

                    int sx = 0;
                    while (sx < kSubDiv) {
                        float pxFirst = (cx - 15.f) + (sx + 0.5f) * stepPx;
                        float localU  = (sx + 0.5f) / static_cast<float>(kSubDiv);
                        ccColor3B c0 = computeRealisticTexelColor(
                            pxFirst, pyWorld, originX, originY, spanW, spanH,
                            topOpen, bottomOpen, leftOpen, rightOpen,
                            localU, localV, ai, styleSeed
                        );

                        int runLen = 1;
                        while (sx + runLen < kSubDiv) {
                            float pxNext = (cx - 15.f) + (sx + runLen + 0.5f) * stepPx;
                            float nextU  = (sx + runLen + 0.5f) / static_cast<float>(kSubDiv);
                            ccColor3B cNext = computeRealisticTexelColor(
                                pxNext, pyWorld, originX, originY, spanW, spanH,
                                topOpen, bottomOpen, leftOpen, rightOpen,
                                nextU, localV, ai, styleSeed
                            );
                            int diff = std::abs(static_cast<int>(c0.r) - static_cast<int>(cNext.r)) +
                                       std::abs(static_cast<int>(c0.g) - static_cast<int>(cNext.g)) +
                                       std::abs(static_cast<int>(c0.b) - static_cast<int>(cNext.b));
                            if (diff > 14) break;
                            runLen++;
                        }

                        float mergedCenterX = (cx - 15.f) + (sx + runLen * 0.5f) * stepPx;
                        float mergedScaleX  = (runLen * 1.02f) / static_cast<float>(kSubDiv);
                        addSpec(
                            result, 211, mergedCenterX, pyWorld, mergedScaleX, baseScaleY, 0.f,
                            LayerRole::InnerDetail, ColorSlot::CustomRGB, 4, 0, false,
                            245, -1, 1, c0, false
                        );
                        sx += runLen;
                    }
                }
            }

            // 5. ПРЯМАЯ ГЕНЕРАЦИЯ ИИ ВНУТРИ КАЖДОЙ КЛЕТКИ 30x30 (cell_elements)
            for (auto const& cel : ai.cellElements) {
                float localX = (cx - 15.f) + std::clamp(cel.rx, -0.15f, 1.15f) * 30.f;
                float localY = (cy - 15.f) + std::clamp(cel.ry, -0.15f, 1.15f) * 30.f;

                GeneratedObjectSpec cs;
                cs.objectId               = resolveAIElementObjectID(cel, cat, true);
                cs.x                      = localX;
                cs.y                      = localY;
                cs.scaleX                 = std::clamp(cel.scaleX, 0.005f, 2.5f);
                cs.scaleY                 = std::clamp(cel.scaleY, 0.005f, 2.5f);
                cs.rotation               = cel.rot;
                cs.layerRole              = LayerRole::InnerDetail;
                cs.editorLayerOffset      = std::clamp(cel.editorLayerOff, 1, 5);
                cs.zLayerCode             = (std::max)(1, cel.zLayerCode);
                cs.zOrder                 = (std::max)(8, cel.zOrder);
                cs.colorSlot              = ColorSlot::CustomRGB;
                cs.customColor            = cel.color;
                cs.customOpacity          = cel.opacity;
                cs.customBlending         = cel.blending;
                cs.hasSeparateDetailColor = cel.hasDetailColor;
                cs.detailColor            = cel.detailColor;
                cs.detailOpacity          = cel.detailOpacity;
                cs.detailBlending         = cel.detailBlending;
                cs.priorityTier           = 0;
                cs.noTouch                = true;
                cs.noGlow                 = true;
                result.push_back(cs);
            }

            // 6. СБОРКА РЕАЛИСТИЧНЫХ ОКОН ИЗ КУБИКОВ 211
            if (ai.wantWindows) {
                int localCol = gx - isl.minGX;
                int localRow = gy - isl.minGY;
                bool isWindowCell = false;
                if (islCols >= 3 && islRows >= 3) {
                    isWindowCell = (localCol > 0 && localCol < islCols - 1 && localRow > 0 && localRow < islRows - 1 &&
                                   (localCol % 2 == 1) && (localRow % 2 == 1));
                } else {
                    isWindowCell = (localCol == islCols / 2 && localRow == islRows / 2);
                }
                if (isWindowCell) {
                    appendCubeRealisticWindowInCell(result, cx, cy, ai);
                }
            }

            // 7. УЛЬТРА-ТОНКАЯ ОБВОДКА ГРАНИЦЫ СТРУКТУРЫ (толщина 0.022 = 0.66 ед. прямо на краю 14.68, НИКОГДА не перекрывает текстуру!)
            bool glowBorder = ai.wantNeon;
            if (topOpen)    addSpec(result, 211, cx, cy + 14.68f, 1.00f, 0.022f, 0.f, LayerRole::Outline3D, ColorSlot::CustomRGB, 10, 0, false, 195, 3, 3, ai.outlineRGB, glowBorder);
            if (leftOpen)   addSpec(result, 211, cx - 14.68f, cy, 0.022f, 1.00f, 0.f, LayerRole::Outline3D, ColorSlot::CustomRGB, 10, 0, false, 185, 3, 3, ai.outlineRGB, glowBorder);
            if (rightOpen)  addSpec(result, 211, cx + 14.68f, cy, 0.022f, 1.00f, 0.f, LayerRole::Outline3D, ColorSlot::CustomRGB, 10, 0, false, 185, 3, 3, ai.outlineRGB, glowBorder);
            if (bottomOpen && gy > 0) addSpec(result, 211, cx, cy - 14.68f, 1.00f, 0.022f, 0.f, LayerRole::Outline3D, ColorSlot::CustomRGB, 10, 0, false, 175, 3, 3, ai.outlineRGB, glowBorder);

            // 8. ОДНОСТОРОННИЙ НЕОН ПО КРАЯМ (тонкий и деликатный, ТОЛЬКО когда запрошен неон!)
            if (ai.wantNeon) {
                appendOneSidedEdgeNeon(result, cx, cy, topOpen, bottomOpen, leftOpen, rightOpen, allowOverhang, ai.glowRGB);
            }
        }

        // 9. СПУСКАЮЩИЕСЯ ЛИАНЫ НА КАЖДОМ ПАРТЕ
        if (ai.wantVines) {
            appendCubeHangingVinesOnIsland(result, isl, contour, allowOverhang, styleSeed);
        }

        // 10. ПРЯМАЯ ГЕНЕРАЦИЯ ЭЛЕМЕНТОВ ИИ НА КАЖДОМ ПАРТЕ
        float islOriginX = isl.minGX * 30.f;
        float islOriginY = isl.minGY * 30.f;
        float islSpanW   = static_cast<float>((std::max)(1, islCols)) * 30.f;
        float islSpanH   = static_cast<float>((std::max)(1, islRows)) * 30.f;

        for (auto const& el : ai.customElements) {
            bool isIntentionalOverhang = (el.rx < 0.0f || el.rx > 1.0f || el.ry < 0.0f || el.ry > 1.0f);
            if (isIntentionalOverhang && !allowOverhang) {
                continue;
            }

            float worldX = islOriginX + el.rx * islSpanW;
            float worldY = islOriginY + el.ry * islSpanH;
            int cellGX = static_cast<int>(std::floor(worldX / 30.f));
            int cellGY = static_cast<int>(std::floor(worldY / 30.f));

            if (!isIntentionalOverhang && isl.cells.count({ cellGX, cellGY }) == 0) {
                int bestGX = isl.cells.begin()->first;
                int bestGY = isl.cells.begin()->second;
                int bestDist = 999999;
                for (auto const& [cgx, cgy] : isl.cells) {
                    int d = std::abs(cgx - cellGX) + std::abs(cgy - cellGY);
                    if (d < bestDist) {
                        bestDist = d;
                        bestGX = cgx;
                        bestGY = cgy;
                    }
                }
                worldX = bestGX * 30.f + (worldX - cellGX * 30.f);
                worldY = bestGY * 30.f + (worldY - cellGY * 30.f);
            }

            GeneratedObjectSpec cs;
            cs.objectId               = resolveAIElementObjectID(el, cat, true);
            cs.x                      = worldX;
            cs.y                      = worldY;
            cs.scaleX                 = std::clamp(el.scaleX, 0.005f, 12.0f);
            cs.scaleY                 = std::clamp(el.scaleY, 0.005f, 12.0f);
            cs.rotation               = el.rot;
            cs.layerRole              = LayerRole::InnerDetail;
            cs.editorLayerOffset      = std::clamp(el.editorLayerOff, 2, 5);
            cs.zLayerCode             = (std::max)(3, el.zLayerCode);
            cs.zOrder                 = (std::max)(18, el.zOrder);
            cs.colorSlot              = ColorSlot::CustomRGB;
            cs.customColor            = el.color;
            cs.customOpacity          = el.opacity;
            cs.customBlending         = el.blending || (ai.wantNeon && (el.color.r > 140 || el.color.g > 140 || el.color.b > 140));
            cs.hasSeparateDetailColor = el.hasDetailColor;
            cs.detailColor            = el.detailColor;
            cs.detailOpacity          = el.detailOpacity;
            cs.detailBlending         = el.detailBlending;
            cs.priorityTier           = 0;
            cs.isOverhang             = isIntentionalOverhang;
            cs.noTouch                = !isSpikeObjectID(cs.objectId);
            cs.noGlow                 = true;
            result.push_back(cs);
        }
    }

    // 11. ДЕКОРИРОВАНИЕ ВЫДЕЛЕННЫХ ШИПОВ ИЛИ СОЗДАНИЕ КАСТОМНЫХ ШИПОВ С НУЛЯ (если попросили шипы!)
    if (!contour.spikes.empty()) {
        for (auto const& sp : contour.spikes) {
            appendSpikeDecoration(result, sp, cat, ai, false);
        }
    } else if (ai.wantCustomSpikes && allowOverhang) {
        for (auto const& [gx, gy] : contour.occupiedCells) {
            if (contour.occupiedCells.count({ gx, gy + 1 }) == 0) {
                SelectedSpikeInfo customSp;
                customSp.x        = gx * 30.f + 15.f;
                customSp.y        = (gy + 1) * 30.f + 15.f;
                customSp.rotation = 0.f;
                customSp.scaleX   = 1.0f;
                customSp.scaleY   = 1.0f;
                customSp.objectId = 8;
                appendSpikeDecoration(result, customSp, cat, ai, true);
            }
        }
    }

    // 12. РАБОТА С ОРБАМИ, ДЭШ-ОРБАМИ И ПОРТАЛАМИ (СТРОГО ТОЛЬКО ЕСЛИ ИГРОК ПОПРОСИЛ ИЛИ ВЫДЕЛИЛ ТОЛЬКО ИХ!)
    bool onlyOrbsSelected = (contour.occupiedCells.empty() && contour.spikes.empty() && !contour.orbsAndPortals.empty());
    if (ai.allowOrbsAndPortals || onlyOrbsSelected) {
        for (auto const& op : contour.orbsAndPortals) {
            appendCustomOrbDashPortalDecoration(result, op, cat, ai);
        }
    }

    // Только если игрок сам написал в чат желаемое количество блоков (userChatRequestedCount > 0) —
    // подгоняем количество объектов под его запрос; иначе оставляем 100% всех построенных блоков без урезаний!
    if (userChatRequestedCount > 0 && !contour.occupiedCells.empty() && static_cast<int>(result.size()) < userChatRequestedCount) {
        std::vector<std::pair<int, int>> cellVec(contour.occupiedCells.begin(), contour.occupiedCells.end());
        int fillIdx = 0;
        while (static_cast<int>(result.size()) < userChatRequestedCount && fillIdx < 8000) {
            auto const& [gx, gy] = cellVec[fillIdx % cellVec.size()];
            int subStep = fillIdx / static_cast<int>(cellVec.size());
            float u = hash2DNoise(gx * 19 + subStep, gy * 23 + fillIdx, styleSeed + 401);
            float v = hash2DNoise(gx * 31 + fillIdx, gy * 13 + subStep, styleSeed + 809);
            float px = gx * 30.f + 3.0f + u * 24.0f;
            float py = gy * 30.f + 3.0f + v * 24.0f;
            float scX = 0.04f + (fillIdx % 5) * 0.03f;
            float scY = 0.04f + ((fillIdx / 3) % 4) * 0.03f;
            ccColor3B detailCol = lerpColor(ai.midRGB, ai.mainRGB, (fillIdx % 10) / 10.0f);
            addSpec(result, 211, px, py, scX, scY, 0.f, LayerRole::InnerDetail, ColorSlot::CustomRGB, 9 + (fillIdx % 6), 2, false, 195, 1, 2, detailCol, ai.wantNeon);
            fillIdx++;
        }
    }

    std::stable_sort(result.begin(), result.end(), [](GeneratedObjectSpec const& a, GeneratedObjectSpec const& b) {
        return a.priorityTier < b.priorityTier;
    });

    if (userChatRequestedCount > 0 && static_cast<int>(result.size()) > userChatRequestedCount) {
        result.resize(userChatRequestedCount);
    }

    return result;
}

// ============================================================================
// Спавн и отмена объектов в редакторе GD (с поддержкой 6 подслоев и ZLayer)
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

static AIDecisionResult g_lastAIDecisionForEditor;

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
    summary.firstLayer = findFirstFreeEditorLayerBlock(editorLayer, 6);

    // 1. ПРЯМЫЕ ОПЕРАЦИИ ИИ С ВЫДЕЛЕННЫМИ ОБЪЕКТАМИ В РЕДАКТОРЕ (удаление, сдвиг, поворот, масштаб, дублирование, покраска!)
    if (g_lastAIDecisionForEditor.directDeleteSelected) {
        for (GameObject* baseObj : contour.solidObjects) {
            if (!baseObj) continue;
            writeObjScale(baseObj, 0.f, 0.f);
            baseObj->setPosition(CCPoint{ -99999.f, -99999.f });
            baseObj->setOpacity(0);
        }
        for (auto const& sp : contour.spikes) {
            if (!sp.sourceObj) continue;
            writeObjScale(sp.sourceObj, 0.f, 0.f);
            sp.sourceObj->setPosition(CCPoint{ -99999.f, -99999.f });
            sp.sourceObj->setOpacity(0);
        }
        return summary;
    }

    bool hasMoveOrRotOrScale = (std::fabs(g_lastAIDecisionForEditor.directMoveX) > 0.01f ||
                                std::fabs(g_lastAIDecisionForEditor.directMoveY) > 0.01f ||
                                std::fabs(g_lastAIDecisionForEditor.directRotateDeg) > 0.01f ||
                                std::fabs(g_lastAIDecisionForEditor.directScaleMulX - 1.0f) > 0.01f ||
                                std::fabs(g_lastAIDecisionForEditor.directScaleMulY - 1.0f) > 0.01f);

    if (hasMoveOrRotOrScale) {
        for (GameObject* baseObj : contour.solidObjects) {
            if (!baseObj) continue;
            CCPoint p = baseObj->getPosition();
            baseObj->setPosition(CCPoint{ p.x + g_lastAIDecisionForEditor.directMoveX, p.y + g_lastAIDecisionForEditor.directMoveY });
            baseObj->setRotation(baseObj->getRotation() + g_lastAIDecisionForEditor.directRotateDeg);
            writeObjScale(baseObj, baseObj->getScaleX() * g_lastAIDecisionForEditor.directScaleMulX, baseObj->getScaleY() * g_lastAIDecisionForEditor.directScaleMulY);
        }
        for (auto const& sp : contour.spikes) {
            if (!sp.sourceObj) continue;
            CCPoint p = sp.sourceObj->getPosition();
            sp.sourceObj->setPosition(CCPoint{ p.x + g_lastAIDecisionForEditor.directMoveX, p.y + g_lastAIDecisionForEditor.directMoveY });
            sp.sourceObj->setRotation(sp.sourceObj->getRotation() + g_lastAIDecisionForEditor.directRotateDeg);
            writeObjScale(sp.sourceObj, sp.sourceObj->getScaleX() * g_lastAIDecisionForEditor.directScaleMulX, sp.sourceObj->getScaleY() * g_lastAIDecisionForEditor.directScaleMulY);
        }
    }

    if (g_lastAIDecisionForEditor.directDuplicateCount > 0) {
        for (int d = 1; d <= g_lastAIDecisionForEditor.directDuplicateCount; ++d) {
            float dx = g_lastAIDecisionForEditor.directDuplicateStepX * d;
            float dy = g_lastAIDecisionForEditor.directDuplicateStepY * d;
            for (GameObject* baseObj : contour.solidObjects) {
                if (!baseObj) continue;
                int id = readObjID(baseObj);
                CCPoint p = baseObj->getPosition();
                if (GameObject* dup = editorLayer->createObject(id > 0 ? id : 211, CCPoint{ p.x + dx, p.y + dy }, false)) {
                    dup->setRotation(baseObj->getRotation());
                    writeObjScale(dup, baseObj->getScaleX(), baseObj->getScaleY());
                    bindObjectToColor(dup, palette, ColorSlot::BrightMain, g_lastAIDecisionForEditor.mainRGB);
                    g_lastPlacedObjects.push_back(dup);
                    summary.createdCount++;
                }
            }
        }
    }

    if (taskMode == AITaskMode::FullDecor || taskMode == AITaskMode::OnlyFill || taskMode == AITaskMode::OnlyNeon) {
        ColorSlot blockSlot = g_lastAIDecisionForEditor.directRecolorOnly ? ColorSlot::BrightMain : ColorSlot::DarkFill;
        for (GameObject* baseObj : contour.solidObjects) {
            if (!baseObj) continue;
            writeObjEditorLayers(baseObj, summary.firstLayer, 0);
            writeObjFlags(baseObj, 0, -3, false, true);
            bindObjectToColor(baseObj, palette, blockSlot, g_lastAIDecisionForEditor.mainRGB);
        }
        for (auto const& sp : contour.spikes) {
            if (!sp.sourceObj) continue;
            writeObjEditorLayers(sp.sourceObj, summary.firstLayer, 0);
            writeObjFlags(sp.sourceObj, 1, -1, false, true);
            bindObjectToColor(
                sp.sourceObj, palette, ColorSlot::DarkFill, ccColor3B{ 0, 0, 0 }, 255, false,
                true, palette.slotColors[static_cast<int>(ColorSlot::HighlightOutline)], 240, false
            );
        }
    }

    for (auto const& s : specs) {
        GameObject* obj = editorLayer->createObject(s.objectId, CCPoint{ s.x, s.y }, false);
        if (!obj) continue;

        obj->setRotation(s.rotation);
        obj->setFlipX(s.flipX);
        obj->setFlipY(s.flipY);
        writeObjScale(obj, s.scaleX, s.scaleY);
        writeObjFlags(obj, s.zOrder, s.zLayerCode, s.noTouch, s.noGlow);

        int layerOff = (s.editorLayerOffset > 0) ? s.editorLayerOffset : static_cast<int>(s.layerRole);
        int assignedLayer = summary.firstLayer + layerOff;
        // Назначаем основной слой редактора и второй вложенный слой (m_editorLayer2) для внутренних деталей блока
        int secondaryLayer = (layerOff >= 1 && layerOff <= 3) ? (summary.firstLayer + 1) : 0;
        writeObjEditorLayers(obj, assignedLayer, secondaryLayer);

        bindObjectToColor(
            obj,
            palette,
            s.colorSlot,
            s.customColor,
            s.customOpacity,
            s.customBlending,
            s.hasSeparateDetailColor,
            s.detailColor,
            s.detailOpacity,
            s.detailBlending
        );

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
    { 'J', "o",   'j', 'J' }, { 'K', "l",   'k', 'K' }, { 'L', "d",   'l', 'L' },
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
// Помощники UI:
// - Прямоугольные кнопки (CCLayerColor)
// - Округлый каркас меню и округлая строка ввода (CCScale9Sprite с увеличенным радиусом)
// - Градиентная подложка под кнопками (CCLayerGradient по размерам каркаса)
// ============================================================================
static CCLayerColor* createSolidPanel(float width, float height, ccColor3B color, GLubyte alpha = 255) {
    auto* panel = CCLayerColor::create(ccColor4B{ color.r, color.g, color.b, alpha }, width, height);
    panel->ignoreAnchorPointForPosition(false);
    panel->setAnchorPoint(CCPoint{ 0.5f, 0.5f });
    return panel;
}

static ccColor4F toColor4F(ccColor3B c, GLubyte alpha = 255) {
    return ccColor4F{ c.r / 255.f, c.g / 255.f, c.b / 255.f, alpha / 255.f };
}

// Генерация настоящей RGBA8888 текстуры с идеальными кругами на углах и антиалиасингом (SDF).
// Работает как обычный CCSprite — поэтому на 100% отображается в Geometry Dash без багов шейдера CCDrawNode!
static CCSprite* createSmoothGradientRoundedRect(
    float width,
    float height,
    float radius,
    ccColor4F topColor,
    ccColor4F bottomColor
) {
    // Генерируем текстуру с запасом четкости (2x) и компенсируем CCDirector::getContentScaleFactor(),
    // чтобы в GD на High/Medium/Low текстурах размер панели на 100% совпадал с width x height!
    int texW = std::clamp(static_cast<int>(std::lround(width * 2.0f)), 8, 1200);
    int texH = std::clamp(static_cast<int>(std::lround(height * 2.0f)), 8, 800);
    float radPx = std::clamp(radius * 2.0f, 2.0f, (std::min)(texW, texH) * 0.5f);

    std::vector<uint8_t> rgba(static_cast<size_t>(texW * texH * 4), 0);
    float halfW = texW * 0.5f;
    float halfH = texH * 0.5f;
    float innerW = halfW - radPx;
    float innerH = halfH - radPx;

    for (int py = 0; py < texH; ++py) {
        float t = (texH > 1) ? (static_cast<float>(py) / static_cast<float>(texH - 1)) : 0.f;
        float rf = std::clamp(topColor.r + (bottomColor.r - topColor.r) * t, 0.f, 1.f);
        float gf = std::clamp(topColor.g + (bottomColor.g - topColor.g) * t, 0.f, 1.f);
        float bf = std::clamp(topColor.b + (bottomColor.b - topColor.b) * t, 0.f, 1.f);
        float af = std::clamp(topColor.a + (bottomColor.a - topColor.a) * t, 0.f, 1.f);

        uint8_t r8 = static_cast<uint8_t>(std::lround(rf * 255.f));
        uint8_t g8 = static_cast<uint8_t>(std::lround(gf * 255.f));
        uint8_t b8 = static_cast<uint8_t>(std::lround(bf * 255.f));

        float dy = std::fabs((py + 0.5f) - halfH) - innerH;

        for (int px = 0; px < texW; ++px) {
            float dx = std::fabs((px + 0.5f) - halfW) - innerW;
            float dist = 0.f;
            if (dx > 0.f && dy > 0.f) {
                dist = std::sqrt(dx * dx + dy * dy) - radPx;
            } else {
                dist = (std::max)(dx, dy) - radPx;
            }

            if (dist >= 0.75f) continue;

            // Плавное сглаживание краев (anti-aliasing) в 1.5 пикселя
            float cov = std::clamp(0.5f - dist / 1.5f, 0.f, 1.f);
            uint8_t a8 = static_cast<uint8_t>(std::lround(af * cov * 255.f));
            if (a8 == 0) continue;

            size_t idx = static_cast<size_t>((py * texW + px) * 4);
            // В Cocos2d-x kCCTexture2DPixelFormat_RGBA8888 использует не-премультиплицированный или премультиплицированный альфа
            rgba[idx + 0] = r8;
            rgba[idx + 1] = g8;
            rgba[idx + 2] = b8;
            rgba[idx + 3] = a8;
        }
    }

    auto* tex = new CCTexture2D();
    if (tex && tex->initWithData(
            rgba.data(),
            kCCTexture2DPixelFormat_RGBA8888,
            texW,
            texH,
            CCSize{ static_cast<float>(texW), static_cast<float>(texH) }
        )) {
        tex->autorelease();
        auto* spr = CCSprite::createWithTexture(tex);
        if (spr) {
            CCSize ptsSize = spr->getContentSize();
            float curW = (ptsSize.width > 0.01f) ? ptsSize.width : static_cast<float>(texW);
            float curH = (ptsSize.height > 0.01f) ? ptsSize.height : static_cast<float>(texH);
            spr->setScaleX(width / curW);
            spr->setScaleY(height / curH);
            return spr;
        }
    } else {
        CC_SAFE_DELETE(tex);
    }

    // Фолбэк на случай нехватки видеопамяти
    auto* fallback = CCSprite::create();
    return fallback;
}

static CCSprite* createSmoothRoundedRect(float width, float height, float radius, ccColor4F color) {
    return createSmoothGradientRoundedRect(width, height, radius, color, color);
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
    auto* border = createSolidPanel(width + 2.f, height + 2.f, ccColor3B{ 78, 36, 42 }, 255);
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
    int m_activeApiField = 0; // 0 = API Key, 1 = Base URL, 2 = Endpoint, 3 = Model Name
    int m_previewTab = 0; // 0 = Shape, 1 = Drawing, 2 = In GD
    std::string m_inputBuffer;
    CCLayerColor* m_apiFieldBg[4]{ nullptr, nullptr, nullptr, nullptr };
    CCLabelBMFont* m_apiFieldLbl[4]{ nullptr, nullptr, nullptr, nullptr };
    bool m_prevKeyStates[256]{};

    CCLabelBMFont* m_inputDisplayLabel = nullptr;
    CCLabelBMFont* m_langBadgeLabel = nullptr;
    CCLabelBMFont* m_apiGateStatusLabel = nullptr;
    CCLayerColor* m_checkBoxBg = nullptr;
    CCNode* m_checkTickContainer = nullptr;

    // Прокручиваемый ИИ-чат для чтения старых и новых сообщений
    geode::ScrollLayer* m_chatScrollLayer = nullptr;
    CCLabelBMFont* m_stepPlanLabel = nullptr;
    CCLabelBMFont* m_stepDrawingLabel = nullptr;
    CCLabelBMFont* m_stepPlaceLabel = nullptr;

    // Правая панель Art Agent (Shape | Drawing | In GD)
    CCLayerColor* m_tabShapeBg = nullptr;
    CCLayerColor* m_tabDrawingBg = nullptr;
    CCLayerColor* m_tabInGDBg = nullptr;
    CCMenuItemSpriteExtra* m_shapeFillToggleBtn = nullptr;
    CCLabelBMFont* m_shapeFillToggleLbl = nullptr;
    CCLayerColor* m_aiCountToggleBg = nullptr;
    CCLabelBMFont* m_aiCountToggleLbl = nullptr;
    CCNode* m_previewCanvasContent = nullptr;
    CCLabelBMFont* m_previewSubInfoLabel = nullptr;
    CCLabelBMFont* m_limitValueLabel = nullptr;
    CCLayerColor* m_insideTabBg = nullptr;
    CCLayerColor* m_overhangTabBg = nullptr;

    bool init(EditorUI* ui, ContourAnalysis contour, PopupStage requestedStage, PopupStage prevStage) {
        if (!Popup::init(500.f, 294.f)) return false;

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

        if constexpr (requires { this->m_bgSprite; }) {
            if (this->m_bgSprite) {
                this->m_bgSprite->setVisible(false);
                this->m_bgSprite->setScale(0.f);
            }
        }

        auto size = m_mainLayer->getContentSize();
        auto center = CCPoint{ size.width / 2.f, size.height / 2.f };

        // 1. Возвращаем мягкое многослойное СВЕЧЕНИЕ (ореол) вокруг округленного каркаса меню!
        auto* frameGlowOuter = createSmoothRoundedRect(
            size.width + 18.f, size.height + 18.f, 34.f,
            toColor4F(ccColor3B{ 155, 22, 38 }, 55)
        );
        frameGlowOuter->setPosition(center);
        frameGlowOuter->setZOrder(-6);
        m_mainLayer->addChild(frameGlowOuter);

        auto* frameGlowMid = createSmoothRoundedRect(
            size.width + 10.f, size.height + 10.f, 30.f,
            toColor4F(ccColor3B{ 205, 34, 56 }, 105)
        );
        frameGlowMid->setPosition(center);
        frameGlowMid->setZOrder(-5);
        m_mainLayer->addChild(frameGlowMid);

        // 2. Ярко выраженный ОКРУГЛЕННЫЙ каркас меню (радиус 26px) со светящимся кантом
        auto* outerRoundedFrame = createSmoothRoundedRect(
            size.width + 2.f, size.height + 2.f, 26.f,
            toColor4F(ccColor3B{ 235, 55, 82 }, 255)
        );
        outerRoundedFrame->setPosition(center);
        outerRoundedFrame->setZOrder(-4);
        m_mainLayer->addChild(outerRoundedFrame);

        auto* innerRoundedShell = createSmoothRoundedRect(
            size.width - 4.f, size.height - 4.f, 24.f,
            toColor4F(ccColor3B{ 14, 8, 11 }, 255)
        );
        innerRoundedShell->setPosition(center);
        innerRoundedShell->setZOrder(-3);
        m_mainLayer->addChild(innerRoundedShell);

        // 3. Скругленная градиентная красно-черная подложка (image_b5b63a.png),
        //    лежащая строго на каркасе меню под всеми кнопками (без прямых углов 90 градусов!)
        auto* frameGradient = createSmoothGradientRoundedRect(
            size.width - 10.f, size.height - 10.f, 22.f,
            toColor4F(ccColor3B{ 78, 9, 14 }, 255),
            toColor4F(ccColor3B{ 5, 2, 4 }, 255)
        );
        frameGradient->setPosition(center);
        frameGradient->setZOrder(-2);
        m_mainLayer->addChild(frameGradient);

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
    // ЭКРАН 1: СТРОГАЯ ПРОВЕРКА ДОСТОВЕРНОСТИ API-КЛЮЧА
    // ========================================================================
    std::string getApiFieldValue(int fieldIdx) const {
        switch (fieldIdx) {
            case 0: return g_connectedApiKey;
            case 1: return g_customBaseUrl;
            case 2: return g_customEndpointPath;
            case 3: return g_customModelName;
            default: return g_connectedApiKey;
        }
    }

    void setApiFieldValue(int fieldIdx, std::string const& val) {
        std::string clean = trimWhitespace(val);
        switch (fieldIdx) {
            case 0: g_connectedApiKey    = sanitizeApiKeyString(clean); break;
            case 1: g_customBaseUrl      = clean; break;
            case 2: g_customEndpointPath = clean; break;
            case 3: g_customModelName    = clean; break;
            default: break;
        }
    }

    void refreshApiGateFieldsUI() {
        static constexpr const char* kFieldTitles[4] = { "API Key", "Base URL", "Endpoint", "Model" };
        for (int i = 0; i < 4; ++i) {
            bool active = (m_activeApiField == i);
            if (m_apiFieldBg[i]) {
                m_apiFieldBg[i]->setColor(active ? ccColor3B{ 44, 76, 108 } : ccColor3B{ 36, 26, 32 });
            }
            if (m_apiFieldLbl[i]) {
                std::string val = (active && !m_inputBuffer.empty()) ? m_inputBuffer : getApiFieldValue(i);
                if (i == 0 && !active) {
                    val = maskApiKeyPreview(val);
                }
                if (val.empty()) val = "(click to set)";
                if (val.size() > 24) val = val.substr(0, 21) + "...";
                std::string fullTxt = fmt::format("{}: {}", kFieldTitles[i], val);
                m_apiFieldLbl[i]->setString(fullTxt.c_str());
                m_apiFieldLbl[i]->setColor(active ? ccColor3B{ 135, 235, 255 } : ccColor3B{ 235, 240, 250 });
            }
        }
    }

    void selectApiGateField(int fieldIdx) {
        setApiFieldValue(m_activeApiField, m_inputBuffer);
        m_activeApiField = std::clamp(fieldIdx, 0, 3);
        m_inputBuffer = getApiFieldValue(m_activeApiField);
        refreshInputLabel();
    }

    void onSelectApiField0(CCObject*) { selectApiGateField(0); }
    void onSelectApiField1(CCObject*) { selectApiGateField(1); }
    void onSelectApiField2(CCObject*) { selectApiGateField(2); }
    void onSelectApiField3(CCObject*) { selectApiGateField(3); }

    void setupStrictApiGateUI(CCPoint center) {
        this->setTitle("ART AGENT — UNIVERSAL API, BASE URL & ENDPOINT");

        if (g_apiVerifiedAndConnected) {
            auto* backBtn = createSolidButton(
                76.f, 22.f, ccColor3B{ 46, 50, 62 },
                "Back", 0.48f, ccColor3B{ 235, 240, 250 },
                this, menu_selector(AIDecorPopup::onBackToArtAgent)
            );
            backBtn->setPosition(CCPoint{ center.x + 190.f, center.y + 120.f });
            m_buttonMenu->addChild(backBtn);
        }

        auto* cardBg = createSmoothRoundedRect(448.f, 130.f, 14.f, toColor4F(ccColor3B{ 22, 16, 20 }, 240));
        cardBg->setPosition(CCPoint{ center.x, center.y + 26.f });
        m_mainLayer->addChild(cardBg);

        createCheckmarkBadge(CCPoint{ center.x - 198.f, center.y + 72.f }, g_apiVerifiedAndConnected);

        std::string initStatus = g_apiVerifiedAndConnected
            ? fmt::format("Connected! Key: {} | Model: {}", maskApiKeyPreview(g_connectedApiKey), g_customModelName)
            : "Click any field below (API Key / Base URL / Endpoint / Model) to edit or Ctrl+V:";
        m_apiGateStatusLabel = CCLabelBMFont::create(initStatus.c_str(), "chatFont.fnt");
        m_apiGateStatusLabel->setScale(0.45f);
        m_apiGateStatusLabel->setColor(g_apiVerifiedAndConnected ? ccColor3B{ 110, 245, 155 } : ccColor3B{ 245, 205, 135 });
        m_apiGateStatusLabel->setPosition(CCPoint{ center.x + 8.f, center.y + 72.f });
        m_mainLayer->addChild(m_apiGateStatusLabel);

        // 4 кликабельных поля: [0: API Key] [1: Base URL] / [2: Endpoint] [3: Model]
        auto createFieldBox = [&](int idx, CCPoint pos, SEL_MenuHandler cb) {
            m_apiFieldBg[idx] = createSolidPanel(208.f, 26.f, ccColor3B{ 36, 26, 32 }, 255);
            auto* border = createSolidPanel(210.f, 28.f, ccColor3B{ 88, 52, 64 }, 255);
            border->setPosition(CCPoint{ 104.f, 13.f });
            border->setZOrder(-1);
            m_apiFieldBg[idx]->addChild(border);

            m_apiFieldLbl[idx] = CCLabelBMFont::create("", "chatFont.fnt");
            m_apiFieldLbl[idx]->setScale(0.42f);
            m_apiFieldLbl[idx]->setPosition(CCPoint{ 104.f, 13.f });
            m_apiFieldBg[idx]->addChild(m_apiFieldLbl[idx]);

            auto* btn = CCMenuItemSpriteExtra::create(m_apiFieldBg[idx], this, cb);
            btn->setPosition(pos);
            m_buttonMenu->addChild(btn);
        };

        createFieldBox(0, CCPoint{ center.x - 108.f, center.y + 38.f }, menu_selector(AIDecorPopup::onSelectApiField0));
        createFieldBox(1, CCPoint{ center.x + 108.f, center.y + 38.f }, menu_selector(AIDecorPopup::onSelectApiField1));
        createFieldBox(2, CCPoint{ center.x - 108.f, center.y + 6.f },  menu_selector(AIDecorPopup::onSelectApiField2));
        createFieldBox(3, CCPoint{ center.x + 108.f, center.y + 6.f },  menu_selector(AIDecorPopup::onSelectApiField3));

        auto* presetListBtn = createSolidButton(
            180.f, 18.f, ccColor3B{ 44, 32, 42 },
            "Choose Model from List...", 0.38f, ccColor3B{ 175, 230, 255 },
            this, menu_selector(AIDecorPopup::onOpenModelSelectorFromGate)
        );
        presetListBtn->setPosition(CCPoint{ center.x, center.y - 22.f });
        m_buttonMenu->addChild(presetListBtn);

        // Округленная строка ввода для выбранного поля (API Key / Base URL / Endpoint / Model)
        auto* inputGlow = createSmoothRoundedRect(448.f, 38.f, 19.f, toColor4F(ccColor3B{ 95, 165, 225 }, 140));
        inputGlow->setPosition(CCPoint{ center.x, center.y - 58.f });
        m_mainLayer->addChild(inputGlow);

        auto* inputBg = createSmoothRoundedRect(444.f, 34.f, 17.f, toColor4F(ccColor3B{ 14, 12, 16 }, 255));
        inputBg->setPosition(CCPoint{ center.x, center.y - 58.f });
        m_mainLayer->addChild(inputBg);

        m_inputDisplayLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_inputDisplayLabel->setScale(0.50f);
        m_inputDisplayLabel->setPosition(CCPoint{ center.x, center.y - 58.f });
        m_mainLayer->addChild(m_inputDisplayLabel);

        m_inputBuffer = getApiFieldValue(m_activeApiField);
        refreshInputLabel();
        refreshApiGateFieldsUI();

        auto* pasteBtn = createSolidButton(
            130.f, 30.f, ccColor3B{ 42, 34, 40 },
            "Paste (Ctrl+V)", 0.50f, ccColor3B{ 240, 245, 255 },
            this, menu_selector(AIDecorPopup::onPasteApiClicked)
        );
        pasteBtn->setPosition(CCPoint{ center.x - 155.f, center.y - 104.f });
        m_buttonMenu->addChild(pasteBtn);

        auto* clearBtn = createSolidButton(
            90.f, 30.f, ccColor3B{ 42, 34, 40 },
            "Clear", 0.50f, ccColor3B{ 240, 245, 255 },
            this, menu_selector(AIDecorPopup::onClearApiClicked)
        );
        clearBtn->setPosition(CCPoint{ center.x - 35.f, center.y - 104.f });
        m_buttonMenu->addChild(clearBtn);

        auto* verifyBtn = createSolidButton(
            195.f, 30.f, ccColor3B{ 212, 46, 78 },
            "Save & Connect API", 0.52f, ccColor3B{ 255, 255, 255 },
            this, menu_selector(AIDecorPopup::onVerifyAndConnectApi)
        );
        verifyBtn->setPosition(CCPoint{ center.x + 122.f, center.y - 104.f });
        m_buttonMenu->addChild(verifyBtn);
    }

    // ========================================================================
    // ЭКРАН 2: ВЫБОР МОДЕЛИ (или быстрых шаблонов Base URL + Endpoint)
    // ========================================================================
    void setupModelSelectorWindowUI(CCPoint center) {
        this->setTitle("VIBOR MODELI II (ILI VVEDITE SVOYU V POLE MODEL)");

        auto* backBtn = createSolidButton(
            110.f, 24.f, ccColor3B{ 46, 52, 64 },
            "Nazad / Done", 0.48f, ccColor3B{ 245, 248, 255 },
            this, menu_selector(AIDecorPopup::onCloseModelSelectorWindow)
        );
        backBtn->setPosition(CCPoint{ center.x + 155.f, center.y + 88.f });
        m_buttonMenu->addChild(backBtn);

        auto* subInfo = CCLabelBMFont::create(
            "Nazhmite na model nizhe ili vvedite lyubuyu svoyu model v okne API:",
            "chatFont.fnt"
        );
        subInfo->setScale(0.44f);
        subInfo->setColor(ccColor3B{ 195, 185, 190 });
        subInfo->setPosition(CCPoint{ center.x - 45.f, center.y + 88.f });
        m_mainLayer->addChild(subInfo);

        struct PresetModelOption {
            const char* modelId;
            const char* title;
            const char* desc;
        };
        static constexpr PresetModelOption kPresets[4] = {
            { "deepseek-chat",     "DeepSeek Chat / V4",  "Universal fast model for custom JSON & building" },
            { "deepseek-v4-pro",   "DeepSeek V4 Pro",     "High-precision 1M context coding & art model" },
            { "gemini-4-hydrogen", "Gemini 4 Hydrogen",   "Fast multimodal vision & building model" },
            { "gpt-4o-mini",       "GPT-4o Mini / GPT-6", "Compatible with OpenAI & custom proxy servers" }
        };

        SEL_MenuHandler const modelCallbacks[4] = {
            menu_selector(AIDecorPopup::onPickModel0),
            menu_selector(AIDecorPopup::onPickModel1),
            menu_selector(AIDecorPopup::onPickModel2),
            menu_selector(AIDecorPopup::onPickModel3)
        };

        for (int i = 0; i < 4; ++i) {
            float rowY = center.y + 38.f - i * 44.f;
            bool isSelected = (g_customModelName == kPresets[i].modelId);

            ccColor3B cardColor = isSelected ? ccColor3B{ 34, 72, 52 } : ccColor3B{ 26, 20, 24 };
            auto* rowBg = createSolidPanel(430.f, 36.f, cardColor, 255);
            auto* rowBorder = createSolidPanel(
                432.f, 38.f,
                isSelected ? ccColor3B{ 46, 215, 105 } : ccColor3B{ 72, 44, 52 },
                255
            );
            rowBorder->setPosition(CCPoint{ 215.f, 18.f });
            rowBorder->setZOrder(-1);
            rowBg->addChild(rowBorder);

            std::string titleLine = fmt::format(
                "{}{}  [{}]",
                isSelected ? "[ACTIVE] " : "",
                kPresets[i].title,
                kPresets[i].modelId
            );
            auto* titleLbl = CCLabelBMFont::create(titleLine.c_str(), "chatFont.fnt");
            titleLbl->setScale(0.48f);
            titleLbl->setColor(isSelected ? ccColor3B{ 125, 255, 165 } : ccColor3B{ 240, 245, 255 });
            titleLbl->setPosition(CCPoint{ 215.f, 24.f });
            rowBg->addChild(titleLbl);

            auto* descLbl = CCLabelBMFont::create(kPresets[i].desc, "chatFont.fnt");
            descLbl->setScale(0.40f);
            descLbl->setColor(ccColor3B{ 185, 180, 190 });
            descLbl->setPosition(CCPoint{ 215.f, 10.f });
            rowBg->addChild(descLbl);

            auto* itemBtn = CCMenuItemSpriteExtra::create(rowBg, this, modelCallbacks[i]);
            itemBtn->setPosition(CCPoint{ center.x, rowY });
            m_buttonMenu->addChild(itemBtn);
        }
    }

    // ========================================================================
    // ЭКРАН 3: ГЛАВНЫЙ ИНТЕРФЕЙС "ART AGENT" С ПРОКРУЧИВАЕМЫМ ИИ-ЧАТОМ
    // ========================================================================
    void setupArtAgentMainUI(CCPoint center) {
        this->setTitle("");

        // --- ВЕРХНЯЯ ПАНЕЛЬ ---
        auto* headerTitle = CCLabelBMFont::create("Art Agent", "chatFont.fnt");
        headerTitle->setScale(0.58f);
        headerTitle->setColor(ccColor3B{ 245, 248, 255 });
        headerTitle->setPosition(CCPoint{ center.x + 15.f, center.y + 126.f });
        m_mainLayer->addChild(headerTitle);

        auto* balPill = createSolidPanel(106.f, 22.f, ccColor3B{ 28, 18, 22 }, 220);
        balPill->setPosition(CCPoint{ center.x - 182.f, center.y + 126.f });
        m_mainLayer->addChild(balPill);

        createCheckmarkBadge(CCPoint{ center.x - 223.f, center.y + 126.f }, true);

        std::string apiTag = "API Config";
        auto* apiTagLbl = CCLabelBMFont::create(apiTag.c_str(), "chatFont.fnt");
        apiTagLbl->setScale(0.44f);
        apiTagLbl->setColor(ccColor3B{ 230, 235, 245 });
        apiTagLbl->setPosition(CCPoint{ center.x - 184.f, center.y + 126.f });
        m_mainLayer->addChild(apiTagLbl);

        auto* changeApiBtn = createSolidButton(
            24.f, 20.f, ccColor3B{ 222, 52, 86 },
            "+", 0.55f, ccColor3B{ 255, 255, 255 },
            this, menu_selector(AIDecorPopup::onOpenApiGate)
        );
        changeApiBtn->setPosition(CCPoint{ center.x - 140.f, center.y + 126.f });
        m_buttonMenu->addChild(changeApiBtn);

        std::string shortMod = fmt::format("Model: {}", getCurrentModelID(m_providerIdx));
        if (shortMod.size() > 22) shortMod = shortMod.substr(0, 22);
        auto* modelBtn = createSolidButton(
            114.f, 20.f, ccColor3B{ 38, 26, 32 },
            shortMod.c_str(), 0.38f, ccColor3B{ 165, 230, 255 },
            this, menu_selector(AIDecorPopup::onOpenModelSelectorFromMain)
        );
        modelBtn->setPosition(CCPoint{ center.x - 65.f, center.y + 126.f });
        m_buttonMenu->addChild(modelBtn);

        auto* langBg = createSolidPanel(44.f, 20.f, ccColor3B{ 38, 26, 32 }, 255);
        m_langBadgeLabel = CCLabelBMFont::create("RUS", "chatFont.fnt");
        m_langBadgeLabel->setScale(0.45f);
        m_langBadgeLabel->setColor(ccColor3B{ 240, 245, 255 });
        m_langBadgeLabel->setPosition(CCPoint{ 22.f, 10.f });
        langBg->addChild(m_langBadgeLabel);
        auto* langBtn = CCMenuItemSpriteExtra::create(langBg, this, menu_selector(AIDecorPopup::onToggleLanguage));
        langBtn->setPosition(CCPoint{ center.x + 186.f, center.y + 126.f });
        m_buttonMenu->addChild(langBtn);

        // Разделители поверх градиента
        auto* topDivider = createSolidPanel(482.f, 1.2f, ccColor3B{ 88, 32, 40 }, 220);
        topDivider->setPosition(CCPoint{ center.x, center.y + 110.f });
        m_mainLayer->addChild(topDivider);

        auto* midDivider = createSolidPanel(1.2f, 246.f, ccColor3B{ 88, 32, 40 }, 220);
        midDivider->setPosition(CCPoint{ center.x - 4.f, center.y - 14.f });
        m_mainLayer->addChild(midDivider);

        // ====================================================================
        // ЛЕВАЯ КОЛОНКА: Прокручиваемый ИИ-чат + Компактный статус + Округленная строка ввода
        // ====================================================================
        float leftX = center.x - 125.f;

        // Сглаженная округленная подложка ленты прокручиваемого чата
        auto* chatBoxBorder = createSmoothRoundedRect(206.f, 160.f, 13.f, toColor4F(ccColor3B{ 68, 26, 34 }, 255));
        chatBoxBorder->setPosition(CCPoint{ leftX - 10.f, center.y + 24.f });
        m_mainLayer->addChild(chatBoxBorder);

        auto* chatBoxBg = createSmoothRoundedRect(204.f, 158.f, 12.f, toColor4F(ccColor3B{ 16, 11, 15 }, 255));
        chatBoxBg->setPosition(CCPoint{ leftX - 10.f, center.y + 24.f });
        m_mainLayer->addChild(chatBoxBg);

        // Настоящий прокручиваемый geode::ScrollLayer (поддерживает колесико мыши и перетаскивание!)
        m_chatScrollLayer = geode::ScrollLayer::create(CCSize{ 196.f, 150.f }, true, true);
        m_chatScrollLayer->ignoreAnchorPointForPosition(false);
        m_chatScrollLayer->setAnchorPoint(CCPoint{ 0.5f, 0.5f });
        m_chatScrollLayer->setPosition(CCPoint{ leftX - 10.f, center.y + 24.f });
        m_mainLayer->addChild(m_chatScrollLayer);

        // Прямоугольные кнопки прокрутки вверх [^] и вниз [v] справа от чата
        auto* scrollUpBtn = createSolidButton(
            18.f, 22.f, ccColor3B{ 44, 28, 34 },
            "^", 0.52f, ccColor3B{ 245, 248, 255 },
            this, menu_selector(AIDecorPopup::onScrollChatUp)
        );
        scrollUpBtn->setPosition(CCPoint{ leftX + 102.f, center.y + 84.f });
        m_buttonMenu->addChild(scrollUpBtn);

        auto* scrollDownBtn = createSolidButton(
            18.f, 22.f, ccColor3B{ 44, 28, 34 },
            "v", 0.48f, ccColor3B{ 245, 248, 255 },
            this, menu_selector(AIDecorPopup::onScrollChatDown)
        );
        scrollDownBtn->setPosition(CCPoint{ leftX + 102.f, center.y - 36.f });
        m_buttonMenu->addChild(scrollDownBtn);

        // Компактная полоска статуса шагов под чатом (Plan | Drawing | Place)
        auto* statusBarBg = createSolidPanel(224.f, 20.f, ccColor3B{ 22, 14, 18 }, 220);
        statusBarBg->setPosition(CCPoint{ leftX, center.y - 66.f });
        m_mainLayer->addChild(statusBarBg);

        m_stepPlanLabel = CCLabelBMFont::create("Plan: ready", "chatFont.fnt");
        m_stepPlanLabel->setScale(0.36f);
        m_stepPlanLabel->setColor(ccColor3B{ 225, 195, 205 });
        m_stepPlanLabel->setPosition(CCPoint{ leftX - 72.f, center.y - 66.f });
        m_mainLayer->addChild(m_stepPlanLabel);

        m_stepDrawingLabel = CCLabelBMFont::create("Draw: wait", "chatFont.fnt");
        m_stepDrawingLabel->setScale(0.36f);
        m_stepDrawingLabel->setColor(ccColor3B{ 195, 185, 195 });
        m_stepDrawingLabel->setPosition(CCPoint{ leftX - 2.f, center.y - 66.f });
        m_mainLayer->addChild(m_stepDrawingLabel);

        m_stepPlaceLabel = CCLabelBMFont::create("Place: wait", "chatFont.fnt");
        m_stepPlaceLabel->setScale(0.36f);
        m_stepPlaceLabel->setColor(ccColor3B{ 195, 185, 195 });
        m_stepPlaceLabel->setPosition(CCPoint{ leftX + 72.f, center.y - 66.f });
        m_mainLayer->addChild(m_stepPlaceLabel);

        // Быстрые прямоугольные кнопки-примеры (For example)
        auto addExampleChip = [&](const char* label, CCPoint pos, SEL_MenuHandler cb) {
            auto* btn = createSolidButton(
                52.f, 15.f, ccColor3B{ 36, 24, 30 },
                label, 0.34f, ccColor3B{ 230, 225, 235 },
                this, cb
            );
            btn->setPosition(pos);
            m_buttonMenu->addChild(btn);
        };

        addExampleChip("+ Ref Image", CCPoint{ leftX - 84.f, center.y - 88.f }, menu_selector(AIDecorPopup::onAttachReferenceImage));
        addExampleChip("Clear Ref",   CCPoint{ leftX - 28.f, center.y - 88.f }, menu_selector(AIDecorPopup::onClearReferenceImage));
        addExampleChip("New Style",   CCPoint{ leftX + 28.f, center.y - 88.f }, menu_selector(AIDecorPopup::onPresetUniqueNewStyle));
        addExampleChip("Build Ref",   CCPoint{ leftX + 84.f, center.y - 88.f }, menu_selector(AIDecorPopup::onPresetBuildFromRef));

        // По-настоящему сглаженная округленная строка ввода сообщения ИИ (pill) со свечением
        auto* inputSoftHalo = createSmoothRoundedRect(202.f, 34.f, 17.f, toColor4F(ccColor3B{ 105, 195, 255 }, 95));
        inputSoftHalo->setPosition(CCPoint{ leftX - 15.f, center.y - 116.f });
        m_mainLayer->addChild(inputSoftHalo);

        auto* inputGlowOuter = createSmoothRoundedRect(196.f, 30.f, 15.f, toColor4F(ccColor3B{ 145, 220, 255 }, 235));
        inputGlowOuter->setPosition(CCPoint{ leftX - 15.f, center.y - 116.f });
        m_mainLayer->addChild(inputGlowOuter);

        auto* inputPillBg = createSmoothRoundedRect(192.f, 26.f, 13.f, toColor4F(ccColor3B{ 18, 15, 20 }, 255));
        inputPillBg->setPosition(CCPoint{ leftX - 15.f, center.y - 116.f });
        m_mainLayer->addChild(inputPillBg);

        m_inputDisplayLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_inputDisplayLabel->setScale(0.44f);
        m_inputDisplayLabel->setPosition(CCPoint{ leftX - 15.f, center.y - 116.f });
        m_mainLayer->addChild(m_inputDisplayLabel);
        refreshInputLabel();

        // Прямоугольная кнопка отправки [>]
        auto* sendBtn = createSolidButton(
            26.f, 24.f, ccColor3B{ 215, 48, 80 },
            ">", 0.52f, ccColor3B{ 255, 255, 255 },
            this, menu_selector(AIDecorPopup::onSendToNeuralNet)
        );
        sendBtn->setPosition(CCPoint{ leftX + 98.f, center.y - 116.f });
        m_buttonMenu->addChild(sendBtn);

        // Заполняем ленту чата историей сообщений
        rebuildChatScrollFeed();

        // ====================================================================
        // ПРАВАЯ КОЛОНКА: Вкладки [Shape | Drawing | In GD] + Синее превью + Limit + Keep
        // ====================================================================
        float rightX = center.x + 120.f;

        auto* tabBarBg = createSolidPanel(224.f, 22.f, ccColor3B{ 22, 16, 20 }, 240);
        tabBarBg->setPosition(CCPoint{ rightX, center.y + 92.f });
        m_mainLayer->addChild(tabBarBg);

        auto createTabButton = [&](const char* title, CCPoint pos, SEL_MenuHandler cb, CCLayerColor*& outBg) {
            outBg = createSolidPanel(72.f, 18.f, ccColor3B{ 28, 20, 24 }, 255);
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

        // Кнопка во вкладке Shape: переключает показ "Только каркас" <-> "100% Заполнение структуры"
        auto* fillBtnBg = createSolidPanel(78.f, 15.f, ccColor3B{ 26, 38, 68 }, 240);
        auto* fillBtnBorder = createSolidPanel(80.f, 17.f, ccColor3B{ 95, 195, 255 }, 255);
        fillBtnBorder->setPosition(CCPoint{ 39.f, 7.5f });
        fillBtnBorder->setZOrder(-1);
        fillBtnBg->addChild(fillBtnBorder);

        m_shapeFillToggleLbl = CCLabelBMFont::create(
            g_showFilledInShapeTab ? "Mode: Filled" : "Show Fill",
            "chatFont.fnt"
        );
        m_shapeFillToggleLbl->setScale(0.36f);
        m_shapeFillToggleLbl->setColor(ccColor3B{ 235, 248, 255 });
        m_shapeFillToggleLbl->setPosition(CCPoint{ 39.f, 7.5f });
        fillBtnBg->addChild(m_shapeFillToggleLbl);

        m_shapeFillToggleBtn = CCMenuItemSpriteExtra::create(
            fillBtnBg, this, menu_selector(AIDecorPopup::onToggleShapeFillPreview)
        );
        m_shapeFillToggleBtn->setPosition(CCPoint{ rightX + 68.f, center.y + 64.f });
        m_buttonMenu->addChild(m_shapeFillToggleBtn);

        std::string dimText = m_contour.hasAnySelection
            ? fmt::format("{} x {} units  |  {} blocks, {} spikes, {} orbs", m_contour.widthUnits(), m_contour.heightUnits(), m_contour.cellCount(), m_contour.spikeCount(), m_contour.orbPortalCount())
            : std::string("0 x 0 units  |  0 blocks (No selection - BG / Free Mode)");
        m_previewSubInfoLabel = CCLabelBMFont::create(dimText.c_str(), "chatFont.fnt");
        m_previewSubInfoLabel->setScale(0.44f);
        m_previewSubInfoLabel->setColor(ccColor3B{ 195, 190, 200 });
        m_previewSubInfoLabel->setPosition(CCPoint{ rightX, center.y - 62.f });
        m_mainLayer->addChild(m_previewSubInfoLabel);

        auto* modePill = createSolidPanel(224.f, 22.f, ccColor3B{ 24, 16, 20 }, 255);
        modePill->setPosition(CCPoint{ rightX, center.y - 84.f });
        m_mainLayer->addChild(modePill);

        auto createModeHalf = [&](const char* txt, CCPoint pos, SEL_MenuHandler cb, CCLayerColor*& outBg) {
            outBg = createSolidPanel(108.f, 18.f, ccColor3B{ 28, 20, 24 }, 255);
            auto* l = CCLabelBMFont::create(txt, "chatFont.fnt");
            l->setScale(0.42f);
            l->setColor(ccColor3B{ 245, 248, 255 });
            l->setPosition(CCPoint{ 54.f, 9.f });
            outBg->addChild(l);
            auto* b = CCMenuItemSpriteExtra::create(outBg, this, cb);
            b->setPosition(pos);
            m_buttonMenu->addChild(b);
        };
        createModeHalf("Inside",   CCPoint{ rightX - 55.f, center.y - 84.f }, menu_selector(AIDecorPopup::onSelectInside),   m_insideTabBg);
        createModeHalf("Overhang", CCPoint{ rightX + 55.f, center.y - 84.f }, menu_selector(AIDecorPopup::onSelectOverhang), m_overhangTabBg);

        std::string varStr = fmt::format("Another variant ({})", g_variantIndex);
        auto* varBtn = createSolidButton(
            108.f, 28.f, ccColor3B{ 34, 22, 28 },
            varStr.c_str(), 0.42f, ccColor3B{ 225, 220, 232 },
            this, menu_selector(AIDecorPopup::onAnotherVariantClicked)
        );
        varBtn->setPosition(CCPoint{ rightX - 58.f, center.y - 116.f });
        m_buttonMenu->addChild(varBtn);

        auto* keepBtn = createSolidButton(
            108.f, 28.f, ccColor3B{ 178, 38, 68 },
            "Keep", 0.50f, ccColor3B{ 255, 245, 248 },
            this, menu_selector(AIDecorPopup::onKeepClicked)
        );
        keepBtn->setPosition(CCPoint{ rightX + 58.f, center.y - 116.f });
        m_buttonMenu->addChild(keepBtn);

        refreshRightPanelTabsAndCanvas();
    }

    // ========================================================================
    // Перестроение прокручиваемой ленты ИИ-чата со всеми старыми сообщениями
    // ========================================================================
    void rebuildChatScrollFeed() {
        if (!m_chatScrollLayer || !m_chatScrollLayer->m_contentLayer) return;

        auto* content = m_chatScrollLayer->m_contentLayer;
        content->removeAllChildrenWithCleanup(true);

        struct ChatBubbleBlock {
            bool isUser = false;
            std::vector<std::string> lines;
        };
        std::vector<ChatBubbleBlock> blocks;

        // Приветственное сообщение в самом начале истории
        {
            ChatBubbleBlock intro;
            intro.isUser = false;
            intro.lines = wrapTextLines(
                fmt::format(
                    "Agent: {} blocks ({}x{}). Ref: {}. I build 100% custom layered art (no 3D presets) & self-verify match!",
                    m_contour.cellCount(), m_contour.widthUnits(), m_contour.heightUnits(),
                    g_hasReferenceImage ? g_referenceFileName : "none (click '+ Ref Image')"
                ),
                36, 4
            );
            blocks.push_back(std::move(intro));
        }

        // Все старые и новые сообщения из истории диалога g_chatHistory
        for (auto const& msg : g_chatHistory) {
            ChatBubbleBlock b;
            b.isUser = (msg.role == "user");
            std::string prefix = b.isUser ? "You: " : "Agent: ";
            b.lines = wrapTextLines(prefix + msg.content, 36, 6);
            blocks.push_back(std::move(b));
        }

        // Считаем суммарную высоту всех пузырей сообщений
        float totalH = 8.f;
        std::vector<float> blockHeights;
        for (auto const& b : blocks) {
            int nonEmpty = 0;
            for (auto const& ln : b.lines) {
                if (!ln.empty()) nonEmpty++;
            }
            nonEmpty = (std::max)(1, nonEmpty);
            float h = 10.f + nonEmpty * 12.f;
            blockHeights.push_back(h);
            totalH += h + 6.f;
        }

        float viewH = m_chatScrollLayer->getContentSize().height;
        float finalContentH = (std::max)(viewH, totalH);
        content->setContentSize(CCSize{ 196.f, finalContentH });

        float curY = finalContentH - 6.f;
        for (size_t i = 0; i < blocks.size(); ++i) {
            auto const& b = blocks[i];
            float h = blockHeights[i];
            float centerY = curY - h * 0.5f;

            ccColor3B cardCol = b.isUser ? ccColor3B{ 54, 24, 34 } : ccColor3B{ 26, 22, 28 };
            auto* bubble = createSmoothRoundedRect(188.f, h, 8.f, toColor4F(cardCol, 255));
            bubble->setPosition(CCPoint{ 98.f, centerY });
            content->addChild(bubble);

            int drawnLines = 0;
            float topTextY = curY - 10.f;
            for (auto const& ln : b.lines) {
                if (ln.empty()) continue;
                auto* lbl = CCLabelBMFont::create(ln.c_str(), "chatFont.fnt");
                lbl->setScale(0.39f);
                lbl->setColor(b.isUser ? ccColor3B{ 255, 225, 232 } : ccColor3B{ 230, 235, 245 });
                lbl->setPosition(CCPoint{ 98.f, topTextY - drawnLines * 12.f });
                content->addChild(lbl);
                drawnLines++;
            }

            curY -= (h + 6.f);
        }

        // Прокручиваем ленту к самому новому сообщению внизу
        content->setPosition(CCPoint{ 0.f, 0.f });
    }

    void onScrollChatUp(CCObject*) {
        if (!m_chatScrollLayer) return;
        m_chatScrollLayer->scrollWheel(-42.f, 0.f);
    }

    void onScrollChatDown(CCObject*) {
        if (!m_chatScrollLayer) return;
        m_chatScrollLayer->scrollWheel(42.f, 0.f);
    }

    void onToggleShapeFillPreview(CCObject*) {
        g_showFilledInShapeTab = !g_showFilledInShapeTab;
        if (m_shapeFillToggleLbl) {
            m_shapeFillToggleLbl->setString(g_showFilledInShapeTab ? "Mode: Filled" : "Show Fill");
        }
        refreshRightPanelTabsAndCanvas();
    }

    // Создает идеальный попиксельный CCSprite для вкладки Drawing, обрезанный строго по силуэту структуры!
    CCSprite* createPixelArtDrawingPreviewSprite(float targetW, float targetH) {
        int cols = (std::max)(1, m_contour.maxGX - m_contour.minGX + 1);
        int rows = (std::max)(1, m_contour.maxGY - m_contour.minGY + 1);
        int SubPx = 16; // 16x16 пикселей на каждую клетку блока для фотореалистичной четкости в Drawing!
        int texW = std::clamp(cols * SubPx, 32, 512);
        int texH = std::clamp(rows * SubPx, 32, 512);

        std::vector<uint8_t> rgba(static_cast<size_t>(texW * texH * 4), 0);

        float worldMinX = m_contour.minGX * 30.f;
        float worldMinY = m_contour.minGY * 30.f;
        float worldSpanW = cols * 30.f;
        float worldSpanH = rows * 30.f;

        AIDecisionResult previewAI;
        previewAI.fillRGB    = g_hasGeneratedArt ? g_lastPreviewPalette.slotColors[static_cast<int>(ColorSlot::DarkFill)] : ccColor3B{ 34, 38, 48 };
        previewAI.midRGB     = g_hasGeneratedArt ? g_lastPreviewPalette.slotColors[static_cast<int>(ColorSlot::MidTone)]  : ccColor3B{ 78, 88, 108 };
        previewAI.mainRGB    = g_hasGeneratedArt ? g_lastPreviewPalette.slotColors[static_cast<int>(ColorSlot::BrightMain)] : ccColor3B{ 155, 175, 205 };
        previewAI.outlineRGB = g_hasGeneratedArt ? g_lastPreviewPalette.slotColors[static_cast<int>(ColorSlot::HighlightOutline)] : ccColor3B{ 230, 240, 255 };
        int styleSeed = static_cast<int>(previewAI.fillRGB.r * 73 + previewAI.mainRGB.g * 151 + g_variantIndex * 997);

        // 1. Попиксельно рендерим PBR-материал (96x96 карта референса + свет + тени AO + швы + зернистость)
        for (int py = 0; py < texH; ++py) {
            float v = 1.0f - (py + 0.5f) / static_cast<float>(texH); // 0 = низ, 1 = верх
            int gy = m_contour.minGY + std::clamp(static_cast<int>(v * rows), 0, rows - 1);
            float localV = std::fmod(v * rows, 1.0f);

            for (int px = 0; px < texW; ++px) {
                float u = (px + 0.5f) / static_cast<float>(texW);
                int gx = m_contour.minGX + std::clamp(static_cast<int>(u * cols), 0, cols - 1);

                float wx = worldMinX + u * worldSpanW;
                float wy = worldMinY + v * worldSpanH;

                bool inBlockCell = (!m_contour.hasAnySelection) || (m_contour.occupiedCells.count({ gx, gy }) > 0);
                bool inSpikeTriangle = false;
                if (!inBlockCell) {
                    for (auto const& sp : m_contour.spikes) {
                        float dx = (wx - sp.x) / (15.0f * (std::max)(0.2f, sp.scaleX));
                        float dy = (wy - sp.y + 15.0f) / (30.0f * (std::max)(0.2f, sp.scaleY));
                        if (dy >= 0.0f && dy <= 1.0f && std::fabs(dx) <= (1.0f - dy)) {
                            inSpikeTriangle = true;
                            break;
                        }
                    }
                }
                // Рисуем строго внутри силуэта блоков ИЛИ треугольного силуэта выделенных шипов!
                if (!inBlockCell && !inSpikeTriangle) continue;

                float localU = std::fmod(u * cols, 1.0f);
                bool topOpen    = (m_contour.occupiedCells.count({ gx, gy + 1 }) == 0);
                bool bottomOpen = (m_contour.occupiedCells.count({ gx, gy - 1 }) == 0);
                bool leftOpen   = (m_contour.occupiedCells.count({ gx - 1, gy }) == 0);
                bool rightOpen  = (m_contour.occupiedCells.count({ gx + 1, gy }) == 0);

                ccColor3B pixCol = computeRealisticTexelColor(
                    wx, wy, worldMinX, worldMinY, worldSpanW, worldSpanH,
                    topOpen, bottomOpen, leftOpen, rightOpen,
                    localU, localV, previewAI, styleSeed
                );

                size_t idx = static_cast<size_t>((py * texW + px) * 4);
                rgba[idx + 0] = pixCol.r;
                rgba[idx + 1] = pixCol.g;
                rgba[idx + 2] = pixCol.b;
                rgba[idx + 3] = 255;
            }
        }

        // 2. Поверх накладываем все микро-пиксельные детали, сгенерированные ИИ
        for (auto const& s : g_lastPreviewSpecs) {
            if (s.layerRole == LayerRole::Fill) continue;
            float u = (s.x - worldMinX) / worldSpanW;
            float v = (s.y - worldMinY) / worldSpanH;
            int centerPx = static_cast<int>(u * texW);
            int centerPy = static_cast<int>((1.0f - v) * texH);
            int halfW = (std::max)(1, static_cast<int>(s.scaleX * (texW / static_cast<float>(cols)) * 0.5f));
            int halfH = (std::max)(1, static_cast<int>(s.scaleY * (texH / static_cast<float>(rows)) * 0.5f));

            ccColor3B col = (s.colorSlot == ColorSlot::CustomRGB)
                ? s.customColor
                : g_lastPreviewPalette.slotColors[std::clamp(static_cast<int>(s.colorSlot), 0, 5)];
            float alpha = (s.customOpacity / 255.0f) * 0.85f;

            for (int dy = -halfH; dy <= halfH; ++dy) {
                int py = centerPy + dy;
                if (py < 0 || py >= texH) continue;
                for (int dx = -halfW; dx <= halfW; ++dx) {
                    int px = centerPx + dx;
                    if (px < 0 || px >= texW) continue;
                    size_t idx = static_cast<size_t>((py * texW + px) * 4);
                    if (rgba[idx + 3] == 0 && !s.isOverhang) continue;
                    rgba[idx + 3] = 255;
                    rgba[idx + 0] = static_cast<uint8_t>(std::clamp(static_cast<int>(rgba[idx + 0] * (1.f - alpha) + col.r * alpha), 0, 255));
                    rgba[idx + 1] = static_cast<uint8_t>(std::clamp(static_cast<int>(rgba[idx + 1] * (1.f - alpha) + col.g * alpha), 0, 255));
                    rgba[idx + 2] = static_cast<uint8_t>(std::clamp(static_cast<int>(rgba[idx + 2] * (1.f - alpha) + col.b * alpha), 0, 255));
                }
            }
        }

        auto* tex = new CCTexture2D();
        if (tex && tex->initWithData(
                rgba.data(),
                kCCTexture2DPixelFormat_RGBA8888,
                texW,
                texH,
                CCSize{ static_cast<float>(texW), static_cast<float>(texH) }
            )) {
            tex->autorelease();
            auto* spr = CCSprite::createWithTexture(tex);
            if (spr) {
                CCSize ptsSize = spr->getContentSize();
                float curW = (ptsSize.width > 0.01f) ? ptsSize.width : static_cast<float>(texW);
                float curH = (ptsSize.height > 0.01f) ? ptsSize.height : static_cast<float>(texH);
                spr->setScaleX(targetW / curW);
                spr->setScaleY(targetH / curH);
                return spr;
            }
        } else {
            CC_SAFE_DELETE(tex);
        }
        return nullptr;
    }

    void refreshRightPanelTabsAndCanvas() {
        if (m_tabShapeBg)   m_tabShapeBg->setColor((m_previewTab == 0) ? ccColor3B{ 225, 52, 86 } : ccColor3B{ 28, 20, 24 });
        if (m_tabDrawingBg) m_tabDrawingBg->setColor((m_previewTab == 1) ? ccColor3B{ 225, 52, 86 } : ccColor3B{ 28, 20, 24 });
        if (m_tabInGDBg)    m_tabInGDBg->setColor((m_previewTab == 2) ? ccColor3B{ 225, 52, 86 } : ccColor3B{ 28, 20, 24 });

        if (m_insideTabBg)   m_insideTabBg->setColor(!g_overhangAllowed ? ccColor3B{ 225, 52, 86 } : ccColor3B{ 28, 20, 24 });
        if (m_overhangTabBg) m_overhangTabBg->setColor(g_overhangAllowed  ? ccColor3B{ 225, 52, 86 } : ccColor3B{ 28, 20, 24 });

        // Кнопка "Show Fill / Mode: Filled" видна во вкладке Shape
        if (m_shapeFillToggleBtn) {
            m_shapeFillToggleBtn->setVisible(m_previewTab == 0);
        }
        if (m_shapeFillToggleLbl) {
            m_shapeFillToggleLbl->setString(g_showFilledInShapeTab ? "Mode: Filled" : "Show Fill");
        }

        if (!m_previewCanvasContent) return;

        CCPoint canvasPos = m_previewCanvasContent->getPosition();
        m_previewCanvasContent->setScale(0.f);
        m_previewCanvasContent = CCNode::create();
        m_previewCanvasContent->setPosition(canvasPos);
        m_mainLayer->addChild(m_previewCanvasContent);

        int cols = (std::max)(1, m_contour.maxGX - m_contour.minGX + 1);
        int rows = (std::max)(1, m_contour.maxGY - m_contour.minGY + 1);
        float cellPx = (std::min)(136.f / cols, 88.f / rows);
        cellPx = std::clamp(cellPx, 6.f, 26.f);

        float totalW = cols * cellPx;
        float totalH = rows * cellPx;
        float startX = -totalW * 0.5f + cellPx * 0.5f;
        float startY = -totalH * 0.5f + cellPx * 0.5f;

        if (m_previewTab == 0) {
            // ВКЛАДКА SHAPE:
            // Если ничего не выделено — НЕ рисуем 9 фейковых блоков, а честно показываем 0 выделенных объектов!
            if (!m_contour.hasAnySelection) {
                auto* emptyLbl = CCLabelBMFont::create("0 objects selected (Ask AI for Background!)", "chatFont.fnt");
                emptyLbl->setScale(0.38f);
                emptyLbl->setColor(ccColor3B{ 215, 235, 255 });
                emptyLbl->setPosition(CCPoint{ 0.f, 0.f });
                m_previewCanvasContent->addChild(emptyLbl);
                return;
            }
            // По умолчанию показывает ТОЛЬКО исходный каркас (rawCells),
            // а при нажатии кнопки "Show Fill" показывает полное 100% заполнение (occupiedCells)!
            auto const& cellsToShow = g_showFilledInShapeTab
                ? m_contour.occupiedCells
                : (m_contour.rawCells.empty() ? m_contour.occupiedCells : m_contour.rawCells);

            for (auto const& [gx, gy] : cellsToShow) {
                float px = startX + (gx - m_contour.minGX) * cellPx;
                float py = startY + (gy - m_contour.minGY) * cellPx;
                bool isOriginalFrame = (m_contour.rawCells.count({ gx, gy }) > 0);
                ccColor3B cellCol = (!g_showFilledInShapeTab || isOriginalFrame)
                    ? ccColor3B{ 245, 248, 255 }
                    : ccColor3B{ 155, 215, 255 };
                auto* blockSpr = createSolidPanel(cellPx - 0.6f, cellPx - 0.6f, cellCol, 255);
                blockSpr->setPosition(CCPoint{ px, py });
                m_previewCanvasContent->addChild(blockSpr);
            }
            // Показываем также все выделенные шипы во вкладке Shape (ступенчатым треугольником)
            for (auto const& sp : m_contour.spikes) {
                int sgx = static_cast<int>(std::floor(sp.x / 30.0f));
                int sgy = static_cast<int>(std::floor(sp.y / 30.0f));
                float px = startX + (sgx - m_contour.minGX) * cellPx;
                float py = startY + (sgy - m_contour.minGY) * cellPx;
                for (int t = 0; t < 5; ++t) {
                    float frac = (t + 0.5f) / 5.0f;
                    float w = (1.0f - frac * 0.85f) * (cellPx - 1.f);
                    float h = (cellPx / 5.0f) + 0.4f;
                    auto* triBand = createSolidPanel(w, h, ccColor3B{ 255, 215, 225 }, 255);
                    triBand->setPosition(CCPoint{ px, py - cellPx * 0.5f + frac * cellPx });
                    m_previewCanvasContent->addChild(triBand);
                }
            }
            // Показываем также выделенные орбы, дэш-орбы и порталы во вкладке Shape
            for (auto const& op : m_contour.orbsAndPortals) {
                int ogx = static_cast<int>(std::floor(op.x / 30.0f));
                int ogy = static_cast<int>(std::floor(op.y / 30.0f));
                float px = startX + (ogx - m_contour.minGX) * cellPx;
                float py = startY + (ogy - m_contour.minGY) * cellPx;
                float w = (op.isPortal ? 0.55f : 0.72f) * cellPx;
                float h = (op.isPortal ? 1.65f : 0.72f) * cellPx;
                auto* orbSpr = createSolidPanel(w, h, ccColor3B{ 120, 255, 195 }, 255);
                orbSpr->setPosition(CCPoint{ px, py });
                m_previewCanvasContent->addChild(orbSpr);
            }
        } else if (m_previewTab == 1 && (g_hasGeneratedArt || g_hasReferenceImage)) {
            // ВКЛАДКА DRAWING:
            // Показывает ИДЕАЛЬНЫЙ ПОПИКСЕЛЬНЫЙ РИСУНОК (1024x1024 стиль) внутри силуэта структуры!
            if (auto* artSpr = createPixelArtDrawingPreviewSprite(totalW, totalH)) {
                artSpr->setPosition(CCPoint{ 0.f, 0.f });
                m_previewCanvasContent->addChild(artSpr);
            }
        } else {
            // ВКЛАДКА IN GD:
            // Показывает все сгенерированные объекты редактора и односторонний неон по краям
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
            }

            float worldMinX = m_contour.minGX * 30.f;
            float worldMinY = m_contour.minGY * 30.f;
            float scaleRatio = cellPx / 30.f;

            size_t shownCount = 0;
            for (auto const& s : g_lastPreviewSpecs) {
                if (s.layerRole == LayerRole::Fill) continue;
                if (++shownCount > 500) break;

                float px = -totalW * 0.5f + (s.x - worldMinX) * scaleRatio;
                float py = -totalH * 0.5f + (s.y - worldMinY) * scaleRatio;

                float w = std::clamp(s.scaleX * cellPx, 0.6f, cellPx * 1.4f);
                float h = std::clamp(s.scaleY * cellPx, 0.6f, cellPx * 1.4f);
                ccColor3B col = (s.colorSlot == ColorSlot::CustomRGB)
                    ? s.customColor
                    : g_lastPreviewPalette.slotColors[std::clamp(static_cast<int>(s.colorSlot), 0, 5)];

                auto* detSpr = createSolidPanel(w, h, col, s.customOpacity);
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
                static constexpr const char* kPlaceholders[4] = {
                    "Paste or type ANY API Key (Ctrl+V)...",
                    "Paste or type Base URL (e.g. https://api.openai.com)...",
                    "Paste or type Endpoint (e.g. /v1/chat/completions)...",
                    "Paste or type Model Name (e.g. deepseek-chat, gpt-4o-mini)..."
                };
                m_inputDisplayLabel->setString(kPlaceholders[std::clamp(m_activeApiField, 0, 3)]);
            } else {
                m_inputDisplayLabel->setString("Describe the art or ask AI...");
            }
            m_inputDisplayLabel->setColor(ccColor3B{ 150, 145, 160 });
        } else {
            std::string shown = m_inputBuffer;
            size_t maxLen = (m_stage == PopupStage::ApiGate) ? 54 : 28;
            if (shown.size() > maxLen) {
                shown = "..." + shown.substr(shown.size() - (maxLen - 3));
            }
            shown += "|";
            m_inputDisplayLabel->setString(shown.c_str());
            m_inputDisplayLabel->setColor(ccColor3B{ 245, 248, 255 });
        }
        if (m_stage == PopupStage::ApiGate) {
            setApiFieldValue(m_activeApiField, m_inputBuffer);
            refreshApiGateFieldsUI();
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

    void onOpenModelSelectorFromGate(CCObject*) {
        if (!m_inputBuffer.empty()) {
            setApiFieldValue(m_activeApiField, m_inputBuffer);
        }
        reopenWithStage(PopupStage::ModelSelector, PopupStage::ApiGate);
    }

    void onOpenModelSelectorFromMain(CCObject*) {
        reopenWithStage(PopupStage::ModelSelector, PopupStage::ArtAgentMain);
    }

    void onCloseModelSelectorWindow(CCObject*) {
        reopenWithStage(m_returnStageAfterModel);
    }

    void selectModelSlot(int slotIdx) {
        static constexpr const char* kPresetIds[4] = {
            "deepseek-chat",
            "deepseek-v4-pro",
            "gemini-4-hydrogen",
            "gpt-4o-mini"
        };
        g_customModelName = kPresetIds[std::clamp(slotIdx, 0, 3)];
        if (!g_connectedApiKey.empty()) {
            saveVerifiedApiToDisk(0, g_connectedApiKey);
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
            if (m_inputBuffer.size() > 220) m_inputBuffer.resize(220);
            setApiFieldValue(m_activeApiField, m_inputBuffer);
            refreshInputLabel();
        }
    }

    void onClearApiClicked(CCObject*) {
        m_inputBuffer.clear();
        setApiFieldValue(m_activeApiField, "");
        if (m_activeApiField == 0) {
            setCheckmarkState(false);
        }
        refreshInputLabel();
    }

    void onVerifyAndConnectApi(CCObject*) {
        if (!m_inputBuffer.empty()) {
            setApiFieldValue(m_activeApiField, m_inputBuffer);
        }

        std::string errMsg;
        if (!verifyApiKeyWithServer(0, g_connectedApiKey, errMsg)) {
            setCheckmarkState(false);
            if (m_apiGateStatusLabel) {
                m_apiGateStatusLabel->setString(utf8ToGDReadable(errMsg).c_str());
                m_apiGateStatusLabel->setColor(ccColor3B{ 255, 95, 105 });
            }
            return;
        }

        setCheckmarkState(true);
        saveVerifiedApiToDisk(0, g_connectedApiKey);
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
    void onTabInGD(CCObject*) {
        if (g_hasPendingDrawingApproval && g_hasGeneratedArt) {
            approveAndPlaceCurrentDrawingInGD(false);
            return;
        }
        m_previewTab = 2;
        refreshRightPanelTabsAndCanvas();
    }



    void onSelectInside(CCObject*)   { g_overhangAllowed = false; refreshRightPanelTabsAndCanvas(); }
    void onSelectOverhang(CCObject*) { g_overhangAllowed = true;  refreshRightPanelTabsAndCanvas(); }

    void onAttachReferenceImage(CCObject*) {
        std::string clipStatus;
        // 1. Сначала мгновенно проверяем, не скопировал ли игрок картинку или путь в буфер обмена (Ctrl+C)
        if (tryLoadReferenceImageFromClipboard(clipStatus)) {
            g_chatHistory.push_back({ "assistant", "Reference loaded from clipboard: " + g_referenceFileName + "! Click 'Build Ref' or write a prompt." });
            if (m_stepPlanLabel) m_stepPlanLabel->setString("Ref: loaded");
            rebuildChatScrollFeed();
            refreshRightPanelTabsAndCanvas();
            return;
        }
        // 2. Запускаем окно выбора файла в отдельном потоке (игра НЕ зависает, окно легко закрывается!)
        startAsyncReferenceImageDialog();
        if (m_stepPlanLabel) m_stepPlanLabel->setString("Ref: picking...");
    }

    void onClearReferenceImage(CCObject*) {
        g_hasReferenceImage = false;
        g_referenceFilePath.clear();
        g_referenceFileName.clear();
        g_referenceBase64.clear();
        g_referenceSampleGrid.clear();
        g_referenceHighResGrid.clear();
        g_chatHistory.push_back({ "assistant", "Reference cleared. Next builds will invent 100% brand-new non-repeating styles!" });
        if (m_stepPlanLabel) m_stepPlanLabel->setString("Ref: cleared");
        rebuildChatScrollFeed();
        refreshRightPanelTabsAndCanvas();
    }

    void onPresetUniqueNewStyle(CCObject*) {
        g_variantIndex++;
        m_inputBuffer = fmt::format("Invent a completely new never-before-seen custom block decor (variant #{}) with rich internal sub-layers and transparency", g_variantIndex);
        refreshInputLabel();
        onSendToNeuralNet(nullptr);
    }

    void onPresetBuildFromRef(CCObject*) {
        if (g_hasReferenceImage) {
            m_inputBuffer = "Recreate the attached reference image on the selected contour with 100% accuracy, multi-layer colors, and opacity";
        } else {
            m_inputBuffer = "Create a completely new unique multi-layered art style with internal block layers and glow";
        }
        refreshInputLabel();
        onSendToNeuralNet(nullptr);
    }

    void onAnotherVariantClicked(CCObject*) {
        g_variantIndex++;
        if (g_hasReferenceImage) {
            m_inputBuffer = fmt::format("Refine and improve the part to match the reference image even closer (variant #{})", g_variantIndex);
        } else if (g_lastBuildPrompt.empty()) {
            m_inputBuffer = fmt::format("Invent a completely new non-repeating block design style (variant #{})", g_variantIndex);
        } else {
            m_inputBuffer = fmt::format("{} (invent a brand-new non-repeating variation #{})", g_lastBuildPrompt, g_variantIndex);
        }
        onSendToNeuralNet(nullptr);
    }

    void onKeepClicked(CCObject*) {
        if (g_hasPendingDrawingApproval && g_hasGeneratedArt) {
            approveAndPlaceCurrentDrawingInGD(true);
            return;
        }
        this->onClose(nullptr);
    }

    void launchAsyncAIWorker(std::string const& promptToRun) {
        if (g_aiRequestThreadRunning) return;
        g_aiRequestThreadRunning = true;
        g_aiRequestHasCompleted = false;

        ContourAnalysis contourCopy = m_contour;
        EditorTabCatalog catalogCopy = loadEditorTabCatalog(m_editorUI);
        int limCopy = g_objectLimit;
        bool ovhCopy = g_overhangAllowed;
        int varCopy = g_variantIndex;

        std::thread([promptToRun, contourCopy, catalogCopy, limCopy, ovhCopy, varCopy]() {
            AIDecisionResult res = queryRealNeuralNetwork(
                promptToRun,
                contourCopy,
                catalogCopy,
                limCopy,
                ovhCopy,
                varCopy
            );
            g_aiThreadResult = std::move(res);
            g_aiRequestHasCompleted = true;
            g_aiRequestThreadRunning = false;
        }).detach();
    }

    static bool isUserApprovalMessage(std::string const& raw) {
        std::string s = raw;
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        s = trimWhitespace(s);
        return (s == "da" || s == "ok" || s == "yes" || s == "go" || s == "stav" ||
                s == "postav" || s == "soglasen" || s == "keep" || s == "norm" ||
                s == "horosho" || s == "krasivo" || s == "idealno" || s == "+");
    }

    static bool isUserRedoMessage(std::string const& raw) {
        std::string s = raw;
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return (s.find("peredel") != std::string::npos ||
                s.find("zanovo") != std::string::npos ||
                s.find("drugoy") != std::string::npos ||
                s.find("redo") != std::string::npos ||
                s.find("change") != std::string::npos ||
                s.find("isprav") != std::string::npos ||
                s == "net" || s == "no" || s == "-");
    }

    void approveAndPlaceCurrentDrawingInGD(bool closePopupAfter) {
        if (!g_hasGeneratedArt || g_lastPreviewSpecs.empty()) {
            if (closePopupAfter) this->onClose(nullptr);
            return;
        }

        SpawnSummary summary = placeGeneratedArtInEditor(
            m_editorUI,
            m_contour,
            g_lastPreviewSpecs,
            g_lastPreviewPalette,
            g_lastPreviewTaskMode
        );
        g_hasPendingDrawingApproval = false;
        m_previewTab = 2; // Переключаем на вкладку In GD
        refreshRightPanelTabsAndCanvas();

        if (m_stepPlaceLabel) {
            std::string plStr = fmt::format("Placed: {} (L{})", summary.createdCount, summary.firstLayer);
            m_stepPlaceLabel->setString(plStr.c_str());
        }
        if (m_previewSubInfoLabel) {
            std::string subStr = fmt::format(
                "Placed in GD! {} objects on Layers L{}..L{}",
                summary.createdCount, summary.firstLayer, summary.firstLayer + 5
            );
            m_previewSubInfoLabel->setString(subStr.c_str());
        }

        g_chatHistory.push_back({
            "assistant",
            fmt::format("Approved! Placed {} objects into the level (Layers L{}..L{}).", summary.createdCount, summary.firstLayer, summary.firstLayer + 5)
        });
        rebuildChatScrollFeed();

        if (closePopupAfter) {
            this->onClose(nullptr);
        }
    }

    void onSendToNeuralNet(CCObject*) {
        restoreLockedContourPositions();

        std::string promptText = trimWhitespace(m_inputBuffer);

        // Если в Drawing уже готов рисунок и игрок написал в чат "да / ок / согласен / ставь" — сразу ставим в уровень!
        if (g_hasPendingDrawingApproval && !promptText.empty() && isUserApprovalMessage(promptText)) {
            m_inputBuffer.clear();
            refreshInputLabel();
            g_chatHistory.push_back({ "user", promptText });
            approveAndPlaceCurrentDrawingInGD(false);
            return;
        }

        // Если игрок написал короткое "переделай / нет / заново" без нового описания — переделываем прошлый запрос,
        // а если написал новый стиль (например "сделай стиль неон") — выполняем ТОЛЬКО новый запрос без примешивания старого!
        if (g_hasPendingDrawingApproval && !promptText.empty() && isUserRedoMessage(promptText) && promptText.size() <= 14) {
            g_variantIndex++;
            promptText = g_lastBuildPrompt.empty() ? "make a better design" : g_lastBuildPrompt;
        }
        if (promptText.empty()) {
            if (g_hasReferenceImage) {
                promptText = "Recreate the attached reference image on the selected contour with maximum accuracy";
            } else {
                return;
            }
        }

        m_inputBuffer.clear();
        refreshInputLabel();

        // Запоминаем написанное сообщение для авто-повтора раз в 2 секунды при сбое!
        g_pendingRetryPrompt = promptText;
        g_autoRetryTimerActive = false;
        g_autoRetryAttemptCount = 0;

        g_floatingDotsTimer = 0.0f;
        g_floatingDotsPhase = 0;

        g_chatHistory.push_back({ "user", promptText });
        g_chatHistory.push_back({ "assistant", " .  " });
        rebuildChatScrollFeed();

        if (m_stepPlanLabel)    m_stepPlanLabel->setString("Plan: ...");
        if (m_stepDrawingLabel) m_stepDrawingLabel->setString("Draw: ...");

        launchAsyncAIWorker(promptText);
    }

    void handleCompletedAIResult(AIDecisionResult const& ai) {
        if (!ai.httpSuccess) {
            g_autoRetryAttemptCount++;

            // Если прошло меньше 10 попыток — НЕ показываем ошибку игроку!
            // Оставляем только плавающие 3 точки и переотправляем сообщение через 2 секунды
            if (g_autoRetryAttemptCount < 10) {
                g_autoRetryTimerActive = true;
                g_autoRetryCountdown = 2.0f;

                // Тихо переключаем на следующую доступную модель, чтобы обойти 503/429
                // Повторяем запрос на выбранный пользователем Base URL и Endpoint
                return;
            }

            // Только если за 10 попыток так и не получилось загрузить ответ — показываем ошибку!
            g_autoRetryTimerActive = false;
            g_autoRetryAttemptCount = 0;

            std::string finalErrMsg = fmt::format("Failed after 10 attempts: {}", ai.replyText);
            if (!g_chatHistory.empty() && g_chatHistory.back().role == "assistant") {
                g_chatHistory.back().content = finalErrMsg;
            } else {
                g_chatHistory.push_back({ "assistant", finalErrMsg });
            }
            rebuildChatScrollFeed();

            if (m_stepPlanLabel)    m_stepPlanLabel->setString("Plan: error");
            if (m_stepDrawingLabel) m_stepDrawingLabel->setString("Draw: failed");
            return;
        }

        // УСПЕХ! Выключаем таймер авто-повтора и обновляем ответ ИИ в чате
        g_autoRetryTimerActive = false;
        g_autoRetryAttemptCount = 0;

        if (!g_chatHistory.empty() && g_chatHistory.back().role == "assistant") {
            g_chatHistory.back().content = ai.replyText;
        } else {
            g_chatHistory.push_back({ "assistant", ai.replyText });
        }
        rebuildChatScrollFeed();

        if (ai.taskMode == AITaskMode::ChatOnly) {
            if (m_stepPlanLabel)    m_stepPlanLabel->setString("Plan: chat");
            if (m_stepDrawingLabel) m_stepDrawingLabel->setString("Draw: idle");
            if (m_stepPlaceLabel)   m_stepPlaceLabel->setString("Place: 0 obj");
            return;
        }

        if (ai.taskMode == AITaskMode::UndoLast) {
            int rem = clearLastPlacedObjects();
            g_hasGeneratedArt = false;
            m_previewTab = 0;
            refreshRightPanelTabsAndCanvas();
            if (m_stepPlaceLabel) {
                std::string uStr = fmt::format("Undo: -{}", rem);
                m_stepPlaceLabel->setString(uStr.c_str());
            }
            return;
        }

        g_lastBuildPrompt = g_pendingRetryPrompt;
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

        // ИИ СНАЧАЛА показывает готовую попиксельную работу во вкладке Drawing (БЕЗ спавна в уровень!),
        // чтобы игрок сам оценил результат и либо согласился (Keep / In GD / 'da'), либо сказал переделать!
        g_lastAIDecisionForEditor = ai;
        g_lastPreviewSpecs = specs;
        g_lastPreviewPalette = palette;
        g_lastPreviewTaskMode = ai.taskMode;
        g_hasGeneratedArt = true;
        g_hasPendingDrawingApproval = true;
        m_previewTab = 1; // Открываем вкладку Drawing
        refreshRightPanelTabsAndCanvas();

        if (!g_chatHistory.empty() && g_chatHistory.back().role == "assistant") {
            g_chatHistory.back().content += " -> Check 'Drawing'! Click 'Keep' (or write 'da') to place in GD, or 'Another variant' (or write 'peredelay') to redo!";
            rebuildChatScrollFeed();
        }

        if (m_stepPlanLabel) {
            std::string pStr = fmt::format("Match: {}%", ai.selfMatchScore);
            m_stepPlanLabel->setString(pStr.c_str());
        }
        if (m_stepDrawingLabel) {
            m_stepDrawingLabel->setString("Draw: ready!");
        }
        if (m_stepPlaceLabel) {
            m_stepPlaceLabel->setString("Wait approve");
        }
        if (m_previewSubInfoLabel) {
            std::string subStr = fmt::format(
                "1024x1024 px | Match {}% | Click 'Keep' to place {} objs or 'Another variant' to redo",
                ai.selfMatchScore, specs.size()
            );
            m_previewSubInfoLabel->setString(subStr.c_str());
        }
    }

    void updateKeyboardAndLock(float dt) {
        restoreLockedContourPositions();

        // 1. Проверяем завершение фонового запроса к ИИ
        if (g_aiRequestHasCompleted) {
            g_aiRequestHasCompleted = false;
            handleCompletedAIResult(g_aiThreadResult);
        }

        // 2. Анимация плавающих 3 точек в чате, пока сообщение грузится или тихо переотправляется (до 10 попыток)
        if (g_aiRequestThreadRunning || g_autoRetryTimerActive) {
            g_floatingDotsTimer += dt;
            if (g_floatingDotsTimer >= 0.32f) {
                g_floatingDotsTimer = 0.0f;
                g_floatingDotsPhase = (g_floatingDotsPhase + 1) % 4;
                static constexpr const char* kFloatingDotsFrames[4] = {
                    " .  ",
                    " .. ",
                    " ...",
                    "  .."
                };
                if (!g_chatHistory.empty() && g_chatHistory.back().role == "assistant") {
                    g_chatHistory.back().content = kFloatingDotsFrames[g_floatingDotsPhase];
                    rebuildChatScrollFeed();
                }
                if (m_stepPlanLabel) {
                    m_stepPlanLabel->setString(kFloatingDotsFrames[g_floatingDotsPhase]);
                }
            }
        }

        // 3. Тихий авто-повтор написанного сообщения каждые 2 секунды (без показа ошибок до 10-й попытки!)
        if (g_autoRetryTimerActive && !g_aiRequestThreadRunning && !g_pendingRetryPrompt.empty()) {
            g_autoRetryCountdown -= dt;
            if (g_autoRetryCountdown <= 0.0f) {
                g_autoRetryTimerActive = false;
                launchAsyncAIWorker(g_pendingRetryPrompt);
            }
        }

        // Проверяем завершение фонового диалога выбора картинки-референса
        if (g_fileDialogHasResult) {
            g_fileDialogHasResult = false;
            if (g_fileDialogPickedSuccess && !g_fileDialogPickedPath.empty()) {
                std::string statusMsg;
                if (loadReferenceImageFromPath(g_fileDialogPickedPath, statusMsg)) {
                    g_chatHistory.push_back({ "assistant", "Reference attached: " + g_referenceFileName + ". Click 'Build Ref' or write your task!" });
                    if (m_stepPlanLabel) m_stepPlanLabel->setString("Ref: loaded");
                } else {
                    g_chatHistory.push_back({ "assistant", "Ref load error: " + statusMsg });
                    if (m_stepPlanLabel) m_stepPlanLabel->setString("Ref: error");
                }
                rebuildChatScrollFeed();
                refreshRightPanelTabsAndCanvas();
            } else {
                if (m_stepPlanLabel) m_stepPlanLabel->setString("Plan: ready");
            }
        }

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
                std::string refStatus;
                if (tryLoadReferenceImageFromClipboard(refStatus)) {
                    g_chatHistory.push_back({ "assistant", "Reference pasted via Ctrl+V: " + g_referenceFileName + "!" });
                    if (m_stepPlanLabel) m_stepPlanLabel->setString("Ref: loaded");
                    rebuildChatScrollFeed();
                    refreshRightPanelTabsAndCanvas();
                } else {
                    std::string clip = readWindowsClipboardText();
                    if (!clip.empty()) {
                        m_inputBuffer += utf8ToGDReadable(clip);
                        if (m_inputBuffer.size() > 160) m_inputBuffer.resize(160);
                        refreshInputLabel();
                    }
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
