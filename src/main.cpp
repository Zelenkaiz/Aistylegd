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
// Стили декора и слоты цветовой палитры (строго единый цвет на один тип/слой)
// ============================================================================
enum class DecorStyle {
    Neon = 0,
    Modern = 1,
    Hell = 2,
    Reality = 3
};

enum class ColorSlot : int {
    DarkFill = 0,         // Слой +0: темное внутреннее заполнение
    Dark3D = 1,           // Слой +0/+2: темная заливка 3D-граней и подложек
    MidTone = 2,          // Слой +1: внутренние арки, тех-полосы и средний шип
    BrightMain = 3,       // Слой +1/+3: основной неон, глазки/череп, трава, внутренний шип
    HighlightOutline = 4, // Слой +2: яркий внешний контур, 3D-линии и микро-квадраты
    AdditiveGlow = 5      // Слой +3: мягкое неоновое свечение (с аддитивным смешиванием)
};

enum class LayerRole : int {
    Fill = 0,        // firstFreeLayer + 0: Заполнение
    InnerDetail = 1, // firstFreeLayer + 1: Внутренний узор, арки, ядро
    Outline3D = 2,   // firstFreeLayer + 2: Внешний контур, 3D и вложенные шипы
    NeonAndGrass = 3 // firstFreeLayer + 3: Неоновый глоу и внешняя трава/капли
};

struct StylePalette {
    ccColor3B masterColor{ 115, 255, 28 };
    ccColor3B slotColors[6];
    GLubyte slotOpacity[6];
    bool slotBlending[6];
    int slotChannelID[6];
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
};

struct LiveColoredNode {
    Ref<GameObject> obj;
    ccColor3B color{ 255, 255, 255 };
    GLubyte opacity = 255;
    bool additiveBlend = false;
};

static std::vector<LiveColoredNode> g_liveColoredObjects;

