#include "GameMenuController.hpp"
#include "RoomCatalog.hpp"
#include "MenuNavigation.hpp"

#include <Bamboo/ApplicationAPI.hpp>
#include <Bamboo/Assets/TextureAPI.hpp>
#include <Bamboo/Logger.hpp>
#include <Bamboo/UI/TextureAPI.hpp>
#include <Bamboo/WorldAPI.hpp>
#include <PandaUI/Elements/Button.hpp>
#include <PandaUI/Elements/ImageView.hpp>
#include <PandaUI/Elements/Label.hpp>
#include <PandaUI/Elements/ScrollView.hpp>
#include <PandaUI/Elements/StackView.hpp>
#include <PandaUI/Runtime/Window.hpp>

#include <array>
#include <functional>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

namespace {

using namespace PandaUI;

enum class Screen { Home, Rooms, Settings, PausePreview };
enum class Action { None, Home, Rooms, Settings, Back, PausePreview, SelectRoom, Play, TextSize, Language, Exit };
enum class MenuLanguage { English, Russian };
constexpr size_t kNoRoom = std::numeric_limits<size_t>::max();
float s_menuTextScale = 1.f; // menus in this script session; no new save subsystem
MenuLanguage s_menuLanguage = MenuLanguage::English;

const char *tr(const char *english, const char *russian) {
    return s_menuLanguage == MenuLanguage::Russian ? russian : english;
}
const Color kText(.94f, .95f, .97f, 1.f);
const Color kMuted(.57f, .60f, .65f, 1.f);
const Color kBorder(1.f, 1.f, 1.f, .12f);
const Color kClear(0.f, 0.f, 0.f, 0.f);

std::shared_ptr<StackView> stack(StackAxis axis = StackAxis::Vertical, float spacing = 16.f) {
    auto view = std::make_shared<StackView>(axis, spacing);
    view->surface().setBackgroundColor(kClear);
    view->surface().setBorderWidth(0.f);
    view->layout().setFlexShrink(0.f);
    return view;
}

std::shared_ptr<Label> label(std::string text, float size = 16.f, Color color = kText,
                             FontWeight weight = FontWeight::Regular) {
    auto view = std::make_shared<Label>(std::move(text));
    view->setFont(Font(size, weight));
    view->setTextColor(color);
    view->setNumberOfLines(0);
    view->setLineSpacing(3.f);
    view->setUserInteractionEnabled(false);
    view->layout().setFlexShrink(0.f);
    return view;
}

void cardSurface(View &view) {
    view.surface().setBackgroundColor(Color(.08f, .085f, .10f, .94f));
    view.surface().setBorderColor(kBorder);
    view.surface().setBorderWidth(1.f);
    view.surface().setCornerRadius(20.f);
    view.surface().setShadowColor(Color(0.f, 0.f, 0.f, .25f));
    view.surface().setShadowOffset({0.f, 8.f});
    view.surface().setShadowRadius(20.f);
}

ButtonAppearance buttonAppearance(bool selected = false) {
    auto result = Button::defaultAppearance(ButtonStyle::Neutral);
    auto state = [](Color background, Color border, Color text) {
        ButtonAppearance::State s;
        s.surface = SurfaceStyle(background, border);
        s.text = text;
        return s;
    };
    result.normal = state(selected ? Color(.22f, .24f, .28f, 1.f) : Color(.12f, .13f, .15f, .9f),
                          selected ? Color(.85f, .9f, 1.f, .7f) : kBorder, kText);
    result.hovered = state(Color(.20f, .21f, .24f, 1.f), Color(1.f, 1.f, 1.f, .4f), Color(1.f, 1.f, 1.f, 1.f));
    result.pressed = state(Color(.07f, .08f, .10f, 1.f), Color(1.f, 1.f, 1.f, .55f), kText);
    result.focused = state(Color(.16f, .18f, .22f, 1.f), Color(.8f, .88f, 1.f, .85f), kText);
    result.disabled = state(Color(.09f, .10f, .12f, .8f), Color(1.f, 1.f, 1.f, .06f), Color(.39f, .42f, .47f, 1.f));
    result.font = Font(16.f, FontWeight::Medium);
    result.padding = EdgeInsets(13.f, 20.f, 20.f, 13.f);
    result.minHeight = 48.f;
    result.cornerRadius = CornerRadii(14.f);
    result.borderWidth = 1.f;
    return result;
}

class MenuRoot final : public View {
public:
    std::function<void()> onBack;
    bool keyDown(KeyEvent &event) override {
        if (event.key == Key::Escape && !event.isSuperDown && !event.isControlDown && !event.isAltDown) {
            if (onBack) { onBack(); }
            return true;
        }
        return false;
    }
};

} // namespace

