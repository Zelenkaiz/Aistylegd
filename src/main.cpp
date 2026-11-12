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
// Стили декора, слоты палитры и роли слоев редактора
// ============================================================================
enum class DecorStyle {
    Neon = 0,
    Modern = 1,
    Hell = 2,
    Reality = 3
};

enum class ColorSlot : int {
    DarkFill = 0,         // Слой +0: темное внутреннее заполнение
    Dark3D = 1,           // Слой +0/+2: темная подложка 3D-блоков и внутренних стоек
    MidTone = 2,          // Слой +1: средний тон для арок, рамок, скоб и средних шипов
    BrightMain = 3,       // Слой +1/+3: главный яркий цвет (эмблемы, глаз, череп, блики, трава)
    HighlightOutline = 4, // Слой +2: светлый неоновый контур, 3D-линии и микро-детали
    AdditiveGlow = 5      // Слой +3: мягкий мелкий и контурный неон (аддитивное свечение)
};

enum class LayerRole : int {
    Fill = 0,        // firstFreeLayer + 0: Базовое заполнение
    InnerDetail = 1, // firstFreeLayer + 1: Внутренние декор-блоки, глаз/череп, узоры
    Outline3D = 2,   // firstFreeLayer + 2: Контур, готовые 3D-блоки и вложенные шипы
    NeonAndGrass = 3 // firstFreeLayer + 3: Мелкий контурный неон (снаружи и внутри) + трава
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
    bool hasCustomSelectionColor = false;
    ccColor3B selectionColor{ 255, 255, 255 };
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
    int priorityTier = 1; // 0 = обязательный контур/фон, 1 = основа, 2 = декор, 3 = микро-детали
};

static int g_lastColorIndex = -1;
static int g_lastArchetypeIndex = -1;

// ============================================================================
// C++23 безопасные шаблоны для работы с полями GameObject и вкладками EditorUI
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
// Нативная окраска ЛЮБОГО объекта через канал 1004 (White) + HSV в GJSpriteColor
// (LevelEditorLayer::updateVisibility сам красит все детали в нужный RGB цвет!)
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

    // Сдвиг оттенка от базового белого (канал 1004: H=0, S=0, V=1)
    float hueSigned = (H > 180.f) ? (H - 360.f) : H;
    float alphaF = static_cast<float>(opacity) / 255.f;

    // Канал 1004 в GD всегда белый (RGB 255,255,255). При включенном m_usesHSV = true
    // и аддитивных флагах (absoluteSaturation=true, absoluteBrightness=true)
    // сам движок GD преобразует белый в точный RGB цвет (R, G, B) на каждом кадре!
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
// Чтение реальных ID готовых объектов прямо из вкладок редактора (Фото 2, 3, 4, 5, 6)
// ============================================================================
struct EditorTabCatalog {
    // Фото 2 (Вкладка 4 — 3D блоки и линии, страница 2 и 0)
    int id3DTrapTop    = 211;
    int id3DHalfStrip  = 211;
    int id3DMedRect    = 211;
    int id3DSmallSq    = 211;
    int id3DLCorner    = 211;
    int id3DTriCorner  = 211;
    int id3DBrickTrap  = 211;
    int id3DBrickStrip = 211;
    int id3DLineRight  = 211;
    int id3DLineTop    = 211;

    // Фото 3 (Вкладка 9 — Декор/Эмблемы, страница 10)
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

    // Фото 4 (Вкладка 10 — Полосы, квадратики и скобы, страница 8)
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

    // Фото 5 (Вкладка 10 — Тонкие линии, точки, дуги, крестики, страница 10)
    int idDoubleDots    = 211;
    int idThinLineLong  = 211;
    int idGradLine      = 211;
    int idThinLineShort = 211;
    int idArcThin       = 211;
    int idCircleDot     = 211;
    int idSmallCross    = 211;
    int idSmallTriangle = 211;
    int idShortDash     = 211;
    int idChevron       = 211;
    int idArcThickLarge = 211;
    int idArcThickSmall = 211;