// ============================================================================
// Безопасные C++23 шаблоны для работы с полями GameObject и LevelEditorLayer
// (не требуют внешних символов линковщика и гарантируют 100% сборку на 2.2081)
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
static void configureGJSpriteColor(SpriteColorT* sc, int channelID, ccColor3B rgb, GLubyte opacity, bool blending) {
    if (!sc) return;
    float alphaF = static_cast<float>(opacity) / 255.f;
    if constexpr (requires { sc->m_colorID = channelID; }) {
        sc->m_colorID = channelID;
    }
    if constexpr (requires { sc->m_defaultColorID = channelID; }) {
        sc->m_defaultColorID = channelID;
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
    if constexpr (requires { sc->m_usesHSV = false; }) {
        sc->m_usesHSV = false;
    }
}

template <typename ColorActionT>
static void configureColorActionStruct(ColorActionT* ca, ccColor3B rgb, GLubyte opacity, bool blending) {
    if (!ca) return;
    float alphaF = static_cast<float>(opacity) / 255.f;
    if constexpr (requires { ca->m_fromColor = rgb; }) {
        ca->m_fromColor = rgb;
    }
    if constexpr (requires { ca->m_toColor = rgb; }) {
        ca->m_toColor = rgb;
    }
    if constexpr (requires { ca->m_color = rgb; }) {
        ca->m_color = rgb;
    }
    if constexpr (requires { ca->m_currentColor = rgb; }) {
        ca->m_currentColor = rgb;
    }
    if constexpr (requires { ca->m_blending = blending; }) {
        ca->m_blending = blending;
    }
    if constexpr (requires { ca->m_opacity = alphaF; }) {
        ca->m_opacity = alphaF;
    }
    if constexpr (requires { ca->m_fromOpacity = alphaF; }) {
        ca->m_fromOpacity = alphaF;
    }
}

template <typename EffectMgrT>
static void syncEffectManagerChannels(EffectMgrT* em, StylePalette const& pal) {
    if (!em) return;
    if constexpr (requires { em->m_colorActionVector; }) {
        auto& vec = em->m_colorActionVector;
        for (int s = 0; s < 6; ++s) {
            int ch = pal.slotChannelID[s];
            if (ch >= 0 && ch < static_cast<int>(vec.size()) && vec[ch]) {
                configureColorActionStruct(vec[ch], pal.slotColors[s], pal.slotOpacity[s], pal.slotBlending[s]);
            }
        }
    }
}

template <typename EditorLayerT>
static void syncPaletteWithLevelChannels(EditorLayerT* editorLayer, StylePalette const& pal) {
    if (!editorLayer) return;
    if constexpr (requires { editorLayer->m_effectManager; }) {
        syncEffectManagerChannels(editorLayer->m_effectManager, pal);
    }
    if constexpr (requires { editorLayer->m_levelSettings; }) {
        if (editorLayer->m_levelSettings) {
            if constexpr (requires { editorLayer->m_levelSettings->m_effectManager; }) {
                syncEffectManagerChannels(editorLayer->m_levelSettings->m_effectManager, pal);
            }
        }
    }
}

static void enforceVisualColorOnNode(GameObject* obj, ccColor3B color, GLubyte opacity, bool additiveBlend) {
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
    int channelID = pal.slotChannelID[idx];

    if constexpr (requires { obj->m_baseColor; }) {
        configureGJSpriteColor(obj->m_baseColor, channelID, col, op, blend);
    }
    if constexpr (requires { obj->m_detailColor; }) {
        configureGJSpriteColor(obj->m_detailColor, channelID, col, op, blend);
    }

    enforceVisualColorOnNode(obj, col, op, blend);
    g_liveColoredObjects.push_back(LiveColoredNode{ obj, col, op, blend });
}

// ============================================================================
// Автоматический поиск первого незаполненного слоя редактора (Editor Layer)
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
// Определение цветовой палитры по стилю, промпту и выделенным блокам
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

static StylePalette buildCohesivePalette(
    DecorStyle style,
    std::string const& rawPrompt,
    ContourAnalysis const& contour,
    int baseChannelStart = 900
) {
    StylePalette pal;
    std::string p = toLowerASCII(rawPrompt);

    // 1. Базовый цвет по выбранному стилю
    switch (style) {
        case DecorStyle::Neon:
            pal.masterColor = ccColor3B{ 115, 255, 25 }; // Яркий кислотно-зеленый неон как на референсах
            break;
        case DecorStyle::Modern:
            pal.masterColor = ccColor3B{ 45, 215, 255 }; // Чистый голубой модерн
            break;
        case DecorStyle::Hell:
            pal.masterColor = ccColor3B{ 255, 48, 22 };  // Адский красно-огненный
            break;
        case DecorStyle::Reality:
            pal.masterColor = ccColor3B{ 50, 230, 115 }; // Изумрудно-природный
            break;
    }

    // 2. Если у выделенных объектов уже был свой цвет — отталкиваемся от него
    if (contour.hasCustomSelectionColor) {
        pal.masterColor = contour.selectionColor;
    }

    // 3. Если пользователь явно указал цвет в промпте — приоритет промпту
    if (p.find("green") != std::string::npos || p.find("зелен") != std::string::npos || p.find("лайм") != std::string::npos) {
        pal.masterColor = ccColor3B{ 115, 255, 25 };
    } else if (p.find("cyan") != std::string::npos || p.find("голуб") != std::string::npos || p.find("бирюз") != std::string::npos) {
        pal.masterColor = ccColor3B{ 35, 240, 255 };
    } else if (p.find("blue") != std::string::npos || p.find("син") != std::string::npos) {
        pal.masterColor = ccColor3B{ 55, 135, 255 };
    } else if (p.find("purple") != std::string::npos || p.find("фиолет") != std::string::npos || p.find("пурпур") != std::string::npos) {
        pal.masterColor = ccColor3B{ 185, 65, 255 };
    } else if (p.find("pink") != std::string::npos || p.find("розов") != std::string::npos || p.find("маджент") != std::string::npos) {
        pal.masterColor = ccColor3B{ 255, 65, 185 };
    } else if (p.find("red") != std::string::npos || p.find("красн") != std::string::npos || p.find("алый") != std::string::npos) {
        pal.masterColor = ccColor3B{ 255, 42, 32 };
    } else if (p.find("orange") != std::string::npos || p.find("оранж") != std::string::npos) {
        pal.masterColor = ccColor3B{ 255, 140, 25 };
    } else if (p.find("yellow") != std::string::npos || p.find("желт") != std::string::npos || p.find("золот") != std::string::npos) {
        pal.masterColor = ccColor3B{ 255, 235, 35 };
    }

    // 4. Строим 6 фиксированных градаций от темного к светлому (один слот = один цвет)
    pal.slotColors[static_cast<int>(ColorSlot::DarkFill)] = ccColor3B{
        static_cast<GLubyte>(std::clamp(pal.masterColor.r * 0.08f + 4.f, 4.f, 28.f)),
        static_cast<GLubyte>(std::clamp(pal.masterColor.g * 0.09f + 6.f, 6.f, 34.f)),
        static_cast<GLubyte>(std::clamp(pal.masterColor.b * 0.08f + 4.f, 4.f, 28.f))
    };
    pal.slotColors[static_cast<int>(ColorSlot::Dark3D)]           = scaleRGB(pal.masterColor, 0.25f);
    pal.slotColors[static_cast<int>(ColorSlot::MidTone)]          = scaleRGB(pal.masterColor, 0.58f);
    pal.slotColors[static_cast<int>(ColorSlot::BrightMain)]       = pal.masterColor;
    pal.slotColors[static_cast<int>(ColorSlot::HighlightOutline)] = mixTowardWhite(pal.masterColor, 0.58f);
    pal.slotColors[static_cast<int>(ColorSlot::AdditiveGlow)]     = pal.masterColor;

    pal.slotOpacity[static_cast<int>(ColorSlot::DarkFill)]         = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::Dark3D)]           = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::MidTone)]          = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::BrightMain)]       = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::HighlightOutline)] = 255;
    pal.slotOpacity[static_cast<int>(ColorSlot::AdditiveGlow)]     = 68;

    for (int i = 0; i < 6; ++i) {
        pal.slotBlending[i] = (i == static_cast<int>(ColorSlot::AdditiveGlow));
        pal.slotChannelID[i] = baseChannelStart + i;
    }

    return pal;
}

