#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/web.hpp>
#include <random>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <vector>
#include <string>

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#undef min
#undef max
#endif

using namespace geode::prelude;

// ============================================================================
// Стили декора и структуры данных
// ============================================================================
enum class DecorStyle {
    Neon = 0,
    Modern = 1,
    Hell = 2,
    Reality = 3
};

struct SelectionBounds {
    float minX = 0.f;
    float minY = 0.f;
    float maxX = 0.f;
    float maxY = 0.f;
    int selectedCount = 0;
    bool valid = false;

    float width() const { return (std::max)(30.f, maxX - minX); }
    float height() const { return (std::max)(30.f, maxY - minY); }
};

struct GeneratedObjectSpec {
    int objectId = 211;
    float x = 0.f;
    float y = 0.f;
    float rotation = 0.f;
    float scale = 1.0f;
    int zOrder = 2;
};

// ============================================================================
// Анализ текстового промпта (ИИ-чат) и процедурный генератор декора
// ============================================================================
struct PromptDirectives {
    bool extraGlow = false;
    bool addSaws = false;
    bool addChains = false;
    bool addSpikes = false;
    bool darkBg = false;
    bool highDensity = false;
};

static std::string toLowerASCII(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    }
    return s;
}

static PromptDirectives parseChatPrompt(std::string const& rawPrompt) {
    PromptDirectives d;
    std::string p = toLowerASCII(rawPrompt);

    if (p.find("glow") != std::string::npos || p.find("свеч") != std::string::npos || p.find("неон") != std::string::npos || p.find("свет") != std::string::npos) {
        d.extraGlow = true;
    }
    if (p.find("saw") != std::string::npos || p.find("пил") != std::string::npos || p.find("шестер") != std::string::npos) {
        d.addSaws = true;
    }
    if (p.find("chain") != std::string::npos || p.find("цеп") != std::string::npos || p.find("висяч") != std::string::npos) {
        d.addChains = true;
    }
    if (p.find("spike") != std::string::npos || p.find("шип") != std::string::npos || p.find("остр") != std::string::npos) {
        d.addSpikes = true;
    }
    if (p.find("dark") != std::string::npos || p.find("темн") != std::string::npos || p.find("черн") != std::string::npos) {
        d.darkBg = true;
    }
    if (p.find("dense") != std::string::npos || p.find("плотн") != std::string::npos || p.find("мног") != std::string::npos) {
        d.highDensity = true;
    }

    return d;
}