    // Фото 6 (Вкладка 10 — Разные виды неона/глоу, включая мелкий контурный неон, страница 0)
    int idGlowEdgeFull  = 1888;
    int idGlowEdgeSmall = 1888;
    int idGlowCorner    = 1888;
    int idGlowCircle    = 1888;
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

static EditorTabCatalog loadEditorTabCatalog(EditorUI* ui) {
    EditorTabCatalog cat;
    if (!ui) return cat;

    // Фото 2: Вкладка 4 (5-я слева — 3D), страница 2 (3-я точка снизу) и страница 0
    cat.id3DLineTop    = readButtonObjectIDFromTab(ui, 4, 0, 1, 211);
    cat.id3DLineRight  = readButtonObjectIDFromTab(ui, 4, 0, 2, 211);
    cat.id3DTrapTop    = readButtonObjectIDFromTab(ui, 4, 2, 0, 211);
    cat.id3DHalfStrip  = readButtonObjectIDFromTab(ui, 4, 2, 1, 211);
    cat.id3DMedRect    = readButtonObjectIDFromTab(ui, 4, 2, 2, 211);
    cat.id3DSmallSq    = readButtonObjectIDFromTab(ui, 4, 2, 3, 211);
    cat.id3DLCorner    = readButtonObjectIDFromTab(ui, 4, 2, 4, 211);
    cat.id3DTriCorner  = readButtonObjectIDFromTab(ui, 4, 2, 5, 211);
    cat.id3DBrickTrap  = readButtonObjectIDFromTab(ui, 4, 2, 9, 211);
    cat.id3DBrickStrip = readButtonObjectIDFromTab(ui, 4, 2, 10, 211);

    // Фото 3: Вкладка 9 (10-я слева — Шестиугольник/Декор), страница 10 (11-я точка снизу)
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

    // Фото 4: Вкладка 10 (11-я слева — Трава/Глоу/Детали), страница 8 (9-я точка снизу)
    cat.idDiagDouble1    = readButtonObjectIDFromTab(ui, 10, 8, 0, 503);
    cat.idDiagDouble2    = readButtonObjectIDFromTab(ui, 10, 8, 1, 504);
    cat.idDiagThick      = readButtonObjectIDFromTab(ui, 10, 8, 2, 505);
    cat.idDiagThin       = readButtonObjectIDFromTab(ui, 10, 8, 3, 505);
    cat.idHollowSquare   = readButtonObjectIDFromTab(ui, 10, 8, 4, 211);
    cat.idParallelLines  = readButtonObjectIDFromTab(ui, 10, 8, 5, 211);
    cat.idBracketCorner  = readButtonObjectIDFromTab(ui, 10, 8, 6, 211);
    cat.idBracketArch    = readButtonObjectIDFromTab(ui, 10, 8, 7, 211);
    cat.idBracketT       = readButtonObjectIDFromTab(ui, 10, 8, 8, 211);
    cat.idBracketCross   = readButtonObjectIDFromTab(ui, 10, 8, 9, 211);
    cat.idSlashTickSmall = readButtonObjectIDFromTab(ui, 10, 8, 10, 211);
    cat.idSlashTickWide  = readButtonObjectIDFromTab(ui, 10, 8, 11, 211);

    // Фото 5: Вкладка 10, страница 10 (11-я точка снизу)
    cat.idDoubleDots    = readButtonObjectIDFromTab(ui, 10, 10, 0, 211);
    cat.idThinLineLong  = readButtonObjectIDFromTab(ui, 10, 10, 1, 211);
    cat.idGradLine      = readButtonObjectIDFromTab(ui, 10, 10, 2, 211);
    cat.idThinLineShort = readButtonObjectIDFromTab(ui, 10, 10, 3, 211);
    cat.idArcThin       = readButtonObjectIDFromTab(ui, 10, 10, 4, 211);
    cat.idCircleDot     = readButtonObjectIDFromTab(ui, 10, 10, 5, 211);
    cat.idSmallCross    = readButtonObjectIDFromTab(ui, 10, 10, 6, 211);
    cat.idSmallTriangle = readButtonObjectIDFromTab(ui, 10, 10, 7, 211);
    cat.idShortDash     = readButtonObjectIDFromTab(ui, 10, 10, 8, 211);
    cat.idChevron       = readButtonObjectIDFromTab(ui, 10, 10, 9, 211);
    cat.idArcThickLarge = readButtonObjectIDFromTab(ui, 10, 10, 10, 211);
    cat.idArcThickSmall = readButtonObjectIDFromTab(ui, 10, 10, 11, 211);

    // Фото 6: Вкладка 10, страница 0 (разные виды глоу, включая мелкий контурный неон)
    cat.idGlowEdgeFull  = readButtonObjectIDFromTab(ui, 10, 0, 1, 1888);
    cat.idGlowCorner    = readButtonObjectIDFromTab(ui, 10, 0, 2, 1888);
    cat.idGlowEdgeSmall = readButtonObjectIDFromTab(ui, 10, 0, 4, 1888);
    cat.idGlowCircle    = 1888;

    return cat;
}

// ============================================================================
// Автоматический поиск первого незаполненного блока слоев (Editor Layer)
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
// Генерация НОВОЙ цветовой палитры при каждой генерации (с единым цветом на слот)
// ============================================================================
static std::string toLowerASCII(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    }
    return s;
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

static StylePalette buildDynamicPalette(
    DecorStyle style,
    std::string const& rawPrompt,
    ContourAnalysis const& contour,
    std::mt19937& rng
) {
    StylePalette pal;
    std::string p = toLowerASCII(rawPrompt);

    // Наборы разнообразных сочных цветов для каждого стиля (чтобы каждая генерация меняла цвет!)
    ccColor3B const neonPresets[8] = {
        { 110, 255,  25 }, // Ядовито-лаймовый неон
        {  25, 245, 255 }, // Электрический циан
        { 255,  35, 175 }, // Неоновая фуксия/розовый
        { 165,  55, 255 }, // Ультрафиолет
        { 255, 230,  25 }, // Кибер-желтый
        {  35, 255, 145 }, // Кислотно-мятный
        { 255,  95,  25 }, // Неоново-оранжевый
        {  55, 130, 255 }  // Лазерно-синий
    };
    ccColor3B const modernPresets[6] = {
        {  45, 215, 255 }, // Чистый лазурный
        { 255, 105,  95 }, // Пастельно-коралловый
        {  85, 235, 175 }, // Мятный модерн
        { 140, 110, 255 }, // Индиго
        { 255, 195,  55 }, // Янтарный
        { 245,  95, 185 }  // Розовый кварц
    };
    ccColor3B const hellPresets[6] = {
        { 255,  42,  22 }, // Кроваво-алый
        { 255, 115,  15 }, // Лавово-оранжевый
        { 235,  20,  95 }, // Багрово-пурпурный
        { 255, 175,  25 }, // Серно-огненный
        { 210,  25,  25 }, // Темный рубин
        { 255,  65,  55 }  // Адское пламя
    };
    ccColor3B const realityPresets[6] = {
        {  50, 235, 115 }, // Изумрудный лес
        {  40, 215, 195 }, // Биолюминесцентная бирюза
        { 145, 235,  45 }, // Солнечный мох
        {  75, 185, 255 }, // Горный ледник
        { 245, 165,  45 }, // Золотая осень
        { 115, 245, 165 }  // Нефрит
    };

    // Выбираем новый случайный цвет, гарантированно отличный от прошлой генерации
    int poolSize = (style == DecorStyle::Neon) ? 8 : 6;
    std::uniform_int_distribution<int> dist(0, poolSize - 1);
    int chosenIdx = dist(rng);
    if (chosenIdx == g_lastColorIndex) {
        chosenIdx = (chosenIdx + 1 + (dist(rng) % (poolSize - 1))) % poolSize;
    }
    g_lastColorIndex = chosenIdx;

    switch (style) {
        case DecorStyle::Neon:    pal.masterColor = neonPresets[chosenIdx]; break;
        case DecorStyle::Modern:  pal.masterColor = modernPresets[chosenIdx]; break;
        case DecorStyle::Hell:    pal.masterColor = hellPresets[chosenIdx]; break;
        case DecorStyle::Reality: pal.masterColor = realityPresets[chosenIdx]; break;
    }

    // Если пользователь явно написал желаемый цвет в промпте — берем его
    if (p.find("green") != std::string::npos || p.find("зелен") != std::string::npos || p.find("лайм") != std::string::npos) {
        pal.masterColor = ccColor3B{ 110, 255, 25 };
    } else if (p.find("cyan") != std::string::npos || p.find("голуб") != std::string::npos || p.find("бирюз") != std::string::npos) {
        pal.masterColor = ccColor3B{ 25, 245, 255 };
    } else if (p.find("blue") != std::string::npos || p.find("син") != std::string::npos) {
        pal.masterColor = ccColor3B{ 55, 130, 255 };
    } else if (p.find("purple") != std::string::npos || p.find("фиолет") != std::string::npos || p.find("пурпур") != std::string::npos) {
        pal.masterColor = ccColor3B{ 170, 55, 255 };
    } else if (p.find("pink") != std::string::npos || p.find("розов") != std::string::npos) {
        pal.masterColor = ccColor3B{ 255, 45, 180 };
    } else if (p.find("red") != std::string::npos || p.find("красн") != std::string::npos || p.find("алый") != std::string::npos) {
        pal.masterColor = ccColor3B{ 255, 40, 30 };
    } else if (p.find("orange") != std::string::npos || p.find("оранж") != std::string::npos) {
        pal.masterColor = ccColor3B{ 255, 135, 20 };
    } else if (p.find("yellow") != std::string::npos || p.find("желт") != std::string::npos || p.find("золот") != std::string::npos) {
        pal.masterColor = ccColor3B{ 255, 230, 30 };
    }

    // 6 гармоничных ступеней яркости от темной подложки до яркого неона
    pal.slotColors[static_cast<int>(ColorSlot::DarkFill)] = ccColor3B{
        static_cast<GLubyte>(std::clamp(pal.masterColor.r * 0.14f + 6.f, 8.f, 45.f)),
        static_cast<GLubyte>(std::clamp(pal.masterColor.g * 0.14f + 6.f, 8.f, 45.f)),
        static_cast<GLubyte>(std::clamp(pal.masterColor.b * 0.14f + 6.f, 8.f, 45.f))
    };
    pal.slotColors[static_cast<int>(ColorSlot::Dark3D)]           = scaleRGB(pal.masterColor, 0.34f);
    pal.slotColors[static_cast<int>(ColorSlot::MidTone)]          = scaleRGB(pal.masterColor, 0.64f);
    pal.slotColors[static_cast<int>(ColorSlot::BrightMain)]       = pal.masterColor;
    pal.slotColors[static_cast<int>(ColorSlot::HighlightOutline)] = mixTowardWhite(pal.masterColor, 0.52f);
    pal.slotColors[static_cast<int>(ColorSlot::AdditiveGlow)]     = pal.masterColor;

    pal.slotOpacity[static_cast<int>(ColorSlot::DarkFill)]         = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::Dark3D)]           = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::MidTone)]          = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::BrightMain)]       = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::HighlightOutline)] = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::AdditiveGlow)]     = 115;

    for (int i = 0; i < 6; ++i) {
        pal.slotBlending[i] = (i == static_cast<int>(ColorSlot::AdditiveGlow));
    }

    return pal;
}