struct GameMenuController::State final : std::enable_shared_from_this<GameMenuController::State> {
    Window window;
    std::shared_ptr<View> previousRoot;
    std::shared_ptr<MenuRoot> root;
    std::shared_ptr<Button> firstButton;
    std::unordered_map<uint32_t, TextureHandle> previews;
    Screen screen = Screen::Home;
    Screen settingsReturn = Screen::Home;
    Action pending = Action::None;
    size_t pendingValue = 0;
    size_t selectedRoom = kNoRoom;
    float previousTextScale = 1.f;
    bool attached = false;

    void queue(Action action, size_t value = 0) {
        // UI dispatch runs after script update. Rebuild/load next frame, when the
        // button callback is over, so no widget/script is destroyed underneath it.
        if (pending == Action::None) { pending = action; pendingValue = value; }
    }

    std::shared_ptr<Button> button(const char *title, Action action, bool enabled = true,
                                   bool selected = false, size_t value = 0) {
        auto result = std::make_shared<Button>(title);
        result->setAppearance(buttonAppearance(selected));
        result->setNumberOfLines(0);
        result->setSelected(selected);
        result->setEnabled(enabled);
        result->layout().setFlexShrink(0.f);
        if (enabled) {
            const std::weak_ptr<State> weak = shared_from_this();
            result->setOnClick([weak, action, value](Button &) {
                if (const auto state = weak.lock()) { state->queue(action, value); }
            });
            if (!firstButton) { firstButton = result; }
        }
        return result;
    }

    TextureHandle preview(uint32_t assetId) {
        if (assetId == 0) { return {}; }
        if (const auto found = previews.find(assetId); found != previews.end()) { return found->second; }
        const auto pixels = Bamboo::TextureAPI::readPixelsRGBA8(Bamboo::TextureHandle(assetId));
        TextureHandle texture;
        if (pixels && pixels.width <= UINT16_MAX && pixels.height <= UINT16_MAX) {
            texture = Bamboo::UI::createTextureRGBA8(pixels.pixels.data(), uint16_t(pixels.width), uint16_t(pixels.height));
        }
        if (!texture.isValid()) { LOG_WARN("Menu preview asset %u is unavailable.", assetId); }
        previews.emplace(assetId, texture); // one upload per asset, shared across screens
        return texture;
    }