static std::vector<GeneratedObjectSpec> generateProceduralStyleDecor(
    SelectionBounds const& bounds,
    DecorStyle style,
    int targetCount,
    std::string const& promptText
) {
    std::vector<GeneratedObjectSpec> result;
    result.reserve(targetCount);

    PromptDirectives dir = parseChatPrompt(promptText);
    std::mt19937 rng(std::random_device{}());

    float minX = bounds.minX;
    float maxX = bounds.maxX;
    float minY = bounds.minY;
    float maxY = bounds.maxY;

    std::uniform_real_distribution<float> distX(minX, maxX);
    std::uniform_real_distribution<float> distY(minY, maxY);
    std::uniform_int_distribution<int> rot90(0, 3);

    int gridCols = (std::max)(1, static_cast<int>(std::round(bounds.width() / 30.f)));
    int gridRows = (std::max)(1, static_cast<int>(std::round(bounds.height() / 30.f)));

    // 1. Фоновая структура по сетке (30% бюджета блоков)
    int baseBudget = (std::max)(2, targetCount * 30 / 100);
    int placedBase = 0;

    for (int r = 0; r < gridRows && placedBase < baseBudget; ++r) {
        for (int c = 0; c < gridCols && placedBase < baseBudget; ++c) {
            float cellX = minX + c * 30.f;
            float cellY = minY + r * 30.f;
            if (cellX > maxX + 1.f || cellY > maxY + 1.f) continue;

            GeneratedObjectSpec spec;
            spec.x = cellX;
            spec.y = cellY;
            spec.zOrder = -5;
            spec.scale = 1.0f;

            switch (style) {
                case DecorStyle::Neon:
                    spec.objectId = 211;
                    break;
                case DecorStyle::Modern:
                    spec.objectId = 211;
                    break;
                case DecorStyle::Hell:
                    spec.objectId = 446;
                    break;
                case DecorStyle::Reality:
                    spec.objectId = 211;
                    break;
            }
            result.push_back(spec);
            placedBase++;
        }
    }

    // 2. Декоративное наполнение и детали (70% бюджета блоков)
    int remaining = (std::max)(0, targetCount - static_cast<int>(result.size()));
    for (int i = 0; i < remaining; ++i) {
        GeneratedObjectSpec spec;
        bool snapToHalfGrid = (i % 2 == 0);
        if (snapToHalfGrid) {
            float rx = std::round((distX(rng) - minX) / 15.f) * 15.f + minX;
            float ry = std::round((distY(rng) - minY) / 15.f) * 15.f + minY;
            spec.x = std::clamp(rx, minX, maxX);
            spec.y = std::clamp(ry, minY, maxY);
        } else {
            spec.x = distX(rng);
            spec.y = distY(rng);
        }

        spec.rotation = static_cast<float>(rot90(rng) * 90);

        if (style == DecorStyle::Neon) {
            if (i % 3 == 0 || dir.extraGlow) {
                spec.objectId = 1888;
                spec.scale = 1.25f + (i % 3) * 0.35f;
                spec.zOrder = 10;
            } else if (dir.addSaws && i % 5 == 0) {
                spec.objectId = 1705;
                spec.scale = 0.75f;
                spec.zOrder = 4;
            } else {
                int neonPool[] = { 503, 504, 505, 579, 211 };
                spec.objectId = neonPool[i % 5];
                spec.scale = (spec.objectId == 211) ? 0.45f : 1.0f;
                spec.zOrder = 5;
            }
        }
        else if (style == DecorStyle::Modern) {
            if (dir.addSaws && i % 4 == 0) {
                spec.objectId = 1705;
                spec.scale = 0.65f + (i % 2) * 0.25f;
                spec.zOrder = 3;
            } else if (i % 4 == 0) {
                spec.objectId = 211;
                spec.scale = (i % 2 == 0) ? 0.35f : 0.6f;
                spec.rotation = 0.f;
                spec.zOrder = 6;
            } else {
                int modernPool[] = { 211, 503, 579, 505 };
                spec.objectId = modernPool[i % 4];
                spec.scale = 0.5f + (i % 3) * 0.25f;
                spec.zOrder = 4;
            }
        }
        else if (style == DecorStyle::Hell) {
            if (i % 3 == 0 || dir.extraGlow) {
                spec.objectId = 1888;
                spec.scale = 1.4f;
                spec.zOrder = 9;
            } else if (dir.addSaws || i % 5 == 0) {
                spec.objectId = 88;
                spec.scale = 0.7f + (i % 2) * 0.3f;
                spec.zOrder = 3;
            } else if (dir.addSpikes || i % 4 == 0) {
                spec.objectId = 8;
                spec.scale = 0.65f;
                spec.zOrder = 7;
            } else {
                int hellPool[] = { 446, 447, 503, 211 };
                spec.objectId = hellPool[i % 4];
                spec.scale = 0.85f + (i % 2) * 0.25f;
                spec.zOrder = 5;
            }
        }
        else if (style == DecorStyle::Reality) {
            if (i % 2 == 0) {
                spec.objectId = 1888;
                spec.scale = 1.1f + (i % 4) * 0.45f;
                spec.zOrder = (i % 4 == 0) ? 12 : 4;
            } else {
                int realPool[] = { 406, 407, 408, 446, 211 };
                spec.objectId = realPool[i % 5];
                spec.scale = 0.75f + (i % 3) * 0.2f;
                spec.zOrder = 6;
            }
        }

        result.push_back(spec);
    }

    return result;
}

