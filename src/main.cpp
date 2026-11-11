#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/web.hpp>
#include <random>
#include <sstream>
#include <algorithm>
#include <cmath>

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
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

    float width() const { return std::max(30.f, maxX - minX); }
    float height() const { return std::max(30.f, maxY - minY); }
};

struct GeneratedObjectSpec {
    int objectId = 1;
    float x = 0.f;
    float y = 0.f;
    float rotation = 0.f;
    float scale = 1.0f;
    int zOrder = 2;
    int zLayer = 3; // B2/B1/T1
    int mainColor = 1005;
    std::string hsv = ""; // Формат GD: "h s v s_checked v_checked"
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
    bool hasCustomHue = false;
    float customHue = 0.f;
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

    // Ключевые слова на русском и английском
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

    // Определение желаемого оттенка (Hue в градусах от -180 до +180)
    if (p.find("red") != std::string::npos || p.find("красн") != std::string::npos || p.find("кров") != std::string::npos) {
        d.hasCustomHue = true; d.customHue = 0.f;
    } else if (p.find("orange") != std::string::npos || p.find("оранж") != std::string::npos || p.find("лава") != std::string::npos) {
        d.hasCustomHue = true; d.customHue = 25.f;
    } else if (p.find("yellow") != std::string::npos || p.find("желт") != std::string::npos || p.find("золот") != std::string::npos) {
        d.hasCustomHue = true; d.customHue = 55.f;
    } else if (p.find("green") != std::string::npos || p.find("зелен") != std::string::npos || p.find("трав") != std::string::npos) {
        d.hasCustomHue = true; d.customHue = 120.f;
    } else if (p.find("cyan") != std::string::npos || p.find("голуб") != std::string::npos || p.find("бирюз") != std::string::npos) {
        d.hasCustomHue = true; d.customHue = -170.f;
    } else if (p.find("blue") != std::string::npos || p.find("син") != std::string::npos) {
        d.hasCustomHue = true; d.customHue = -135.f;
    } else if (p.find("purple") != std::string::npos || p.find("фиолет") != std::string::npos || p.find("пурпур") != std::string::npos) {
        d.hasCustomHue = true; d.customHue = -75.f;
    } else if (p.find("pink") != std::string::npos || p.find("розов") != std::string::npos) {
        d.hasCustomHue = true; d.customHue = -40.f;
    }

    return d;
}

static std::string makeHSV(float h, float s, float v) {
    std::ostringstream ss;
    ss << static_cast<int>(h) << "a" << s << "a" << v << "a0a0";
    return ss.str();
}

