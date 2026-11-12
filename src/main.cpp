#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/Popup.hpp>
#include <algorithm>
#include <cctype>
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
static bool g_apiConnectedSession = false;

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
// Чтение готовых блоков из вкладок редактора (3D, Глаз, Череп, Декор, Неон)
// ============================================================================
struct EditorTabCatalog {
    // Готовые 3D-блоки из вкладки 4 (страница 2, как на скриншоте 3D)
    int id3DTrapTop    = 211; // Слот 0: скошенная 3D-трапеция сверху слева (/¯¯¯|)
    int id3DHalfStrip  = 211; // Слот 1: прямая 3D-полоса (30x15 сверху и 15x30 справа)
    int id3DMedRect    = 211; // Слот 2: средний 3D-блок
    int id3DSmallSq    = 211; // Слот 3: малый 3D-квадрат (15x15)
    int id3DLCorner    = 211; // Слот 4: угловой 3D-блок
    int id3DTriCorner  = 211; // Слот 5: верхне-левый 3D-треугольник 15x15 (|‾/)
    int id3DTriBR      = 211; // Слот 6: нижне-правый 3D-треугольник 15x15 (/_|)
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

    // Готовые тонкие линии, градиент, дуги, точки, крестики из вкладки 10 (страница 10)
    int idDoubleDots    = 211;
    int idThinLineLong  = 211;
    int idGradLine      = 1888;
    int idThinLineShort = 211;
    int idArcThin       = 211;
    int idCircleDot     = 211;
    int idSmallCross    = 211;
    int idSmallTriangle = 211;
    int idShortDash     = 211;
    int idChevron       = 211;
    int idArcThickLarge = 211;
    int idArcThickSmall = 211;

    // Настоящие объекты неона
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

    // Вкладка 4 (3D), страница 2 (точные блоки 3D со скриншота!)
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

    findRealGlowObjectsInTab(ui, cat);
    return cat;
}

// ============================================================================
// Автоматический поиск первого свободного блока из 4 слоев редактора
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
// Умный ИИ-анализатор русских и английских команд игрока в чате
// ============================================================================
struct ChatBotPlan {
    AITaskMode taskMode = AITaskMode::FullDecor;
    int targetCount = 80;
    bool hasExplicitColor = false;
    ccColor3B explicitColor{ 115, 255, 25 };
    bool wantEye = false;
    bool wantSkull = false;
    bool wantChains = false;
    bool wantArcs = false;
    bool wantGrass = true;
    bool want3D = true;
    bool wantNeon = true;
    std::string botResponseRU;
};