// ============================================================================
// Точное считывание выделенного контура (клетки 30x30 + шипы)
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

    // Любая клетка внутри замкнутого контура, не достижимая снаружи, является внутренней частью постройки
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
        // Если ничего не выделено, берем аккуратный контур 3x2 в центре камеры
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

            ccColor3B c = obj->getColor();
            if ((c.r < 245 || c.g < 245 || c.b < 245) && (c.r > 15 || c.g > 15 || c.b > 15)) {
                info.hasCustomSelectionColor = true;
                info.selectionColor = c;
            }

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
// Разбиение выделенных клеток на связные острова (отдельные блоки, колонны, платформы)
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
// Генератор структуры строго внутри выделенного контура (по референсам пользователя)
// ============================================================================
static void addRectPiece(
    std::vector<GeneratedObjectSpec>& out,
    float x, float y,
    float sx, float sy,
    float rot,
    LayerRole role,
    ColorSlot slot,
    int zOrder
) {
    GeneratedObjectSpec s;
    s.objectId = 211;
    s.x = x;
    s.y = y;
    s.scaleX = sx;
    s.scaleY = sy;
    s.rotation = rot;
    s.layerRole = role;
    s.colorSlot = slot;
    s.zOrder = zOrder;
    s.noTouch = true;
    s.noGlow = true;
    out.push_back(s);
}

static void addHollowMicroSquare(
    std::vector<GeneratedObjectSpec>& out,
    float x, float y,
    float outerScale,
    ColorSlot frameSlot
) {
    // Внешний светящийся квадратик (как на референсах колонны и платформ)
    addRectPiece(out, x, y, outerScale, outerScale, 0.f, LayerRole::InnerDetail, frameSlot, 6);
    // Темная сердцевина внутри квадратика
    addRectPiece(out, x, y, outerScale * 0.52f, outerScale * 0.52f, 0.f, LayerRole::InnerDetail, ColorSlot::DarkFill, 7);
}

