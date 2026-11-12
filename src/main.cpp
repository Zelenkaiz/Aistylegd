#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <algorithm>
#include <cmath>
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
// Слоты цветовой палитры (один тип объектов на слое = строго один оттенок)
// и роли слоев редактора (первый свободный блок из 4 слоев)
// ============================================================================
enum class ColorSlot : int {
    DarkFill = 0,         // Слой +0: глубокий темный фон внутри структуры
    Dark3D = 1,           // Слой +2: темные 3D-блоки (как на фото-примере 3D)
    MidTone = 2,          // Слой +1: внутренние рамки, скобы, дуги, узоры среднего тона
    BrightMain = 3,       // Слой +1/+3: яркий акцент (глаз, череп, блики, трава, вложенный шип)
    HighlightOutline = 4, // Слой +2: светлая четкая обводка контура и мелкие акценты
    AdditiveGlow = 5      // Слой +3: неоновое свечение (мелкий контурный неон + мягкий глоу)
};

enum class LayerRole : int {
    Fill = 0,        // firstFreeLayer + 0: Сплошное внутреннее заполнение
    InnerDetail = 1, // firstFreeLayer + 1: Внутренний декор (уменьшенные детали, арки, глаз/череп)
    Outline3D = 2,   // firstFreeLayer + 2: Белая/светлая обводка контура + 3D-блоки
    NeonAndGrass = 3 // firstFreeLayer + 3: Двусторонний мелкий неон, свечение и трава
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

struct ContourAnalysis {
    std::set<std::pair<int, int>> occupiedCells;
    std::vector<SelectedSpikeInfo> spikes;
    std::vector<GameObject*> solidObjects;
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
    int priorityTier = 1; // 0 = обязательная база/3D/неон, 1 = структура, 2 = декор, 3 = микро-детали
};

static int g_lastHueBucket = -1;
static int g_lastLayoutSeed = -1;

// ============================================================================
// Безопасные C++23 шаблоны для доступа к полям GameObject и EditorUI
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
static int readObjEditorLayer1(ObjT* obj) {
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
static void writeObjEditorLayers(ObjT* obj, int layer1, int layer2) {
    if (!obj) return;
    if constexpr (requires { obj->m_editorLayer = layer1; }) {
        obj->m_editorLayer = static_cast<decltype(obj->m_editorLayer)>(layer1);
    }
    if constexpr (requires { obj->m_editorLayer2 = layer2; }) {
        obj->m_editorLayer2 = static_cast<decltype(obj->m_editorLayer2)>(layer2);
    }
}

template <typename ObjT>
static void writeObjScale(ObjT* obj, float sx, float sy) {
    if (!obj) return;
    obj->setScaleX(sx);
    obj->setScaleY(sy);
    if constexpr (requires { obj->m_scaleX = sx; }) {
        obj->m_scaleX = sx;
    }
    if constexpr (requires { obj->m_scaleY = sy; }) {
        obj->m_scaleY = sy;
    }
    if constexpr (requires { obj->m_startScaleX = sx; }) {
        obj->m_startScaleX = sx;
    }
    if constexpr (requires { obj->m_startScaleY = sy; }) {
        obj->m_startScaleY = sy;
    }
}

template <typename ObjT>
static void writeObjFlags(ObjT* obj, int zOrder, bool noTouch, bool noGlow) {
    if (!obj) return;
    obj->setZOrder(zOrder);
    if constexpr (requires { obj->m_zOrder = zOrder; }) {
        obj->m_zOrder = static_cast<decltype(obj->m_zOrder)>(zOrder);
    }
    if constexpr (requires { obj->m_isNoTouch = noTouch; }) {
        obj->m_isNoTouch = noTouch;
    }
    if constexpr (requires { obj->m_isPassable = noTouch; }) {
        obj->m_isPassable = noTouch;
    }
    if constexpr (requires { obj->m_hasNoGlow = noGlow; }) {
        obj->m_hasNoGlow = noGlow;
    }
}

// ============================================================================
// Нативная покраска через канал 1004 (White) + m_usesHSV = true
// ============================================================================
static void rgbToHsv(ccColor3B c, float& outH, float& outS, float& outV) {
    float r = c.r / 255.f;
    float g = c.g / 255.f;
    float b = c.b / 255.f;
    float maxC = (std::max)({ r, g, b });
    float minC = (std::min)({ r, g, b });
    float delta = maxC - minC;

    outV = maxC;
    outS = (maxC > 0.0001f) ? (delta / maxC) : 0.f;
    if (delta < 0.0001f) {
        outH = 0.f;
    } else if (maxC == r) {
        outH = 60.f * std::fmod(((g - b) / delta), 6.f);
    } else if (maxC == g) {
        outH = 60.f * (((b - r) / delta) + 2.f);
    } else {
        outH = 60.f * (((r - g) / delta) + 4.f);
    }
    if (outH < 0.f) outH += 360.f;
}

template <typename SpriteColorT>
static void applyNativeHSVColorToSpriteColor(SpriteColorT* sc, ccColor3B rgb, GLubyte opacity, bool blending) {
    if (!sc) return;
    float H = 0.f, S = 0.f, V = 1.f;
    rgbToHsv(rgb, H, S, V);

    float hueSigned = (H > 180.f) ? (H - 360.f) : H;
    float alphaF = static_cast<float>(opacity) / 255.f;

    if constexpr (requires { sc->m_colorID = 1004; }) {
        sc->m_colorID = 1004;
    }
    if constexpr (requires { sc->m_defaultColorID = 1004; }) {
        sc->m_defaultColorID = 1004;
    }
    if constexpr (requires { sc->m_opacity = alphaF; }) {
        sc->m_opacity = alphaF;
    }
    if constexpr (requires { sc->m_baseOpacity = alphaF; }) {
        sc->m_baseOpacity = alphaF;
    }
    if constexpr (requires { sc->m_customColor = rgb; }) {
        sc->m_customColor = rgb;
    }
    if constexpr (requires { sc->m_usesCustomBlend = blending; }) {
        sc->m_usesCustomBlend = blending;
    }
    if constexpr (requires { sc->m_usesHSV = true; }) {
        sc->m_usesHSV = true;
    }
    if constexpr (requires { sc->m_hsv; }) {
        sc->m_hsv = cocos2d::ccHSVValue{ hueSigned, S, V - 1.0f, true, true };
    }
}

static void applyDirectSpriteVisuals(GameObject* obj, ccColor3B color, GLubyte opacity, bool additiveBlend) {
    if (!obj) return;
    obj->setColor(color);
    obj->setOpacity(opacity);
    if (additiveBlend) {
        obj->setBlendFunc(ccBlendFunc{ GL_SRC_ALPHA, GL_ONE });
    }
    if (auto* children = obj->getChildren()) {
        for (unsigned int i = 0; i < children->count(); ++i) {
            if (auto* spr = typeinfo_cast<CCSprite*>(children->objectAtIndex(i))) {
                spr->setColor(color);
                spr->setOpacity(opacity);
                if (additiveBlend) {
                    spr->setBlendFunc(ccBlendFunc{ GL_SRC_ALPHA, GL_ONE });
                }
            } else if (auto* rgba = typeinfo_cast<CCNodeRGBA*>(children->objectAtIndex(i))) {
                rgba->setColor(color);
                rgba->setOpacity(opacity);
            }
        }
    }
}

template <typename ObjT>
static void bindObjectToPaletteSlot(ObjT* obj, StylePalette const& pal, ColorSlot slot, GLubyte customOpacity = 255) {
    if (!obj) return;
    int idx = static_cast<int>(slot);
    ccColor3B col = pal.slotColors[idx];
    GLubyte op = (customOpacity < 255) ? customOpacity : pal.slotOpacity[idx];
    bool blend = pal.slotBlending[idx];

    if constexpr (requires { obj->m_baseColor; }) {
        applyNativeHSVColorToSpriteColor(obj->m_baseColor, col, op, blend);
    }
    if constexpr (requires { obj->m_detailColor; }) {
        applyNativeHSVColorToSpriteColor(obj->m_detailColor, col, op, blend);
    }

    applyDirectSpriteVisuals(obj, col, op, blend);
}

// ============================================================================
// Каталог готовых блоков из вкладок редактора (3D, Глаз, Череп, Декор, Неон)
// ============================================================================
struct EditorTabCatalog {
    // Готовые 3D-блоки из вкладки 4 (страница 2, как на скриншоте 3D)
    int id3DTrapTop    = 211; // Слот 0: скошенная 3D-трапеция сверху слева (/¯¯¯|)
    int id3DHalfStrip  = 211; // Слот 1: прямая 3D-полоса (30x15 сверху или 15x30 справа)
    int id3DMedRect    = 211; // Слот 2: средний 3D-блок
    int id3DSmallSq    = 211; // Слот 3: малый 3D-квадрат (15x15)
    int id3DLCorner    = 211; // Слот 4: угловой 3D-блок
    int id3DTriCorner  = 211; // Слот 5: верхне-левый 3D-треугольник 15x15
    int id3DTriBR      = 211; // Слот 6: нижне-правый 3D-треугольник 15x15 (для угла!)
    int id3DBrickTrap  = 211; // Слот 9: кирпичная 3D-трапеция
    int id3DBrickStrip = 211; // Слот 10: кирпичная 3D-полоса

    // Готовые эмблемы из вкладки 9 (страница 10)
    int idBone        = 211;
    int idLeaf        = 211;
    int idChainLink   = 211;
    int idHeart       = 211;
    int idCrossCircle = 211;
    int idHashtag     = 211;
    int idLightning   = 211;
    int idGhost       = 211;
    int idSkull       = 211;
    int idKey         = 211;
    int idEye         = 211;
    int idSwirl       = 211;

    // Готовые геометрические детали из вкладки 10 (страница 8)
    int idDiagDouble1    = 211;
    int idDiagDouble2    = 211;
    int idDiagThick      = 211;
    int idDiagThin       = 211;
    int idHollowSquare   = 211;
    int idParallelLines  = 211;
    int idBracketCorner  = 211;
    int idBracketArch    = 211;
    int idBracketT       = 211;
    int idBracketCross   = 211;
    int idSlashTickSmall = 211;
    int idSlashTickWide  = 211;

    // Готовые тонкие линии, дуги, точки, крестики из вкладки 10 (страница 10)
    int idDoubleDots    = 211;
    int idThinLineLong  = 211;
    int idGradLine      = 211; // Слот 2: градиентная неоновая полоса!
    int idThinLineShort = 211;
    int idArcThin       = 211;
    int idCircleDot     = 211;
    int idSmallCross    = 211;
    int idSmallTriangle = 211;
    int idShortDash     = 211;
    int idChevron       = 211;
    int idArcThickLarge = 211;
    int idArcThickSmall = 211;

    // Настоящие объекты неона/свечения (найденные на странице с 1888 + градиент)
    int idGlowCircle    = 1888;
    int idGlowEdgeSmall = 1888;
    int idGlowEdgeFull  = 1888;
};

template <typename EditorUIT>
static int readButtonObjectIDFromTab(EditorUIT* ui, int tabIndex, int pageIndex, int slotIndex, int fallbackID) {
    if (!ui) return fallbackID;
    if constexpr (requires { ui->m_createButtonBars; }) {
        CCArray* bars = ui->m_createButtonBars;
        if (!bars || tabIndex < 0 || tabIndex >= static_cast<int>(bars->count())) return fallbackID;
        auto* bar = static_cast<EditButtonBar*>(bars->objectAtIndex(tabIndex));
        if (!bar) return fallbackID;
        if constexpr (requires { bar->m_buttonArray; }) {
            CCArray* btns = bar->m_buttonArray;
            if (!btns || btns->count() == 0) return fallbackID;
            int idx = pageIndex * 12 + slotIndex;
            if (idx < 0 || idx >= static_cast<int>(btns->count())) return fallbackID;
            auto* item = static_cast<CreateMenuItem*>(btns->objectAtIndex(idx));
            if (!item) return fallbackID;
            if constexpr (requires { item->m_objectID; }) {
                int objID = static_cast<int>(item->m_objectID);
                if (objID > 0) return objID;
            }
        }
    }
    return fallbackID;
}

template <typename EditorUIT>
static void findRealGlowObjectsInTab(EditorUIT* ui, EditorTabCatalog& cat) {
    cat.idGlowCircle = 1888;
    cat.idGlowEdgeSmall = cat.idGradLine;
    cat.idGlowEdgeFull = 1888;
    if (!ui) return;

    if constexpr (requires { ui->m_createButtonBars; }) {
        CCArray* bars = ui->m_createButtonBars;
        if (!bars || bars->count() <= 10) return;
        auto* bar = static_cast<EditButtonBar*>(bars->objectAtIndex(10));
        if (!bar) return;
        if constexpr (requires { bar->m_buttonArray; }) {
            CCArray* btns = bar->m_buttonArray;
            if (!btns) return;
            // Ищем страницу, где находится настоящий глоу 1888, чтобы взять соседний мелкий краевой неон!
            for (unsigned int i = 0; i < btns->count(); ++i) {
                auto* item = static_cast<CreateMenuItem*>(btns->objectAtIndex(i));
                if (!item) continue;
                if constexpr (requires { item->m_objectID; }) {
                    if (item->m_objectID == 1888) {
                        int pageStart = (static_cast<int>(i) / 12) * 12;
                        if (pageStart + 1 < static_cast<int>(btns->count())) {
                            auto* edgeItem = static_cast<CreateMenuItem*>(btns->objectAtIndex(pageStart + 1));
                            if (edgeItem && edgeItem->m_objectID > 0) {
                                cat.idGlowEdgeFull = edgeItem->m_objectID;
                            }
                        }
                        if (pageStart + 4 < static_cast<int>(btns->count())) {
                            auto* smallItem = static_cast<CreateMenuItem*>(btns->objectAtIndex(pageStart + 4));
                            if (smallItem && smallItem->m_objectID > 0) {
                                cat.idGlowEdgeSmall = smallItem->m_objectID;
                            }
                        }
                        return;
                    }
                }
            }
        }
    }
}

static EditorTabCatalog loadEditorTabCatalog(EditorUI* ui) {
    EditorTabCatalog cat;
    if (!ui) return cat;

    // Вкладка 4 (3D), страница 2 (ровно те блоки, из которых построен 3D на скриншоте!)
    cat.id3DTrapTop    = readButtonObjectIDFromTab(ui, 4, 2, 0, 211);
    cat.id3DHalfStrip  = readButtonObjectIDFromTab(ui, 4, 2, 1, 211);
    cat.id3DMedRect    = readButtonObjectIDFromTab(ui, 4, 2, 2, 211);
    cat.id3DSmallSq    = readButtonObjectIDFromTab(ui, 4, 2, 3, 211);
    cat.id3DLCorner    = readButtonObjectIDFromTab(ui, 4, 2, 4, 211);
    cat.id3DTriCorner  = readButtonObjectIDFromTab(ui, 4, 2, 5, 211);
    cat.id3DTriBR      = readButtonObjectIDFromTab(ui, 4, 2, 6, 211);
    cat.id3DBrickTrap  = readButtonObjectIDFromTab(ui, 4, 2, 9, 211);
    cat.id3DBrickStrip = readButtonObjectIDFromTab(ui, 4, 2, 10, 211);

    // Вкладка 9 (Эмблемы), страница 10
    cat.idBone        = readButtonObjectIDFromTab(ui, 9, 10, 0, 211);
    cat.idLeaf        = readButtonObjectIDFromTab(ui, 9, 10, 1, 211);
    cat.idChainLink   = readButtonObjectIDFromTab(ui, 9, 10, 2, 211);
    cat.idHeart       = readButtonObjectIDFromTab(ui, 9, 10, 3, 211);
    cat.idCrossCircle = readButtonObjectIDFromTab(ui, 9, 10, 4, 211);
    cat.idHashtag     = readButtonObjectIDFromTab(ui, 9, 10, 5, 211);
    cat.idLightning   = readButtonObjectIDFromTab(ui, 9, 10, 6, 211);
    cat.idGhost       = readButtonObjectIDFromTab(ui, 9, 10, 7, 211);
    cat.idSkull       = readButtonObjectIDFromTab(ui, 9, 10, 8, 211);
    cat.idKey         = readButtonObjectIDFromTab(ui, 9, 10, 9, 211);
    cat.idEye         = readButtonObjectIDFromTab(ui, 9, 10, 10, 211);
    cat.idSwirl       = readButtonObjectIDFromTab(ui, 9, 10, 11, 211);

    // Вкладка 10 (Детали), страница 8
    cat.idDiagDouble1    = readButtonObjectIDFromTab(ui, 10, 8, 0, 211);
    cat.idDiagDouble2    = readButtonObjectIDFromTab(ui, 10, 8, 1, 211);
    cat.idDiagThick      = readButtonObjectIDFromTab(ui, 10, 8, 2, 211);
    cat.idDiagThin       = readButtonObjectIDFromTab(ui, 10, 8, 3, 211);
    cat.idHollowSquare   = readButtonObjectIDFromTab(ui, 10, 8, 4, 211);
    cat.idParallelLines  = readButtonObjectIDFromTab(ui, 10, 8, 5, 211);
    cat.idBracketCorner  = readButtonObjectIDFromTab(ui, 10, 8, 6, 211);
    cat.idBracketArch    = readButtonObjectIDFromTab(ui, 10, 8, 7, 211);
    cat.idBracketT       = readButtonObjectIDFromTab(ui, 10, 8, 8, 211);
    cat.idBracketCross   = readButtonObjectIDFromTab(ui, 10, 8, 9, 211);
    cat.idSlashTickSmall = readButtonObjectIDFromTab(ui, 10, 8, 10, 211);
    cat.idSlashTickWide  = readButtonObjectIDFromTab(ui, 10, 8, 11, 211);

    // Вкладка 10 (Тонкие линии, градиент, дуги, точки), страница 10
    cat.idDoubleDots    = readButtonObjectIDFromTab(ui, 10, 10, 0, 211);
    cat.idThinLineLong  = readButtonObjectIDFromTab(ui, 10, 10, 1, 211);
    cat.idGradLine      = readButtonObjectIDFromTab(ui, 10, 10, 2, 1888);
    cat.idThinLineShort = readButtonObjectIDFromTab(ui, 10, 10, 3, 211);
    cat.idArcThin       = readButtonObjectIDFromTab(ui, 10, 10, 4, 211);
    cat.idCircleDot     = readButtonObjectIDFromTab(ui, 10, 10, 5, 211);
    cat.idSmallCross    = readButtonObjectIDFromTab(ui, 10, 10, 6, 211);
    cat.idSmallTriangle = readButtonObjectIDFromTab(ui, 10, 10, 7, 211);
    cat.idShortDash     = readButtonObjectIDFromTab(ui, 10, 10, 8, 211);
    cat.idChevron       = readButtonObjectIDFromTab(ui, 10, 10, 9, 211);
    cat.idArcThickLarge = readButtonObjectIDFromTab(ui, 10, 10, 10, 211);
    cat.idArcThickSmall = readButtonObjectIDFromTab(ui, 10, 10, 11, 211);

    // Находим точные объекты глоу на странице, где лежит 1888
    findRealGlowObjectsInTab(ui, cat);

    return cat;
}

// ============================================================================
// Поиск первого свободного блока из 4 слоев (Editor Layer)
// ============================================================================
template <typename EditorLayerT>
static int findFirstFreeEditorLayerBlock(EditorLayerT* editorLayer, int neededConsecutive = 4) {
    std::set<int> usedLayers;
    if (editorLayer) {
        if constexpr (requires { editorLayer->m_objects; }) {
            CCArray* allObjs = editorLayer->m_objects;
            if (allObjs) {
                for (unsigned int i = 0; i < allObjs->count(); ++i) {
                    auto* o = static_cast<GameObject*>(allObjs->objectAtIndex(i));
                    if (!o) continue;
                    int l1 = readObjEditorLayer1(o);
                    int l2 = readObjEditorLayer2(o);
                    if (l1 > 0) usedLayers.insert(l1);
                    if (l2 > 0) usedLayers.insert(l2);
                }
            }
        }
    }

    for (int candidate = 1; candidate < 990; ++candidate) {
        bool allFree = true;
        for (int k = 0; k < neededConsecutive; ++k) {
            if (usedLayers.count(candidate + k) > 0) {
                allFree = false;
                break;
            }
        }
        if (allFree) return candidate;
    }
    return 1;
}

// ============================================================================
// Разбор команд ИИ Чат-Боту и генерация нового цвета при каждом запуске
// ============================================================================
struct ChatBotPlan {
    bool hasExplicitColor = false;
    ccColor3B explicitColor{ 115, 255, 25 };
    bool wantEye = false;
    bool wantSkull = false;
    bool wantChains = false;
    bool wantArcs = false;
    bool wantBricks = false;
    bool wantGrass = true;
    bool want3D = true;
    bool wantNeon = true;
    std::string botReply;
};

static std::string toLowerASCII(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    }
    return s;
}

static ChatBotPlan parseChatBotMessage(std::string const& rawPrompt) {
    ChatBotPlan plan;
    std::string p = toLowerASCII(rawPrompt);

    if (p.find("green") != std::string::npos || p.find("зелен") != std::string::npos || p.find("лайм") != std::string::npos) {
        plan.hasExplicitColor = true;
        plan.explicitColor = ccColor3B{ 105, 255, 25 };
    } else if (p.find("cyan") != std::string::npos || p.find("голуб") != std::string::npos || p.find("бирюз") != std::string::npos) {
        plan.hasExplicitColor = true;
        plan.explicitColor = ccColor3B{ 25, 245, 255 };
    } else if (p.find("blue") != std::string::npos || p.find("син") != std::string::npos) {
        plan.hasExplicitColor = true;
        plan.explicitColor = ccColor3B{ 55, 130, 255 };
    } else if (p.find("purple") != std::string::npos || p.find("фиолет") != std::string::npos || p.find("пурпур") != std::string::npos) {
        plan.hasExplicitColor = true;
        plan.explicitColor = ccColor3B{ 175, 55, 255 };
    } else if (p.find("pink") != std::string::npos || p.find("розов") != std::string::npos) {
        plan.hasExplicitColor = true;
        plan.explicitColor = ccColor3B{ 255, 45, 185 };
    } else if (p.find("red") != std::string::npos || p.find("красн") != std::string::npos || p.find("адск") != std::string::npos || p.find("hell") != std::string::npos) {
        plan.hasExplicitColor = true;
        plan.explicitColor = ccColor3B{ 255, 40, 25 };
    } else if (p.find("orange") != std::string::npos || p.find("оранж") != std::string::npos) {
        plan.hasExplicitColor = true;
        plan.explicitColor = ccColor3B{ 255, 140, 20 };
    } else if (p.find("yellow") != std::string::npos || p.find("желт") != std::string::npos || p.find("золот") != std::string::npos) {
        plan.hasExplicitColor = true;
        plan.explicitColor = ccColor3B{ 255, 230, 30 };
    }

    if (p.find("eye") != std::string::npos || p.find("глаз") != std::string::npos) plan.wantEye = true;
    if (p.find("skull") != std::string::npos || p.find("череп") != std::string::npos || p.find("hell") != std::string::npos) plan.wantSkull = true;
    if (p.find("chain") != std::string::npos || p.find("цеп") != std::string::npos) plan.wantChains = true;
    if (p.find("arc") != std::string::npos || p.find("дуг") != std::string::npos || p.find("круг") != std::string::npos) plan.wantArcs = true;
    if (p.find("brick") != std::string::npos || p.find("кирпич") != std::string::npos) plan.wantBricks = true;
    if (p.find("no grass") != std::string::npos || p.find("без трав") != std::string::npos) plan.wantGrass = false;
    if (p.find("no 3d") != std::string::npos || p.find("без 3д") != std::string::npos) plan.want3D = false;
    if (p.find("no neon") != std::string::npos || p.find("без неон") != std::string::npos) plan.wantNeon = false;

    if (rawPrompt.empty()) {
        plan.botReply = "AI Bot: Full structure fill + new random color & unique pattern + 3D + 2-sided neon!";
    } else {
        plan.botReply = fmt::format("AI Bot: Building custom request ('{}')!", rawPrompt.substr(0, 36));
    }

    return plan;
}

static ccColor3B scaleRGB(ccColor3B c, float factor) {
    return ccColor3B{
        static_cast<GLubyte>(std::clamp(std::round(c.r * factor), 0.f, 255.f)),
        static_cast<GLubyte>(std::clamp(std::round(c.g * factor), 0.f, 255.f)),
        static_cast<GLubyte>(std::clamp(std::round(c.b * factor), 0.f, 255.f))
    };
}

static ccColor3B mixTowardWhite(ccColor3B c, float whiteMix) {
    return ccColor3B{
        static_cast<GLubyte>(std::clamp(std::round(c.r + (255.f - c.r) * whiteMix), 0.f, 255.f)),
        static_cast<GLubyte>(std::clamp(std::round(c.g + (255.f - c.g) * whiteMix), 0.f, 255.f)),
        static_cast<GLubyte>(std::clamp(std::round(c.b + (255.f - c.b) * whiteMix), 0.f, 255.f))
    };
}

static StylePalette buildPaletteFromBotPlan(ChatBotPlan const& plan, std::mt19937& rng) {
    StylePalette pal;

    ccColor3B const vibrantPool[12] = {
        { 105, 255,  25 }, // Лаймово-зеленый неон
        {  25, 245, 255 }, // Электрический циан
        { 255,  40, 180 }, // Неоновая фуксия
        { 170,  55, 255 }, // Фиолетовый лазер
        { 255, 225,  25 }, // Кибер-золотой
        {  35, 255, 145 }, // Изумрудно-мятный
        { 255,  95,  20 }, // Плазменно-оранжевый
        {  55, 130, 255 }, // Кобальтово-синий
        { 255,  38,  32 }, // Алый неон
        { 195, 255,  35 }, // Кислотно-шартрез
        {  95, 210, 255 }, // Ледяной голубой
        { 255,  75, 125 }  // Неоново-коралловый
    };

    if (plan.hasExplicitColor) {
        pal.masterColor = plan.explicitColor;
    } else {
        std::uniform_int_distribution<int> dist(0, 11);
        int idx = dist(rng);
        if (idx == g_lastHueBucket) {
            idx = (idx + 1 + (dist(rng) % 11)) % 12;
        }
        g_lastHueBucket = idx;
        pal.masterColor = vibrantPool[idx];
    }

    // 6 четких ступеней одного цвета:
    // DarkFill — очень темный фон внутри структуры;
    // Dark3D — еще более глубокий почти черный тон для 3D-блоков (как на фото 3D!);
    // MidTone — насыщенный средний тон;
    // BrightMain — чистый яркий неон;
    // HighlightOutline — светлая неоновая обводка контура;
    // AdditiveGlow — мягкое аддитивное свечение.
    pal.slotColors[static_cast<int>(ColorSlot::DarkFill)] = ccColor3B{
        static_cast<GLubyte>(std::clamp(pal.masterColor.r * 0.14f + 5.f, 6.f, 42.f)),
        static_cast<GLubyte>(std::clamp(pal.masterColor.g * 0.14f + 5.f, 6.f, 42.f)),
        static_cast<GLubyte>(std::clamp(pal.masterColor.b * 0.14f + 5.f, 6.f, 42.f))
    };
    pal.slotColors[static_cast<int>(ColorSlot::Dark3D)] = ccColor3B{
        static_cast<GLubyte>(std::clamp(pal.masterColor.r * 0.06f + 2.f, 2.f, 18.f)),
        static_cast<GLubyte>(std::clamp(pal.masterColor.g * 0.06f + 2.f, 2.f, 18.f)),
        static_cast<GLubyte>(std::clamp(pal.masterColor.b * 0.06f + 2.f, 2.f, 18.f))
    };
    pal.slotColors[static_cast<int>(ColorSlot::MidTone)]          = scaleRGB(pal.masterColor, 0.62f);
    pal.slotColors[static_cast<int>(ColorSlot::BrightMain)]       = pal.masterColor;
    pal.slotColors[static_cast<int>(ColorSlot::HighlightOutline)] = mixTowardWhite(pal.masterColor, 0.62f);
    pal.slotColors[static_cast<int>(ColorSlot::AdditiveGlow)]     = pal.masterColor;

    pal.slotOpacity[static_cast<int>(ColorSlot::DarkFill)]         = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::Dark3D)]           = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::MidTone)]          = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::BrightMain)]       = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::HighlightOutline)] = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::AdditiveGlow)]     = 130;

    for (int i = 0; i < 6; ++i) {
        pal.slotBlending[i] = (i == static_cast<int>(ColorSlot::AdditiveGlow));
    }

    return pal;
}

