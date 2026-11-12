#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/Popup.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
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
    DarkFill = 0,         // Слой +0: темный фон внутри структуры
    Dark3D = 1,           // Слой +2: глубокий черный/темный тон для 3D-блоков
    MidTone = 2,          // Слой +1: средний тон для внутренних рамок, дуг и полос
    BrightMain = 3,       // Слой +1/+3: главный яркий цвет (эмблемы, глаз, череп, блики, трава)
    HighlightOutline = 4, // Слой +2: светлая четкая обводка контура и микро-детали
    AdditiveGlow = 5      // Слой +3: неоновое свечение (мелкий контурный неон + мягкий глоу)
};

enum class LayerRole : int {
    Fill = 0,        // firstFreeLayer + 0: Заполнение
    InnerDetail = 1, // firstFreeLayer + 1: Внутренний декор
    Outline3D = 2,   // firstFreeLayer + 2: Контур и 3D-блоки
    NeonAndGrass = 3 // firstFreeLayer + 3: Неон и внешняя трава
};

enum class AITaskMode {
    Only3D = 0,     // Игрок попросил сделать ТОЛЬКО 3D
    OnlyNeon = 1,   // Игрок попросил сделать ТОЛЬКО неон/свечение
    OnlyFill = 2,   // Игрок попросил ТОЛЬКО залить/заполнить контур
    FullDecor = 3   // Полноценный декор выбранного стиля и количества объектов
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

    int cellCount() const { return static_cast<int>(occupiedCells.size()); }
    int spikeCount() const { return static_cast<int>(spikes.size()); }
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
    GLubyte customOpacity = 255;
    bool noTouch = true;
    bool noGlow = true;
    int priorityTier = 1;
};

static int g_lastHueBucket = -1;
static int g_lastLayoutSeed = -1;

// ============================================================================
// Хранение и смена настроек API (провайдер + ключ) с сохранением в файл
// ============================================================================
struct ApiProviderPreset {
    const char* name;
    const char* endpointUrl;
    const char* defaultModel;
};

static constexpr ApiProviderPreset kApiProviders[] = {
    { "OpenAI",     "https://api.openai.com/v1/chat/completions",                              "gpt-4o-mini" },
    { "Gemini",     "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions", "gemini-2.5-flash" },
    { "DeepSeek",   "https://api.deepseek.com/chat/completions",                               "deepseek-chat" },
    { "Groq",       "https://api.groq.com/openai/v1/chat/completions",                         "llama-3.3-70b-versatile" },
    { "OpenRouter", "https://openrouter.ai/api/v1/chat/completions",                           "openai/gpt-4o-mini" }
};

static bool g_apiConfigLoaded = false;
static bool g_apiConnected = false;
static int g_apiProviderIndex = 0;
static std::string g_connectedApiKey;

static void loadApiConfigFromDisk() {
    if (g_apiConfigLoaded) return;
    g_apiConfigLoaded = true;
    std::ifstream in("ai_decor_api.cfg");
    if (!in.is_open()) return;
    int prov = 0;
    std::string key;
    if (in >> prov) {
        std::getline(in >> std::ws, key);
        if (prov >= 0 && prov < 5 && !key.empty()) {
            g_apiProviderIndex = prov;
            g_connectedApiKey = key;
            g_apiConnected = true;
        }
    }
}

static void saveApiConfigToDisk(int provIdx, std::string const& key) {
    g_apiConfigLoaded = true;
    g_apiProviderIndex = std::clamp(provIdx, 0, 4);
    g_connectedApiKey = key;
    g_apiConnected = !key.empty();
    std::ofstream out("ai_decor_api.cfg", std::ios::trunc);
    if (out.is_open()) {
        out << g_apiProviderIndex << "\n" << g_connectedApiKey << "\n";
    }
}

static int detectProviderFromKey(std::string const& key, int currentIdx) {
    if (key.rfind("AIza", 0) == 0) return 1;   // Gemini
    if (key.rfind("gsk_", 0) == 0) return 3;   // Groq
    if (key.rfind("sk-or-", 0) == 0) return 4; // OpenRouter
    return currentIdx;
}