// ============================================================================
// Спавн сгенерированных объектов в LevelEditorLayer
// ============================================================================
static int spawnGeneratedObjectsInEditor(EditorUI* ui, std::vector<GeneratedObjectSpec> const& specs) {
    if (!ui || !ui->m_editorLayer || specs.empty()) return 0;

    auto* editorLayer = ui->m_editorLayer;
    bool tagGroup999 = Mod::get()->getSettingValue<bool>("clear-previous-decor");
    int createdCount = 0;

    for (auto const& s : specs) {
        GameObject* obj = editorLayer->createObject(s.objectId, CCPoint{s.x, s.y}, false);
        if (!obj) continue;

        obj->setRotation(s.rotation);
        if (std::abs(s.scale - 1.0f) > 0.01f) {
            obj->setScale(s.scale);
        }
        obj->setZOrder(s.zOrder);

        if (tagGroup999) {
            editorLayer->addToGroup(obj, 999, false);
        }

        createdCount++;
    }

    return createdCount;
}

// ============================================================================
// Всплывающее меню ИИ-декоратора на базе geode::Popup (Geode v4.9.0)
// ============================================================================
class AIDecorPopup : public geode::Popup<EditorUI*, SelectionBounds> {
protected:
    EditorUI* m_editorUI = nullptr;
    SelectionBounds m_bounds;
    DecorStyle m_selectedStyle = DecorStyle::Neon;
    int m_targetBlockCount = 80;

    TextInput* m_promptInput = nullptr;
    TextInput* m_countInput = nullptr;
    CCLabelBMFont* m_statusLabel = nullptr;
    std::vector<ButtonSprite*> m_styleSprites;
    EventListener<web::WebTask> m_webListener;

    bool setup(EditorUI* ui, SelectionBounds bounds) override {
        m_editorUI = ui;
        m_bounds = bounds;
        m_targetBlockCount = static_cast<int>(Mod::get()->getSettingValue<int64_t>("default-block-count"));

        this->setTitle("AI Style Decorator");

        auto size = m_mainLayer->getContentSize();
        auto center = CCPoint{ size.width / 2.f, size.height / 2.f };

        std::string regionStr = fmt::format(
            "Selected Area: {:.0f}x{:.0f} ({} base blocks selected)",
            m_bounds.width(), m_bounds.height(), m_bounds.selectedCount
        );
        auto regionLabel = CCLabelBMFont::create(regionStr.c_str(), "chatFont.fnt");
        regionLabel->setScale(0.65f);
        regionLabel->setColor(ccColor3B{ 180, 230, 255 });
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

        // 2. Регулятор количества блоков
        auto countHeader = CCLabelBMFont::create("2. Target Block Count (10 - 500):", "bigFont.fnt");
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

        // 3. Поле ИИ-чата
        auto chatHeader = CCLabelBMFont::create("3. AI Chat Instructions (what to add, colors, details):", "bigFont.fnt");
        chatHeader->setScale(0.38f);
        chatHeader->setPosition(CCPoint{ center.x, center.y - 26.f });
        m_mainLayer->addChild(chatHeader);

        m_promptInput = TextInput::create(360.f, "e.g. extra glow, add saws on top, spikes, dark base...", "chatFont.fnt");
        m_promptInput->setMaxCharCount(240);
        m_promptInput->setPosition(CCPoint{ center.x, center.y - 56.f });
        m_mainLayer->addChild(m_promptInput);

        m_statusLabel = CCLabelBMFont::create("Ready! Select style or describe your idea & click Generate.", "chatFont.fnt");
        m_statusLabel->setScale(0.6f);
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
        m_statusLabel->setString(fmt::format("Selected style: {}", styleNames[tag]).c_str());
    }

    void onAdjustCount(CCObject* sender) {
        int delta = static_cast<CCNode*>(sender)->getTag();
        int current = numFromString<int>(m_countInput->getString()).unwrapOr(m_targetBlockCount);
        m_targetBlockCount = std::clamp(current + delta, 10, 500);
        m_countInput->setString(std::to_string(m_targetBlockCount));
    }