// ============================================================================
// ПОЛНОЕ заполнение выделенных структур (даже если выделен только П-контур!)
// ============================================================================
static bool isSpikeOrHazardObjectID(int id) {
    switch (id) {
        case 8: case 9: case 39: case 103:
        case 133: case 134: case 135: case 136:
        case 144: case 145: case 177: case 178: case 179:
        case 205: case 216: case 217: case 218:
        case 392: case 421: case 422: case 458: case 459:
            return true;
        default:
            return false;
    }
}

struct StructureIsland {
    std::set<std::pair<int, int>> cells;
    int minGX = 0, maxGX = 0;
    int minGY = 0, maxGY = 0;
    int cols() const { return maxGX - minGX + 1; }
    int rows() const { return maxGY - minGY + 1; }
};

static std::vector<StructureIsland> extractAndFillIslandsCompletely(std::set<std::pair<int, int>>& allCells) {
    std::vector<StructureIsland> islands;
    std::set<std::pair<int, int>> visited;

    // Группируем клетки, находящиеся в пределах одной структуры (включая соседство через 1 клетку)
    for (auto const& startCell : allCells) {
        if (visited.count(startCell)) continue;

        StructureIsland isl;
        isl.minGX = isl.maxGX = startCell.first;
        isl.minGY = isl.maxGY = startCell.second;

        std::queue<std::pair<int, int>> q;
        q.push(startCell);
        visited.insert(startCell);

        while (!q.empty()) {
            auto [gx, gy] = q.front();
            q.pop();
            isl.cells.insert({ gx, gy });
            isl.minGX = (std::min)(isl.minGX, gx);
            isl.maxGX = (std::max)(isl.maxGX, gx);
            isl.minGY = (std::min)(isl.minGY, gy);
            isl.maxGY = (std::max)(isl.maxGY, gy);

            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    if (dx == 0 && dy == 0) continue;
                    std::pair<int, int> nb{ gx + dx, gy + dy };
                    if (allCells.count(nb) && !visited.count(nb)) {
                        visited.insert(nb);
                        q.push(nb);
                    }
                }
            }
        }

        // 1. Горизонтальная заливка каждой строки между левой и правой стенкой острова
        for (int gy = isl.minGY; gy <= isl.maxGY; ++gy) {
            int rowMinX = 1000000, rowMaxX = -1000000;
            for (auto const& [cx, cy] : isl.cells) {
                if (cy == gy) {
                    rowMinX = (std::min)(rowMinX, cx);
                    rowMaxX = (std::max)(rowMaxX, cx);
                }
            }
            if (rowMinX <= rowMaxX) {
                for (int gx = rowMinX; gx <= rowMaxX; ++gx) {
                    isl.cells.insert({ gx, gy });
                    allCells.insert({ gx, gy });
                }
            }
        }

        // 2. Вертикальная заливка под крышей до основания структуры (чтобы П-контур на земле заполнялся целиком!)
        for (int gx = isl.minGX; gx <= isl.maxGX; ++gx) {
            int colTopY = -1000000;
            for (auto const& [cx, cy] : isl.cells) {
                if (cx == gx) {
                    colTopY = (std::max)(colTopY, cy);
                }
            }
            if (colTopY >= isl.minGY) {
                for (int gy = isl.minGY; gy <= colTopY; ++gy) {
                    isl.cells.insert({ gx, gy });
                    allCells.insert({ gx, gy });
                }
            }
        }

        islands.push_back(std::move(isl));
    }

    return islands;
}