static std::string maskApiKeyPreview(std::string const& key) {
    if (key.empty()) return "not set";
    if (key.size() <= 8) return key;
    return key.substr(0, 5) + "..." + key.substr(key.size() - 4);
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

static ccColor3B hsvToRgb(float h, float s, float v) {
    h = std::fmod(h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    s = std::clamp(s, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);

    float c = v * s;
    float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;

    float r = 0.f, g = 0.f, b = 0.f;
    if (h < 60.f)       { r = c; g = x; b = 0.f; }
    else if (h < 120.f) { r = x; g = c; b = 0.f; }
    else if (h < 180.f) { r = 0.f; g = c; b = x; }
    else if (h < 240.f) { r = 0.f; g = x; b = c; }
    else if (h < 300.f) { r = x; g = 0.f; b = c; }
    else                { r = c; g = 0.f; b = x; }

    return ccColor3B{
        static_cast<GLubyte>(std::clamp((r + m) * 255.0f, 0.0f, 255.0f)),
        static_cast<GLubyte>(std::clamp((g + m) * 255.0f, 0.0f, 255.0f)),
        static_cast<GLubyte>(std::clamp((b + m) * 255.0f, 0.0f, 255.0f))
    };
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
static void bindObjectToPaletteSlot(
    ObjT* obj,
    StylePalette const& palette,
    ColorSlot slot,
    GLubyte overrideOpacity = 255
) {
    if (!obj) return;
    int idx = std::clamp(static_cast<int>(slot), 0, 5);
    ccColor3B col = palette.slotColors[idx];
    bool blend = palette.slotBlending[idx];
    GLubyte finalOp = (overrideOpacity < 255) ? overrideOpacity : palette.slotOpacity[idx];

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
    // 3D-блоки из вкладки 4 (страница 2) — ровно те блоки, из которых сделан 3D на image_29bf02.png
    int id3DTrapTop    = 211; // Слот 0: верхняя скошенная 3D-трапеция (для левого верхнего края)
    int id3DHalfStrip  = 211; // Слот 1: прямая 3D-полоса (30x15 сверху и 15x30 справа)
    int id3DTriCorner  = 211; // Слот 5: верхне-левый 3D-треугольник 15x15 (|‾/)
    int id3DTriBR      = 211; // Слот 6: нижне-правый 3D-треугольник 15x15 (/_|)

    // Декоративные блоки из вкладки 10 (страница 9)
    int idEyeBlock      = 211; // Слот 5: готовый декор-глаз
    int idSkullBlock    = 211; // Слот 10: готовый череп
    int idSwirlSign     = 211; // Слот 4: вихрь/спираль
    int idChainLink     = 211; // Слот 1: цепь
    int idBrickBlock    = 211; // Слот 6: кирпичики
    int idBracketArch   = 211; // Слот 7: П-образная неоновая арка
    int idBracketCorner = 211; // Слот 8: Г-образный уголок
    int idDiagShine1    = 211; // Слот 11: диагональный блик //

    // Готовые тонкие линии, градиент, дуги, точки, крестики из вкладки 10 (страница 10)
    int idThinLineLong  = 211; // Слот 0
    int idGradLine      = 1888;// Слот 2: градиентная неоновая полоса
    int idCornerMark    = 211; // Слот 3
    int idParallelLines = 211; // Слот 4
    int idArcThin       = 211; // Слот 6
    int idCircleRing    = 211; // Слот 7
    int idCircleDot     = 211; // Слот 8
    int idSmallCross    = 211; // Слот 9
    int idSmallTriangle = 211; // Слот 10
    int idChevron       = 211; // Слот 11
    int idHollowSquare  = 211; // Слот 13
    int idDiagThin      = 211; // Слот 14
    int idShortDash     = 211; // Слот 15
    int idDoubleDots    = 211; // Слот 17

    // Настоящие объекты неона
    int idGlowEdgeSmall = 1888;
    int idGlowBeam      = 1888;
    int idGlowCircle    = 1888;
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

        // Вкладка 4 (3D), страница 2 (точные блоки 3D со скриншота image_29bf02.png!)
        if (best3DBar.size() >= 20) {
            cat.id3DTrapTop   = getSlotObjectID(best3DBar, 1, 0, 211);
            cat.id3DHalfStrip = getSlotObjectID(best3DBar, 1, 1, 211);
            cat.id3DTriCorner = getSlotObjectID(best3DBar, 1, 5, 211);
            cat.id3DTriBR     = getSlotObjectID(best3DBar, 1, 6, 211);
        }

        // Вкладка 10 (Декор), страницы 9 и 10
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

// ============================================================================
// Автоматический поиск первого свободного блока из 4 слоев редактора
// ============================================================================
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
// Умный ИИ-анализатор русских и английских команд игрока в чате
// ============================================================================
struct ChatBotPlan {
    AITaskMode taskMode = AITaskMode::FullDecor;
    int targetCount = 80;
    float preferredHue = -1.f;
    bool wantEye = false;
    bool wantSkull = false;
    bool wantChains = false;
    bool wantArcs = false;
    bool wantGrass = true;
    bool want3D = true;
    bool wantNeon = true;
    std::string botResponse;
};

static std::string toLowerASCII(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

static bool containsAny(std::string const& text, std::initializer_list<const char*> terms) {
    for (const char* t : terms) {
        if (text.find(t) != std::string::npos) return true;
    }
    return false;
}

static int extractObjectCountFromChat(std::string s) {
    // Исключаем "3d" / "3д" / "3l", чтобы цифра 3 из "3d" не считалась количеством объектов
    for (const char* token : { "3d", "3l", "3\xD0\xB4", "3\xD0\x94" }) {
        size_t tLen = std::strlen(token);
        for (size_t pos = 0; (pos = s.find(token, pos)) != std::string::npos;) {
            s.replace(pos, tLen, std::string(tLen, ' '));
        }
    }

    int foundNumber = -1;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] >= '0' && s[i] <= '9') {
            int val = 0;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
                val = val * 10 + (s[i] - '0');
                ++i;
            }
            if (val >= 5 && val <= 1000) {
                foundNumber = val;
                break;
            }
        } else {
            ++i;
        }
    }
    return foundNumber;
}

static ChatBotPlan parseUserChatCommand(std::string const& rawMessage) {
    ChatBotPlan plan;
    std::string p = toLowerASCII(rawMessage);

    int extractedCount = extractObjectCountFromChat(p);
    if (extractedCount > 0) {
        plan.targetCount = std::clamp(extractedCount, 10, 450);
    }

    bool mentions3D = containsAny(p, {
        "3d", "3д", "3Д", "3l", "tri d", "trid", "obem", "obyem", "объем", "объём"
    });
    bool mentionsNeon = containsAny(p, {
        "neon", "glow", "svet", "svech", "glou", "неон", "НЕОН", "глоу", "свеч", "свет", "ytjy"
    });
    bool mentionsFill = containsAny(p, {
        "fill", "zapoln", "zaley", "zaliv", "fon", "заполн", "залей", "залив", "фон"
    });
    bool mentionsDecorOrStyle = containsAny(p, {
        "decor", "dekor", "stil", "style", "modern", "hell", "adsk", "real",
        "eye", "glaz", "skull", "cherep", "arch", "ark", "dug", "arc", "chain",
        "tsep", "cep", "uzor", "struktur", "postroy", "ukras", "random",
        "декор", "стил", "модерн", "адск", "реали", "глаз", "череп", "арк", "дуг", "цеп", "узор", "структур"
    });

    // 1. Если игрок просит сделать 3D (например "sdelay 3d", "сделай 3д", "3d") и не просит полный декор:
    // ИИ строит СТРОГО ТОЛЬКО 3D из готовых 3D-блоков!
    if (mentions3D && !mentionsDecorOrStyle && !mentionsNeon && !mentionsFill && extractedCount <= 0) {
        plan.taskMode = AITaskMode::Only3D;
        plan.botResponse = "II: Zadacha ponyatna! Stroyu strogo cherniy 3D-obyem i beliy kontur po referensu.";
        return plan;
    }

    // 2. Если игрок просит только неон/свечение
    if (mentionsNeon && !mentionsDecorOrStyle && !mentions3D && !mentionsFill && extractedCount <= 0) {
        plan.taskMode = AITaskMode::OnlyNeon;
        plan.botResponse = "II: Zadacha ponyatna! Dobavlyayu tolko dvustoronniy konturniy neon i svechenie.";
        return plan;
    }

    // 3. Если игрок просит только залить/заполнить структуру
    if (mentionsFill && !mentionsDecorOrStyle && !mentions3D && !mentionsNeon) {
        plan.taskMode = AITaskMode::OnlyFill;
        plan.botResponse = "II: Zadacha ponyatna! Polnostyu zalivayu vnutrennyuyu oblast strukturi.";
        return plan;
    }

    // 4. Иначе — полноценный декор по стилю и количеству объектов из сообщения
    plan.taskMode = AITaskMode::FullDecor;

    if (containsAny(p, { "zelen", "zel", "laym", "green", "lime", "зелен", "лайм", "ptkty" })) {
        plan.preferredHue = 112.f;
    } else if (containsAny(p, { "golub", "biryuz", "tsian", "cyan", "голуб", "бирюз", "циан", "ujke," })) {
        plan.preferredHue = 186.f;
    } else if (containsAny(p, { "sin", "blue", "син", "cby" })) {
        plan.preferredHue = 222.f;
    } else if (containsAny(p, { "fiolet", "purpur", "purple", "violet", "фиолет", "пурпур", "abjktn" })) {
        plan.preferredHue = 278.f;
    } else if (containsAny(p, { "rozov", "fuksi", "pink", "magenta", "розов", "фукси", "hjpjd" })) {
        plan.preferredHue = 322.f;
    } else if (containsAny(p, { "krasn", "aliy", "adsk", "hell", "red", "красн", "адск", "rhfcy" })) {
        plan.preferredHue = 6.f;
    } else if (containsAny(p, { "oranzh", "orange", "оранж", "jhfy;" })) {
        plan.preferredHue = 28.f;
    } else if (containsAny(p, { "zhelt", "zolot", "yellow", "gold", "желт", "золот", ";tkn" })) {
        plan.preferredHue = 52.f;
    }

    if (containsAny(p, { "glaz", "eye", "глаз", "ukfp" })) plan.wantEye = true;
    if (containsAny(p, { "cherep", "adsk", "skull", "hell", "череп", "адск", "xthtg" })) plan.wantSkull = true;
    if (containsAny(p, { "tsep", "cep", "chain", "цеп" })) plan.wantChains = true;
    if (containsAny(p, { "dug", "krug", "arc", "дуг", "круг" })) plan.wantArcs = true;
    if (containsAny(p, { "modern", "bez trav", "no grass", "модерн", "без трав" })) plan.wantGrass = false;
    if (containsAny(p, { "bez 3d", "no 3d", "без 3д" })) plan.want3D = false;
    if (containsAny(p, { "bez neon", "no neon", "без неон" })) plan.wantNeon = false;

    plan.botResponse = fmt::format(
        "II: Stroyu novuyu unikalnuyu strukturu (~{} obyektov, polniy kontur + 3D + neon)!",
        plan.targetCount
    );
    return plan;
}

static StylePalette buildPaletteFromBotPlan(ChatBotPlan const& plan, std::mt19937& rng) {
    StylePalette pal;

    float H = plan.preferredHue;
    if (H < 0.f) {
        float const vibrantHues[8] = { 108.f, 184.f, 218.f, 276.f, 322.f, 8.f, 32.f, 152.f };
        std::uniform_int_distribution<int> pick(0, 7);
        int bucket = pick(rng);
        if (bucket == g_lastHueBucket) {
            bucket = (bucket + 1 + (rng() % 6)) % 8;
        }
        g_lastHueBucket = bucket;
        std::uniform_real_distribution<float> jitter(-9.f, 9.f);
        H = vibrantHues[bucket] + jitter(rng);
    }

    pal.masterColor = hsvToRgb(H, 0.90f, 1.00f);

    auto scaleColor = [](ccColor3B c, float factor, float lift = 0.f) -> ccColor3B {
        return ccColor3B{
            static_cast<GLubyte>(std::clamp(c.r * factor + lift, 0.f, 255.f)),
            static_cast<GLubyte>(std::clamp(c.g * factor + lift, 0.f, 255.f)),
            static_cast<GLubyte>(std::clamp(c.b * factor + lift, 0.f, 255.f))
        };
    };

    auto mixTowardWhite = [](ccColor3B c, float whiteRatio) -> ccColor3B {
        return ccColor3B{
            static_cast<GLubyte>(std::clamp(c.r + (255.f - c.r) * whiteRatio, 0.f, 255.f)),
            static_cast<GLubyte>(std::clamp(c.g + (255.f - c.g) * whiteRatio, 0.f, 255.f)),
            static_cast<GLubyte>(std::clamp(c.b + (255.f - c.b) * whiteRatio, 0.f, 255.f))
        };
    };

    pal.slotColors[static_cast<int>(ColorSlot::DarkFill)] = scaleColor(pal.masterColor, 0.14f, 4.f);
    pal.slotColors[static_cast<int>(ColorSlot::Dark3D)]   = ccColor3B{ 4, 5, 7 };
    pal.slotColors[static_cast<int>(ColorSlot::MidTone)]  = scaleColor(pal.masterColor, 0.56f, 12.f);
    pal.slotColors[static_cast<int>(ColorSlot::BrightMain)] = pal.masterColor;
    pal.slotColors[static_cast<int>(ColorSlot::HighlightOutline)] = (plan.taskMode == AITaskMode::Only3D)
        ? ccColor3B{ 255, 255, 255 }
        : mixTowardWhite(pal.masterColor, 0.62f);
    pal.slotColors[static_cast<int>(ColorSlot::AdditiveGlow)] = mixTowardWhite(pal.masterColor, 0.25f);

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

// ============================================================================
// Считывание и ПОЛНОЕ заполнение выделенного контура
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

    return info;
}

// ============================================================================
// Построение объектов строго по задаче ИИ (Only3D / OnlyNeon / OnlyFill / FullDecor)
// ============================================================================
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
    bool flipX = false,
    bool flipY = false,
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
    spec.flipX = flipX;
    spec.flipY = flipY;
    spec.customOpacity = opacity;
    spec.noTouch = true;
    spec.noGlow = true;
    out.push_back(spec);
}