// Генерация списка объектов под конкретный стиль, размер выделения, бюджет блоков и промпт
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

    // Палитры объектов Geometry Dash по стилям:
    // ID 211 — базовый плоский блок (Modern/База)
    // ID 503, 504, 505 — неоновые/дизайнерские контуры и панели
    // ID 1888, 1757, 1758 — мягкое свечение (Glow разных размеров)
    // ID 88, 89, 397, 1705 — пилы и вращающиеся декоративные шестерни
    // ID 446, 447, 1715 — адские камни, лавовые детали, черепа/шипы
    // ID 406, 407, 408 — трава, листва, природные детали (Reality)

    int gridCols = std::max(1, static_cast<int>(std::round(bounds.width() / 30.f)));
    int gridRows = std::max(1, static_cast<int>(std::round(bounds.height() / 30.f)));

    // 1. Фоновая подложка по сетке выделенной области (около 30% бюджета блоков)
    int baseBudget = std::max(2, targetCount * 30 / 100);
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
                    spec.objectId = 211; // Темная основа под неон
                    spec.hsv = makeHSV(dir.hasCustomHue ? dir.customHue : -150.f, 0.8f, 0.25f);
                    break;
                case DecorStyle::Modern:
                    spec.objectId = 211; // Чистая матовая плитка
                    spec.hsv = makeHSV(dir.hasCustomHue ? dir.customHue : -130.f, 0.65f, dir.darkBg ? 0.35f : 0.9f);
                    break;
                case DecorStyle::Hell:
                    spec.objectId = 446; // Текстурный темный камень
                    spec.hsv = makeHSV(dir.hasCustomHue ? dir.customHue : 5.f, 1.3f, 0.3f);
                    break;
                case DecorStyle::Reality:
                    spec.objectId = 211; // Глубокая теневая база
                    spec.hsv = makeHSV(dir.hasCustomHue ? dir.customHue : 35.f, 0.45f, 0.4f);
                    break;
            }
            result.push_back(spec);
            placedBase++;
        }
    }

    // 2. Основное наполнение и акценты (оставшиеся 70% бюджета блоков)
    int remaining = std::max(0, targetCount - static_cast<int>(result.size()));
    for (int i = 0; i < remaining; ++i) {
        GeneratedObjectSpec spec;
        // Привязка к полу-сетке (15 единиц) или свободное размещение для органичности
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
            float baseHue = dir.hasCustomHue ? dir.customHue : ((i % 2 == 0) ? -170.f : -45.f); // Циан + Маджента
            if (i % 3 == 0 || dir.extraGlow) {
                spec.objectId = 1888; // Мягкий Glow
                spec.scale = 1.25f + (i % 3) * 0.35f;
                spec.zOrder = 10;
                spec.hsv = makeHSV(baseHue, 1.2f, 1.15f);
            } else if (dir.addSaws && i % 5 == 0) {
                spec.objectId = 1705; // Неоновая пила
                spec.scale = 0.75f;
                spec.zOrder = 4;
                spec.hsv = makeHSV(baseHue, 1.1f, 1.2f);
            } else {
                int neonPool[] = { 503, 504, 505, 579, 211 };
                spec.objectId = neonPool[i % 5];
                spec.scale = (spec.objectId == 211) ? 0.45f : 1.0f;
                spec.zOrder = 5;
                spec.hsv = makeHSV(baseHue, 1.0f, 1.25f);
            }
        }
        else if (style == DecorStyle::Modern) {
            float modHue = dir.hasCustomHue ? dir.customHue : ((i % 3 == 0) ? 15.f : -140.f);
            if (dir.addSaws && i % 4 == 0) {
                spec.objectId = 1705;
                spec.scale = 0.65f + (i % 2) * 0.25f;
                spec.zOrder = 3;
                spec.hsv = makeHSV(modHue, 0.9f, 1.1f);
            } else if (i % 4 == 0) {
                // Контрастные минималистичные белые/черные вставки
                spec.objectId = 211;
                spec.scale = (i % 2 == 0) ? 0.35f : 0.6f;
                spec.rotation = 0.f;
                spec.zOrder = 6;
                spec.hsv = makeHSV(0.f, 0.0f, (i % 2 == 0) ? 1.5f : 0.2f);
            } else {
                int modernPool[] = { 211, 503, 579, 505 };
                spec.objectId = modernPool[i % 4];
                spec.scale = 0.5f + (i % 3) * 0.25f;
                spec.zOrder = 4;
                spec.hsv = makeHSV(modHue, 0.85f, 1.15f);
            }
        }
        else if (style == DecorStyle::Hell) {
            float hellHue = dir.hasCustomHue ? dir.customHue : ((i % 3 == 0) ? 20.f : 0.f); // Кроваво-красный и огненно-оранжевый
            if (i % 3 == 0 || dir.extraGlow) {
                spec.objectId = 1888; // Огненное свечение
                spec.scale = 1.4f;
                spec.zOrder = 9;
                spec.hsv = makeHSV(hellHue, 1.4f, 0.95f);
            } else if (dir.addSaws || i % 5 == 0) {
                spec.objectId = 88; // Металлическая зубчатая пила
                spec.scale = 0.7f + (i % 2) * 0.3f;
                spec.zOrder = 3;
                spec.hsv = makeHSV(0.f, 1.3f, 0.65f);
            } else if (dir.addSpikes || i % 4 == 0) {
                spec.objectId = 8; // Декоративные шипы внутри структуры
                spec.scale = 0.65f;
                spec.zOrder = 7;
                spec.hsv = makeHSV(5.f, 1.2f, 0.45f);
            } else {
                int hellPool[] = { 446, 447, 503, 211 };
                spec.objectId = hellPool[i % 4];
                spec.scale = 0.85f + (i % 2) * 0.25f;
                spec.zOrder = 5;
                spec.hsv = makeHSV(hellHue, 1.25f, 0.7f);
            }
        }
        else if (style == DecorStyle::Reality) {
            float realHue = dir.hasCustomHue ? dir.customHue : ((i % 3 == 0) ? 110.f : 35.f); // Природная зелень и теплый камень/свет
            if (i % 2 == 0) {
                // Многослойный мягкий свет и объемные тени
                spec.objectId = 1888;
                spec.scale = 1.1f + (i % 4) * 0.45f;
                spec.zOrder = (i % 4 == 0) ? 12 : 4;
                float val = (i % 4 == 0) ? 1.35f : 0.55f;
                spec.hsv = makeHSV(realHue, 0.65f, val);
            } else {
                int realPool[] = { 406, 407, 408, 446, 211 };
                spec.objectId = realPool[i % 5];
                spec.scale = 0.75f + (i % 3) * 0.2f;
                spec.zOrder = 6;
                spec.hsv = makeHSV(realHue, 0.75f, 0.9f);
            }
        }

        result.push_back(spec);
    }

    return result;
}