    std::shared_ptr<View> roomCard(size_t index) {
        const auto &room = SmashLab::kRooms[index];
        const bool ready = room.playable();
        const bool selected = selectedRoom == index && ready;
        auto card = stack(StackAxis::Vertical, 15.f);
        cardSurface(*card);
        card->layout().setPadding(18.f);
        if (selected) { card->surface().setBorderColor(Color(.8f, .88f, 1.f, .7f)); }

        auto picture = std::make_shared<View>();
        picture->layout().setHeight(Length::points(225.f));
        picture->surface().setBackgroundColor(Color(.13f, .14f, .16f, 1.f));
        picture->surface().setCornerRadius(12.f);
        picture->setClipsToBounds(true);
        if (const auto texture = preview(room.previewAssetId); texture.isValid()) {
            auto image = std::make_shared<ImageView>(texture);
            image->setContentMode(ImageContentMode::Fill);
            image->layoutSetAbsolute();
            image->layoutSetInsetsFromParent({});
            image->setUserInteractionEnabled(false);
            picture->addSubview(image);
        } else {
            picture->layout().setPadding(20.f);
            picture->addSubview(label(tr("Preview coming soon", "Изображение появится позже"), 15.f, kMuted));
        }
        card->addSubview(picture);
        card->addSubview(label(ready ? tr("READY TO PLAY", "ДОСТУПНА") : tr("IN DEVELOPMENT", "В РАЗРАБОТКЕ"), 11.f,
                                ready ? Color(.55f, .85f, .7f, 1.f) : Color(.8f, .7f, .5f, 1.f), FontWeight::Medium));
        card->addSubview(label(tr(room.name, room.nameRu), 26.f, kText, FontWeight::Bold));
        card->addSubview(label(tr(room.description, room.descriptionRu), 15.f, kMuted));
        auto select = button(ready ? (selected ? tr("Selected", "Выбрано") : tr("Select room", "Выбрать комнату")) : tr("Coming soon", "Скоро"),
                             Action::SelectRoom, ready, selected, index);
        if (!ready) { select->setTooltip(tr("This room is still being created.", "Эта комната ещё создаётся.")); }
        card->addSubview(select);
        return card;
    }

    std::shared_ptr<StackView> navigation() {
        auto row = stack(StackAxis::Horizontal, 8.f);
        row->layout().setFlexWrap(Wrap::Wrap);
        row->layout().setFlexShrink(1.f);
        row->layout().setMaxWidth(Length::percent(100.f));
        row->addSubview(button(tr("Main Menu", "Главное меню"), Action::Home, true, screen == Screen::Home));
        row->addSubview(button(tr("Rooms", "Комнаты"), Action::Rooms, true, screen == Screen::Rooms));
        row->addSubview(button(tr("Settings", "Настройки"), Action::Settings, true, screen == Screen::Settings));
        return row;
    }

    void home(StackView &content) {
        content.addSubview(label(tr("PHYSICS SANDBOX", "ФИЗИЧЕСКАЯ ПЕСОЧНИЦА"), 12.f, kMuted, FontWeight::Medium));
        content.addSubview(label("SMASH LAB", 54.f, kText, FontWeight::Bold));
        content.addSubview(label(tr("Choose a room. Make an impact.", "Выбери комнату. Испытай её на прочность."), 19.f, kMuted));
        auto row = stack(StackAxis::Horizontal, 28.f);
        row->layout().setFlexWrap(Wrap::Wrap);
        row->setAlignment(Align::Start);

        auto controls = stack(StackAxis::Vertical, 12.f);
        controls->layout().setWidth(Length::points(240.f));
        bool hasReadyRoom = false;
        for (const auto &room : SmashLab::kRooms) { hasReadyRoom |= room.playable(); }
        auto play = button(tr("Play", "Играть"), Action::Play, hasReadyRoom);
        if (!hasReadyRoom) { play->setTooltip(tr("No playable rooms yet. Check Rooms for progress.", "Пока нет доступных комнат. Загляни в раздел «Комнаты».")); }
        controls->addSubview(play);
        controls->addSubview(button(tr("Rooms", "Комнаты"), Action::Rooms));
        controls->addSubview(button(tr("Settings", "Настройки"), Action::Settings));
        controls->addSubview(button(tr("Exit", "Выход"), Action::Exit));
        if (!hasReadyRoom) { controls->addSubview(label(tr("Your first room is on the way.", "Первая игровая комната скоро появится."), 14.f, kMuted)); }
        row->addSubview(controls);
        if (!SmashLab::kRooms.empty()) {
            auto feature = roomCard(selectedRoom < SmashLab::kRooms.size() ? selectedRoom : 0);
            feature->layout().setFlexBasis(360.f);
            feature->layout().setMinWidth(Length::points(260.f));
            feature->layout().setFlexGrow(1.f);
            feature->layout().setFlexShrink(1.f);
            row->addSubview(feature);
        }
        content.addSubview(row);
    }