// ============================================================================
// Считывание выделенного контура (клетки 30x30 + шипы)
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

static void fillEnclosedInteriorCells(std::set<std::pair<int, int>>& cells) {
    if (cells.size() < 6) return;

    int minGX = 1000000, maxGX = -1000000;
    int minGY = 1000000, maxGY = -1000000;
    for (auto const& [gx, gy] : cells) {
        minGX = (std::min)(minGX, gx);
        maxGX = (std::max)(maxGX, gx);
        minGY = (std::min)(minGY, gy);
        maxGY = (std::max)(maxGY, gy);
    }

    int padMinX = minGX - 1, padMaxX = maxGX + 1;
    int padMinY = minGY - 1, padMaxY = maxGY + 1;
    if ((padMaxX - padMinX) > 64 || (padMaxY - padMinY) > 64) return;

    std::set<std::pair<int, int>> outsideVisited;
    std::queue<std::pair<int, int>> q;
    q.push({ padMinX, padMinY });
    outsideVisited.insert({ padMinX, padMinY });

    int const dx[4] = { 1, -1, 0, 0 };
    int const dy[4] = { 0, 0, 1, -1 };

    while (!q.empty()) {
        auto [cx, cy] = q.front();
        q.pop();
        for (int dir = 0; dir < 4; ++dir) {
            int nx = cx + dx[dir];
            int ny = cy + dy[dir];
            if (nx < padMinX || nx > padMaxX || ny < padMinY || ny > padMaxY) continue;
            if (cells.count({ nx, ny }) || outsideVisited.count({ nx, ny })) continue;
            outsideVisited.insert({ nx, ny });
            q.push({ nx, ny });
        }
    }

    for (int gx = minGX; gx <= maxGX; ++gx) {
        for (int gy = minGY; gy <= maxGY; ++gy) {
            if (!outsideVisited.count({ gx, gy })) {
                cells.insert({ gx, gy });
            }
        }
    }
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
        fillEnclosedInteriorCells(info.occupiedCells);
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
// Разбиение клеток на связные острова (отдельные кубы, платформы, колонны)
// ============================================================================
struct StructureIsland {
    std::set<std::pair<int, int>> cells;
    int minGX = 0, maxGX = 0;
    int minGY = 0, maxGY = 0;
    int cols() const { return maxGX - minGX + 1; }
    int rows() const { return maxGY - minGY + 1; }
};

static std::vector<StructureIsland> extractIslands(std::set<std::pair<int, int>> const& allCells) {
    std::vector<StructureIsland> islands;
    std::set<std::pair<int, int>> visited;

    int const dx[4] = { 1, -1, 0, 0 };
    int const dy[4] = { 0, 0, 1, -1 };

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

            for (int d = 0; d < 4; ++d) {
                std::pair<int, int> nb{ gx + dx[d], gy + dy[d] };
                if (allCells.count(nb) && !visited.count(nb)) {
                    visited.insert(nb);
                    q.push(nb);
                }
            }
        }
        islands.push_back(std::move(isl));
    }
    return islands;
}