// ============================================================================
// Спавн сгенерированных объектов в LevelEditorLayer с поддержкой Undo (Ctrl+Z)
// ============================================================================
static int spawnGeneratedObjectsInEditor(EditorUI* ui, std::vector<GeneratedObjectSpec> const& specs) {
    if (!ui || !ui->m_editorLayer || specs.empty()) return 0;

    auto* editorLayer = ui->m_editorLayer;
    auto* createdArray = CCArray::create();
    bool tagGroup999 = Mod::get()->getSettingValue<bool>("clear-previous-decor");

    for (auto const& s : specs) {
        GameObject* obj = editorLayer->createObject(s.objectId, CCPoint{s.x, s.y}, false);
        if (!obj) continue;

        obj->setRotation(s.rotation);
        if (std::abs(s.scale - 1.0f) > 0.01f) {
            obj->m_scaleX = s.scale;
            obj->m_scaleY = s.scale;
            obj->setScale(s.scale);
        }
        obj->m_zOrder = s.zOrder;

        if (tagGroup999) {
            obj->addToGroup(999);
        }

        obj->updateStartValues();
        createdArray->addObject(obj);
    }

    if (createdArray->count() > 0) {
        ui->deselectAll();
        ui->selectObjects(createdArray, true);
        ui->updateButtons();
        ui->updateObjectInfoLabel();
    }

    return static_cast<int>(createdArray->count());
}

// ============================================================================
// Всплывающее меню ИИ-декоратора (открывается на Right Alt в редакторе)
// ============================================================================
class AIDecorPopup : public FLAlertLayer {
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