    void rooms(StackView &content) {
        content.addSubview(label(tr("THE COLLECTION", "ВЫБОР КОМНАТЫ"), 12.f, kMuted, FontWeight::Medium));
        content.addSubview(label(tr("Rooms", "Комнаты"), 38.f, kText, FontWeight::Bold));
        content.addSubview(label(tr("Each room is its own space. More rooms will appear here as they become ready.", "У каждой комнаты свой мир. Новые комнаты появятся здесь по мере готовности."), 16.f, kMuted));
        auto cards = stack(StackAxis::Horizontal, 20.f);
        cards->layout().setFlexWrap(Wrap::Wrap);
        cards->setAlignment(Align::Start);
        for (size_t i = 0; i < SmashLab::kRooms.size(); ++i) {
            auto card = roomCard(i);
            card->layout().setFlexBasis(320.f);
            card->layout().setMinWidth(Length::points(260.f));
            card->layout().setMaxWidth(Length::points(490.f));
            card->layout().setFlexGrow(1.f);
            card->layout().setFlexShrink(1.f);
            cards->addSubview(card);
        }
        content.addSubview(cards);
        const bool canPlay = selectedRoom < SmashLab::kRooms.size() && SmashLab::kRooms[selectedRoom].playable();
        auto play = button(tr("Play selected room", "Играть в выбранной комнате"), Action::Play, canPlay);
        play->layout().setMaxWidth(Length::points(320.f));
        content.addSubview(play);
        if (!canPlay) { content.addSubview(label(tr("Select an available room to continue.", "Выбери доступную комнату для запуска."), 14.f, kMuted)); }
    }

    void settings(StackView &content) {
        content.addSubview(label(tr("MAKE YOURSELF COMFORTABLE", "УСТРОЙСЯ ПОУДОБНЕЕ"), 12.f, kMuted, FontWeight::Medium));
        content.addSubview(label(tr("Settings", "Настройки"), 38.f, kText, FontWeight::Bold));
        auto language = stack(StackAxis::Vertical, 16.f);
        cardSurface(*language);
        language->layout().setPadding(24.f);
        language->addSubview(label(tr("Language / Язык", "Язык / Language"), 23.f, kText, FontWeight::Medium));
        auto languages = stack(StackAxis::Horizontal, 10.f);
        languages->layout().setFlexWrap(Wrap::Wrap);
        languages->addSubview(button("English", Action::Language, true, s_menuLanguage == MenuLanguage::English, 0));
        languages->addSubview(button("Русский", Action::Language, true, s_menuLanguage == MenuLanguage::Russian, 1));
        language->addSubview(languages);
        language->addSubview(label(tr("Menu language for this session.", "Язык меню в текущем сеансе."), 15.f, kMuted));
        content.addSubview(language);
        auto panel = stack(StackAxis::Vertical, 18.f);
        cardSurface(*panel);
        panel->layout().setPadding(24.f);
        panel->addSubview(label(tr("Text size", "Размер текста"), 23.f, kText, FontWeight::Medium));
        panel->addSubview(label(tr("Applies to the menu text during this session.", "Применяется к тексту меню в текущем сеансе."), 15.f, kMuted));
        auto sizes = stack(StackAxis::Horizontal, 10.f);
        sizes->layout().setFlexWrap(Wrap::Wrap);
        constexpr std::array<float, 3> scales = {.9f, 1.f, 1.15f};
        const std::array<const char *, 3> names = {tr("Small", "Маленький"), tr("Default", "Обычный"), tr("Large", "Большой")};
        for (size_t i = 0; i < names.size(); ++i) {
            sizes->addSubview(button(names[i], Action::TextSize, true, s_menuTextScale == scales[i], i));
        }
        panel->addSubview(sizes);
        content.addSubview(panel);
        content.addSubview(label(tr("Room controls", "Меню комнаты"), 22.f, kText, FontWeight::Medium));
        content.addSubview(label(tr("Preview the pause-menu layout. It does not pause gameplay yet.", "Посмотри, как будет выглядеть меню паузы. Саму игру оно пока не останавливает."), 15.f, kMuted));
        auto previewButton = button(tr("Preview pause menu", "Посмотреть меню паузы"), Action::PausePreview);
        previewButton->layout().setMaxWidth(Length::points(320.f));
        content.addSubview(previewButton);
        auto back = button(tr("Back", "Назад"), Action::Back);
        back->layout().setMaxWidth(Length::points(180.f));
        content.addSubview(back);
    }