static void addEyeEmblemInCell(std::vector<GeneratedObjectSpec>& out, float cx, float cy) {
    // Ромбовидно-миндалевидный неоновый глаз внутри одиночного блока 1x1 (как на референсе 1)
    addRectPiece(out, cx, cy, 0.44f, 0.44f, 45.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 5);
    addRectPiece(out, cx, cy, 0.54f, 0.28f, 0.f,  LayerRole::InnerDetail, ColorSlot::BrightMain, 5);
    // Темная радужка
    addRectPiece(out, cx, cy, 0.25f, 0.25f, 45.f, LayerRole::InnerDetail, ColorSlot::DarkFill, 6);
    // Два вертикальных светящихся зрачка-точки внутри глаза (точно как на скриншоте!)
    addRectPiece(out, cx, cy + 2.3f, 0.07f, 0.07f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 7);
    addRectPiece(out, cx, cy - 2.3f, 0.07f, 0.07f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 7);
}

static void addSkullEmblemInCell(std::vector<GeneratedObjectSpec>& out, float cx, float cy) {
    // Пиксельно-геометрический неоновый череп в правом верхнем углу платформы (как на референсе 1)
    addRectPiece(out, cx,        cy + 1.5f, 0.42f, 0.28f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 6);
    addRectPiece(out, cx,        cy + 5.2f, 0.30f, 0.14f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 6);
    addRectPiece(out, cx - 0.8f, cy - 4.0f, 0.24f, 0.14f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 6);
    // Глазницы и носовая впадина
    addRectPiece(out, cx - 3.2f, cy + 1.2f, 0.10f, 0.10f, 0.f, LayerRole::InnerDetail, ColorSlot::DarkFill, 7);
    addRectPiece(out, cx + 2.8f, cy + 1.2f, 0.10f, 0.10f, 0.f, LayerRole::InnerDetail, ColorSlot::DarkFill, 7);
    addRectPiece(out, cx - 0.2f, cy - 1.8f, 0.06f, 0.08f, 0.f, LayerRole::InnerDetail, ColorSlot::DarkFill, 7);
}