    bool init(EditorUI* ui, SelectionBounds const& bounds) {
        if (!FLAlertLayer::init(160)) return false;

        m_editorUI = ui;
        m_bounds = bounds;
        m_targetBlockCount = static_cast<int>(Mod::get()->getSettingValue<int64_t>("default-block-count"));

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto center = CCPoint{ winSize.width / 2.f, winSize.height / 2.f };

        m_mainLayer = CCLayer::create();
        this->addChild(m_mainLayer);

        // Фон окна
        auto bg = CCScale9Sprite::create("GJ_square01.png", CCRect{0, 0, 80, 80});
        bg->setContentSize(CCSize{ 420.f, 290.f });
        bg->setPosition(center);
        m_mainLayer->addChild(bg, -1);

        m_buttonMenu = CCMenu::create();
        m_buttonMenu->setPosition(CCPoint{0.f, 0.f});
        m_mainLayer->addChild(m_buttonMenu, 10);

        // Заголовок
        auto title = CCLabelBMFont::create("AI Style Decorator", "goldFont.fnt");
        title->setScale(0.75f);
        title->setPosition(CCPoint{ center.x, center.y + 122.f });
        m_mainLayer->addChild(title);

        // Кнопка закрытия
        auto closeSpr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
        closeSpr->setScale(0.85f);
        auto closeBtn = CCMenuItemSpriteExtra::create(closeSpr, this, menu_selector(AIDecorPopup::onClose));
        closeBtn->setPosition(CCPoint{ center.x - 195.f, center.y + 130.f });
        m_buttonMenu->addChild(closeBtn);

        // Информация о выделенной области
        std::string regionStr = fmt::format(
            "Selected Area: {:.0f}x{:.0f} ({} base blocks selected)",
            m_bounds.width(), m_bounds.height(), m_bounds.selectedCount
        );
        auto regionLabel = CCLabelBMFont::create(regionStr.c_str(), "chatFont.fnt");
        regionLabel->setScale(0.65f);
        regionLabel->setColor(ccColor3B{ 180, 230, 255 });
        regionLabel->setPosition(CCPoint{ center.x, center.y + 98.f });
        m_mainLayer->addChild(regionLabel);

        // 1. Кнопки выбора стиля: Neon, Modern, Hell, Reality
        auto styleHeader = CCLabelBMFont::create("1. Choose Style:", "bigFont.fnt");
        styleHeader->setScale(0.42f);
        styleHeader->setPosition(CCPoint{ center.x, center.y + 74.f });
        m_mainLayer->addChild(styleHeader);

        const char* styleNames[4] = { "Neon", "Modern", "Hell", "Reality" };
        float startX = center.x - 135.f;
        for (int i = 0; i < 4; ++i) {
            auto spr = ButtonSprite::create(
                styleNames[i], 78, true, "bigFont.fnt",
                (i == 0) ? "GJ_button_02.png" : "GJ_button_01.png",
                28.f, 0.5f
            );
            m_styleSprites.push_back(spr);

            auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(AIDecorPopup::onSelectStyle));
            btn->setTag(i);
            btn->setPosition(CCPoint{ startX + i * 90.f, center.y + 46.f });
            m_buttonMenu->addChild(btn);
        }

        // 2. Регулятор примерного количества блоков
        auto countHeader = CCLabelBMFont::create("2. Target Block Count (10 - 500):", "bigFont.fnt");
        countHeader->setScale(0.4f);
        countHeader->setPosition(CCPoint{ center.x - 55.f, center.y + 12.f });
        m_mainLayer->addChild(countHeader);

        auto minusSpr = CCSprite::createWithSpriteFrameName("edit_leftBtn_001.png");
        auto minusBtn = CCMenuItemSpriteExtra::create(minusSpr, this, menu_selector(AIDecorPopup::onAdjustCount));
        minusBtn->setTag(-20);
        minusBtn->setPosition(CCPoint{ center.x + 65.f, center.y + 12.f });
        m_buttonMenu->addChild(minusBtn);

        m_countInput = TextInput::create(60.f, "80", "bigFont.fnt");
        m_countInput->setCommonFilter(CommonFilter::Uint);
        m_countInput->setString(std::to_string(m_targetBlockCount));
        m_countInput->setPosition(CCPoint{ center.x + 110.f, center.y + 12.f });
        m_mainLayer->addChild(m_countInput);