static std::string toLowerASCII(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
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
    // Убираем "3д" и "3d", чтобы цифра 3 из "3д" не считалась количеством объектов!
    for (size_t pos = 0; (pos = s.find("3d", pos)) != std::string::npos;) {
        s.replace(pos, 2, "  ");
    }
    for (size_t pos = 0; (pos = s.find("3\xD0\xB4", pos)) != std::string::npos;) { // "3д"
        s.replace(pos, 3, "   ");
    }
    for (size_t pos = 0; (pos = s.find("3\xD0\x94", pos)) != std::string::npos;) { // "3Д"
        s.replace(pos, 3, "   ");
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

    bool mentions3D = containsAny(p, { "3d", "3д", "3Д", "объем", "объём", "трид", "3l" });
    bool mentionsNeon = containsAny(p, { "неон", "НЕОН", "neon", "glow", "глоу", "свеч", "свет", "ytjy" });
    bool mentionsFill = containsAny(p, { "заполн", "залей", "залив", "фон", "fill" });
    bool mentionsDecorOrStyle = containsAny(p, {
        "декор", "ДЕКОР", "decor", "стил", "style", "модерн", "modern",
        "адск", "hell", "реали", "real", "глаз", "eye", "череп", "skull",
        "арк", "arch", "дуг", "arc", "цеп", "chain", "узор", "структур",
        "построй", "украс", "рандом", "random", "нов"
    });

    // 1. Точное определение задачи ИИ:
    // Если игрок просит сделать 3D (например "сделай 3д", "3д", "добавь 3д") и НЕ просит полный декор/стиль —
    // ИИ строит СТРОГО ТОЛЬКО 3D из готовых 3D-блоков!
    if (mentions3D && !mentionsDecorOrStyle && !mentionsNeon && !mentionsFill && extractedCount <= 0) {
        plan.taskMode = AITaskMode::Only3D;
        plan.botResponseRU = "ИИ: Понял задачу! Строю строго 3D-объем и контур из твоих 3D-блоков без лишнего декора.";
        return plan;
    }

    // Если игрок просит только неон/свечение
    if (mentionsNeon && !mentionsDecorOrStyle && !mentions3D && !mentionsFill && extractedCount <= 0) {
        plan.taskMode = AITaskMode::OnlyNeon;
        plan.botResponseRU = "ИИ: Понял задачу! Добавляю только двусторонний контурный неон и мягкое свечение.";
        return plan;
    }

    // Если игрок просит только залить/заполнить структуру
    if (mentionsFill && !mentionsDecorOrStyle && !mentions3D && !mentionsNeon) {
        plan.taskMode = AITaskMode::OnlyFill;
        plan.botResponseRU = "ИИ: Понял задачу! Полностью заливаю внутреннюю область выделенной структуры.";
        return plan;
    }

    // Иначе — полноценный декор по стилю и количеству объектов из сообщения
    plan.taskMode = AITaskMode::FullDecor;

    // Цвета (русский, английский и раскладка)
    if (containsAny(p, { "зелен", "ЗЕЛЕН", "лайм", "green", "lime", "ptkty" })) {
        plan.hasExplicitColor = true;
        plan.explicitColor = ccColor3B{ 105, 255, 25 };
    } else if (containsAny(p, { "голуб", "ГОЛУБ", "бирюз", "циан", "cyan", "ujke," })) {
        plan.hasExplicitColor = true;
        plan.explicitColor = ccColor3B{ 25, 245, 255 };
    } else if (containsAny(p, { "син", "СИН", "blue", "cby" })) {
        plan.hasExplicitColor = true;
        plan.explicitColor = ccColor3B{ 55, 130, 255 };
    } else if (containsAny(p, { "фиолет", "ФИОЛЕТ", "пурпур", "purple", "violet", "abjktn" })) {
        plan.hasExplicitColor = true;
        plan.explicitColor = ccColor3B{ 175, 55, 255 };
    } else if (containsAny(p, { "розов", "РОЗОВ", "фукси", "pink", "magenta", "hjpjd" })) {
        plan.hasExplicitColor = true;
        plan.explicitColor = ccColor3B{ 255, 45, 185 };
    } else if (containsAny(p, { "красн", "КРАСН", "алый", "адск", "hell", "red", "rhfcy" })) {
        plan.hasExplicitColor = true;
        plan.explicitColor = ccColor3B{ 255, 40, 25 };
    } else if (containsAny(p, { "оранж", "ОРАНЖ", "orange", "jhfy;" })) {
        plan.hasExplicitColor = true;
        plan.explicitColor = ccColor3B{ 255, 140, 20 };
    } else if (containsAny(p, { "желт", "ЖЕЛТ", "золот", "yellow", "gold", ";tkn" })) {
        plan.hasExplicitColor = true;
        plan.explicitColor = ccColor3B{ 255, 230, 30 };
    }

    if (containsAny(p, { "глаз", "ГЛАЗ", "eye", "ukfp" })) plan.wantEye = true;
    if (containsAny(p, { "череп", "ЧЕРЕП", "адск", "skull", "hell", "xthtg" })) plan.wantSkull = true;
    if (containsAny(p, { "цеп", "ЦЕП", "chain" })) plan.wantChains = true;
    if (containsAny(p, { "дуг", "ДУГ", "круг", "arc" })) plan.wantArcs = true;
    if (containsAny(p, { "модерн", "МОДЕРН", "modern", "без трав", "no grass" })) plan.wantGrass = false;
    if (containsAny(p, { "без 3д", "no 3d" })) plan.want3D = false;
    if (containsAny(p, { "без неон", "no neon" })) plan.wantNeon = false;

    plan.botResponseRU = fmt::format(
        "ИИ: Строю новую уникальную структуру (~{} объектов, полный контур + 3D + неон)!",
        plan.targetCount
    );
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

    pal.slotColors[static_cast<int>(ColorSlot::DarkFill)] = ccColor3B{
        static_cast<GLubyte>(std::clamp(pal.masterColor.r * 0.14f + 5.f, 6.f, 42.f)),
        static_cast<GLubyte>(std::clamp(pal.masterColor.g * 0.14f + 5.f, 6.f, 42.f)),
        static_cast<GLubyte>(std::clamp(pal.masterColor.b * 0.14f + 5.f, 6.f, 42.f))
    };
    // Для 3D-блоков делаем глубокий черный цвет (как на эталонном фото 3D image_29bf02.png!)
    pal.slotColors[static_cast<int>(ColorSlot::Dark3D)] = ccColor3B{ 4, 5, 7 };
    pal.slotColors[static_cast<int>(ColorSlot::MidTone)]          = scaleRGB(pal.masterColor, 0.62f);
    pal.slotColors[static_cast<int>(ColorSlot::BrightMain)]       = pal.masterColor;
    pal.slotColors[static_cast<int>(ColorSlot::HighlightOutline)] = (plan.taskMode == AITaskMode::Only3D)
        ? ccColor3B{ 255, 255, 255 }
        : mixTowardWhite(pal.masterColor, 0.62f);
    pal.slotColors[static_cast<int>(ColorSlot::AdditiveGlow)]     = pal.masterColor;

    pal.slotOpacity[static_cast<int>(ColorSlot::DarkFill)]         = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::Dark3D)]           = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::MidTone)]          = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::BrightMain)]       = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::HighlightOutline)] = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::AdditiveGlow)]     = 135;

    for (int i = 0; i < 6; ++i) {
        pal.slotBlending[i] = (i == static_cast<int>(ColorSlot::AdditiveGlow));
    }

    return pal;
}

// ============================================================================
// Считывание и ПОЛНОЕ заполнение выделенного контура
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
            info.lockedStates.push_back(LockedObjectState{ obj, pos, obj->getRotation() });

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
// Построение объектов строго по задаче ИИ (Only3D / OnlyNeon / OnlyFill / FullDecor)
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