static ContourAnalysis analyzeSelectedContour(EditorUI* ui) {
    ContourAnalysis info;
    if (!ui) return info;

    std::vector<GameObject*> rawList;
    if (ui->m_selectedObjects && ui->m_selectedObjects->count() > 0) {
        for (unsigned int i = 0; i < ui->m_selectedObjects->count(); ++i) {
            if (auto* o = static_cast<GameObject*>(ui->m_selectedObjects->objectAtIndex(i))) {
                rawList.push_back(o);
            }
        }
    } else if (ui->m_selectedObject) {
        rawList.push_back(ui->m_selectedObject);
    }

    if (rawList.empty()) {
        if (ui->m_editorLayer && ui->m_editorLayer->m_objectLayer) {
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            CCPoint worldCenter = ui->m_editorLayer->m_objectLayer->convertToNodeSpace(
                CCPoint{ winSize.width / 2.f, winSize.height / 2.f }
            );
            int baseGX = static_cast<int>(std::floor(worldCenter.x / 30.f));
            int baseGY = static_cast<int>(std::floor(worldCenter.y / 30.f));
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = 0; dy <= 1; ++dy) {
                    info.occupiedCells.insert({ baseGX + dx, baseGY + dy });
                }
            }
        }
    } else {
        for (GameObject* obj : rawList) {
            int id = readObjID(obj);
            CCPoint pos = obj->getPosition();

            if (isSpikeOrHazardObjectID(id)) {
                SelectedSpikeInfo sp;
                sp.x = pos.x;
                sp.y = pos.y;
                sp.rotation = obj->getRotation();
                sp.scaleX = obj->getScaleX();
                sp.scaleY = obj->getScaleY();
                sp.flipX = obj->isFlipX();
                sp.flipY = obj->isFlipY();
                sp.objectId = (id > 0) ? id : 8;
                sp.sourceObj = obj;
                info.spikes.push_back(sp);
            } else {
                int gx = static_cast<int>(std::floor((pos.x + 0.1f) / 30.f));
                int gy = static_cast<int>(std::floor((pos.y + 0.1f) / 30.f));
                info.occupiedCells.insert({ gx, gy });
                info.solidObjects.push_back(obj);
            }
        }
        // Заполняем все пустоты внутри выделенного контура!
        extractAndFillIslandsCompletely(info.occupiedCells);
    }

    info.minX = 1e9f;
    info.minY = 1e9f;
    info.maxX = -1e9f;
    info.maxY = -1e9f;

    for (auto const& [gx, gy] : info.occupiedCells) {
        float cx = gx * 30.f + 15.f;
        float cy = gy * 30.f + 15.f;
        info.minX = (std::min)(info.minX, cx - 15.f);
        info.minY = (std::min)(info.minY, cy - 15.f);
        info.maxX = (std::max)(info.maxX, cx + 15.f);
        info.maxY = (std::max)(info.maxY, cy + 15.f);
    }
    for (auto const& sp : info.spikes) {
        info.minX = (std::min)(info.minX, sp.x - 15.f);
        info.minY = (std::min)(info.minY, sp.y - 15.f);
        info.maxX = (std::max)(info.maxX, sp.x + 15.f);
        info.maxY = (std::max)(info.maxY, sp.y + 15.f);
    }

    if (info.minX > info.maxX) {
        info.minX = 0.f; info.maxX = 30.f;
        info.minY = 0.f; info.maxY = 30.f;
    }

    return info;
}