        auto plusSpr = CCSprite::createWithSpriteFrameName("edit_rightBtn_001.png");
        auto plusBtn = CCMenuItemSpriteExtra::create(plusSpr, this, menu_selector(AIDecorPopup::onAdjustCount));
        plusBtn->setTag(20);
        plusBtn->setPosition(CCPoint{ center.x + 155.f, center.y + 12.f });
        m_buttonMenu->addChild(plusBtn);

        // 3. ИИ-чат: поле текстового запроса к нейросети
        auto chatHeader = CCLabelBMFont::create("3. AI Chat Instructions (what to add, colors, details):", "bigFont.fnt");
        chatHeader->setScale(0.38f);
        chatHeader->setPosition(CCPoint{ center.x, center.y - 22.f });
        m_mainLayer->addChild(chatHeader);

        m_promptInput = TextInput::create(360.f, "e.g. cyan & pink glow, add saws on top, dark base...", "chatFont.fnt");
        m_promptInput->setMaxCharCount(240);
        m_promptInput->setPosition(CCPoint{ center.x, center.y - 52.f });
        m_mainLayer->addChild(m_promptInput);

        // Статус генерации
        m_statusLabel = CCLabelBMFont::create("Ready! Select style or describe your idea & click Generate.", "chatFont.fnt");
        m_statusLabel->setScale(0.6f);
        m_statusLabel->setColor(ccColor3B{ 200, 255, 200 });
        m_statusLabel->setPosition(CCPoint{ center.x, center.y - 84.f });
        m_mainLayer->addChild(m_statusLabel);

        // Кнопка генерации в одно нажатие
        auto genSpr = ButtonSprite::create("Generate Decor", 180, true, "goldFont.fnt", "GJ_button_01.png", 34.f, 0.75f);
        auto genBtn = CCMenuItemSpriteExtra::create(genSpr, this, menu_selector(AIDecorPopup::onGenerate));
        genBtn->setPosition(CCPoint{ center.x, center.y - 116.f });
        m_buttonMenu->addChild(genBtn);