    void pausePreview(StackView &content) {
        content.addSubview(label(tr("PREVIEW ONLY", "ПРЕДПРОСМОТР"), 12.f, Color(.8f, .7f, .5f, 1.f), FontWeight::Medium));
        content.addSubview(label(tr("Pause menu", "Меню паузы"), 38.f, kText, FontWeight::Bold));
        content.addSubview(label(tr("No active room. Resume and restart are unavailable.", "Комната не запущена. Продолжение и перезапуск недоступны."), 16.f, kMuted));
        auto panel = stack(StackAxis::Vertical, 12.f);
        cardSurface(*panel);
        panel->layout().setPadding(24.f);
        panel->layout().setMaxWidth(Length::points(420.f));
        panel->addSubview(button(tr("Resume", "Продолжить"), Action::None, false));
        panel->addSubview(button(tr("Restart Room", "Перезапустить комнату"), Action::None, false));
        panel->addSubview(button(tr("Settings", "Настройки"), Action::Settings));
        panel->addSubview(button(tr("Main Menu", "Главное меню"), Action::Home));
        content.addSubview(panel);
    }

    void render() {
        firstButton.reset();
        auto nextRoot = std::make_shared<MenuRoot>();
        nextRoot->setFocusable(true);
        nextRoot->layout().setFlexDirection(FlexDirection::Column);
        nextRoot->layout().setPadding(24.f);
        nextRoot->layout().setGap(16.f);
        nextRoot->surface().setBackgroundGradient(SurfaceGradient(Color(.035f, .04f, .05f, 1.f),
            Color(.07f, .075f, .09f, 1.f), GradientDirection::TopLeftToBottomRight));
        const std::weak_ptr<State> weak = shared_from_this();
        nextRoot->onBack = [weak] { if (const auto state = weak.lock()) { state->queue(Action::Back); } };

        auto header = stack(StackAxis::Horizontal, 12.f);
        header->layout().setFlexWrap(Wrap::Wrap);
        header->setAlignment(Align::Center);
        header->setDistribution(Justify::SpaceBetween);
        auto logo = label("SMASH LAB", 21.f, kText, FontWeight::Bold);
        logo->setLetterSpacing(2.f);
        header->addSubview(logo);
        header->addSubview(navigation());
        nextRoot->addSubview(header);

        auto scroll = std::make_shared<ScrollView>();
        scroll->setScrollDirection(ScrollDirection::Vertical);
        scroll->setBounces(false);
        scroll->setAutomaticallyAdjustsContentSize(true);
        scroll->layout().setFlexGrow(1.f);
        scroll->layout().setFlexBasis(0.f);
        scroll->layout().setMinHeight(Length::points(0.f));
        scroll->getContentView()->layout().setAlignItems(Align::Center);
        auto content = stack(StackAxis::Vertical, 20.f);
        content->layout().setWidth(Length::percent(100.f));
        content->layout().setMaxWidth(Length::points(1040.f));
        content->layout().setPadding(Edge::Vertical, 24.f);
        content->layout().setPadding(Edge::Horizontal, 4.f);
        switch (screen) {
            case Screen::Home: home(*content); break;
            case Screen::Rooms: rooms(*content); break;
            case Screen::Settings: settings(*content); break;
            case Screen::PausePreview: pausePreview(*content); break;
        }
        scroll->addContentSubview(content);
        nextRoot->addSubview(scroll);
        nextRoot->addSubview(label(tr("EARLY DEVELOPMENT   /   Tab to navigate   /   Esc to go back", "В РАЗРАБОТКЕ   /   Tab — навигация   /   Esc — назад"), 11.f, kMuted));
        root = nextRoot;
        window.setRootView(root);
        window.context().setFocusedView(firstButton ? static_cast<View *>(firstButton.get()) : root.get());
    }