static std::vector<GeneratedObjectSpec> generateContourAwareDecor(
    ContourAnalysis const& contour,
    DecorStyle style,
    int averageTargetCount,
    std::string const& rawPrompt
) {
    std::vector<GeneratedObjectSpec> result;
    std::string p = toLowerASCII(rawPrompt);

    // Коэффициент детализации от среднего значения (по умолчанию 80 = 1.0x)
    float densityFactor = std::clamp(static_cast<float>(averageTargetCount) / 80.f, 0.45f, 2.2f);
    bool wantGrass = (style == DecorStyle::Neon || style == DecorStyle::Reality || p.find("трав") != std::string::npos || p.find("grass") != std::string::npos);
    bool want3D = (p.find("no 3d") == std::string::npos && p.find("без 3д") == std::string::npos);

    auto islands = extractIslands(contour.occupiedCells);

    for (auto const& isl : islands) {
        int cols = isl.cols();
        int rows = isl.rows();
        bool isSingleCube = (isl.cells.size() == 1);

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
            // 1. Слой +0 (Fill): Темное контрастное заполнение строго в клетке
            // ================================================================
            addRectPiece(result, cx, cy, 1.00f, 1.00f, 0.f, LayerRole::Fill, ColorSlot::DarkFill, 1);

            // ================================================================
            // 2. Слой +1 (InnerDetail): Внутренняя геометрия по референсам
            // ================================================================
            if (isSingleCube) {
                // Одиночный блок 1x1: внутренний контур + 4 угловых микро-квадрата + глаз в центре
                addRectPiece(result, cx, cy, 0.84f, 0.84f, 0.f, LayerRole::InnerDetail, ColorSlot::Dark3D, 2);
                addRectPiece(result, cx, cy, 0.74f, 0.74f, 0.f, LayerRole::InnerDetail, ColorSlot::DarkFill, 3);

                if (densityFactor >= 0.65f) {
                    addHollowMicroSquare(result, cx - 8.2f, cy + 8.2f, 0.15f, ColorSlot::MidTone);
                    addHollowMicroSquare(result, cx + 8.2f, cy + 8.2f, 0.15f, ColorSlot::MidTone);
                    addHollowMicroSquare(result, cx - 8.2f, cy - 8.2f, 0.15f, ColorSlot::MidTone);
                    addHollowMicroSquare(result, cx + 8.2f, cy - 8.2f, 0.15f, ColorSlot::MidTone);
                }
                addEyeEmblemInCell(result, cx, cy);
            } else {
                // Многоклеточная структура (платформа 3x2 или высокая колонна 3x5 как на референсе 2)
                // Вертикальные тех-стойки внутри каждой клетки
                addRectPiece(result, cx - 10.5f, cy, 0.10f, 0.98f, 0.f, LayerRole::InnerDetail, ColorSlot::Dark3D, 2);
                addRectPiece(result, cx + 10.5f, cy, 0.10f, 0.98f, 0.f, LayerRole::InnerDetail, ColorSlot::Dark3D, 2);

                if (densityFactor >= 0.75f) {
                    // Горизонтальные внутренние перемычки
                    addRectPiece(result, cx, cy, 0.72f, 0.06f, 0.f, LayerRole::InnerDetail, ColorSlot::Dark3D, 2);
                }

                // Верхняя светящаяся шапка-полоса вдоль открытого верха структуры
                if (topOpen) {
                    addRectPiece(result, cx, cy + 10.2f, 0.96f, 0.28f, 0.f, LayerRole::InnerDetail, ColorSlot::MidTone, 4);
                    addRectPiece(result, cx, cy + 11.0f, 0.86f, 0.14f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 5);
                    if ( localCol > 0 ) {
                        addRectPiece(result, cx - 14.5f, cy + 10.2f, 0.06f, 0.28f, 0.f, LayerRole::InnerDetail, ColorSlot::HighlightOutline, 6);
                    }
                }

                // Диагональные блики 45° в левой верхней части структуры (как на референсе 1 и 2)
                if (localCol == 0 && localRow == rows - 1) {
                    addRectPiece(result, cx - 1.5f, cy + 1.5f, 0.68f, 0.14f, 45.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 5);
                    if (densityFactor >= 0.8f) {
                        addRectPiece(result, cx + 4.5f, cy - 4.5f, 0.48f, 0.06f, 45.f, LayerRole::InnerDetail, ColorSlot::MidTone, 5);
                    }
                }

                // Концентрическая внутренняя арка в центральной части колонны/платформы
                bool isCenterCol = (cols >= 3 && localCol == cols / 2) || (cols == 2 && (localCol == 0 || localCol == 1));
                if (isCenterCol && localRow < rows) {
                    // Стенки внутренней арки
                    addRectPiece(result, cx - 6.5f, cy, 0.14f, 0.98f, 0.f, LayerRole::InnerDetail, ColorSlot::MidTone, 4);
                    addRectPiece(result, cx + 6.5f, cy, 0.14f, 0.98f, 0.f, LayerRole::InnerDetail, ColorSlot::MidTone, 4);
                    addRectPiece(result, cx - 4.5f, cy, 0.05f, 0.98f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 5);
                    addRectPiece(result, cx + 4.5f, cy, 0.05f, 0.98f, 0.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 5);

                    // Свод внутренней арки со скошенными углами 45° (точно как на референсе 2!)
                    if (localRow == rows - 1) {
                        addRectPiece(result, cx,        cy + 4.5f, 0.46f, 0.10f, 0.f,   LayerRole::InnerDetail, ColorSlot::BrightMain, 5);
                        addRectPiece(result, cx - 5.2f, cy + 2.2f, 0.22f, 0.08f, 45.f,  LayerRole::InnerDetail, ColorSlot::BrightMain, 5);
                        addRectPiece(result, cx + 5.2f, cy + 2.2f, 0.22f, 0.08f, -45.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 5);
                    } else if (densityFactor >= 0.85f) {
                        // Внутренний диагональный блик в сердцевине колонны
                        addRectPiece(result, cx, cy + 3.0f, 0.30f, 0.09f, 45.f, LayerRole::InnerDetail, ColorSlot::BrightMain, 5);
                    }
                }

                // Симметричные полые светящиеся микро-квадраты 回 по бокам арки (как на референсе 2)
                if (densityFactor >= 0.65f && localRow >= (std::max)(0, rows - 3)) {
                    if (localCol == 0) {
                        addHollowMicroSquare(result, cx - 6.5f, cy - 2.0f, 0.17f, ColorSlot::HighlightOutline);
                        if (cols >= 3 && localRow == rows - 2) {
                            addHollowMicroSquare(result, cx + 6.5f, cy + 4.0f, 0.17f, ColorSlot::HighlightOutline);
                        }
                    } else if (localCol == cols - 1) {
                        addHollowMicroSquare(result, cx + 6.5f, cy - 2.0f, 0.17f, ColorSlot::HighlightOutline);
                        if (cols >= 3 && localRow == rows - 2) {
                            addHollowMicroSquare(result, cx - 6.5f, cy + 4.0f, 0.17f, ColorSlot::HighlightOutline);
                        }
                    }
                }

                // Акцентный череп/эмблема в правом верхнем углу широкой платформы (как на референсе 1)
                if (cols >= 2 && rows >= 2 && localCol == cols - 1 && localRow == rows - 1 && style != DecorStyle::Modern) {
                    addSkullEmblemInCell(result, cx - 1.5f, cy - 2.5f);
                }
            }

            // ================================================================
            // 3. Слой +2 (Outline3D): Четкий внешний контур и 3D-объем по границе
            // ================================================================
            if (topOpen) {
                addRectPiece(result, cx, cy + 14.1f, 1.02f, 0.07f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11);
            }
            if (bottomOpen) {
                addRectPiece(result, cx, cy - 14.1f, 1.02f, 0.07f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11);
            }
            if (leftOpen) {
                addRectPiece(result, cx - 14.1f, cy, 0.07f, 1.02f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11);
            }
            if (rightOpen) {
                addRectPiece(result, cx + 14.1f, cy, 0.07f, 1.02f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11);
            }

            // 3D-экструзия на внешней правой и верхней гранях (как на кубиках в референсе 1)
            if (want3D && rightOpen) {
                addRectPiece(result, cx + 18.2f, cy + 1.5f, 0.22f, 0.98f, 0.f,  LayerRole::Outline3D, ColorSlot::Dark3D, 9);
                addRectPiece(result, cx + 21.4f, cy + 2.4f, 0.05f, 1.00f, 0.f,  LayerRole::Outline3D, ColorSlot::HighlightOutline, 10);
                addRectPiece(result, cx + 18.2f, cy - 12.6f, 0.26f, 0.05f, 24.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 10);
                if (topOpen) {
                    addRectPiece(result, cx + 18.0f, cy + 16.0f, 0.26f, 0.06f, 28.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 11);
                    if (isSingleCube) {
                        addRectPiece(result, cx + 2.5f, cy + 17.5f, 0.96f, 0.16f, 0.f, LayerRole::Outline3D, ColorSlot::Dark3D, 9);
                        addRectPiece(result, cx + 4.0f, cy + 19.8f, 0.98f, 0.05f, 0.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 10);
                        addRectPiece(result, cx - 12.6f, cy + 17.2f, 0.24f, 0.05f, 28.f, LayerRole::Outline3D, ColorSlot::HighlightOutline, 10);
                    }
                }
            }

            // ================================================================
            // 4. Слой +3 (NeonAndGrass): Декоративная трава, неоновые капли и мягкий глоу
            // ================================================================
            if (topOpen && wantGrass) {
                int const grassIDs[3] = { 406, 407, 408 };
                GeneratedObjectSpec g1;
                g1.objectId = grassIDs[(gx + gy + 100) % 3];
                g1.x = cx - 5.5f;
                g1.y = cy + 16.5f;
                g1.scaleX = 0.65f;
                g1.scaleY = 0.68f;
                g1.layerRole = LayerRole::NeonAndGrass;
                g1.colorSlot = ColorSlot::BrightMain;
                g1.zOrder = 14;
                result.push_back(g1);

                if (densityFactor >= 0.8f) {
                    GeneratedObjectSpec g2 = g1;
                    g2.objectId = grassIDs[(gx + gy + 101) % 3];
                    g2.x = cx + 5.5f;
                    g2.scaleX = 0.52f;
                    g2.scaleY = 0.58f;
                    g2.flipX = true;
                    result.push_back(g2);
                }
            }

            // Неоновые капли снизу висячих блоков (как под блоком справа на референсе 1)
            if (bottomOpen && cy > 55.f && densityFactor >= 0.75f && style == DecorStyle::Neon) {
                addRectPiece(result, cx - 4.5f, cy - 17.5f, 0.06f, 0.22f, 0.f, LayerRole::NeonAndGrass, ColorSlot::BrightMain, 14);
                addRectPiece(result, cx + 3.5f, cy - 16.8f, 0.06f, 0.16f, 0.f, LayerRole::NeonAndGrass, ColorSlot::BrightMain, 14);
            }

            // Мягкое неоновое свечение строго по внешним краям контура
            if (topOpen || leftOpen || rightOpen || bottomOpen) {
                GeneratedObjectSpec glow;
                glow.objectId = 1888;
                glow.x = cx;
                glow.y = cy;
                glow.scaleX = 1.25f;
                glow.scaleY = 1.25f;
                glow.layerRole = LayerRole::NeonAndGrass;
                glow.colorSlot = ColorSlot::AdditiveGlow;
                glow.customOpacity = 62;
                glow.zOrder = -2;
                result.push_back(glow);
            }
        }
    }

    // ========================================================================
    // 5. Многослойное декорирование выделенных шипов (Nested Neon Spikes + Glow)
    // ========================================================================
    for (auto const& sp : contour.spikes) {
        // Три вложенных шипа внутри контура выделенного шипа (как на обоих референсах!)
        float const scales[3] = { 0.72f, 0.46f, 0.22f };
        ColorSlot const slots[3] = {
            ColorSlot::BrightMain,
            ColorSlot::MidTone,
            ColorSlot::HighlightOutline
        };

        for (int k = 0; k < 3; ++k) {
            GeneratedObjectSpec innerSp;
            innerSp.objectId = sp.objectId;
            innerSp.x = sp.x;
            innerSp.y = sp.y;
            innerSp.rotation = sp.rotation;
            innerSp.scaleX = sp.scaleX * scales[k];
            innerSp.scaleY = sp.scaleY * scales[k];
            innerSp.flipX = sp.flipX;
            innerSp.flipY = sp.flipY;
            innerSp.layerRole = LayerRole::Outline3D;
            innerSp.colorSlot = slots[k];
            innerSp.zOrder = 11 + k;
            innerSp.noTouch = true;
            innerSp.noGlow = true;
            result.push_back(innerSp);
        }

        // Мягкое неоновое свечение вокруг шипа на слое неона
        GeneratedObjectSpec spGlow;
        spGlow.objectId = 1888;
        spGlow.x = sp.x;
        spGlow.y = sp.y;
        spGlow.scaleX = 1.20f * sp.scaleX;
        spGlow.scaleY = 1.20f * sp.scaleY;
        spGlow.layerRole = LayerRole::NeonAndGrass;
        spGlow.colorSlot = ColorSlot::AdditiveGlow;
        spGlow.customOpacity = 68;
        spGlow.zOrder = -1;
        result.push_back(spGlow);
    }

    return result;
}