// ============================================================================
// Процедурный генератор разнообразных конструкций с использованием готовых
// блоков из редактора (3D, глаз, череп, скобы, дуги, мелкий двусторонний неон)
// ============================================================================
static void addObjSpec(
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

static std::vector<GeneratedObjectSpec> generateVariedContourDecor(
    ContourAnalysis const& contour,
    EditorTabCatalog const& cat,
    DecorStyle style,
    int targetCount,
    std::string const& rawPrompt,
    std::mt19937& rng
) {
    std::vector<GeneratedObjectSpec> candidates;
    std::string p = toLowerASCII(rawPrompt);

    // Выбираем новый архитектурный архетип (0..4), отличный от прошлой генерации
    std::uniform_int_distribution<int> archDist(0, 4);
    int archetype = archDist(rng);
    if (archetype == g_lastArchetypeIndex) {
        archetype = (archetype + 1 + (archDist(rng) % 4)) % 5;
    }
    g_lastArchetypeIndex = archetype;

    // Режим мелкого контура неона (Фото 6):
    // 0 = снаружи мелкий неон + мягкое свечение
    // 1 = ДВУСТОРОННИЙ мелкий неон (и снаружи, и внутри контура!)
    // 2 = внутри мелкий неон + снаружи угловой/градиентный неон
    std::uniform_int_distribution<int> neonModeDist(0, 2);
    int neonBorderMode = neonModeDist(rng);

    // Случайный выбор готовых эмблем из Фото 3 (глаз, череп, вихрь, ключ, сердце, молния, призрак, перекрестие)
    int const emblemPool[8] = {
        cat.idEye, cat.idSkull, cat.idSwirl, cat.idKey,
        cat.idLightning, cat.idCrossCircle, cat.idHeart, cat.idGhost
    };
    std::uniform_int_distribution<int> embDist(0, 7);
    int primaryEmblemID = (style == DecorStyle::Hell) ? cat.idSkull : cat.idEye;
    int secondaryEmblemID = emblemPool[embDist(rng)];
    if (rng() % 2 == 0) {
        primaryEmblemID = emblemPool[embDist(rng)];
    }

    auto islands = extractIslands(contour.occupiedCells);

    for (auto const& isl : islands) {
        int cols = isl.cols();
        int rows = isl.rows();
        bool isSingleCube = (isl.cells.size() == 1);

        // Для каждого острова можно слегка варьировать под-стиль
        int islArch = (archetype + (isl.minGX & 3)) % 5;

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
            // Слой +0 (Fill, Tier 0): Темное контрастное заполнение внутри клетки
            // ================================================================
            addObjSpec(candidates, 211, cx, cy, 1.00f, 1.00f, 0.f, LayerRole::Fill, ColorSlot::DarkFill, 1, 0);

            // Внутренняя подложка средней темноты (меняет форму по архетипу)
            if (islArch == 0 || islArch == 3) {
                addObjSpec(candidates, 211, cx, cy, 0.82f, 0.82f, 0.f, LayerRole::Fill, ColorSlot::Dark3D, 2, 1);
                addObjSpec(candidates, 211, cx, cy, 0.70f, 0.70f, 0.f, LayerRole::Fill, ColorSlot::DarkFill, 3, 1);
            } else if (islArch == 1) {
                // Шахматные/решетчатые тех-секции
                addObjSpec(candidates, cat.idHashtag, cx, cy, 0.85f, 0.85f, 0.f, LayerRole::Fill, ColorSlot::Dark3D, 2, 1);
            } else if (islArch == 2) {
                // Ромбовидная темная подложка
                addObjSpec(candidates, 211, cx, cy, 0.62f, 0.62f, 45.f, LayerRole::Fill, ColorSlot::Dark3D, 2, 1);
            }

            // ================================================================
            // Слой +1 (InnerDetail): Готовые декор-блоки из Фото 3, 4, 5
            // ================================================================
            if (isSingleCube) {
                // Одиночный куб 1x1: готовый глаз/эмблема из Фото 3 + угловые детали из Фото 4 и 5
                int cubeEmblem = (rng() % 3 == 0) ? secondaryEmblemID : cat.idEye;
                addObjSpec(candidates, cubeEmblem, cx, cy, 0.82f, 0.82f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 6, 1);

                // Готовые полые квадратики 回 (Фото 4, слот 4) или скобы ╔ (Фото 4, слот 6) по углам
                bool useBrackets = (rng() % 2 == 0);
                float const offsX[4] = { -8.5f,  8.5f, -8.5f,  8.5f };
                float const offsY[4] = {  8.5f,  8.5f, -8.5f, -8.5f };
                float const rots[4]  = {  0.f,  90.f, 270.f, 180.f };
                for (int cIdx = 0; cIdx < 4; ++cIdx) {
                    if (useBrackets) {
                        addObjSpec(candidates, cat.idBracketCorner, cx + offsX[cIdx], cy + offsY[cIdx], 0.52f, 0.52f, rots[cIdx], LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
                    } else {
                        addObjSpec(candidates, cat.idHollowSquare, cx + offsX[cIdx], cy + offsY[cIdx], 0.65f, 0.65f, 0.f, LayerRole::InnerDetail, ColorSlot::MidTone, 6, 2);
                    }
                }

                // Двойной диагональный блик // из Фото 4
                addObjSpec(candidates, cat.idDiagDouble1, cx - 3.f, cy + 3.f, 0.65f, 0.65f, 0.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 7, 3);
            } else {
                // Многоклеточная конструкция: 5 разных процедурных архетипов!
                if (topOpen) {
                    // Верхняя светящаяся полоса-шапка (использует готовые полу-блоки и градиентные линии)
                    addObjSpec(candidates, cat.id3DHalfStrip, cx, cy + 9.5f, 0.96f, 0.45f, 0.f, LayerRole::InnerDetail, ColorSlot::MidTone, 4, 1);
                    addObjSpec(candidates, cat.idGradLine,    cx, cy + 12.0f, 0.95f, 1.0f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 5, 1);
                }

                if (islArch == 0) {
                    // Архетип 0: Концентрические арки ∏ (Фото 4), диагональные блики // и готовый череп/глаз (Фото 3)
                    addObjSpec(candidates, cat.idParallelLines, cx, cy, 0.90f, 0.95f, 90.f, LayerRole::InnerDetail, ColorSlot::MidTone, 4, 1);
                    if (topOpen) {
                        addObjSpec(candidates, cat.idBracketArch, cx, cy - 2.f, 0.90f, 0.90f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 5, 1);
                    }
                    if (localCol == 0 && localRow == rows - 1) {
                        addObjSpec(candidates, cat.idDiagDouble1, cx, cy, 0.88f, 0.88f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 6, 1);
                    }
                    if (localCol == cols - 1 && localRow == rows - 1) {
                        addObjSpec(candidates, cat.idSkull, cx, cy - 1.5f, 0.78f, 0.78f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 6, 1);
                    }
                    // Готовые полые квадратики 回 (Фото 4)
                    addObjSpec(candidates, cat.idHollowSquare, cx - 7.f, cy - 2.f, 0.75f, 0.75f, 0.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
                    addObjSpec(candidates, cat.idHollowSquare, cx + 7.f, cy + 3.f, 0.75f, 0.75f, 0.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 3);
                }
                else if (islArch == 1) {
                    // Архетип 1: Кибер-решетка с перекрестиями ╬, Т-скобами ╩ (Фото 4), точками •. и крестиками × (Фото 5)
                    addObjSpec(candidates, cat.idBracketCross, cx, cy, 0.85f, 0.85f, 0.f, LayerRole::InnerDetail, ColorSlot::MidTone, 4, 1);
                    if ((localCol + localRow) % 2 == 0) {
                        addObjSpec(candidates, primaryEmblemID, cx, cy, 0.68f, 0.68f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 6, 1);
                    } else {
                        addObjSpec(candidates, cat.idSmallCross, cx - 6.f, cy + 6.f, 0.75f, 0.75f, 0.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
                        addObjSpec(candidates, cat.idSmallCross, cx + 6.f, cy - 6.f, 0.75f, 0.75f, 0.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
                        addObjSpec(candidates, cat.idDoubleDots, cx + 6.f, cy + 6.f, 0.80f, 0.80f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 6, 3);
                    }
                    if (bottomOpen) {
                        addObjSpec(candidates, cat.idBracketT, cx, cy - 6.f, 0.85f, 0.85f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 5, 2);
                    }
                }
                else if (islArch == 2) {
                    // Архетип 2: Дуговой реактор из готовых дуг ╭, кругов ●, шевронов > и треугольников ▲ (Фото 5)
                    float arcRot = static_cast<float>(((localCol + localRow * 2) % 4) * 90);
                    addObjSpec(candidates, cat.idArcThickLarge, cx, cy, 0.88f, 0.88f, arcRot, LayerRole::InnerDetail, ColorSlot::MidTone, 4, 1);
                    addObjSpec(candidates, cat.idArcThin,       cx, cy, 0.65f, 0.65f, arcRot + 180.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 5, 2);

                    if (localCol == cols / 2 && localRow == rows / 2) {
                        addObjSpec(candidates, cat.idEye, cx, cy, 0.85f, 0.85f, 0.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 7, 1);
                    } else {
                        addObjSpec(candidates, cat.idCircleDot, cx, cy, 0.55f, 0.55f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 6, 2);
                    }
                    addObjSpec(candidates, cat.idChevron, cx - 7.f, cy, 0.65f, 0.65f, 0.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 3);
                    addObjSpec(candidates, cat.idChevron, cx + 7.f, cy, 0.65f, 0.65f, 180.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 3);
                }
                else if (islArch == 3) {
                    // Архетип 3: Цепи 8 (Фото 3), диагональные лучи / (Фото 4) и угловые скобы ╔
                    addObjSpec(candidates, cat.idDiagThick, cx, cy, 0.88f, 0.88f, (localCol % 2 == 0) ? 0.f : 90.f, LayerRole::InnerDetail, ColorSlot::MidTone, 4, 1);
                    addObjSpec(candidates, cat.idChainLink, cx, cy, 0.72f, 0.72f, (rows > cols) ? 90.f : 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 5, 1);
                    addObjSpec(candidates, cat.idBracketCorner, cx - 7.5f, cy + 7.5f, 0.65f, 0.65f, 0.f,   LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
                    addObjSpec(candidates, cat.idBracketCorner, cx + 7.5f, cy - 7.5f, 0.65f, 0.65f, 180.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 2);
                    addObjSpec(candidates, cat.idSlashTickSmall, cx + 6.f, cy + 6.f, 0.75f, 0.75f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 6, 3);
                }
                else {
                    // Архетип 4: Кирпично-неоновый храм с готовыми эмблемами (череп, глаз, кость, молния, вихрь из Фото 3)
                    addObjSpec(candidates, cat.id3DBrickStrip, cx, cy + 5.f, 0.90f, 0.85f, 0.f, LayerRole::InnerDetail, ColorSlot::MidTone, 4, 1);
                    addObjSpec(candidates, cat.id3DBrickStrip, cx, cy - 5.f, 0.90f, 0.85f, 0.f, LayerRole::InnerDetail, ColorSlot::Dark3D, 3, 2);

                    int cellEmb = ((localCol + localRow) % 2 == 0) ? primaryEmblemID : secondaryEmblemID;
                    addObjSpec(candidates, cellEmb, cx, cy, 0.75f, 0.75f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 6, 1);
                    addObjSpec(candidates, cat.idSmallTriangle, cx - 8.f, cy - 8.f, 0.60f, 0.60f, 0.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 3);
                    addObjSpec(candidates, cat.idSmallTriangle, cx + 8.f, cy - 8.f, 0.60f, 0.60f, 0.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6, 3);
                }

                // Дополнительные микро-детали (Tier 3) для высоких значений количества блоков (120..300)
                addObjSpec(candidates, cat.idShortDash,     cx,        cy + 12.8f, 0.65f, 0.65f, 0.f,  LayerRole::InnerDetail, ColorSlot::HighlightOutline, 7, 3);
                addObjSpec(candidates, cat.idDoubleDots,    cx - 9.f,  cy - 9.f,   0.70f, 0.70f, 0.f,  LayerRole::InnerDetail, ColorSlot::BrightMain,       7, 3);
                addObjSpec(candidates, cat.idArcThickSmall, cx + 8.5f, cy + 8.5f,  0.55f, 0.55f, 90.f, LayerRole::InnerDetail, ColorSlot::MidTone,          6, 3);
            }

            // ================================================================
            // Слой +2 (Outline3D): Внешний контур + готовые 3D-блоки из Фото 2
            // ================================================================
            if (topOpen) {
                addObjSpec(candidates, cat.idThinLineLong, cx, cy + 14.2f, 1.0f, 1.0f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11, 0);
            }
            if (bottomOpen) {
                addObjSpec(candidates, cat.idThinLineLong, cx, cy - 14.2f, 1.0f, 1.0f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11, 0);
            }
            if (leftOpen) {
                addObjSpec(candidates, cat.idThinLineLong, cx - 14.2f, cy, 1.0f, 1.0f, 90.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11, 0);
            }
            if (rightOpen) {
                addObjSpec(candidates, cat.idThinLineLong, cx + 14.2f, cy, 1.0f, 1.0f, 90.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11, 0);
            }

            // Готовые 3D-блоки из Фото 2 на внешних гранях (справа и сверху)
            if (rightOpen) {
                int side3DID = (islArch == 4) ? cat.id3DBrickTrap : cat.id3DTrapTop;
                addObjSpec(candidates, side3DID, cx + 19.5f, cy, 0.35f, 0.98f, 90.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 1);
                addObjSpec(candidates, cat.idThinLineLong, cx + 21.5f, cy + 2.f, 0.98f, 0.98f, 90.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 10, 1);
                if (topOpen) {
                    addObjSpec(candidates, cat.id3DTriCorner, cx + 18.5f, cy + 17.5f, 0.45f, 0.45f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 10, 1);
                }
            }
            if (topOpen && isSingleCube) {
                addObjSpec(candidates, cat.id3DTrapTop, cx + 2.f, cy + 18.5f, 0.96f, 0.32f, 0.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9, 1);
                addObjSpec(candidates, cat.idThinLineLong, cx + 3.5f, cy + 20.5f, 0.98f, 0.98f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 10, 1);
            }

            // ================================================================
            // Слой +3 (NeonAndGrass): Мелкий контурный неон (Фото 6) ИНОГДА с двух сторон + трава
            // ================================================================
            bool doDoubleSided = (neonBorderMode == 1) || (neonBorderMode == 2 && ((gx + gy) % 2 == 0));

            if (topOpen) {
                // Снаружи контура (мелкий неон из Фото 6)
                addObjSpec(candidates, cat.idGlowEdgeSmall, cx - 7.5f, cy + 18.0f, 1.0f, 0.75f, 0.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 14, 1, false, false, 115);
                addObjSpec(candidates, cat.idGlowEdgeSmall, cx + 7.5f, cy + 18.0f, 1.0f, 0.75f, 0.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 14, 2, false, false, 115);
                // Внутри контура (развернутый на 180° мелкий неон)
                if (doDoubleSided) {
                    addObjSpec(candidates, cat.idGlowEdgeSmall, cx - 7.5f, cy + 11.5f, 1.0f, 0.70f, 180.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 2, false, false, 95);
                    addObjSpec(candidates, cat.idGlowEdgeSmall, cx + 7.5f, cy + 11.5f, 1.0f, 0.70f, 180.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 3, false, false, 95);
                }

                // Декоративная трава на верхней грани
                if (style != DecorStyle::Modern) {
                    int const grassIDs[3] = { 406, 407, 408 };
                    int gID = grassIDs[(gx + gy + rng()) % 3];
                    addObjSpec(candidates, gID, cx - 5.f, cy + 16.5f, 0.65f, 0.68f, 0.f, LayerRole::NeonAndGrass, ColorSlot::BrightMain, 15, 1);
                    addObjSpec(candidates, gID, cx + 5.5f, cy + 16.2f, 0.52f, 0.55f, 0.f, LayerRole::NeonAndGrass, ColorSlot::BrightMain, 15, 2, true, false);
                }
            }

            if (bottomOpen) {
                // Снаружи нижней грани
                addObjSpec(candidates, cat.idGlowEdgeSmall, cx - 7.5f, cy - 18.0f, 1.0f, 0.75f, 180.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 14, 2, false, false, 110);
                addObjSpec(candidates, cat.idGlowEdgeSmall, cx + 7.5f, cy - 18.0f, 1.0f, 0.75f, 180.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 14, 3, false, false, 110);
                // Внутри нижней грани
                if (doDoubleSided) {
                    addObjSpec(candidates, cat.idGlowEdgeSmall, cx, cy - 11.5f, 1.5f, 0.65f, 0.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 2, false, false, 90);
                }
            }

            if (leftOpen) {
                // Снаружи левой грани
                addObjSpec(candidates, cat.idGlowEdgeSmall, cx - 18.0f, cy, 1.4f, 0.75f, 270.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 14, 1, false, false, 110);
                // Внутри левой грани
                if (doDoubleSided) {
                    addObjSpec(candidates, cat.idGlowEdgeSmall, cx - 11.5f, cy, 1.4f, 0.65f, 90.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 2, false, false, 90);
                }
            }

            if (rightOpen) {
                // Снаружи правой грани
                addObjSpec(candidates, cat.idGlowEdgeSmall, cx + 18.0f, cy, 1.4f, 0.75f, 90.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 14, 1, false, false, 110);
                // Внутри правой грани
                if (doDoubleSided) {
                    addObjSpec(candidates, cat.idGlowEdgeSmall, cx + 11.5f, cy, 1.4f, 0.65f, 270.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, 13, 2, false, false, 90);
                }
            }

            // Мягкое фоновое свечение на внешних углах
            if ((topOpen && leftOpen) || (topOpen && rightOpen)) {
                addObjSpec(candidates, cat.idGlowCircle, cx, cy, 1.15f, 1.15f, 0.f, LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow, -2, 2, false, false, 65);
            }
        }
    }

    // ========================================================================
    // Декорирование выделенных шипов (вложенные шипы + мелкий неон у основания)
    // ========================================================================
    for (auto const& sp : contour.spikes) {
        float const scales[3] = { 0.72f, 0.46f, 0.22f };
        ColorSlot const slots[3] = {
            ColorSlot::BrightMain,
            ColorSlot::MidTone,
            ColorSlot::HighlightOutline
        };
        for (int k = 0; k < 3; ++k) {
            addObjSpec(
                candidates, sp.objectId,
                sp.x, sp.y,
                sp.scaleX * scales[k], sp.scaleY * scales[k],
                sp.rotation,
                LayerRole::Outline3D, slots[k],
                11 + k, (k < 2) ? 0 : 2,
                sp.flipX, sp.flipY
            );
        }
        addObjSpec(
            candidates, cat.idGlowEdgeSmall,
            sp.x, sp.y - 8.f,
            1.2f * sp.scaleX, 0.85f * sp.scaleY,
            sp.rotation,
            LayerRole::NeonAndGrass, ColorSlot::AdditiveGlow,
            14, 1, false, false, 115
        );
    }

    // ========================================================================
    // Точный контроль количества блоков (targetCount реально меняет плотность!)
    // ========================================================================
    std::stable_sort(candidates.begin(), candidates.end(), [](GeneratedObjectSpec const& a, GeneratedObjectSpec const& b) {
        return a.priorityTier < b.priorityTier;
    });

    // Обязательный минимум — темное заполнение и внешний контур (Tier 0),
    // а количество деталей (Tier 1..3) напрямую масштабируется от выбранного пользователем числа
    int minRequired = 0;
    for (auto const& c : candidates) {
        if (c.priorityTier == 0) minRequired++;
    }

    int desiredTotal = (std::max)(minRequired + 4, targetCount);
    if (static_cast<int>(candidates.size()) > desiredTotal) {
        candidates.resize(desiredTotal);
    }

    return candidates;
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

    // Окрашиваем исходные выделенные блоки и шипы в цвета палитры через нативный HSV
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
// Всплывающее меню ИИ-декоратора (Geode v5 / GD 2.2081)
// ============================================================================
class AIDecorPopup : public geode::Popup {
protected:
    EditorUI* m_editorUI = nullptr;
    ContourAnalysis m_contour;
    DecorStyle m_selectedStyle = DecorStyle::Neon;
    int m_targetBlockCount = 80;

    TextInput* m_promptInput = nullptr;
    TextInput* m_countInput = nullptr;
    CCLabelBMFont* m_statusLabel = nullptr;
    std::vector<ButtonSprite*> m_styleSprites;

    bool init(EditorUI* ui, ContourAnalysis contour) {
        if (!Popup::init(420.f, 280.f)) return false;

        m_editorUI = ui;
        m_contour = std::move(contour);
        m_targetBlockCount = static_cast<int>(Mod::get()->getSettingValue<int64_t>("default-block-count"));

        this->setTitle("AI Style Decorator");

        auto size = m_mainLayer->getContentSize();
        auto center = CCPoint{ size.width / 2.f, size.height / 2.f };

        int freeLayer = (ui && ui->m_editorLayer) ? findFirstFreeEditorLayerBlock(ui->m_editorLayer, 4) : 1;
        std::string regionStr = fmt::format(
            "Contour: {} cells, {} spikes | Free Layers: {}-{} (Fill/Deco/3D/Neon)",
            m_contour.cellCount(), m_contour.spikeCount(), freeLayer, freeLayer + 3
        );
        auto regionLabel = CCLabelBMFont::create(regionStr.c_str(), "chatFont.fnt");
        regionLabel->setScale(0.58f);
        regionLabel->setColor(ccColor3B{ 150, 255, 175 });
        regionLabel->setPosition(CCPoint{ center.x, center.y + 92.f });
        m_mainLayer->addChild(regionLabel);

        auto styleHeader = CCLabelBMFont::create("1. Choose Style (New Random Color & Pattern Each Click):", "bigFont.fnt");
        styleHeader->setScale(0.38f);
        styleHeader->setPosition(CCPoint{ center.x, center.y + 68.f });
        m_mainLayer->addChild(styleHeader);

        const char* styleNames[4] = { "Neon", "Modern", "Hell", "Reality" };
        float startX = center.x - 135.f;
        for (int i = 0; i < 4; ++i) {
            auto spr = ButtonSprite::create(styleNames[i]);
            spr->setScale((i == 0) ? 0.62f : 0.52f);
            spr->setColor((i == 0) ? ccColor3B{ 120, 255, 160 } : ccColor3B{ 210, 210, 210 });
            m_styleSprites.push_back(spr);

            auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(AIDecorPopup::onSelectStyle));
            btn->setTag(i);
            btn->setPosition(CCPoint{ startX + i * 90.f, center.y + 40.f });
            m_buttonMenu->addChild(btn);
        }

        auto countHeader = CCLabelBMFont::create("2. Target Detail Budget (20 - 300):", "bigFont.fnt");
        countHeader->setScale(0.4f);
        countHeader->setPosition(CCPoint{ center.x - 55.f, center.y + 6.f });
        m_mainLayer->addChild(countHeader);

        auto minusSpr = CCSprite::createWithSpriteFrameName("edit_leftBtn_001.png");
        auto minusBtn = CCMenuItemSpriteExtra::create(minusSpr, this, menu_selector(AIDecorPopup::onAdjustCount));
        minusBtn->setTag(-20);
        minusBtn->setPosition(CCPoint{ center.x + 65.f, center.y + 6.f });
        m_buttonMenu->addChild(minusBtn);

        m_countInput = TextInput::create(60.f, "80", "bigFont.fnt");
        m_countInput->setCommonFilter(CommonFilter::Uint);
        m_countInput->setString(std::to_string(m_targetBlockCount));
        m_countInput->setPosition(CCPoint{ center.x + 110.f, center.y + 6.f });
        m_mainLayer->addChild(m_countInput);

        auto plusSpr = CCSprite::createWithSpriteFrameName("edit_rightBtn_001.png");
        auto plusBtn = CCMenuItemSpriteExtra::create(plusSpr, this, menu_selector(AIDecorPopup::onAdjustCount));
        plusBtn->setTag(20);
        plusBtn->setPosition(CCPoint{ center.x + 155.f, center.y + 6.f });
        m_buttonMenu->addChild(plusBtn);

        auto chatHeader = CCLabelBMFont::create("3. Optional Prompt (or leave empty for random color/design):", "bigFont.fnt");
        chatHeader->setScale(0.36f);
        chatHeader->setPosition(CCPoint{ center.x, center.y - 26.f });
        m_mainLayer->addChild(chatHeader);

        m_promptInput = TextInput::create(360.f, "Leave empty for random color, or type: green, cyan, purple, red...", "chatFont.fnt");
        m_promptInput->setMaxCharCount(240);
        m_promptInput->setPosition(CCPoint{ center.x, center.y - 56.f });
        m_mainLayer->addChild(m_promptInput);

        m_statusLabel = CCLabelBMFont::create("Uses native 3D blocks, Eye/Skull/Emblems & 2-sided small neon!", "chatFont.fnt");
        m_statusLabel->setScale(0.56f);
        m_statusLabel->setColor(ccColor3B{ 200, 255, 200 });
        m_statusLabel->setPosition(CCPoint{ center.x, center.y - 86.f });
        m_mainLayer->addChild(m_statusLabel);

        auto genSpr = ButtonSprite::create("Generate Decor");
        genSpr->setScale(0.75f);
        auto genBtn = CCMenuItemSpriteExtra::create(genSpr, this, menu_selector(AIDecorPopup::onGenerate));
        genBtn->setPosition(CCPoint{ center.x, center.y - 116.f });
        m_buttonMenu->addChild(genBtn);

        return true;
    }

    void onSelectStyle(CCObject* sender) {
        int tag = static_cast<CCNode*>(sender)->getTag();
        m_selectedStyle = static_cast<DecorStyle>(tag);

        const char* styleNames[4] = { "Neon", "Modern", "Hell", "Reality" };
        for (int i = 0; i < 4; ++i) {
            if (i < static_cast<int>(m_styleSprites.size()) && m_styleSprites[i]) {
                m_styleSprites[i]->setScale((i == tag) ? 0.62f : 0.52f);
                m_styleSprites[i]->setColor((i == tag) ? ccColor3B{ 120, 255, 160 } : ccColor3B{ 210, 210, 210 });
            }
        }
        m_statusLabel->setString(fmt::format("Style: {} (new color & structure on every click)", styleNames[tag]).c_str());
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
        EditorTabCatalog catalog = loadEditorTabCatalog(m_editorUI);
        StylePalette palette = buildDynamicPalette(m_selectedStyle, userPrompt, m_contour, rng);
        auto specs = generateVariedContourDecor(m_contour, catalog, m_selectedStyle, m_targetBlockCount, userPrompt, rng);
        SpawnSummary summary = spawnContourDecorInEditor(m_editorUI, m_contour, specs, palette);

        Notification::create(
            fmt::format(
                "Built {} blocks on Layers {}-{}!",
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