// ============================================================================
// Процедурный генератор:
// 1. Сплошное заполнение контура без дыр
// 2. Точный 3D из готовых блоков (как на скриншоте 3D!)
// 3. Настоящий неон (включая двусторонний мелкий неон вдоль контура)
// 4. Аккуратные сильно уменьшенные внутренние детали, каждый раз НОВЫЙ узор!
// ============================================================================
static void addSpec(
    std::vector<GeneratedObjectSpec>& out,
    int objectId,
    float x, float y,
    float sx, float sy,
    float rot,
    LayerRole role,
    ColorSlot slot,
    int zOrder,
    int priorityTier = 1,
    bool flipX = false,
    bool flipY = false,
    GLubyte opacity = 255
) {
    GeneratedObjectSpec s;
    s.objectId = (objectId > 0) ? objectId : 211;
    s.x = x;
    s.y = y;
    s.scaleX = sx;
    s.scaleY = sy;
    s.rotation = rot;
    s.flipX = flipX;
    s.flipY = flipY;
    s.layerRole = role;
    s.colorSlot = slot;
    s.zOrder = zOrder;
    s.priorityTier = priorityTier;
    s.customOpacity = opacity;
    s.noTouch = true;
    s.noGlow = true;
    out.push_back(s);
}

static std::vector<GeneratedObjectSpec> generateUniqueStructureDecor(
    ContourAnalysis& contour,
    EditorTabCatalog const& cat,
    ChatBotPlan const& plan,
    int targetCount,
    std::mt19937& rng
) {
    std::vector<GeneratedObjectSpec> result;

    auto islands = extractAndFillIslandsCompletely(contour.occupiedCells);
    int totalCells = (std::max)(1, contour.cellCount());

    // Чем больше блоков запросил игрок, тем сильнее уменьшаем внутренние детали (от 0.42f до 0.18f),
    // чтобы они никогда не налезали друг на друга и выглядели аккуратно!
    float densityRatio = std::clamp(static_cast<float>(targetCount) / (totalCells * 8.f), 0.5f, 3.0f);
    float microScale = std::clamp(0.42f / std::sqrt(densityRatio), 0.18f, 0.42f);

    // Гарантируем новую уникальную композицию при каждом нажатии Generate
    std::uniform_int_distribution<int> layoutDist(0, 5);
    int layoutStyle = layoutDist(rng);
    if (layoutStyle == g_lastLayoutSeed) {
        layoutStyle = (layoutStyle + 1 + (layoutDist(rng) % 5)) % 6;
    }
    g_lastLayoutSeed = layoutStyle;

    // Выбираем 1 главный тип эмблемы и 2-3 аккуратных геометрических элемента на эту генерацию
    // (чтобы мод НЕ ставил все подряд!)
    int const emblemChoices[8] = {
        cat.idEye, cat.idSkull, cat.idSwirl, cat.idHeart,
        cat.idLightning, cat.idKey, cat.idGhost, cat.idCrossCircle
    };
    int chosenEmblem = emblemChoices[rng() % 8];
    if (plan.wantEye)   chosenEmblem = cat.idEye;
    if (plan.wantSkull) chosenEmblem = cat.idSkull;

    int const cornerChoices[4] = {
        cat.idHollowSquare, cat.idBracketCorner, cat.idSmallCross, cat.idDoubleDots
    };
    int chosenCornerPiece = cornerChoices[rng() % 4];

    int const lineChoices[4] = {
        cat.idParallelLines, cat.idBracketArch, cat.idArcThin, cat.idBracketT
    };
    int chosenInnerLine = lineChoices[rng() % 4];
    if (plan.wantArcs) chosenInnerLine = cat.idArcThickSmall;

    int const shineChoices[3] = {
        cat.idDiagDouble1, cat.idDiagDouble2, cat.idDiagThin
    };
    int chosenShine = shineChoices[rng() % 3];

    // Двусторонний мелкий неон (внутри и снаружи контура) включается процедурно
    bool doubleSidedNeon = (rng() % 2 == 0);

    for (auto const& isl : islands) {
        int cols = isl.cols();
        int rows = isl.rows();
        bool isSingleCube = (isl.cells.size() == 1);

        // Случайная позиция для единственной главной эмблемы (в углу или по центру структуры)
        int focalCol = (cols >= 2 && (rng() % 2 == 0)) ? (cols - 1) : (cols / 2);
        int focalRow = (rows >= 2 && (rng() % 2 == 0)) ? (rows - 1) : (rows / 2);

        for (auto const& [gx, gy] : isl.cells) {
            float cx = gx * 30.f + 15.f;
            float cy = gy * 30.f + 15.f;

            bool topOpen    = (contour.occupiedCells.count({ gx, gy + 1 }) == 0);
            bool bottomOpen = (contour.occupiedCells.count({ gx, gy - 1 }) == 0);
            bool leftOpen   = (contour.occupiedCells.count({ gx - 1, gy }) == 0);
            bool rightOpen  = (contour.occupiedCells.count({ gx + 1, gy }) == 0);

            int localCol = gx - isl.minGX;
            int localRow = gy - isl.minGY;

            // ================================================================
            // 1. Слой +0 (Fill): Сплошное темное заполнение ВСЕХ клеток структуры
            // ================================================================
            addSpec(result, 211, cx, cy, 1.00f, 1.00f, 0.f, LayerRole::Fill, ColorSlot::DarkFill, 1, 0);

            // Внутренний темный рельеф (варьируется от генерации к генерации)
            if (layoutStyle % 2 == 0) {
                addSpec(result, 211, cx, cy, 0.86f, 0.86f, 0.f, LayerRole::Fill, ColorSlot::MidTone, 2, 1, false, false, 45);
                addSpec(result, 211, cx, cy, 0.78f, 0.78f, 0.f, LayerRole::Fill, ColorSlot::DarkFill, 3, 1);
            } else {
                addSpec(result, 211, cx, cy, 0.88f, 0.22f, 0.f, LayerRole::Fill, ColorSlot::MidTone, 2, 1, false, false, 40);
            }

            // ================================================================
            // 2. Слой +2 (Outline3D): Четкая светлая обводка + ТОЧНЫЙ 3D по фото!
            // ================================================================
            if (topOpen) {
                addSpec(result, 211, cx, cy + 14.2f, 1.02f, 0.06f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11, 0);
            }
            if (bottomOpen && gy > 0) {
                addSpec(result, 211, cx, cy - 14.2f, 1.02f, 0.06f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11, 0);
            }
            if (leftOpen) {
                addSpec(result, 211, cx - 14.2f, cy, 0.06f, 1.02f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11, 0);
            }
            if (rightOpen) {
                addSpec(result, 211, cx + 14.2f, cy, 0.06f, 1.02f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11, 0);
            }

            // ТОЧНЫЙ 3D из готовых блоков вкладки 4 (страница 2) — ровно как на фотоimage_29bf02.png!
            if (plan.want3D) {
                // Сверху структуры: в левой верхней клетке — скошенная трапеция (Slot 0),
                // во всех остальных верхних клетках — прямая 3D-полоса (Slot 1) на высоте cy + 22.5f!
                if (topOpen) {
                    int top3DBlock = leftOpen ? cat.id3DTrapTop : cat.id3DHalfStrip;
                    addSpec(result, top3DBlock, cx, cy + 22.5f, 1.00f, 1.00f, 0.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 0);
                }
                // Справа структуры: прямая 3D-полоса (Slot 1), повернутая на 90°, на позиции cx + 22.5f!
                if (rightOpen) {
                    addSpec(result, cat.id3DHalfStrip, cx + 22.5f, cy, 1.00f, 1.00f, 90.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 0);
                }
                // Правый верхний угол 15x15 (cx + 22.5f, cy + 22.5f): треугольник 3D (Slot 6),
                // соединяющий верхнюю и правую 3D-полосы под 45° ровно как на скриншоте!
                if (topOpen && rightOpen) {
                    addSpec(result, cat.id3DTriBR, cx + 22.5f, cy + 22.5f, 1.00f, 1.00f, 0.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 0);
                }
            }

            // ================================================================
            // 3. Слой +3 (NeonAndGrass): Настоящий мелкий и мягкий неон (внутри и снаружи)
            // ================================================================
            if (plan.wantNeon) {
                if (topOpen) {
                    // Внешний мелкий неон вдоль верхней границы
                    addSpec(result, cat.idGlowEdgeSmall, cx - 7.5f, cy + 18.0f, 0.50f, 0.45f, 0.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 14, 0, false, false, 135);
                    addSpec(result, cat.idGlowEdgeSmall, cx + 7.5f, cy + 18.0f, 0.50f, 0.45f, 0.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 14, 0, false, false, 135);
                    // Внутренний мелкий неон (развернутый внутрь блока!)
                    if (doubleSidedNeon) {
                        addSpec(result, cat.idGlowEdgeSmall, cx - 7.5f, cy + 11.5f, 0.50f, 0.40f, 180.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 1, false, false, 115);
                        addSpec(result, cat.idGlowEdgeSmall, cx + 7.5f, cy + 11.5f, 0.50f, 0.40f, 180.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 1, false, false, 115);
                    }
                    // Мягкое неоновое свечение 1888 вдоль шапки
                    addSpec(result, 1888, cx, cy + 13.f, 0.95f, 0.42f, 0.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 15, 0, false, false, 95);
                }
                if (leftOpen) {
                    addSpec(result, cat.idGlowEdgeSmall, cx - 18.0f, cy, 0.50f, 0.45f, 270.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 14, 0, false, false, 125);
                    if (doubleSidedNeon) {
                        addSpec(result, cat.idGlowEdgeSmall, cx - 11.5f, cy, 0.50f, 0.40f, 90.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 1, false, false, 105);
                    }
                    addSpec(result, 1888, cx - 13.f, cy, 0.42f, 0.95f, 0.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 15, 1, false, false, 85);
                }
                if (rightOpen) {
                    addSpec(result, cat.idGlowEdgeSmall, cx + 18.0f, cy, 0.50f, 0.45f, 90.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 14, 0, false, false, 125);
                    if (doubleSidedNeon) {
                        addSpec(result, cat.idGlowEdgeSmall, cx + 11.5f, cy, 0.50f, 0.40f, 270.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 1, false, false, 105);
                    }
                    addSpec(result, 1888, cx + 13.f, cy, 0.42f, 0.95f, 0.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 15, 1, false, false, 85);
                }
                if (bottomOpen && gy > 0) {
                    addSpec(result, cat.idGlowEdgeSmall, cx, cy - 18.0f, 0.80f, 0.45f, 180.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 14, 1, false, false, 120);
                }
            }

            // Аккуратная уменьшенная трава на верхней грани
            if (topOpen && plan.wantGrass) {
                int const grassIDs[3] = { 406, 407, 408 };
                int gID = grassIDs[(gx + gy + layoutStyle) % 3];
                addSpec(result, gID, cx - 5.f, cy + 15.8f, 0.45f, 0.48f, 0.f, LayerRole::NeonAndGrass, ColorSlot::BrightMain, 15, 1);
                if ((gx + layoutStyle) % 2 == 0) {
                    addSpec(result, gID, cx + 6.f, cy + 15.5f, 0.36f, 0.38f, 0.f, LayerRole::NeonAndGrass, ColorSlot::BrightMain, 15, 2, true, false);
                }
            }

            // ================================================================
            // 4. Слой +1 (InnerDetail): Сильно уменьшенные, чистые детали (6 разных схем!)
            // ================================================================
            if (isSingleCube) {
                // В одиночном кубе 1x1: один аккуратный глаз/череп в центре (0.58x) + 4 крошечных уголка (0.28x)
                addSpec(result, chosenEmblem, cx, cy, 0.58f, 0.58f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 6, 1);
                addSpec(result, chosenCornerPiece, cx - 8.5f, cy + 8.5f, microScale, microScale, 0.f,   LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
                addSpec(result, chosenCornerPiece, cx + 8.5f, cy + 8.5f, microScale, microScale, 90.f,  LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
                addSpec(result, chosenCornerPiece, cx - 8.5f, cy - 8.5f, microScale, microScale, 270.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
                addSpec(result, chosenCornerPiece, cx + 8.5f, cy - 8.5f, microScale, microScale, 180.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
            } else {
                // Верхняя светящаяся полоса вдоль крыши
                if (topOpen) {
                    addSpec(result, 211, cx, cy + 10.5f, 0.94f, 0.22f, 0.f, LayerRole::InnerDetail, ColorSlot::MidTone, 4, 1);
                    addSpec(result, 211, cx, cy + 11.2f, 0.82f, 0.10f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 5, 1);
                }

                // Диагональный блик // только в левой верхней клетке (уменьшенный, чтобы не вылезал!)
                if (localCol == 0 && localRow == rows - 1) {
                    addSpec(result, chosenShine, cx - 2.f, cy + 1.f, 0.52f, 0.52f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 6, 1);
                }

                // Единственная фокусная эмблема (глаз/череп/вихрь) в фокусной клетке острова
                if (localCol == focalCol && localRow == focalRow) {
                    addSpec(result, chosenEmblem, cx, cy - 1.f, 0.54f, 0.54f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 6, 1);
                } else {
                    // В остальных клетках строим чистый геометрический узор выбранного стиля (layoutStyle 0..5)
                    if (layoutStyle == 0) {
                        // Стиль 0: Чистые вертикальные неоновые колонны + арка в центре + мелкие квадратики 回
                        if (localCol == cols / 2) {
                            addSpec(result, 211, cx - 6.f, cy, 0.10f, 0.94f, 0.f, LayerRole::InnerDetail, ColorSlot::MidTone, 4, 1);
                            addSpec(result, 211, cx + 6.f, cy, 0.10f, 0.94f, 0.f, LayerRole::InnerDetail, ColorSlot::MidTone, 4, 1);
                            if (localRow == rows - 1) {
                                addSpec(result, cat.idBracketArch, cx, cy - 2.f, 0.55f, 0.55f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 5, 1);
                            }
                        } else {
                            addSpec(result, cat.idHollowSquare, cx, cy, microScale, microScale, 0.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
                        }
                    }
                    else if (layoutStyle == 1) {
                        // Стиль 1: Минималистичная тех-сетка с сильно уменьшенными крестиками × и точками •.
                        addSpec(result, chosenInnerLine, cx, cy, 0.48f, 0.48f, (localCol % 2 == 0) ? 0.f : 90.f, LayerRole::InnerDetail, ColorSlot::MidTone, 4, 1);
                        addSpec(result, cat.idSmallCross, cx - 6.5f, cy + 6.5f, microScale * 0.85f, microScale * 0.85f, 0.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
                        addSpec(result, cat.idDoubleDots, cx + 6.5f, cy - 6.5f, microScale * 0.85f, microScale * 0.85f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 6, 2);
                    }
                    else if (layoutStyle == 2) {
                        // Стиль 2: Концентрические тонкие дуги ╭ и светящиеся точки ●
                        float rot = static_cast<float>(((localCol + localRow) % 4) * 90);
                        addSpec(result, cat.idArcThin,   cx, cy, 0.52f, 0.52f, rot, LayerRole::InnerDetail, ColorSlot::BrightMain, 5, 1);
                        addSpec(result, cat.idCircleDot, cx, cy, microScale * 0.8f, microScale * 0.8f, 0.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
                    }
                    else if (layoutStyle == 3) {
                        // Стиль 3: Внутренние цепи или диагональные микро-лучи с уголками ╔
                        int beamID = plan.wantChains ? cat.idChainLink : cat.idDiagThin;
                        addSpec(result, beamID, cx, cy, 0.46f, 0.46f, 0.f, LayerRole::InnerDetail, ColorSlot::MidTone, 4, 1);
                        addSpec(result, cat.idBracketCorner, cx - 7.f, cy - 7.f, microScale, microScale, 270.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
                        addSpec(result, cat.idBracketCorner, cx + 7.f, cy + 7.f, microScale, microScale, 90.f,  LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
                    }
                    else if (layoutStyle == 4) {
                        // Стиль 4: Горизонтальные технологические уровни с шевронами > и тире ▬
                        addSpec(result, cat.idParallelLines, cx, cy, 0.58f, 0.45f, 0.f, LayerRole::InnerDetail, ColorSlot::MidTone, 4, 1);
                        addSpec(result, cat.idChevron, cx, cy, microScale, microScale, (localCol < cols / 2) ? 0.f : 180.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 6, 2);
                    }
                    else {
                        // Стиль 5: Внутренние ромбы и микро-треугольники ▲
                        addSpec(result, 211, cx, cy, 0.36f, 0.36f, 45.f, LayerRole::InnerDetail, ColorSlot::MidTone, 4, 1);
                        addSpec(result, 211, cx, cy, 0.22f, 0.22f, 45.f, LayerRole::InnerDetail, ColorSlot::DarkFill, 5, 1);
                        addSpec(result, cat.idSmallTriangle, cx, cy, microScale * 0.85f, microScale * 0.85f, 0.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
                    }
                }

                // Если запрошено много блоков — добавляем сильно уменьшенные (microScale * 0.7f) суб-детали по сетке 4x4 внутри клетки!
                if (densityRatio > 1.15f) {
                    float subScale = std::clamp(microScale * 0.68f, 0.15f, 0.26f);
                    addSpec(result, chosenCornerPiece, cx - 8.5f, cy + 8.5f, subScale, subScale, 0.f,   LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 3);
                    addSpec(result, chosenCornerPiece, cx + 8.5f, cy - 8.5f, subScale, subScale, 180.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 3);
                    if (densityRatio > 1.6f) {
                        addSpec(result, cat.idShortDash,  cx,        cy - 9.5f, subScale, subScale, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 6, 3);
                        addSpec(result, cat.idDoubleDots, cx + 8.5f, cy + 8.5f, subScale, subScale, 0.f, LayerRole::InnerDetail, ColorSlot::MidTone,    6, 3);
                    }
                }
            }
        }
    }

    // ========================================================================
    // 5. Декорирование выделенных шипов (вложенные шипы + неоновое свечение)
    // ========================================================================
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

    // Сортируем по важности (Tier 0 = база, заполнение, 3D и неон никогда не обрезаются!)
    std::stable_sort(result.begin(), result.end(), [](GeneratedObjectSpec const& a, GeneratedObjectSpec const& b) {
        return a.priorityTier < b.priorityTier;
    });

    int essentialCount = 0;
    for (auto const& item : result) {
        if (item.priorityTier == 0) essentialCount++;
    }

    int finalCap = (std::max)(essentialCount + 6, targetCount);
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
    StylePalette const& palette
) {
    SpawnSummary summary;
    if (!ui || !ui->m_editorLayer) return summary;

    auto* editorLayer = ui->m_editorLayer;
    summary.firstLayer = findFirstFreeEditorLayerBlock(editorLayer, 4);

    for (GameObject* baseObj : contour.solidObjects) {
        if (!baseObj) continue;
        writeObjEditorLayers(baseObj, summary.firstLayer + static_cast<int>(LayerRole::Fill), 0);
        writeObjFlags(baseObj, 0, false, true);
        bindObjectToPaletteSlot(baseObj, palette, ColorSlot::DarkFill);
    }
    for (auto const& sp : contour.spikes) {
        if (!sp.sourceObj) continue;
        writeObjEditorLayers(sp.sourceObj, summary.firstLayer + static_cast<int>(LayerRole::Outline3D), 0);
        writeObjFlags(sp.sourceObj, 10, false, true);
        bindObjectToPaletteSlot(sp.sourceObj, palette, ColorSlot::HighlightOutline);
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
// Новое меню с ИИ Чат-Ботом СЛЕВА (без кнопок Neon/Modern/Hell/Reality)
// ============================================================================
class AIDecorPopup : public geode::Popup {
protected:
    EditorUI* m_editorUI = nullptr;
    ContourAnalysis m_contour;
    int m_targetBlockCount = 80;

    TextInput* m_promptInput = nullptr;
    TextInput* m_countInput = nullptr;
    CCLabelBMFont* m_botReplyLabel = nullptr;

    bool init(EditorUI* ui, ContourAnalysis contour) {
        if (!Popup::init(440.f, 260.f)) return false;

        m_editorUI = ui;
        m_contour = std::move(contour);
        m_targetBlockCount = static_cast<int>(Mod::get()->getSettingValue<int64_t>("default-block-count"));

        this->setTitle("AI Builder Chat-Bot");

        auto size = m_mainLayer->getContentSize();
        auto center = CCPoint{ size.width / 2.f, size.height / 2.f };

        // ====================================================================
        // ЛЕВАЯ ПАНЕЛЬ: ИИ Чат-Бот (игрок сам пишет, что он хочет построить)
        // ====================================================================
        float leftX = center.x - 75.f;

        auto botHeader = CCLabelBMFont::create("AI Chat-Bot (write what you want):", "goldFont.fnt");
        botHeader->setScale(0.45f);
        botHeader->setPosition(CCPoint{ leftX, center.y + 78.f });
        m_mainLayer->addChild(botHeader);

        auto chatBoxBg = CCScale9Sprite::create("square02b_001.png");
        chatBoxBg->setContentSize(CCSize{ 250.f, 95.f });
        chatBoxBg->setColor(ccColor3B{ 18, 24, 38 });
        chatBoxBg->setOpacity(210);
        chatBoxBg->setPosition(CCPoint{ leftX, center.y + 18.f });
        m_mainLayer->addChild(chatBoxBg);

        auto hint1 = CCLabelBMFont::create("Bot: Tell me what to build inside your contour!", "chatFont.fnt");
        hint1->setScale(0.55f);
        hint1->setColor(ccColor3B{ 130, 255, 180 });
        hint1->setPosition(CCPoint{ leftX, center.y + 46.f });
        m_mainLayer->addChild(hint1);

        auto hint2 = CCLabelBMFont::create("Examples: 'green eye 3d', 'purple skull chains',", "chatFont.fnt");
        hint2->setScale(0.48f);
        hint2->setColor(ccColor3B{ 200, 220, 255 });
        hint2->setPosition(CCPoint{ leftX, center.y + 22.f });
        m_mainLayer->addChild(hint2);

        auto hint3 = CCLabelBMFont::create("'cyan arcs', 'red hell', or leave empty for NEW random!", "chatFont.fnt");
        hint3->setScale(0.46f);
        hint3->setColor(ccColor3B{ 200, 220, 255 });
        hint3->setPosition(CCPoint{ leftX, center.y + 2.f });
        m_mainLayer->addChild(hint3);

        m_botReplyLabel = CCLabelBMFont::create("Status: Ready for your command!", "chatFont.fnt");
        m_botReplyLabel->setScale(0.50f);
        m_botReplyLabel->setColor(ccColor3B{ 255, 240, 130 });
        m_botReplyLabel->setPosition(CCPoint{ leftX, center.y - 18.f });
        m_mainLayer->addChild(m_botReplyLabel);

        m_promptInput = TextInput::create(250.f, "Write to AI Bot (e.g. green neon eye 3d)...", "chatFont.fnt");
        m_promptInput->setMaxCharCount(240);
        m_promptInput->setPosition(CCPoint{ leftX, center.y - 54.f });
        m_mainLayer->addChild(m_promptInput);

        // ====================================================================
        // ПРАВАЯ ПАНЕЛЬ: Инфо о контуре, слоях и количестве деталей
        // ====================================================================
        float rightX = center.x + 130.f;
        int freeLayer = (ui && ui->m_editorLayer) ? findFirstFreeEditorLayerBlock(ui->m_editorLayer, 4) : 1;

        auto infoHeader = CCLabelBMFont::create("Contour & Layers", "bigFont.fnt");
        infoHeader->setScale(0.36f);
        infoHeader->setPosition(CCPoint{ rightX, center.y + 78.f });
        m_mainLayer->addChild(infoHeader);

        std::string cellsText = fmt::format("Filled Cells: {}", m_contour.cellCount());
        auto cellsLbl = CCLabelBMFont::create(cellsText.c_str(), "chatFont.fnt");
        cellsLbl->setScale(0.58f);
        cellsLbl->setColor(ccColor3B{ 160, 255, 200 });
        cellsLbl->setPosition(CCPoint{ rightX, center.y + 52.f });
        m_mainLayer->addChild(cellsLbl);

        std::string layersText = fmt::format("Auto Layers: {}-{}", freeLayer, freeLayer + 3);
        auto layersLbl = CCLabelBMFont::create(layersText.c_str(), "chatFont.fnt");
        layersLbl->setScale(0.55f);
        layersLbl->setColor(ccColor3B{ 180, 220, 255 });
        layersLbl->setPosition(CCPoint{ rightX, center.y + 32.f });
        m_mainLayer->addChild(layersLbl);

        auto countTitle = CCLabelBMFont::create("Block Count:", "bigFont.fnt");
        countTitle->setScale(0.34f);
        countTitle->setPosition(CCPoint{ rightX, center.y + 4.f });
        m_mainLayer->addChild(countTitle);

        auto minusSpr = CCSprite::createWithSpriteFrameName("edit_leftBtn_001.png");
        minusSpr->setScale(0.75f);
        auto minusBtn = CCMenuItemSpriteExtra::create(minusSpr, this, menu_selector(AIDecorPopup::onAdjustCount));
        minusBtn->setTag(-20);
        minusBtn->setPosition(CCPoint{ rightX - 48.f, center.y - 24.f });
        m_buttonMenu->addChild(minusBtn);

        m_countInput = TextInput::create(55.f, "80", "bigFont.fnt");
        m_countInput->setCommonFilter(CommonFilter::Uint);
        m_countInput->setString(std::to_string(m_targetBlockCount));
        m_countInput->setPosition(CCPoint{ rightX, center.y - 24.f });
        m_mainLayer->addChild(m_countInput);

        auto plusSpr = CCSprite::createWithSpriteFrameName("edit_rightBtn_001.png");
        plusSpr->setScale(0.75f);
        auto plusBtn = CCMenuItemSpriteExtra::create(plusSpr, this, menu_selector(AIDecorPopup::onAdjustCount));
        plusBtn->setTag(20);
        plusBtn->setPosition(CCPoint{ rightX + 48.f, center.y - 24.f });
        m_buttonMenu->addChild(plusBtn);

        auto scaleNote = CCLabelBMFont::create("(Higher count = smaller micro-details)", "chatFont.fnt");
        scaleNote->setScale(0.42f);
        scaleNote->setColor(ccColor3B{ 180, 180, 200 });
        scaleNote->setPosition(CCPoint{ rightX, center.y - 52.f });
        m_mainLayer->addChild(scaleNote);

        // Кнопка генерации внизу
        auto genSpr = ButtonSprite::create("Build Structure");
        genSpr->setScale(0.75f);
        auto genBtn = CCMenuItemSpriteExtra::create(genSpr, this, menu_selector(AIDecorPopup::onGenerate));
        genBtn->setPosition(CCPoint{ center.x, center.y - 98.f });
        m_buttonMenu->addChild(genBtn);

        return true;
    }

    void onAdjustCount(CCObject* sender) {
        int delta = static_cast<CCNode*>(sender)->getTag();
        int current = numFromString<int>(m_countInput->getString()).unwrapOr(m_targetBlockCount);
        m_targetBlockCount = std::clamp(current + delta, 15, 350);
        m_countInput->setString(std::to_string(m_targetBlockCount));
    }

    void onGenerate(CCObject*) {
        m_targetBlockCount = std::clamp(
            numFromString<int>(m_countInput->getString()).unwrapOr(80),
            15, 350
        );
        std::string userPrompt = m_promptInput ? std::string(m_promptInput->getString()) : "";

        std::mt19937 rng(std::random_device{}());
        ChatBotPlan plan = parseChatBotMessage(userPrompt);
        EditorTabCatalog catalog = loadEditorTabCatalog(m_editorUI);
        StylePalette palette = buildPaletteFromBotPlan(plan, rng);

        auto specs = generateUniqueStructureDecor(m_contour, catalog, plan, m_targetBlockCount, rng);
        SpawnSummary summary = spawnContourDecorInEditor(m_editorUI, m_contour, specs, palette);

        Notification::create(
            fmt::format(
                "AI Bot built {} blocks (Layers {}-{})!",
                summary.createdCount,
                summary.firstLayer,
                summary.firstLayer + 3
            ),
            NotificationIcon::Success
        )->show();

        this->onClose(nullptr);
    }

public:
    static AIDecorPopup* create(EditorUI* ui, ContourAnalysis contour) {
        auto ret = new AIDecorPopup();
        if (ret && ret->init(ui, std::move(contour))) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

// ============================================================================
// Перехват клавиши Right Alt в редакторе уровней (EditorUI)
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
            openAIDecorMenu();
        } else if (!isDown) {
            m_fields->m_rightAltWasDown = false;
        }
#endif
    }

    void openAIDecorMenu() {
        auto* scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;
        if (scene->getChildByID("ai-decor-popup"_spr)) return;

        ContourAnalysis contour = analyzeSelectedContour(this);
        if (auto* popup = AIDecorPopup::create(this, std::move(contour))) {
            popup->setID("ai-decor-popup"_spr);
            popup->show();
        }
    }
};