        this->setKeypadEnabled(true);
        this->setTouchEnabled(true);
        return true;
    }

    void onSelectStyle(CCObject* sender) {
        int tag = static_cast<CCNode*>(sender)->getTag();
        m_selectedStyle = static_cast<DecorStyle>(tag);

        const char* styleNames[4] = { "Neon", "Modern", "Hell", "Reality" };
        for (int i = 0; i < 4; ++i) {
            if (i < static_cast<int>(m_styleSprites.size()) && m_styleSprites[i]) {
                m_styleSprites[i]->updateBGImage((i == tag) ? "GJ_button_02.png" : "GJ_button_01.png");
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

        // Если задан облачный API-ключ — отправляем запрос к LLM, иначе мгновенно используем встроенный ИИ-генератор
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
            "{\"id\": int, \"x\": float, \"y\": float, \"rot\": float, \"scale\": float, \"z\": int, \"hsv\": \"h a s a v a 0 a 0\"}. "
            "Use authentic GD object IDs (211=block, 503/504/505=neon outlines, 1888=glow, 1705/88=saw, 446/447=rock, 406/407=nature, 8=spike).";

        std::string fullUserPrompt = fmt::format(
            "Decorate bounding box minX={:.1f}, minY={:.1f}, maxX={:.1f}, maxY={:.1f} in '{}' style using ~{} objects. User request: {}",
            m_bounds.minX, m_bounds.minY, m_bounds.maxX, m_bounds.maxY,
            styleName, m_targetBlockCount,
            userPrompt.empty() ? "Standard rich decoration for this style" : userPrompt
        );

        matjson::Value body = matjson::makeObject({
            {"model", modelName},
            {"messages", std::vector<matjson::Value>{
                matjson::makeObject({{"role", "system"}, {"content", systemPrompt}}),
                matjson::makeObject({{"role", "user"}, {"content", fullUserPrompt}})
            }},
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
                        // Парсинг ответа OpenAI-совместимого API; при нестандартном ответе — fallback на процедурный движок
                        std::vector<GeneratedObjectSpec> aiSpecs;
                        try {
                            auto contentStr = root["choices"][0]["message"]["content"].asString().unwrapOr("");
                            auto parsedContent = matjson::parse(contentStr);
                            if (parsedContent.isOk()) {
                                auto objs = parsedContent.unwrap()["objects"];
                                if (objs.isArray()) {
                                    for (auto const& item : objs.asArray().unwrap()) {
                                        GeneratedObjectSpec s;
                                        s.objectId = static_cast<int>(item["id"].asInt().unwrapOr(211));
                                        s.x = static_cast<float>(item["x"].asDouble().unwrapOr(m_bounds.minX));
                                        s.y = static_cast<float>(item["y"].asDouble().unwrapOr(m_bounds.minY));
                                        s.rotation = static_cast<float>(item["rot"].asDouble().unwrapOr(0.0));
                                        s.scale = static_cast<float>(item["scale"].asDouble().unwrapOr(1.0));
                                        s.zOrder = static_cast<int>(item["z"].asInt().unwrapOr(5));
                                        s.hsv = item["hsv"].asString().unwrapOr("");
                                        aiSpecs.push_back(s);
                                    }
                                }
                            }
                        } catch (...) {}

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
                // Если сервер вернул ошибку или лимит — используем встроенный генератор с учетом промпта
                runProceduralGeneration(userPrompt);
            }
        });

        m_webListener.setFilter(req.post(apiUrl));
    }

    void onClose(CCObject*) {
        this->setTouchEnabled(false);
        this->removeFromParentAndCleanup(true);
    }

    void keyBackClicked() override {
        this->onClose(nullptr);
    }

public:
    static AIDecorPopup* create(EditorUI* ui, SelectionBounds const& bounds) {
        auto ret = new AIDecorPopup();
        if (ret && ret->init(ui, bounds)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

// ============================================================================
// Вычисление границ выделенной области в редакторе
// ============================================================================
static SelectionBounds computeSelectionBounds(EditorUI* ui) {
    SelectionBounds b;
    if (!ui) return b;

    CCArray* selected = ui->getSelectedObjects();
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
            b.minX = std::min(b.minX, pos.x);
            b.minY = std::min(b.minY, pos.y);
            b.maxX = std::max(b.maxX, pos.x);
            b.maxY = std::max(b.maxY, pos.y);
        }

        // Если выделен всего 1 блок или тонкая линия — расширяем зону минимум до 90x90 (3x3 блока)
        if (b.maxX - b.minX < 30.f) {
            b.minX -= 30.f;
            b.maxX += 30.f;
        }
        if (b.maxY - b.minY < 30.f) {
            b.minY -= 30.f;
            b.maxY += 30.f;
        }
        b.valid = true;
    } else if (ui->m_editorLayer && ui->m_editorLayer->m_objectLayer) {
        // Если ничего не выделено — берем центральную область экрана в редакторе (6x4 блока)
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

        // Запускаем опрос клавиши Right Alt (VK_RMENU) каждый кадр в редакторе,
        // так как стандартный Cocos2d-x объединяет левый и правый Alt
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
        // На macOS / других платформах поддерживаем стандартный код клавиши Alt
        if (key == cocos2d::enumKeyCodes::KEY_Alt) {
            openAIDecorMenu();
        }
#endif
    }

    void openAIDecorMenu() {
        auto* scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;

        // Проверяем, не открыто ли уже окно AI Decorator
        if (scene->getChildByID("ai-decor-popup"_spr)) return;

        SelectionBounds bounds = computeSelectionBounds(this);
        if (auto* popup = AIDecorPopup::create(this, bounds)) {
            popup->setID("ai-decor-popup"_spr);
            popup->show();
        }
    }
};