static std::vector<GeneratedObjectSpec> generateByAITask(
    ContourAnalysis& contour,
    EditorTabCatalog const& cat,
    ChatBotPlan const& plan,
    std::mt19937& rng
) {
    std::vector<GeneratedObjectSpec> result;
    auto islands = extractAndFillIslandsCompletely(contour.occupiedCells);
    int totalCells = (std::max)(1, contour.cellCount());

    // 1. РЕЖИМ "ТОЛЬКО 3D" (строит 1-в-1 эталонный черный 3D-объем и белую обводку со скриншота image_29bf02.png!)
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
                // Верхний правый угол 15x15 (cx + 22.5f, cy + 22.5f): два треугольника (Slot 5 + Slot 6),
                // образующие квадратный угол с диагональным швом ровно как на фото image_29bf02.png!
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
        layoutStyle = (layoutStyle + 1 + (layoutDist(rng) % 5)) % 6;
    }
    g_lastLayoutSeed = layoutStyle;

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

    bool doubleSidedNeon = (rng() % 2 == 0);

    for (auto const& isl : islands) {
        int cols = isl.cols();
        int rows = isl.rows();
        bool isSingleCube = (isl.cells.size() == 1);

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

            // Слой +0: Полная заливка клетки
            addSpec(result, 211, cx, cy, 1.00f, 1.00f, 0.f, LayerRole::Fill, ColorSlot::DarkFill, 1, 0);

            if (layoutStyle % 2 == 0) {
                addSpec(result, 211, cx, cy, 0.86f, 0.86f, 0.f, LayerRole::Fill, ColorSlot::MidTone, 2, 1, false, false, 42);
                addSpec(result, 211, cx, cy, 0.78f, 0.78f, 0.f, LayerRole::Fill, ColorSlot::DarkFill, 3, 1);
            }

            // Слой +2: Четкая обводка + точный 3D из готовых 3D-блоков (как на image_29bf02.png)
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
                        addSpec(result, cat.idGlowEdgeSmall, cx - 7.5f, cy + 11.5f, 0.50f, 0.40f, 180.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 1, false, false, 115);
                        addSpec(result, cat.idGlowEdgeSmall, cx + 7.5f, cy + 11.5f, 0.50f, 0.40f, 180.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 1, false, false, 115);
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

            // Слой +1: Сильно уменьшенные, аккуратные внутренние детали (без нагромождения!)
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
// Русский + Английский ввод текста (ЙЦУКЕН / QWERTY + Ctrl+V + Backspace UTF-8)
// ============================================================================
static void popLastUTF8Char(std::string& s) {
    if (s.empty()) return;
    while (!s.empty() && ((static_cast<unsigned char>(s.back()) & 0xC0) == 0x80)) {
        s.pop_back();
    }
    if (!s.empty()) {
        s.pop_back();
    }
}

struct RusKeyMapEntry {
    int vk;
    const char* rusLower;
    char engLower;
    char engShift;
};

static constexpr RusKeyMapEntry kKeyTable[] = {
    { 'Q', "й", 'q', 'Q' }, { 'W', "ц", 'w', 'W' }, { 'E', "у", 'e', 'E' },
    { 'R', "к", 'r', 'R' }, { 'T', "е", 't', 'T' }, { 'Y', "н", 'y', 'Y' },
    { 'U', "г", 'u', 'U' }, { 'I', "ш", 'i', 'I' }, { 'O', "щ", 'o', 'O' },
    { 'P', "з", 'p', 'P' }, { 0xDB, "х", '[', '{' }, { 0xDD, "ъ", ']', '}' },
    { 'A', "ф", 'a', 'A' }, { 'S', "ы", 's', 'S' }, { 'D', "в", 'd', 'D' },
    { 'F', "а", 'f', 'F' }, { 'G', "п", 'g', 'G' }, { 'H', "р", 'h', 'H' },
    { 'J', "о", 'j', 'J' }, { 'K', "л", 'k', 'K' }, { 'L', "д", 'l', 'L' },
    { 0xBA, "ж", ';', ':' }, { 0xDE, "э", '\'', '"' },
    { 'Z', "я", 'z', 'Z' }, { 'X', "ч", 'x', 'X' }, { 'C', "с", 'c', 'C' },
    { 'V', "м", 'v', 'V' }, { 'B', "и", 'b', 'B' }, { 'N', "т", 'n', 'N' },
    { 'M', "ь", 'm', 'M' }, { 0xBC, "б", ',', '<' }, { 0xBE, "ю", '.', '>' },
    { 0xC0, "ё", '`', '~' }, { 0xBF, ".", '/', '?' }, { 0xBD, "-", '-', '_' }
};

// ============================================================================
// СЕРОЕ МЕНЮ МОДА:
// 1. Экран подключения и смены API (провайдер + ключ + Вставить/Очистить/Сохранить)
// 2. После подключения — строго СЕРОЕ окно ЧАТА с кнопкой «Сменить API» и русским вводом!
// ============================================================================
struct ApiProviderPreset {
    const char* name;
    const char* endpointUrl;
    const char* defaultModel;
};

static constexpr ApiProviderPreset kApiProviders[] = {
    { "OpenAI",     "https://api.openai.com/v1/chat/completions",                            "gpt-4o-mini" },
    { "Gemini",     "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions", "gemini-2.5-flash" },
    { "DeepSeek",   "https://api.deepseek.com/chat/completions",                             "deepseek-chat" },
    { "Groq",       "https://api.groq.com/openai/v1/chat/completions",                       "llama-3.3-70b-versatile" },
    { "OpenRouter", "https://openrouter.ai/api/v1/chat/completions",                         "openai/gpt-4o-mini" }
};

static int detectProviderIndexFromKey(std::string const& key, int currentIdx) {
    if (key.rfind("AIza", 0) == 0) return 1;      // Gemini
    if (key.rfind("gsk_", 0) == 0) return 3;      // Groq
    if (key.rfind("sk-or-", 0) == 0) return 4;    // OpenRouter
    return currentIdx;
}

static std::string maskApiKeyPreview(std::string const& key) {
    if (key.empty()) return "не задан";
    if (key.size() <= 8) return key;
    return key.substr(0, 5) + "..." + key.substr(key.size() - 4);
}

class AIDecorPopup : public geode::Popup {
protected:
    EditorUI* m_editorUI = nullptr;
    ContourAnalysis m_contour;
    bool m_isApiSetupStage = false;
    bool m_russianLayout = true;
    int m_providerIdx = 0;
    std::string m_inputBuffer;
    std::string m_statusBannerRU;
    bool m_prevKeyStates[256]{};

    CCNode* m_stageContainer = nullptr;
    CCLabelTTF* m_inputDisplayLabel = nullptr;
    CCLabelTTF* m_langBadgeLabel = nullptr;
    CCLabelTTF* m_providerBtnLabel = nullptr;
    CCLabelTTF* m_apiCurrentStatusLabel = nullptr;
    CCLabelTTF* m_chatHistoryLabel1 = nullptr;
    CCLabelTTF* m_chatHistoryLabel2 = nullptr;
    CCLabelTTF* m_chatHistoryLabel3 = nullptr;

    bool init(EditorUI* ui, ContourAnalysis contour) {
        if (!Popup::init(440.f, 260.f)) return false;

        m_editorUI = ui;
        m_contour = std::move(contour);

        // Скрываем стандартный коричневый фон GD и делаем полностью СЕРОЕ окно
        if constexpr (requires { this->m_bgSprite; }) {
            if (this->m_bgSprite) {
                this->m_bgSprite->setColor(ccColor3B{ 52, 55, 62 });
            }
        }

        auto size = m_mainLayer->getContentSize();
        auto center = CCPoint{ size.width / 2.f, size.height / 2.f };

        // Внешний серый корпус меню
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

        m_providerIdx = static_cast<int>(Mod::get()->getSavedValue<int64_t>("api-provider-index", 0));
        if (m_providerIdx < 0 || m_providerIdx >= 5) m_providerIdx = 0;

        std::string savedKey = getSavedApiKey();
        bool hasKey = g_apiConnectedSession || !savedKey.empty();

        m_stageContainer = CCNode::create();
        m_stageContainer->setPosition(CCPoint{ 0.f, 0.f });
        m_mainLayer->addChild(m_stageContainer);

        if (!hasKey) {
            buildApiConnectStage();
        } else {
            buildGrayChatStage();
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

    std::string getSavedApiKey() const {
        std::string savedKey = Mod::get()->getSavedValue<std::string>("connected-api-key", "");
        if (!savedKey.empty()) return savedKey;
        return Mod::get()->getSettingValue<std::string>("ai-api-key");
    }

    void clearStageButtons() {
        if (!m_buttonMenu) return;
        std::vector<std::string> ids = {
            "api-connect-btn"_spr,
            "api-paste-btn"_spr,
            "api-clear-btn"_spr,
            "api-provider-btn"_spr,
            "api-back-btn"_spr,
            "chat-send-btn"_spr,
            "chat-lang-btn"_spr,
            "chat-change-api-btn"_spr
        };
        for (auto const& id : ids) {
            while (auto* node = m_buttonMenu->getChildByID(id)) {
                node->removeFromParentAndCleanup(true);
            }
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

    void buildApiConnectStage() {
        m_isApiSetupStage = true;
        m_russianLayout = false; // Для ввода API-ключа включаем ENG
        m_stageContainer->removeAllChildrenWithCleanup(true);
        clearStageButtons();

        m_langBadgeLabel = nullptr;
        m_chatHistoryLabel1 = nullptr;
        m_chatHistoryLabel2 = nullptr;
        m_chatHistoryLabel3 = nullptr;

        std::string currentKey = getSavedApiKey();
        m_inputBuffer = currentKey;

        auto size = m_mainLayer->getContentSize();
        auto center = CCPoint{ size.width / 2.f, size.height / 2.f };

        bool hadConnectedApi = g_apiConnectedSession || !currentKey.empty();

        auto titleLbl = CCLabelTTF::create(
            hadConnectedApi ? "НАСТРОЙКА И СМЕНА ИИ ПО API" : "ПОДКЛЮЧЕНИЕ ИИ ПО API",
            "Arial", 17.f
        );
        titleLbl->setColor(ccColor3B{ 235, 240, 248 });
        titleLbl->setPosition(CCPoint{ center.x - (hadConnectedApi ? 40.f : 0.f), center.y + 96.f });
        m_stageContainer->addChild(titleLbl);

        // Если API уже был подключен — добавляем кнопку «Назад в чат» в правом верхнем углу
        if (hadConnectedApi) {
            auto backBg = CCScale9Sprite::create("square02b_001.png");
            backBg->setContentSize(CCSize{ 92.f, 24.f });
            backBg->setColor(ccColor3B{ 74, 80, 90 });
            auto backTxt = CCLabelTTF::create("Назад в чат", "Arial", 12.f);
            backTxt->setColor(ccColor3B{ 235, 240, 250 });
            backTxt->setPosition(CCPoint{ 46.f, 12.f });
            backBg->addChild(backTxt);

            auto backBtn = CCMenuItemSpriteExtra::create(backBg, this, menu_selector(AIDecorPopup::onBackToChatClicked));
            backBtn->setPosition(CCPoint{ center.x + 154.f, center.y + 96.f });
            backBtn->setID("api-back-btn"_spr);
            m_buttonMenu->addChild(backBtn);
        }

        auto cardBg = CCScale9Sprite::create("square02b_001.png");
        cardBg->setContentSize(CCSize{ 400.f, 94.f });
        cardBg->setColor(ccColor3B{ 32, 35, 40 });
        cardBg->setPosition(CCPoint{ center.x, center.y + 30.f });
        m_stageContainer->addChild(cardBg);

        std::string statusText = hadConnectedApi
            ? fmt::format("Текущий API: {} (ключ: {})", kApiProviders[m_providerIdx].name, maskApiKeyPreview(currentKey))
            : "Перед запуском мода необходимо подключить ИИ по API.";

        m_apiCurrentStatusLabel = CCLabelTTF::create(statusText.c_str(), "Arial", 13.f);
        m_apiCurrentStatusLabel->setColor(hadConnectedApi ? ccColor3B{ 165, 235, 195 } : ccColor3B{ 215, 222, 235 });
        m_apiCurrentStatusLabel->setPosition(CCPoint{ center.x, center.y + 56.f });
        m_stageContainer->addChild(m_apiCurrentStatusLabel);

        // Кнопка выбора провайдера API (OpenAI / Gemini / DeepSeek / Groq / OpenRouter)
        auto provBg = CCScale9Sprite::create("square02b_001.png");
        provBg->setContentSize(CCSize{ 280.f, 26.f });
        provBg->setColor(ccColor3B{ 68, 74, 86 });
        std::string provText = fmt::format("Провайдер API: {} (нажми чтобы сменить)", kApiProviders[m_providerIdx].name);
        m_providerBtnLabel = CCLabelTTF::create(provText.c_str(), "Arial", 12.f);
        m_providerBtnLabel->setColor(ccColor3B{ 240, 245, 255 });
        m_providerBtnLabel->setPosition(CCPoint{ 140.f, 13.f });
        provBg->addChild(m_providerBtnLabel);

        auto provBtn = CCMenuItemSpriteExtra::create(provBg, this, menu_selector(AIDecorPopup::onCycleApiProvider));
        provBtn->setPosition(CCPoint{ center.x, center.y + 26.f });
        provBtn->setID("api-provider-btn"_spr);
        m_buttonMenu->addChild(provBtn);

        auto notice2 = CCLabelTTF::create(
            "Вставьте новый API-ключ (Ctrl+V или кнопкой ниже) и нажмите «Сохранить API»:",
            "Arial", 12.f
        );
        notice2->setColor(ccColor3B{ 165, 175, 190 });
        notice2->setPosition(CCPoint{ center.x, center.y - 4.f });
        m_stageContainer->addChild(notice2);

        // Серое поле ввода API-ключа
        auto inputBg = CCScale9Sprite::create("square02b_001.png");
        inputBg->setContentSize(CCSize{ 400.f, 34.f });
        inputBg->setColor(ccColor3B{ 22, 24, 28 });
        inputBg->setPosition(CCPoint{ center.x, center.y - 44.f });
        m_stageContainer->addChild(inputBg);

        m_inputDisplayLabel = CCLabelTTF::create("", "Arial", 13.f);
        m_inputDisplayLabel->setPosition(CCPoint{ center.x, center.y - 44.f });
        m_stageContainer->addChild(m_inputDisplayLabel);
        refreshInputLabel();

        // Три удобные серые кнопки внизу: [Вставить Ctrl+V] [Очистить] [Сохранить API]
        auto pasteBg = CCScale9Sprite::create("square02b_001.png");
        pasteBg->setContentSize(CCSize{ 122.f, 32.f });
        pasteBg->setColor(ccColor3B{ 72, 78, 88 });
        auto pasteTxt = CCLabelTTF::create("Вставить (Ctrl+V)", "Arial", 12.f);
        pasteTxt->setColor(ccColor3B{ 235, 240, 250 });
        pasteTxt->setPosition(CCPoint{ 61.f, 16.f });
        pasteBg->addChild(pasteTxt);

        auto pasteBtn = CCMenuItemSpriteExtra::create(pasteBg, this, menu_selector(AIDecorPopup::onPasteApiClicked));
        pasteBtn->setPosition(CCPoint{ center.x - 136.f, center.y - 92.f });
        pasteBtn->setID("api-paste-btn"_spr);
        m_buttonMenu->addChild(pasteBtn);

        auto clearBg = CCScale9Sprite::create("square02b_001.png");
        clearBg->setContentSize(CCSize{ 96.f, 32.f });
        clearBg->setColor(ccColor3B{ 72, 78, 88 });
        auto clearTxt = CCLabelTTF::create("Очистить", "Arial", 12.f);
        clearTxt->setColor(ccColor3B{ 235, 240, 250 });
        clearTxt->setPosition(CCPoint{ 48.f, 16.f });
        clearBg->addChild(clearTxt);

        auto clearBtn = CCMenuItemSpriteExtra::create(clearBg, this, menu_selector(AIDecorPopup::onClearApiClicked));
        clearBtn->setPosition(CCPoint{ center.x - 16.f, center.y - 92.f });
        clearBtn->setID("api-clear-btn"_spr);
        m_buttonMenu->addChild(clearBtn);

        auto saveBg = CCScale9Sprite::create("square02b_001.png");
        saveBg->setContentSize(CCSize{ 148.f, 32.f });
        saveBg->setColor(ccColor3B{ 92, 102, 118 });
        auto saveTxt = CCLabelTTF::create(hadConnectedApi ? "Сохранить новый API" : "Подключить API", "Arial", 13.f);
        saveTxt->setColor(ccColor3B{ 255, 255, 255 });
        saveTxt->setPosition(CCPoint{ 74.f, 16.f });
        saveBg->addChild(saveTxt);

        auto connectBtn = CCMenuItemSpriteExtra::create(saveBg, this, menu_selector(AIDecorPopup::onConnectApiClicked));
        connectBtn->setPosition(CCPoint{ center.x + 118.f, center.y - 92.f });
        connectBtn->setID("api-connect-btn"_spr);
        m_buttonMenu->addChild(connectBtn);
    }

    void buildGrayChatStage() {
        m_isApiSetupStage = false;
        m_russianLayout = true; // В чате сразу включаем РУССКИЙ язык!
        m_inputBuffer.clear();
        m_stageContainer->removeAllChildrenWithCleanup(true);
        clearStageButtons();

        m_providerBtnLabel = nullptr;
        m_apiCurrentStatusLabel = nullptr;

        auto size = m_mainLayer->getContentSize();
        auto center = CCPoint{ size.width / 2.f, size.height / 2.f };

        // Верхняя серая шапка чата
        auto headerLbl = CCLabelTTF::create("ИИ ЧАТ-БОТ (Правый Alt — открыть/закрыть)", "Arial", 15.f);
        headerLbl->setColor(ccColor3B{ 230, 235, 242 });
        headerLbl->setPosition(CCPoint{ center.x - 65.f, center.y + 96.f });
        m_stageContainer->addChild(headerLbl);

        // Кнопка «Сменить API» прямо в шапке серого окна чата!
        auto apiBtnBg = CCScale9Sprite::create("square02b_001.png");
        apiBtnBg->setContentSize(CCSize{ 92.f, 22.f });
        apiBtnBg->setColor(ccColor3B{ 75, 80, 90 });
        auto apiBtnLbl = CCLabelTTF::create("Сменить API", "Arial", 12.f);
        apiBtnLbl->setColor(ccColor3B{ 235, 240, 250 });
        apiBtnLbl->setPosition(CCPoint{ 46.f, 11.f });
        apiBtnBg->addChild(apiBtnLbl);

        auto changeApiBtn = CCMenuItemSpriteExtra::create(apiBtnBg, this, menu_selector(AIDecorPopup::onOpenApiSettings));
        changeApiBtn->setPosition(CCPoint{ center.x + 102.f, center.y + 96.f });
        changeApiBtn->setID("chat-change-api-btn"_spr);
        m_buttonMenu->addChild(changeApiBtn);

        // Кнопка переключения языка РУС / ENG
        auto langBg = CCScale9Sprite::create("square02b_001.png");
        langBg->setContentSize(CCSize{ 52.f, 22.f });
        langBg->setColor(ccColor3B{ 75, 80, 90 });
        m_langBadgeLabel = CCLabelTTF::create("РУС", "Arial", 12.f);
        m_langBadgeLabel->setColor(ccColor3B{ 240, 245, 255 });
        m_langBadgeLabel->setPosition(CCPoint{ 26.f, 11.f });
        langBg->addChild(m_langBadgeLabel);

        auto langBtn = CCMenuItemSpriteExtra::create(langBg, this, menu_selector(AIDecorPopup::onToggleLanguage));
        langBtn->setPosition(CCPoint{ center.x + 178.f, center.y + 96.f });
        langBtn->setID("chat-lang-btn"_spr);
        m_buttonMenu->addChild(langBtn);

        // Серое окно истории сообщений чата
        auto chatAreaBg = CCScale9Sprite::create("square02b_001.png");
        chatAreaBg->setContentSize(CCSize{ 404.f, 124.f });
        chatAreaBg->setColor(ccColor3B{ 30, 32, 37 });
        chatAreaBg->setPosition(CCPoint{ center.x, center.y + 16.f });
        m_stageContainer->addChild(chatAreaBg);

        int freeLayer = (m_editorUI && m_editorUI->m_editorLayer)
            ? findFirstFreeEditorLayerBlock(m_editorUI->m_editorLayer, 4)
            : 1;

        std::string savedKey = getSavedApiKey();
        std::string greeting1 = !m_statusBannerRU.empty()
            ? m_statusBannerRU
            : fmt::format(
                "ИИ ({} API: {}): Выделено {} клеток (слои {}-{}).",
                kApiProviders[m_providerIdx].name,
                maskApiKeyPreview(savedKey),
                m_contour.cellCount(),
                freeLayer,
                freeLayer + 3
            );
        m_statusBannerRU.clear();

        m_chatHistoryLabel1 = CCLabelTTF::create(greeting1.c_str(), "Arial", 13.f);
        m_chatHistoryLabel1->setColor(ccColor3B{ 195, 205, 220 });
        m_chatHistoryLabel1->setPosition(CCPoint{ center.x, center.y + 54.f });
        m_stageContainer->addChild(m_chatHistoryLabel1);

        m_chatHistoryLabel2 = CCLabelTTF::create(
            "Напиши задачу, стиль декора и количество объектов (или «сменить апи»):",
            "Arial", 13.f
        );
        m_chatHistoryLabel2->setColor(ccColor3B{ 170, 178, 192 });
        m_chatHistoryLabel2->setPosition(CCPoint{ center.x, center.y + 18.f });
        m_stageContainer->addChild(m_chatHistoryLabel2);

        m_chatHistoryLabel3 = CCLabelTTF::create(
            "Примеры: «сделай 3д», «неон 90 объектов», «адский стиль с черепом 120»",
            "Arial", 12.f
        );
        m_chatHistoryLabel3->setColor(ccColor3B{ 140, 150, 165 });
        m_chatHistoryLabel3->setPosition(CCPoint{ center.x, center.y - 16.f });
        m_stageContainer->addChild(m_chatHistoryLabel3);

        // Серая строка ввода чата внизу
        auto inputBarBg = CCScale9Sprite::create("square02b_001.png");
        inputBarBg->setContentSize(CCSize{ 320.f, 34.f });
        inputBarBg->setColor(ccColor3B{ 22, 24, 28 });
        inputBarBg->setPosition(CCPoint{ center.x - 42.f, center.y - 78.f });
        m_stageContainer->addChild(inputBarBg);

        m_inputDisplayLabel = CCLabelTTF::create("Напиши задачу ИИ на русском (Enter — отправить)...", "Arial", 13.f);
        m_inputDisplayLabel->setColor(ccColor3B{ 150, 156, 168 });
        m_inputDisplayLabel->setPosition(CCPoint{ center.x - 42.f, center.y - 78.f });
        m_stageContainer->addChild(m_inputDisplayLabel);

        // Серая кнопка отправки в чат
        auto sendBg = CCScale9Sprite::create("square02b_001.png");
        sendBg->setContentSize(CCSize{ 76.f, 34.f });
        sendBg->setColor(ccColor3B{ 82, 88, 98 });
        auto sendTxt = CCLabelTTF::create("Отправить", "Arial", 13.f);
        sendTxt->setColor(ccColor3B{ 245, 248, 255 });
        sendTxt->setPosition(CCPoint{ 38.f, 17.f });
        sendBg->addChild(sendTxt);

        auto sendBtn = CCMenuItemSpriteExtra::create(sendBg, this, menu_selector(AIDecorPopup::onSendChatClicked));
        sendBtn->setPosition(CCPoint{ center.x + 164.f, center.y - 78.f });
        sendBtn->setID("chat-send-btn"_spr);
        m_buttonMenu->addChild(sendBtn);
    }

    void refreshInputLabel() {
        if (!m_inputDisplayLabel) return;
        if (m_inputBuffer.empty()) {
            if (m_isApiSetupStage) {
                m_inputDisplayLabel->setString("Вставьте API-ключ (Ctrl+V) или введите здесь...");
            } else {
                m_inputDisplayLabel->setString("Напиши задачу ИИ на русском (Enter — отправить)...");
            }
            m_inputDisplayLabel->setColor(ccColor3B{ 145, 150, 162 });
        } else {
            std::string shown = m_inputBuffer;
            if (shown.size() > 48) {
                shown = "..." + shown.substr(shown.size() - 45);
            }
            shown += "|";
            m_inputDisplayLabel->setString(shown.c_str());
            m_inputDisplayLabel->setColor(ccColor3B{ 245, 248, 255 });
        }
    }

    void onToggleLanguage(CCObject*) {
        m_russianLayout = !m_russianLayout;
        if (m_langBadgeLabel) {
            m_langBadgeLabel->setString(m_russianLayout ? "РУС" : "ENG");
        }
    }

    void onOpenApiSettings(CCObject*) {
        buildApiConnectStage();
    }

    void onBackToChatClicked(CCObject*) {
        buildGrayChatStage();
    }

    void onCycleApiProvider(CCObject*) {
        m_providerIdx = (m_providerIdx + 1) % 5;
        if (m_providerBtnLabel) {
            std::string provText = fmt::format("Провайдер API: {} (нажми чтобы сменить)", kApiProviders[m_providerIdx].name);
            m_providerBtnLabel->setString(provText.c_str());
        }
    }

    void onPasteApiClicked(CCObject*) {
        std::string clip = geode::utils::clipboard::read();
        if (!clip.empty()) {
            // Убираем пробелы и переносы строк по краям ключа
            while (!clip.empty() && (clip.back() == '\n' || clip.back() == '\r' || clip.back() == ' ' || clip.back() == '\t')) {
                clip.pop_back();
            }
            size_t start = 0;
            while (start < clip.size() && (clip[start] == '\n' || clip[start] == '\r' || clip[start] == ' ' || clip[start] == '\t')) {
                start++;
            }
            clip = clip.substr(start);

            m_inputBuffer = clip;
            if (m_inputBuffer.size() > 220) m_inputBuffer.resize(220);

            int detected = detectProviderIndexFromKey(m_inputBuffer, m_providerIdx);
            if (detected != m_providerIdx) {
                m_providerIdx = detected;
                if (m_providerBtnLabel) {
                    std::string provText = fmt::format("Провайдер API: {} (автоопределён)", kApiProviders[m_providerIdx].name);
                    m_providerBtnLabel->setString(provText.c_str());
                }
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
        m_providerIdx = detectProviderIndexFromKey(key, m_providerIdx);

        g_apiConnectedSession = true;
        Mod::get()->setSavedValue("connected-api-key", key);
        Mod::get()->setSavedValue("api-provider-index", static_cast<int64_t>(m_providerIdx));
        Mod::get()->setSettingValue("ai-api-key", key);
        Mod::get()->setSettingValue("ai-api-url", std::string(kApiProviders[m_providerIdx].endpointUrl));
        Mod::get()->setSettingValue("ai-model", std::string(kApiProviders[m_providerIdx].defaultModel));

        m_statusBannerRU = fmt::format(
            "API обновлён: {} (ключ {}). Выделено {} клеток.",
            kApiProviders[m_providerIdx].name,
            maskApiKeyPreview(key),
            m_contour.cellCount()
        );
        buildGrayChatStage();
    }

    void onSendChatClicked(CCObject*) {
        restoreLockedContourPositions();

        std::string lowCmd = normalizeLowerUTF8(m_inputBuffer);
        if (hasWord(lowCmd, "сменить апи") || hasWord(lowCmd, "поменять апи") ||
            hasWord(lowCmd, "изменить апи") || hasWord(lowCmd, "новый апи") ||
            lowCmd == "апи" || lowCmd == "api" || lowCmd == "/api") {
            buildApiConnectStage();
            return;
        }

        std::mt19937 rng(std::random_device{}());
        ChatBotPlan plan = parseUserChatCommand(m_inputBuffer);
        EditorTabCatalog catalog = loadEditorTabCatalog(m_editorUI);
        StylePalette palette = buildPaletteFromBotPlan(plan, rng);

        auto specs = generateByAITask(m_contour, catalog, plan, rng);
        SpawnSummary summary = spawnContourDecorInEditor(m_editorUI, m_contour, specs, palette, plan.taskMode);

        if (m_chatHistoryLabel1 && m_chatHistoryLabel2 && m_chatHistoryLabel3) {
            std::string userLine = m_inputBuffer.empty()
                ? "Вы: [Сделать новый случайный декор]"
                : ("Вы: " + m_inputBuffer);
            m_chatHistoryLabel1->setString(userLine.c_str());
            m_chatHistoryLabel1->setColor(ccColor3B{ 235, 240, 250 });

            m_chatHistoryLabel2->setString(plan.botResponseRU.c_str());
            m_chatHistoryLabel2->setColor(ccColor3B{ 165, 240, 195 });

            std::string statLine = fmt::format(
                "Готово: создано {} объектов на слоях {}-{}. Нажми Правый Alt чтобы закрыть.",
                summary.createdCount, summary.firstLayer, summary.firstLayer + 3
            );
            m_chatHistoryLabel3->setString(statLine.c_str());
            m_chatHistoryLabel3->setColor(ccColor3B{ 200, 210, 225 });
        }

        m_inputBuffer.clear();
        refreshInputLabel();
    }

    void updateKeyboardAndLock(float) {
        // Держим выделенные объекты редактора замороженными на своих местах, пока открыт чат
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

        // Alt+Shift переключает язык РУС / ENG прямо в чате
        if (altDown && wasJustPressed(VK_SHIFT)) {
            onToggleLanguage(nullptr);
            return;
        }

        // Ctrl+V вставляет текст (API-ключ в окне API или русский текст в чате)
        if (ctrlDown && wasJustPressed('V')) {
            if (m_isApiSetupStage) {
                onPasteApiClicked(nullptr);
            } else {
                std::string clip = geode::utils::clipboard::read();
                if (!clip.empty()) {
                    m_inputBuffer += clip;
                    if (m_inputBuffer.size() > 220) m_inputBuffer.resize(220);
                    refreshInputLabel();
                }
            }
            return;
        }

        if (wasJustPressed(VK_BACK)) {
            popLastUTF8Char(m_inputBuffer);
            refreshInputLabel();
            return;
        }

        if (wasJustPressed(VK_RETURN)) {
            if (m_isApiSetupStage) {
                onConnectApiClicked(nullptr);
            } else {
                onSendChatClicked(nullptr);
            }
            return;
        }

        if (wasJustPressed(VK_SPACE)) {
            if (m_inputBuffer.size() < 200) {
                m_inputBuffer.push_back(' ');
                refreshInputLabel();
            }
            return;
        }

        // Цифры 0..9 и Numpad 0..9
        for (int d = 0; d <= 9; ++d) {
            if (wasJustPressed('0' + d) || wasJustPressed(VK_NUMPAD0 + d)) {
                if (m_inputBuffer.size() < 200) {
                    m_inputBuffer.push_back(static_cast<char>('0' + d));
                    refreshInputLabel();
                }
            }
        }

        // Буквы (РУС ЙЦУКЕН или ENG QWERTY)
        if (!ctrlDown && !altDown) {
            for (auto const& entry : kKeyTable) {
                if (wasJustPressed(entry.vk)) {
                    if (m_inputBuffer.size() < 200) {
                        if (m_russianLayout && !m_isApiSetupStage) {
                            m_inputBuffer += entry.rusLower;
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

public:
    void closeByRightAlt() {
        restoreLockedContourPositions();
        this->onClose(nullptr);
    }

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
        auto* scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;

        // Если меню уже открыто — повторное нажатие Правого Alt ЗАКРЫВАЕТ его!
        if (auto* existing = scene->getChildByID("ai-decor-popup"_spr)) {
            if (auto* popup = typeinfo_cast<AIDecorPopup*>(existing)) {
                popup->closeByRightAlt();
            } else {
                existing->removeFromParentAndCleanup(true);
            }
            return;
        }

        ContourAnalysis contour = analyzeSelectedContour(this);
        if (auto* popup = AIDecorPopup::create(this, std::move(contour))) {
            popup->setID("ai-decor-popup"_spr);
            popup->show();
        }
    }
};