    void attach() {
        window = Window::main();
        if (!window.isValid()) { return; }
        previousRoot = window.context().getRootView();
        previousTextScale = window.context().textScale();
        window.context().setTextScale(s_menuTextScale);
        for (size_t i = 0; i < SmashLab::kRooms.size(); ++i) {
            if (SmashLab::kRooms[i].playable()) { selectedRoom = i; break; }
        }
        attached = true;
        render();
    }

    void update() {
        if (!attached) { attach(); return; }
        if (!window.isValid() || pending == Action::None) { return; }
        const auto action = std::exchange(pending, Action::None);
        switch (action) {
            case Action::Home: screen = Screen::Home; break;
            case Action::Rooms: screen = Screen::Rooms; break;
            case Action::Settings:
                if (screen != Screen::Settings) { settingsReturn = screen; }
                screen = Screen::Settings;
                break;
            case Action::Back:
                screen = screen == Screen::Settings ? settingsReturn : Screen::Home;
                break;
            case Action::PausePreview: screen = Screen::PausePreview; break;
            case Action::SelectRoom:
                if (pendingValue < SmashLab::kRooms.size() && SmashLab::kRooms[pendingValue].playable()) {
                    selectedRoom = pendingValue;
                }
                break;
            case Action::Play:
                if (selectedRoom < SmashLab::kRooms.size() && SmashLab::kRooms[selectedRoom].playable()) {
                    // Host queues a safe world transition. This callback does not destroy
                    // its own world/script, drive physics, or load files by itself.
                    SmashLab::MenuNavigation::openRoom(SmashLab::kRooms[selectedRoom]);
                }
                return;
            case Action::TextSize: {
                constexpr std::array<float, 3> scales = {.9f, 1.f, 1.15f};
                if (pendingValue < scales.size()) {
                    s_menuTextScale = scales[pendingValue];
                    window.context().setTextScale(s_menuTextScale);
                }
                break;
            }
            case Action::Language:
                if (pendingValue < 2) {
                    s_menuLanguage = pendingValue == 1 ? MenuLanguage::Russian : MenuLanguage::English;
                }
                break;
            case Action::Exit:
                Bamboo::ApplicationAPI::quit(); // Stop in PandaEditor; quit in a standalone game
                return;
            case Action::None: return;
        }
        render();
    }

    void detach() {
        pending = Action::None;
        if (attached && window.isValid() && window.context().getRootView() == root) {
            window.setRootView(previousRoot);
            window.context().setTextScale(previousTextScale);
        }
        root.reset();
        firstButton.reset();
        previousRoot.reset();
        for (const auto &[id, texture] : previews) {
            if (texture.isValid()) { Bamboo::UI::destroyTexture(texture); }
        }
        previews.clear();
        attached = false;
    }
};

void GameMenuController::start() {
    SmashLab::MenuNavigation::menuStarted();
    m_state = std::make_shared<State>();
    m_state->attach();
}

void GameMenuController::update(float) {
    if (m_state) { m_state->update(); }
}

void GameMenuController::shutdown() {
    if (m_state) { m_state->detach(); }
    m_state.reset();
}