static std::vector<GeneratedObjectSpec> generateByAITask(
    ContourAnalysis const& contour,
    EditorTabCatalog const& cat,
    ChatBotPlan const& plan,
    std::mt19937& rng
) {
    std::vector<GeneratedObjectSpec> result;

    auto islands = splitIntoIslands(contour.occupiedCells);
    int totalCells = (std::max)(1, contour.cellCount());

    // 1. РЕЖИМ "ТОЛЬКО 3D" (строит эталонный черный 3D-объем и белую обводку со скриншота image_29bf02.png!)
    if (plan.taskMode == AITaskMode::Only3D) {
        for (auto const& isl : islands) {
            for (auto const& [gx, gy] : isl.cells) {
                float cx = gx * 30.f + 15.f;
                float cy = gy * 30.f + 15.f;

                bool topOpen    = (contour.occupiedCells.count({ gx, gy + 1 }) == 0);
                bool bottomOpen = (contour.occupiedCells.count({ gx, gy - 1 }) == 0);
                bool leftOpen   = (contour.occupiedCells.count({ gx - 1, gy }) == 0);
                bool rightOpen  = (contour.occupiedCells.count({ gx + 1, gy }) == 0);

                // Четкая белая линия контура
                if (topOpen)    addSpec(result, 211, cx, cy + 14.2f, 1.02f, 0.06f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11, 0);
                if (leftOpen)   addSpec(result, 211, cx - 14.2f, cy, 0.06f, 1.02f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11, 0);
                if (rightOpen)  addSpec(result, 211, cx + 14.2f, cy, 0.06f, 1.02f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11, 0);
                if (bottomOpen && gy > 0) addSpec(result, 211, cx, cy - 14.2f, 1.02f, 0.06f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11, 0);

                // Верхний 3D: слева скошенная трапеция (Slot 0), далее прямая 3D-полоса (Slot 1)
                if (topOpen) {
                    int top3DId = leftOpen ? cat.id3DTrapTop : cat.id3DHalfStrip;
                    addSpec(result, top3DId, cx, cy + 22.5f, 1.00f, 1.00f, 0.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 0);
                }
                // Правый 3D: прямая 3D-полоса (Slot 1), повернутая на 90°
                if (rightOpen) {
                    addSpec(result, cat.id3DHalfStrip, cx + 22.5f, cy, 1.00f, 1.00f, 90.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 0);
                }
                // Верхний правый угол 15x15 (cx + 22.5f, cy + 22.5f): два треугольника (Slot 5 + Slot 6)
                if (topOpen && rightOpen) {
                    addSpec(result, cat.id3DTriCorner, cx + 22.5f, cy + 22.5f, 1.00f, 1.00f, 0.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 0);
                    addSpec(result, cat.id3DTriBR,     cx + 22.5f, cy + 22.5f, 1.00f, 1.00f, 0.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 0);
                }
            }
        }
        return result;
    }

    // 2. РЕЖИМ "ТОЛЬКО НЕОН"
    if (plan.taskMode == AITaskMode::OnlyNeon) {
        for (auto const& isl : islands) {
            for (auto const& [gx, gy] : isl.cells) {
                float cx = gx * 30.f + 15.f;
                float cy = gy * 30.f + 15.f;
                bool topOpen    = (contour.occupiedCells.count({ gx, gy + 1 }) == 0);
                bool bottomOpen = (contour.occupiedCells.count({ gx, gy - 1 }) == 0);
                bool leftOpen   = (contour.occupiedCells.count({ gx - 1, gy }) == 0);
                bool rightOpen  = (contour.occupiedCells.count({ gx + 1, gy }) == 0);

                if (topOpen) {
                    addSpec(result, cat.idGlowEdgeSmall, cx - 7.5f, cy + 18.0f, 0.50f, 0.45f, 0.f,   LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 14, 0, false, false, 140);
                    addSpec(result, cat.idGlowEdgeSmall, cx + 7.5f, cy + 18.0f, 0.50f, 0.45f, 0.f,   LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 14, 0, false, false, 140);
                    addSpec(result, cat.idGlowEdgeSmall, cx - 7.5f, cy + 11.5f, 0.50f, 0.40f, 180.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 0, false, false, 115);
                    addSpec(result, cat.idGlowEdgeSmall, cx + 7.5f, cy + 11.5f, 0.50f, 0.40f, 180.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 0, false, false, 115);
                    addSpec(result, 1888, cx, cy + 13.f, 0.95f, 0.42f, 0.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 15, 0, false, false, 100);
                }
                if (leftOpen) {
                    addSpec(result, cat.idGlowEdgeSmall, cx - 18.0f, cy, 0.50f, 0.45f, 270.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 14, 0, false, false, 130);
                    addSpec(result, cat.idGlowEdgeSmall, cx - 11.5f, cy, 0.50f, 0.40f, 90.f,  LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 0, false, false, 110);
                }
                if (rightOpen) {
                    addSpec(result, cat.idGlowEdgeSmall, cx + 18.0f, cy, 0.50f, 0.45f, 90.f,  LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 14, 0, false, false, 130);
                    addSpec(result, cat.idGlowEdgeSmall, cx + 11.5f, cy, 0.50f, 0.40f, 270.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 0, false, false, 110);
                }
                if (bottomOpen && gy > 0) {
                    addSpec(result, cat.idGlowEdgeSmall, cx, cy - 18.0f, 0.80f, 0.45f, 180.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 14, 0, false, false, 120);
                }
            }
        }
        return result;
    }

    // 3. РЕЖИМ "ТОЛЬКО ЗАЛИВКА"
    if (plan.taskMode == AITaskMode::OnlyFill) {
        for (auto const& isl : islands) {
            for (auto const& [gx, gy] : isl.cells) {
                float cx = gx * 30.f + 15.f;
                float cy = gy * 30.f + 15.f;
                addSpec(result, 211, cx, cy, 1.00f, 1.00f, 0.f, LayerRole::Fill, ColorSlot::DarkFill, 1, 0);
            }
        }
        return result;
    }

    // 4. РЕЖИМ ПОЛНОГО УНИКАЛЬНОГО ДЕКОРА (FullDecor)
    float densityRatio = std::clamp(static_cast<float>(plan.targetCount) / (totalCells * 8.f), 0.5f, 3.0f);
    float microScale = std::clamp(0.40f / std::sqrt(densityRatio), 0.16f, 0.40f);

    std::uniform_int_distribution<int> layoutDist(0, 5);
    int layoutStyle = layoutDist(rng);
    if (layoutStyle == g_lastLayoutSeed) {
        layoutStyle = (layoutStyle + 1 + (rng() % 5)) % 6;
    }
    g_lastLayoutSeed = layoutStyle;

    int const emblemPool[4] = { cat.idEyeBlock, cat.idSkullBlock, cat.idSwirlSign, cat.idCircleRing };
    int chosenEmblem = emblemPool[rng() % 4];
    if (plan.wantEye)   chosenEmblem = cat.idEyeBlock;
    if (plan.wantSkull) chosenEmblem = cat.idSkullBlock;
    if (plan.wantArcs)  chosenEmblem = cat.idCircleRing;

    int const cornerPool[4] = { cat.idBracketCorner, cat.idCornerMark, cat.idArcThin, cat.idSmallTriangle };
    int chosenCornerPiece = cornerPool[rng() % 4];

    int const innerLinePool[4] = { cat.idParallelLines, cat.idShortDash, cat.idDiagThin, cat.idChainLink };
    int chosenInnerLine = plan.wantChains ? cat.idChainLink : innerLinePool[rng() % 4];

    int chosenShine = cat.idDiagShine1;
    bool doubleSidedNeon = (rng() % 100) < 75;

    for (auto const& isl : islands) {
        int cols = isl.maxGX - isl.minGX + 1;
        int rows = isl.maxGY - isl.minGY + 1;
        bool isSingleCube = (cols == 1 && rows == 1);

        int focalCol = (rng() % 2 == 0) ? (cols - 1) : (cols / 2);
        int focalRow = rows - 1;

        for (auto const& [gx, gy] : isl.cells) {
            float cx = gx * 30.f + 15.f;
            float cy = gy * 30.f + 15.f;

            bool topOpen    = (contour.occupiedCells.count({ gx, gy + 1 }) == 0);
            bool bottomOpen = (contour.occupiedCells.count({ gx, gy - 1 }) == 0);
            bool leftOpen   = (contour.occupiedCells.count({ gx - 1, gy }) == 0);
            bool rightOpen  = (contour.occupiedCells.count({ gx + 1, gy }) == 0);

            int localCol = gx - isl.minGX;
            int localRow = gy - isl.minGY;

            // Слой +0: Полная заливка клетки
            addSpec(result, 211, cx, cy, 1.00f, 1.00f, 0.f, LayerRole::Fill, ColorSlot::DarkFill, 1, 0);

            if (((localCol + localRow + layoutStyle) % 2) == 0) {
                addSpec(result, 211, cx, cy, 0.86f, 0.86f, 0.f, LayerRole::Fill, ColorSlot::MidTone, 2, 1, false, false, 42);
            }

            // Слой +2: Четкая обводка + точный 3D из готовых 3D-блоков
            if (topOpen)    addSpec(result, 211, cx, cy + 14.2f, 1.02f, 0.06f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11, 0);
            if (leftOpen)   addSpec(result, 211, cx - 14.2f, cy, 0.06f, 1.02f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11, 0);
            if (rightOpen)  addSpec(result, 211, cx + 14.2f, cy, 0.06f, 1.02f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11, 0);
            if (bottomOpen && gy > 0) addSpec(result, 211, cx, cy - 14.2f, 1.02f, 0.06f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11, 0);

            if (plan.want3D) {
                if (topOpen) {
                    int top3DId = leftOpen ? cat.id3DTrapTop : cat.id3DHalfStrip;
                    addSpec(result, top3DId, cx, cy + 22.5f, 1.00f, 1.00f, 0.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 0);
                }
                if (rightOpen) {
                    addSpec(result, cat.id3DHalfStrip, cx + 22.5f, cy, 1.00f, 1.00f, 90.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 0);
                }
                if (topOpen && rightOpen) {
                    addSpec(result, cat.id3DTriCorner, cx + 22.5f, cy + 22.5f, 1.00f, 1.00f, 0.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 0);
                    addSpec(result, cat.id3DTriBR,     cx + 22.5f, cy + 22.5f, 1.00f, 1.00f, 0.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 0);
                }
            }

            // Слой +3: Мелкий контурный неон (снаружи и внутри) + мягкое свечение 1888 + трава
            if (plan.wantNeon) {
                if (topOpen) {
                    addSpec(result, cat.idGlowEdgeSmall, cx - 7.5f, cy + 18.0f, 0.50f, 0.45f, 0.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 14, 0, false, false, 135);
                    addSpec(result, cat.idGlowEdgeSmall, cx + 7.5f, cy + 18.0f, 0.50f, 0.45f, 0.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 14, 0, false, false, 135);
                    if (doubleSidedNeon) {
                        addSpec(result, cat.idGlowEdgeSmall, cx - 7.5f, cy + 11.5f, 0.50f, 0.40f, 180.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 1, false, false, 110);
                        addSpec(result, cat.idGlowEdgeSmall, cx + 7.5f, cy + 11.5f, 0.50f, 0.40f, 180.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 1, false, false, 110);
                    }
                    addSpec(result, 1888, cx, cy + 13.f, 0.95f, 0.42f, 0.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 15, 0, false, false, 95);
                }
                if (leftOpen) {
                    addSpec(result, cat.idGlowEdgeSmall, cx - 18.0f, cy, 0.50f, 0.45f, 270.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 14, 0, false, false, 125);
                    if (doubleSidedNeon) {
                        addSpec(result, cat.idGlowEdgeSmall, cx - 11.5f, cy, 0.50f, 0.40f, 90.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 1, false, false, 105);
                    }
                }
                if (rightOpen) {
                    addSpec(result, cat.idGlowEdgeSmall, cx + 18.0f, cy, 0.50f, 0.45f, 90.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 14, 0, false, false, 125);
                    if (doubleSidedNeon) {
                        addSpec(result, cat.idGlowEdgeSmall, cx + 11.5f, cy, 0.50f, 0.40f, 270.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 1, false, false, 105);
                    }
                }
            }

            if (topOpen && plan.wantGrass) {
                int const grassIDs[3] = { 406, 407, 408 };
                int gID = grassIDs[(gx + gy + layoutStyle) % 3];
                addSpec(result, gID, cx - 5.f, cy + 15.8f, 0.44f, 0.46f, 0.f, LayerRole::NeonAndGrass, ColorSlot::BrightMain, 15, 1);
            }

            // Слой +1: Сильно уменьшенные, аккуратные внутренние детали
            if (isSingleCube) {
                addSpec(result, chosenEmblem, cx, cy, 0.55f, 0.55f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 6, 1);
                addSpec(result, chosenCornerPiece, cx - 8.5f, cy + 8.5f, microScale, microScale, 0.f,   LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
                addSpec(result, chosenCornerPiece, cx + 8.5f, cy + 8.5f, microScale, microScale, 90.f,  LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
                addSpec(result, chosenCornerPiece, cx - 8.5f, cy - 8.5f, microScale, microScale, 270.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
                addSpec(result, chosenCornerPiece, cx + 8.5f, cy - 8.5f, microScale, microScale, 180.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
            } else {
                if (topOpen) {
                    addSpec(result, 211, cx, cy + 10.5f, 0.94f, 0.20f, 0.f, LayerRole::InnerDetail, ColorSlot::MidTone, 4, 1);
                    addSpec(result, 211, cx, cy + 11.2f, 0.82f, 0.09f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 5, 1);
                }

                if (localCol == 0 && localRow == rows - 1) {
                    addSpec(result, chosenShine, cx - 2.f, cy + 1.f, 0.50f, 0.50f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 6, 1);
                }

                if (localCol == focalCol && localRow == focalRow) {
                    addSpec(result, chosenEmblem, cx, cy - 1.f, 0.52f, 0.52f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 6, 1);
                } else {
                    if (layoutStyle == 0) {
                        if (localCol == cols / 2) {
                            addSpec(result, 211, cx - 6.f, cy, 0.09f, 0.94f, 0.f, LayerRole::InnerDetail, ColorSlot::MidTone, 4, 1);
                            addSpec(result, 211, cx + 6.f, cy, 0.09f, 0.94f, 0.f, LayerRole::InnerDetail, ColorSlot::MidTone, 4, 1);
                            if (localRow == rows - 1) {
                                addSpec(result, cat.idBracketArch, cx, cy - 2.f, 0.52f, 0.52f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 5, 1);
                            }
                        } else {
                            addSpec(result, cat.idHollowSquare, cx, cy, microScale, microScale, 0.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
                        }
                    } else if (layoutStyle == 1) {
                        addSpec(result, chosenInnerLine, cx, cy, 0.44f, 0.44f, (localCol % 2 == 0) ? 0.f : 90.f, LayerRole::InnerDetail, ColorSlot::MidTone, 4, 1);
                        addSpec(result, cat.idSmallCross, cx - 6.5f, cy + 6.5f, microScale * 0.8f, microScale * 0.8f, 0.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
                    } else if (layoutStyle == 2) {
                        float rot = static_cast<float>(((localCol + localRow) % 4) * 90);
                        addSpec(result, cat.idArcThin,   cx, cy, 0.48f, 0.48f, rot, LayerRole::InnerDetail, ColorSlot::BrightMain, 5, 1);
                        addSpec(result, cat.idCircleDot, cx, cy, microScale * 0.75f, microScale * 0.75f, 0.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
                    } else if (layoutStyle == 3) {
                        int beamID = plan.wantChains ? cat.idChainLink : cat.idDiagThin;
                        addSpec(result, beamID, cx, cy, 0.44f, 0.44f, 0.f, LayerRole::InnerDetail, ColorSlot::MidTone, 4, 1);
                        addSpec(result, cat.idBracketCorner, cx - 7.f, cy - 7.f, microScale, microScale, 270.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
                    } else if (layoutStyle == 4) {
                        addSpec(result, cat.idParallelLines, cx, cy, 0.52f, 0.42f, 0.f, LayerRole::InnerDetail, ColorSlot::MidTone, 4, 1);
                        addSpec(result, cat.idChevron, cx, cy, microScale, microScale, (localCol < cols / 2) ? 0.f : 180.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 6, 2);
                    } else {
                        addSpec(result, 211, cx, cy, 0.34f, 0.34f, 45.f, LayerRole::InnerDetail, ColorSlot::MidTone, 4, 1);
                        addSpec(result, 211, cx, cy, 0.20f, 0.20f, 45.f, LayerRole::InnerDetail, ColorSlot::DarkFill, 5, 1);
                        addSpec(result, cat.idSmallTriangle, cx, cy, microScale * 0.8f, microScale * 0.8f, 0.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
                    }
                }

                if (densityRatio > 1.15f) {
                    float subScale = std::clamp(microScale * 0.65f, 0.14f, 0.24f);
                    addSpec(result, chosenCornerPiece, cx - 8.5f, cy + 8.5f, subScale, subScale, 0.f,   LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 3);
                    addSpec(result, chosenCornerPiece, cx + 8.5f, cy - 8.5f, subScale, subScale, 180.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 3);
                }
            }
        }
    }

    // Вложенные шипы
    for (auto const& sp : contour.spikes) {
        float const scales[3] = { 0.70f, 0.44f, 0.22f };
        ColorSlot const slots[3] = {
            ColorSlot::BrightMain,
            ColorSlot::MidTone,
            ColorSlot::HighlightOutline
        };
        for (int k = 0; k < 3; ++k) {
            addSpec(
                result, sp.objectId,
                sp.x, sp.y,
                sp.scaleX * scales[k], sp.scaleY * scales[k],
                sp.rotation,
                LayerRole::Outline3D, slots[k],
                11 + k, 0,
                sp.flipX, sp.flipY
            );
        }
        if (plan.wantNeon) {
            addSpec(
                result, 1888,
                sp.x, sp.y,
                0.95f * sp.scaleX, 0.95f * sp.scaleY,
                sp.rotation,
                LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow,
                14, 0, false, false, 105
            );
        }
    }

    std::stable_sort(result.begin(), result.end(), [](GeneratedObjectSpec const& a, GeneratedObjectSpec const& b) {
        return a.priorityTier < b.priorityTier;
    });

    int essentialCount = 0;
    for (auto const& item : result) {
        if (item.priorityTier == 0) essentialCount++;
    }

    int finalCap = (std::max)(essentialCount + 4, plan.targetCount);
    if (static_cast<int>(result.size()) > finalCap) {
        result.resize(finalCap);
    }

    return result;
}

// ============================================================================
// Спавн сгенерированных объектов в LevelEditorLayer
// ============================================================================
struct SpawnSummary {
    int createdCount = 0;
    int firstLayer = 1;
};

static SpawnSummary spawnContourDecorInEditor(
    EditorUI* ui,
    ContourAnalysis const& contour,
    std::vector<GeneratedObjectSpec> const& specs,
    StylePalette const& palette,
    AITaskMode taskMode
) {
    SpawnSummary summary;
    if (!ui || !ui->m_editorLayer) return summary;

    auto* editorLayer = ui->m_editorLayer;
    summary.firstLayer = findFirstFreeEditorLayerBlock(editorLayer, 4);

    if (taskMode == AITaskMode::FullDecor || taskMode == AITaskMode::OnlyFill) {
        for (GameObject* baseObj : contour.solidObjects) {
            if (!baseObj) continue;
            writeObjEditorLayers(baseObj, summary.firstLayer + static_cast<int>(LayerRole::Fill), 0);
            writeObjFlags(baseObj, 0, false, true);
            bindObjectToPaletteSlot(baseObj, palette, ColorSlot::DarkFill);
        }
    }
    if (taskMode == AITaskMode::FullDecor) {
        for (auto const& sp : contour.spikes) {
            if (!sp.sourceObj) continue;
            writeObjEditorLayers(sp.sourceObj, summary.firstLayer + static_cast<int>(LayerRole::Outline3D), 0);
            writeObjFlags(sp.sourceObj, 10, false, true);
            bindObjectToPaletteSlot(sp.sourceObj, palette, ColorSlot::HighlightOutline);
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
        bindObjectToPaletteSlot(obj, palette, s.colorSlot, s.customOpacity);

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
    { 0xBF, ".",  '/', '?' }, { 0xBD, "-",  '-', '_' }
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
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ' || out.back() == '\t')) {
        out.pop_back();
    }
    return out;
}

// ============================================================================
// СЕРОЕ МЕНЮ МОДА (на проверенных CCLabelBMFont + CCScale9Sprite + Popup):
// 1. Сначала окно подключения/смены API (с выбором провайдера и вставкой ключа)
// 2. После подключения — серое окно ЧАТА с кнопкой «Change API / Smenit API»
// ============================================================================
class AIDecorPopup;
static AIDecorPopup* g_activePopup = nullptr;

class AIDecorPopup : public geode::Popup {
protected:
    EditorUI* m_editorUI = nullptr;
    ContourAnalysis m_contour;
    bool m_isApiStage = false;
    bool m_russianLayout = true;
    int m_providerIdx = 0;
    std::string m_inputBuffer;
    bool m_prevKeyStates[256]{};

    CCLabelBMFont* m_inputDisplayLabel = nullptr;
    CCLabelBMFont* m_langBadgeLabel = nullptr;
    CCLabelBMFont* m_providerBtnLabel = nullptr;
    CCLabelBMFont* m_chatLine1 = nullptr;
    CCLabelBMFont* m_chatLine2 = nullptr;
    CCLabelBMFont* m_chatLine3 = nullptr;

    bool init(EditorUI* ui, ContourAnalysis contour, bool forceApiStage) {
        if (!Popup::init(440.f, 255.f)) return false;

        g_activePopup = this;
        m_editorUI = ui;
        m_contour = std::move(contour);

        loadApiConfigFromDisk();
        m_providerIdx = g_apiProviderIndex;
        m_isApiStage = forceApiStage || !g_apiConnected;
        m_russianLayout = !m_isApiStage; // В окне API по умолчанию ENG, в чате — RUS!

        if (m_isApiStage && !g_connectedApiKey.empty()) {
            m_inputBuffer = g_connectedApiKey;
        }

        if constexpr (requires { this->m_bgSprite; }) {
            if (this->m_bgSprite) {
                this->m_bgSprite->setColor(ccColor3B{ 52, 55, 62 });
            }
        }

        auto size = m_mainLayer->getContentSize();
        auto center = CCPoint{ size.width / 2.f, size.height / 2.f };

        // Полностью серый корпус меню
        auto grayOuter = CCScale9Sprite::create("square02b_001.png");
        grayOuter->setContentSize(CCSize{ size.width, size.height });
        grayOuter->setColor(ccColor3B{ 62, 66, 74 });
        grayOuter->setPosition(center);
        grayOuter->setZOrder(-2);
        m_mainLayer->addChild(grayOuter);

        auto grayInner = CCScale9Sprite::create("square02b_001.png");
        grayInner->setContentSize(CCSize{ size.width - 10.f, size.height - 10.f });
        grayInner->setColor(ccColor3B{ 44, 47, 54 });
        grayInner->setPosition(center);
        grayInner->setZOrder(-1);
        m_mainLayer->addChild(grayInner);

        if (m_isApiStage) {
            setupApiStageUI(center);
        } else {
            setupChatStageUI(center);
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

    void restoreLockedContourPositions() {
        for (auto const& st : m_contour.lockedStates) {
            if (st.obj && st.obj->getParent()) {
                st.obj->setPosition(st.pos);
                st.obj->setRotation(st.rotation);
            }
        }
    }

    void setupApiStageUI(CCPoint center) {
        this->setTitle(g_apiConnected
            ? "SMENA I NASTROYKA II PO API"
            : "PODKLYUCHENIE II PO API"
        );

        if (g_apiConnected) {
            auto backSpr = ButtonSprite::create("Back to Chat");
            backSpr->setColor(ccColor3B{ 155, 165, 180 });
            backSpr->setScale(0.50f);
            auto backBtn = CCMenuItemSpriteExtra::create(backSpr, this, menu_selector(AIDecorPopup::onBackToChatClicked));
            backBtn->setPosition(CCPoint{ center.x + 152.f, center.y + 96.f });
            m_buttonMenu->addChild(backBtn);
        }

        auto cardBg = CCScale9Sprite::create("square02b_001.png");
        cardBg->setContentSize(CCSize{ 400.f, 92.f });
        cardBg->setColor(ccColor3B{ 30, 32, 37 });
        cardBg->setPosition(CCPoint{ center.x, center.y + 28.f });
        m_mainLayer->addChild(cardBg);

        std::string statusLine = g_apiConnected
            ? fmt::format("Tekuschiy API: {} (Key: {})", kApiProviders[m_providerIdx].name, maskApiKeyPreview(g_connectedApiKey))
            : "Pered zapuskom moda podklyuchite II po API!";
        auto notice1 = CCLabelBMFont::create(statusLine.c_str(), "chatFont.fnt");
        notice1->setScale(0.60f);
        notice1->setColor(g_apiConnected ? ccColor3B{ 165, 240, 195 } : ccColor3B{ 225, 232, 245 });
        notice1->setPosition(CCPoint{ center.x, center.y + 54.f });
        m_mainLayer->addChild(notice1);

        // Кнопка переключения провайдера API (OpenAI / Gemini / DeepSeek / Groq / OpenRouter)
        auto provBg = CCScale9Sprite::create("square02b_001.png");
        provBg->setContentSize(CCSize{ 290.f, 24.f });
        provBg->setColor(ccColor3B{ 72, 78, 88 });
        std::string provText = fmt::format("API Provider: {} (Click to change)", kApiProviders[m_providerIdx].name);
        m_providerBtnLabel = CCLabelBMFont::create(provText.c_str(), "chatFont.fnt");
        m_providerBtnLabel->setScale(0.55f);
        m_providerBtnLabel->setColor(ccColor3B{ 245, 248, 255 });
        m_providerBtnLabel->setPosition(CCPoint{ 145.f, 12.f });
        provBg->addChild(m_providerBtnLabel);

        auto provBtn = CCMenuItemSpriteExtra::create(provBg, this, menu_selector(AIDecorPopup::onCycleApiProvider));
        provBtn->setPosition(CCPoint{ center.x, center.y + 24.f });
        m_buttonMenu->addChild(provBtn);

        auto notice2 = CCLabelBMFont::create(
            "Vstavte API-key (Ctrl+V ili knopkoy) i nazhmite Save / Connect API:",
            "chatFont.fnt"
        );
        notice2->setScale(0.52f);
        notice2->setColor(ccColor3B{ 170, 178, 192 });
        notice2->setPosition(CCPoint{ center.x, center.y - 4.f });
        m_mainLayer->addChild(notice2);

        // Серое поле ввода API-ключа
        auto inputBg = CCScale9Sprite::create("square02b_001.png");
        inputBg->setContentSize(CCSize{ 400.f, 32.f });
        inputBg->setColor(ccColor3B{ 22, 24, 28 });
        inputBg->setPosition(CCPoint{ center.x, center.y - 42.f });
        m_mainLayer->addChild(inputBg);

        m_inputDisplayLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_inputDisplayLabel->setScale(0.58f);
        m_inputDisplayLabel->setPosition(CCPoint{ center.x, center.y - 42.f });
        m_mainLayer->addChild(m_inputDisplayLabel);
        refreshInputLabel();

        // Кнопки управления API: [Paste Ctrl+V] [Clear] [Save / Connect API]
        auto pasteSpr = ButtonSprite::create("Paste (Ctrl+V)");
        pasteSpr->setColor(ccColor3B{ 165, 172, 185 });
        pasteSpr->setScale(0.56f);
        auto pasteBtn = CCMenuItemSpriteExtra::create(pasteSpr, this, menu_selector(AIDecorPopup::onPasteApiClicked));
        pasteBtn->setPosition(CCPoint{ center.x - 132.f, center.y - 90.f });
        m_buttonMenu->addChild(pasteBtn);

        auto clearSpr = ButtonSprite::create("Clear");
        clearSpr->setColor(ccColor3B{ 165, 172, 185 });
        clearSpr->setScale(0.56f);
        auto clearBtn = CCMenuItemSpriteExtra::create(clearSpr, this, menu_selector(AIDecorPopup::onClearApiClicked));
        clearBtn->setPosition(CCPoint{ center.x - 18.f, center.y - 90.f });
        m_buttonMenu->addChild(clearBtn);

        auto connectSpr = ButtonSprite::create(g_apiConnected ? "Save New API" : "Connect API");
        connectSpr->setColor(ccColor3B{ 195, 205, 220 });
        connectSpr->setScale(0.62f);
        auto connectBtn = CCMenuItemSpriteExtra::create(connectSpr, this, menu_selector(AIDecorPopup::onConnectApiClicked));
        connectBtn->setPosition(CCPoint{ center.x + 112.f, center.y - 90.f });
        m_buttonMenu->addChild(connectBtn);
    }

    void setupChatStageUI(CCPoint center) {
        this->setTitle("II CHAT-BOT (Right Alt - Open/Close)");

        // Кнопка смены API прямо в шапке серого окна чата!
        auto apiBg = CCScale9Sprite::create("square02b_001.png");
        apiBg->setContentSize(CCSize{ 88.f, 22.f });
        apiBg->setColor(ccColor3B{ 75, 80, 90 });
        auto apiLbl = CCLabelBMFont::create("Smenit API", "chatFont.fnt");
        apiLbl->setScale(0.52f);
        apiLbl->setColor(ccColor3B{ 235, 240, 250 });
        apiLbl->setPosition(CCPoint{ 44.f, 11.f });
        apiBg->addChild(apiLbl);

        auto changeApiBtn = CCMenuItemSpriteExtra::create(apiBg, this, menu_selector(AIDecorPopup::onOpenApiSettings));
        changeApiBtn->setPosition(CCPoint{ center.x + 104.f, center.y + 96.f });
        m_buttonMenu->addChild(changeApiBtn);

        // Кнопка переключения русской/английской раскладки [RUS / ENG]
        auto langBg = CCScale9Sprite::create("square02b_001.png");
        langBg->setContentSize(CCSize{ 50.f, 22.f });
        langBg->setColor(ccColor3B{ 75, 80, 90 });
        m_langBadgeLabel = CCLabelBMFont::create("RUS", "chatFont.fnt");
        m_langBadgeLabel->setScale(0.54f);
        m_langBadgeLabel->setColor(ccColor3B{ 240, 245, 255 });
        m_langBadgeLabel->setPosition(CCPoint{ 25.f, 11.f });
        langBg->addChild(m_langBadgeLabel);

        auto langBtn = CCMenuItemSpriteExtra::create(langBg, this, menu_selector(AIDecorPopup::onToggleLanguage));
        langBtn->setPosition(CCPoint{ center.x + 178.f, center.y + 96.f });
        m_buttonMenu->addChild(langBtn);

        // Серое окно истории сообщений чата
        auto chatAreaBg = CCScale9Sprite::create("square02b_001.png");
        chatAreaBg->setContentSize(CCSize{ 404.f, 122.f });
        chatAreaBg->setColor(ccColor3B{ 30, 32, 37 });
        chatAreaBg->setPosition(CCPoint{ center.x, center.y + 16.f });
        m_mainLayer->addChild(chatAreaBg);

        int freeLayer = (m_editorUI && m_editorUI->m_editorLayer)
            ? findFirstFreeEditorLayerBlock(m_editorUI->m_editorLayer, 4)
            : 1;

        std::string greeting1 = fmt::format(
            "II ({} API: {}): Videleno {} kletok (sloi {}-{}).",
            kApiProviders[m_providerIdx].name,
            maskApiKeyPreview(g_connectedApiKey),
            m_contour.cellCount(),
            freeLayer,
            freeLayer + 3
        );
        m_chatLine1 = CCLabelBMFont::create(greeting1.c_str(), "chatFont.fnt");
        m_chatLine1->setScale(0.56f);
        m_chatLine1->setColor(ccColor3B{ 195, 205, 220 });
        m_chatLine1->setPosition(CCPoint{ center.x, center.y + 52.f });
        m_mainLayer->addChild(m_chatLine1);

        m_chatLine2 = CCLabelBMFont::create(
            "Napishi zadachu, stil dekora i kolichestvo obyektov (ili 'smenit api'):",
            "chatFont.fnt"
        );
        m_chatLine2->setScale(0.54f);
        m_chatLine2->setColor(ccColor3B{ 170, 178, 192 });
        m_chatLine2->setPosition(CCPoint{ center.x, center.y + 18.f });
        m_mainLayer->addChild(m_chatLine2);

        m_chatLine3 = CCLabelBMFont::create(
            "Primeri: 'sdelay 3d', 'neon 90', 'adskiy stil s cherepom 120', 'glaz 80'",
            "chatFont.fnt"
        );
        m_chatLine3->setScale(0.50f);
        m_chatLine3->setColor(ccColor3B{ 140, 150, 165 });
        m_chatLine3->setPosition(CCPoint{ center.x, center.y - 16.f });
        m_mainLayer->addChild(m_chatLine3);

        // Серая строка ввода чата внизу
        auto inputBarBg = CCScale9Sprite::create("square02b_001.png");
        inputBarBg->setContentSize(CCSize{ 318.f, 34.f });
        inputBarBg->setColor(ccColor3B{ 22, 24, 28 });
        inputBarBg->setPosition(CCPoint{ center.x - 42.f, center.y - 78.f });
        m_mainLayer->addChild(inputBarBg);

        m_inputDisplayLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_inputDisplayLabel->setScale(0.56f);
        m_inputDisplayLabel->setPosition(CCPoint{ center.x - 42.f, center.y - 78.f });
        m_mainLayer->addChild(m_inputDisplayLabel);
        refreshInputLabel();

        // Серая кнопка отправки в чат
        auto sendBg = CCScale9Sprite::create("square02b_001.png");
        sendBg->setContentSize(CCSize{ 78.f, 34.f });
        sendBg->setColor(ccColor3B{ 82, 88, 98 });
        auto sendTxt = CCLabelBMFont::create("Otpravit", "chatFont.fnt");
        sendTxt->setScale(0.56f);
        sendTxt->setColor(ccColor3B{ 245, 248, 255 });
        sendTxt->setPosition(CCPoint{ 39.f, 17.f });
        sendBg->addChild(sendTxt);

        auto sendBtn = CCMenuItemSpriteExtra::create(sendBg, this, menu_selector(AIDecorPopup::onSendChatClicked));
        sendBtn->setPosition(CCPoint{ center.x + 163.f, center.y - 78.f });
        m_buttonMenu->addChild(sendBtn);
    }

    void refreshInputLabel() {
        if (!m_inputDisplayLabel) return;
        if (m_inputBuffer.empty()) {
            if (m_isApiStage) {
                m_inputDisplayLabel->setString("Paste API key (Ctrl+V) or type here...");
            } else {
                m_inputDisplayLabel->setString("Napishi zadachu II na russkom (Enter - otpravit)...");
            }
            m_inputDisplayLabel->setColor(ccColor3B{ 145, 150, 162 });
        } else {
            std::string shown = m_inputBuffer;
            if (shown.size() > 46) {
                shown = "..." + shown.substr(shown.size() - 43);
            }
            shown += "|";
            m_inputDisplayLabel->setString(shown.c_str());
            m_inputDisplayLabel->setColor(ccColor3B{ 245, 248, 255 });
        }
    }

    void onToggleLanguage(CCObject*) {
        m_russianLayout = !m_russianLayout;
        if (m_langBadgeLabel) {
            m_langBadgeLabel->setString(m_russianLayout ? "RUS" : "ENG");
        }
    }

    void reopenWithStage(bool apiStage) {
        EditorUI* ui = m_editorUI;
        ContourAnalysis contourCopy = m_contour;
        restoreLockedContourPositions();
        g_activePopup = nullptr;
        this->onClose(nullptr);
        if (auto* nextPopup = AIDecorPopup::create(ui, std::move(contourCopy), apiStage)) {
            nextPopup->show();
        }
    }

    void onOpenApiSettings(CCObject*) {
        reopenWithStage(true);
    }

    void onBackToChatClicked(CCObject*) {
        reopenWithStage(false);
    }

    void onCycleApiProvider(CCObject*) {
        m_providerIdx = (m_providerIdx + 1) % 5;
        if (m_providerBtnLabel) {
            std::string provText = fmt::format("API Provider: {} (Click to change)", kApiProviders[m_providerIdx].name);
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
                std::string provText = fmt::format("API Provider: {} (Auto-detected)", kApiProviders[m_providerIdx].name);
                m_providerBtnLabel->setString(provText.c_str());
            }
            refreshInputLabel();
        }
    }

    void onClearApiClicked(CCObject*) {
        m_inputBuffer.clear();
        refreshInputLabel();
    }

    void onConnectApiClicked(CCObject*) {
        std::string key = m_inputBuffer;
        if (key.empty()) {
            key = "ai-connected-key";
        }
        m_providerIdx = detectProviderFromKey(key, m_providerIdx);
        saveApiConfigToDisk(m_providerIdx, key);
        reopenWithStage(false);
    }

    void onSendChatClicked(CCObject*) {
        restoreLockedContourPositions();

        std::string lowCmd = toLowerASCII(m_inputBuffer);
        if (containsAny(lowCmd, { "smenit api", "pomenyat api", "izmenit api", "noviy api", "change api" }) ||
            lowCmd == "api" || lowCmd == "/api") {
            reopenWithStage(true);
            return;
        }

        std::mt19937 rng(std::random_device{}());
        ChatBotPlan plan = parseUserChatCommand(m_inputBuffer);
        EditorTabCatalog catalog = loadEditorTabCatalog(m_editorUI);
        StylePalette palette = buildPaletteFromBotPlan(plan, rng);

        auto specs = generateByAITask(m_contour, catalog, plan, rng);
        SpawnSummary summary = spawnContourDecorInEditor(m_editorUI, m_contour, specs, palette, plan.taskMode);

        if (m_chatLine1 && m_chatLine2 && m_chatLine3) {
            std::string userLine = m_inputBuffer.empty()
                ? "Vi: [Sdelat noviy sluchayniy dekor]"
                : ("Vi: " + m_inputBuffer);
            m_chatLine1->setString(userLine.c_str());
            m_chatLine1->setColor(ccColor3B{ 235, 240, 250 });

            m_chatLine2->setString(plan.botResponse.c_str());
            m_chatLine2->setColor(ccColor3B{ 165, 240, 195 });

            std::string statLine = fmt::format(
                "Gotovo: sozdano {} obyektov na sloyah {}-{}. Nazhmi Right Alt chtobi zakrit.",
                summary.createdCount, summary.firstLayer, summary.firstLayer + 3
            );
            m_chatLine3->setString(statLine.c_str());
            m_chatLine3->setColor(ccColor3B{ 200, 210, 225 });
        }

        m_inputBuffer.clear();
        refreshInputLabel();
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

        // Alt+Shift переключает раскладку RUS / ENG
        if (altDown && wasJustPressed(VK_SHIFT)) {
            onToggleLanguage(nullptr);
            return;
        }

        // Ctrl+V вставляет текст из буфера обмена Windows
        if (ctrlDown && wasJustPressed('V')) {
            if (m_isApiStage) {
                onPasteApiClicked(nullptr);
            } else {
                std::string clip = readWindowsClipboardText();
                if (!clip.empty()) {
                    m_inputBuffer += clip;
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
            if (m_isApiStage) {
                onConnectApiClicked(nullptr);
            } else {
                onSendChatClicked(nullptr);
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
                        if (m_russianLayout && !m_isApiStage) {
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

    static AIDecorPopup* create(EditorUI* ui, ContourAnalysis contour, bool forceApiStage = false) {
        auto ret = new AIDecorPopup();
        if (ret && ret->init(ui, std::move(contour), forceApiStage)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

// ============================================================================
// Открытие И ЗАКРЫТИЕ серого меню по нажатию Правого Alt (VK_RMENU)
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