// ============================================================================
// Спавн сгенерированных объектов с привязкой к слоям и цветовой палитре
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

    // Синхронизируем палитру с цветовыми каналами уровня
    syncPaletteWithLevelChannels(editorLayer, palette);

    // Окрашиваем исходные блоки контура в темный базовый цвет, а исходные шипы — в яркий контур,
    // чтобы стандартные белые текстуры не просвечивали и хитбоксы оставались на своих местах
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
// Всплывающее меню ИИ-декоратора на базе geode::Popup (Geode v5 / GD 2.2081)
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
            "Contour: {} cells, {} spikes | Auto Layers: {}-{} (Fill/Detail/3D/Neon)",
            m_contour.cellCount(), m_contour.spikeCount(), freeLayer, freeLayer + 3
        );
        auto regionLabel = CCLabelBMFont::create(regionStr.c_str(), "chatFont.fnt");
        regionLabel->setScale(0.58f);
        regionLabel->setColor(ccColor3B{ 150, 255, 175 });
        regionLabel->setPosition(CCPoint{ center.x, center.y + 92.f });
        m_mainLayer->addChild(regionLabel);

        // 1. Кнопки выбора стиля: Neon, Modern, Hell, Reality
        auto styleHeader = CCLabelBMFont::create("1. Choose Style:", "bigFont.fnt");
        styleHeader->setScale(0.42f);
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

        // 2. Регулятор средней плотности блоков
        auto countHeader = CCLabelBMFont::create("2. Avg Detail Guide (Auto-Scales to Contour):", "bigFont.fnt");
        countHeader->setScale(0.38f);
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

        // 3. Поле ИИ-чата (цвет, детали, трава, 3D)
        auto chatHeader = CCLabelBMFont::create("3. AI Prompt (color: green/cyan/red/purple, grass, 3d):", "bigFont.fnt");
        chatHeader->setScale(0.37f);
        chatHeader->setPosition(CCPoint{ center.x, center.y - 26.f });
        m_mainLayer->addChild(chatHeader);

        m_promptInput = TextInput::create(360.f, "e.g. neon green, extra grass, cyan glow, skull...", "chatFont.fnt");
        m_promptInput->setMaxCharCount(240);
        m_promptInput->setPosition(CCPoint{ center.x, center.y - 56.f });
        m_mainLayer->addChild(m_promptInput);

        m_statusLabel = CCLabelBMFont::create("Strict contour mode: fills & details stay inside selected blocks!", "chatFont.fnt");
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
        m_statusLabel->setString(fmt::format("Style: {} (strict per-layer color shading)", styleNames[tag]).c_str());
    }

    void onAdjustCount(CCObject* sender) {
        int delta = static_cast<CCNode*>(sender)->getTag();
        int current = numFromString<int>(m_countInput->getString()).unwrapOr(m_targetBlockCount);
        m_targetBlockCount = std::clamp(current + delta, 20, 300);
        m_countInput->setString(std::to_string(m_targetBlockCount));
    }

    void onGenerate(CCObject*) {
        m_targetBlockCount = std::clamp(
            numFromString<int>(m_countInput->getString()).unwrapOr(80),
            20, 300
        );
        std::string userPrompt = m_promptInput ? std::string(m_promptInput->getString()) : "";

        StylePalette palette = buildCohesivePalette(m_selectedStyle, userPrompt, m_contour, 900);
        auto specs = generateContourAwareDecor(m_contour, m_selectedStyle, m_targetBlockCount, userPrompt);
        SpawnSummary summary = spawnContourDecorInEditor(m_editorUI, m_contour, specs, palette);

        Notification::create(
            fmt::format(
                "Built {} pieces on Layers {}-{}!",
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
// Перехват Right Alt и поддержание цветов/блендинга в редакторе уровней
// ============================================================================
class $modify(AIDecorEditorUI, EditorUI) {
    struct Fields {
        bool m_rightAltWasDown = false;
    };

    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;
        g_liveColoredObjects.clear();
        this->schedule(schedule_selector(AIDecorEditorUI::checkRightAltKey), 0.0f);
        return true;
    }

    void checkRightAltKey(float) {
        // Поддерживаем точные цвета и аддитивное свечение в редакторе, чтобы GD не сбрасывал их в белый
        if (!g_liveColoredObjects.empty()) {
            for (size_t i = 0; i < g_liveColoredObjects.size();) {
                auto& item = g_liveColoredObjects[i];
                if (!item.obj || !item.obj->getParent()) {
                    g_liveColoredObjects.erase(g_liveColoredObjects.begin() + i);
                    continue;
                }
                enforceVisualColorOnNode(item.obj, item.color, item.opacity, item.additiveBlend);
                ++i;
            }
        }

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