    void onGenerate(CCObject*) {
        m_targetBlockCount = std::clamp(
            numFromString<int>(m_countInput->getString()).unwrapOr(80),
            10, 500
        );
        std::string userPrompt = m_promptInput ? std::string(m_promptInput->getString()) : "";

        std::string apiKey = Mod::get()->getSettingValue<std::string>("ai-api-key");
        std::string apiUrl = Mod::get()->getSettingValue<std::string>("ai-api-url");

        if (!apiKey.empty() && !apiUrl.empty()) {
            requestCloudAIDecor(apiUrl, apiKey, userPrompt);
        } else {
            runProceduralGeneration(userPrompt);
        }
    }

    void runProceduralGeneration(std::string const& userPrompt) {
        auto specs = generateProceduralStyleDecor(m_bounds, m_selectedStyle, m_targetBlockCount, userPrompt);
        int placed = spawnGeneratedObjectsInEditor(m_editorUI, specs);

        Notification::create(
            fmt::format("AI Decorated {} objects!", placed),
            NotificationIcon::Success
        )->show();

        this->onClose(nullptr);
    }

    void requestCloudAIDecor(std::string const& apiUrl, std::string const& apiKey, std::string const& userPrompt) {
        m_statusLabel->setString("Contacting AI Server...");

        const char* styleNames[4] = { "Neon", "Modern", "Hell", "Reality" };
        std::string styleName = styleNames[static_cast<int>(m_selectedStyle)];
        std::string modelName = Mod::get()->getSettingValue<std::string>("ai-model");

        std::string systemPrompt =
            "You are a Geometry Dash 2.2 Level Editor decoration assistant. "
            "Respond ONLY with a valid JSON object containing an array field 'objects', where each item has: "
            "{\"id\": int, \"x\": float, \"y\": float, \"rot\": float, \"scale\": float, \"z\": int}. "
            "Use authentic GD object IDs (211=block, 503/504/505=neon outlines, 1888=glow, 1705/88=saw, 446/447=rock, 406/407=nature, 8=spike).";

        std::string fullUserPrompt = fmt::format(
            "Decorate bounding box minX={:.1f}, minY={:.1f}, maxX={:.1f}, maxY={:.1f} in '{}' style using ~{} objects. User request: {}",
            m_bounds.minX, m_bounds.minY, m_bounds.maxX, m_bounds.maxY,
            styleName, m_targetBlockCount,
            userPrompt.empty() ? "Standard rich decoration for this style" : userPrompt
        );

        matjson::Value msgs = matjson::Value::array();
        msgs.push(matjson::makeObject({{"role", "system"}, {"content", systemPrompt}}));
        msgs.push(matjson::makeObject({{"role", "user"}, {"content", fullUserPrompt}}));

        matjson::Value body = matjson::makeObject({
            {"model", modelName},
            {"messages", msgs},
            {"temperature", 0.7}
        });

        web::WebRequest req = web::WebRequest();
        req.header("Content-Type", "application/json");
        req.header("Authorization", fmt::format("Bearer {}", apiKey));
        req.bodyJSON(body);

        m_webListener.bind([this, userPrompt](web::WebTask::Event* e) {
            if (web::WebResponse* res = e->getValue()) {
                if (res->ok()) {
                    auto jsonRes = res->json();
                    if (jsonRes.isOk()) {
                        auto root = jsonRes.unwrap();
                        std::vector<GeneratedObjectSpec> aiSpecs;
                        auto contentStr = root["choices"][0]["message"]["content"].asString().unwrapOr("");
                        auto parsedContent = matjson::parse(contentStr);
                        if (parsedContent.isOk()) {
                            auto objs = parsedContent.unwrap()["objects"];
                            if (objs.isArray()) {
                                for (auto const& item : objs) {
                                    GeneratedObjectSpec s;
                                    s.objectId = static_cast<int>(item["id"].asInt().unwrapOr(211));
                                    s.x = static_cast<float>(item["x"].asDouble().unwrapOr(m_bounds.minX));
                                    s.y = static_cast<float>(item["y"].asDouble().unwrapOr(m_bounds.minY));
                                    s.rotation = static_cast<float>(item["rot"].asDouble().unwrapOr(0.0));
                                    s.scale = static_cast<float>(item["scale"].asDouble().unwrapOr(1.0));
                                    s.zOrder = static_cast<int>(item["z"].asInt().unwrapOr(5));
                                    aiSpecs.push_back(s);
                                }
                            }
                        }

                        if (!aiSpecs.empty()) {
                            int placed = spawnGeneratedObjectsInEditor(m_editorUI, aiSpecs);
                            Notification::create(
                                fmt::format("Cloud AI generated {} objects!", placed),
                                NotificationIcon::Success
                            )->show();
                            this->onClose(nullptr);
                            return;
                        }
                    }
                }
                runProceduralGeneration(userPrompt);
            }
        });

        m_webListener.setFilter(req.post(apiUrl));
    }

public:
    static AIDecorPopup* create(EditorUI* ui, SelectionBounds bounds) {
        auto ret = new AIDecorPopup();
        if (ret && ret->initAnchored(420.f, 280.f, ui, bounds)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

// ============================================================================
// Вычисление границ выделенной области через поля m_selectedObjects / m_selectedObject
// ============================================================================
static SelectionBounds computeSelectionBounds(EditorUI* ui) {
    SelectionBounds b;
    if (!ui) return b;

    CCArray* selected = ui->m_selectedObjects;
    if (selected && selected->count() > 0) {
        b.selectedCount = static_cast<int>(selected->count());
        b.minX = 1e9f;
        b.minY = 1e9f;
        b.maxX = -1e9f;
        b.maxY = -1e9f;

        for (unsigned int i = 0; i < selected->count(); ++i) {
            auto* obj = static_cast<GameObject*>(selected->objectAtIndex(i));
            if (!obj) continue;
            CCPoint pos = obj->getPosition();
            b.minX = (std::min)(b.minX, pos.x);
            b.minY = (std::min)(b.minY, pos.y);
            b.maxX = (std::max)(b.maxX, pos.x);
            b.maxY = (std::max)(b.maxY, pos.y);
        }

        if (b.maxX - b.minX < 30.f) {
            b.minX -= 30.f;
            b.maxX += 30.f;
        }
        if (b.maxY - b.minY < 30.f) {
            b.minY -= 30.f;
            b.maxY += 30.f;
        }
        b.valid = true;
    } else if (ui->m_selectedObject) {
        CCPoint pos = ui->m_selectedObject->getPosition();
        b.minX = pos.x - 60.f;
        b.maxX = pos.x + 60.f;
        b.minY = pos.y - 60.f;
        b.maxY = pos.y + 60.f;
        b.selectedCount = 1;
        b.valid = true;
    } else if (ui->m_editorLayer && ui->m_editorLayer->m_objectLayer) {
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        CCPoint worldCenter = ui->m_editorLayer->m_objectLayer->convertToNodeSpace(
            CCPoint{ winSize.width / 2.f, winSize.height / 2.f }
        );
        float snappedX = std::round(worldCenter.x / 30.f) * 30.f + 15.f;
        float snappedY = std::round(worldCenter.y / 30.f) * 30.f + 15.f;
        b.minX = snappedX - 75.f;
        b.maxX = snappedX + 75.f;
        b.minY = snappedY - 45.f;
        b.maxY = snappedY + 45.f;
        b.selectedCount = 0;
        b.valid = true;
    }

    return b;
}

// ============================================================================
// Перехват клавиши Right Alt строго внутри редактора уровней (EditorUI)
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

    void keyDown(cocos2d::enumKeyCodes key) {
        EditorUI::keyDown(key);

#ifndef GEODE_IS_WINDOWS
        if (key == cocos2d::enumKeyCodes::KEY_Alt) {
            openAIDecorMenu();
        }
#endif
    }

    void openAIDecorMenu() {
        auto* scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;
        if (scene->getChildByID("ai-decor-popup"_spr)) return;

        SelectionBounds bounds = computeSelectionBounds(this);
        if (auto* popup = AIDecorPopup::create(this, bounds)) {
            popup->setID("ai-decor-popup"_spr);
            popup->show();
        }
    }
};
