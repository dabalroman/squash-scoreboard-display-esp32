#ifndef VOLLEYBALL_MODE__TOURNAMENT_CHOOSE_PLAYERS_VIEW_H
#define VOLLEYBALL_MODE__TOURNAMENT_CHOOSE_PLAYERS_VIEW_H

#include <vector>

#include "Strings.h"
#include "UserProfile.h"
#include "DeviceMode/View.h"
#include "DeviceMode/VolleyballMode/VolleyballModeState.h"
#include "Tournament/Tournament.h"
#include "DeviceMode/DeviceModeState.h"
#include "Display/Scrollable.h"
#include "Display/ScrollableWidget.h"
#include "Display/LedDisplay/Renderer/TournamentPlayersBarRenderer.h"
#include "PlayerRoster.h"

class VolleyballTournamentChoosePlayersView final : public View {
    Tournament &tournament;
    std::vector<UserProfile *> &users;
    std::function<void(DeviceModeState)> onDeviceModeChange;
    std::function<void(VolleyballModeState)> onStateChange;

    std::vector<String> menuOptions;
    std::unique_ptr<Scrollable> scrollable;
    std::unique_ptr<ScrollableWidget> scrollableWidget;

    uint8_t startOptionId;
    uint8_t exitOptionId;

public:
    explicit VolleyballTournamentChoosePlayersView(
        Tournament &tournament,
        std::vector<UserProfile *> &players,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        std::function<void(VolleyballModeState)> onStateChange
    )
        : tournament(tournament), users(players), onDeviceModeChange(onDeviceModeChange),
          onStateChange(std::move(onStateChange)) {

        menuOptions.reserve(players.size() + 2);
        menuOptions.push_back(Str::PLAYERS_OPTION_START_OLED);

        for (const UserProfile *user: players) {
            menuOptions.push_back(user->getName());
        }
        menuOptions.push_back(Str::PLAYERS_OPTION_EXIT_OLED);

        startOptionId = 0;
        exitOptionId = menuOptions.size() - 1;

        scrollable = std::make_unique<Scrollable>(menuOptions);
        scrollableWidget = std::make_unique<ScrollableWidget>(*scrollable);
    }

    uint8_t getPlayerIdFromOptionId(const uint8_t optionId) const {
        if (optionId == startOptionId || optionId == exitOptionId) {
            return 0;
        }

        return optionId - 1;
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        if (users.empty()) {
            return;
        }

        if (remoteInputManager.buttonA.takeActionIfPossible()) {
            scrollable->cycleSelectedOption(-1);
            queueRender();
        }

        if (remoteInputManager.buttonB.takeActionIfPossible()) {
            scrollable->cycleSelectedOption(1);
            queueRender();
        }

        if (remoteInputManager.buttonC.takeActionIfPossible() || remoteInputManager.buttonD.takeActionIfPossible()) {
            const uint8_t selectedOptionId = scrollable->getSelectedOptionId();

            if (selectedOptionId == startOptionId) {
                if (tournament.getPlayers().size() < 2) {
                    return;
                }

                remoteInputManager.preventTriggerForMs();
                onStateChange(VolleyballModeState::MatchStartGame);
                return;
            }

            if (selectedOptionId == exitOptionId) {
                remoteInputManager.preventTriggerForMs();
                onDeviceModeChange(DeviceModeState::ModeSwitchingMode);
                return;
            }

            const uint8_t playerId = getPlayerIdFromOptionId(selectedOptionId);
            if (tournament.isPlayerIn(*users.at(playerId))) {
                tournament.removePlayer(*users.at(playerId));
            } else {
                tournament.addPlayer(*users.at(playerId));
            }

            queueRender();
        }
    }

    void initLedDisplay(LedDisplay &ledDisplay) override {
        ledDisplay.resetAnimations();
        ledDisplay.setColonAppearance();
        ledDisplay.setPlayersIndicatorsState(true);
        ledDisplay.setBorderEnabled(false);
    }

    void renderLedDisplay(LedDisplay &ledDisplay) override {
        if (!shouldRenderLedDisplay) {
            return;
        }

        const uint8_t optionId = scrollable->getSelectedOptionId();
        const uint8_t playerId = getPlayerIdFromOptionId(optionId);

        const bool isPlayerIn = tournament.isPlayerIn(*users.at(playerId));
        const Color playerColor = users.at(playerId)->getColor();
        const Color playerStateColor = isPlayerIn ? Colors::Green : Colors::Red;

        if (optionId == startOptionId) {
            const Color color = tournament.getPlayers().size() < 2 ? Colors::Red : Colors::Green;
            ledDisplay.setGlyphsColor(color, color);
            ledDisplay.setGlyphsText(Str::LED_PLAYERS_START);
            ledDisplay.setIndicatorAppearancePlayerA(color);
            ledDisplay.setIndicatorAppearancePlayerB(color);
        } else if (optionId == exitOptionId) {
            ledDisplay.setGlyphsText(Str::LED_PLAYERS_EXIT);
            ledDisplay.setGlyphsColor(Colors::White, Colors::White);
            ledDisplay.setIndicatorAppearancePlayerA(Colors::White);
            ledDisplay.setIndicatorAppearancePlayerB(Colors::White);
        } else {
            // P plus the id, right-aligned in the two rightmost slots: "P  5", "P 31".
            // The roster now goes to 32 and digitToGlyph returns Empty above 9, so a
            // single digit slot showed nothing from player 10 up. Right-aligned, not
            // left, so the ones digit stays put as the id crosses 10.
            // The in/out dot is dropped to make room - it was never the only signal:
            // both digits are tinted green/red and indicator B carries it too.
            const uint8_t tens = playerId / 10;
            ledDisplay.setGlyphsGlyph(
                Glyph::P,
                Glyph::Empty,
                tens > 0 ? LedDisplay::digitToGlyph(tens) : Glyph::Empty,
                LedDisplay::digitToGlyph(playerId % 10)
            );
            ledDisplay.setGlyphsColor(playerColor, playerStateColor);
            ledDisplay.setIndicatorAppearancePlayerA(playerColor);
            ledDisplay.setIndicatorAppearancePlayerB(playerStateColor);
        }

        ledDisplay.setLedBarState([&] { return TournamentPlayersBarRenderer::toLedBarPixels(users, tournament.getPlayers()); });
        ledDisplay.display();

        shouldRenderLedDisplay = false;
    }

    void initBackDisplay(BackDisplay &backDisplay) override {
        backDisplay.initSmallFont();
    }

    void renderEInkDisplay(EInkDisplay &einkDisplay) override {
        if (!einkDisplay.available() || users.empty()) {
            return;
        }

        // TODO: USE SCROLLABLE WIDGET
        // Rows follow menuOptions: [Start], one per user, [Exit].
        // [Start] + up to 32 players + [Exit]. Stack-local, ~408 bytes at this size.
        constexpr uint8_t MAX_ROWS = 2 + PlayerRosterLimits::MAX_PLAYERS;
        EInkMenuRow rows[MAX_ROWS];
        uint8_t count = 0;

        const size_t playersIn = tournament.getPlayers().size();
        rows[count++] = {Str::PLAYERS_ROW_START, nullptr, static_cast<int8_t>(playersIn >= 2 ? 1 : 0)};
        for (const UserProfile *user : users) {
            if (count >= MAX_ROWS - 1) {
                break;
            }
            rows[count++] = {user->getName(), nullptr, static_cast<int8_t>(tournament.isPlayerIn(*user) ? 1 : 0)};
        }
        rows[count++] = {Str::PLAYERS_ROW_EXIT, nullptr, -1};

        char footer[12];
        snprintf(footer, sizeof(footer), Str::PLAYERS_FOOTER_COUNT_FMT, static_cast<unsigned>(playersIn));

        einkDisplay.showMenu(Str::PLAYERS_MENU_TITLE, rows, count, scrollable->getSelectedOptionId(), footer);
    }

    void renderBackDisplay(BackDisplay &backDisplay) override {
        if (!shouldRenderBack) {
            return;
        }

        backDisplay.clear();
        scrollableWidget->render(backDisplay);
        backDisplay.display();

        shouldRenderBack = false;
    }
};


#endif //VOLLEYBALL_MODE__TOURNAMENT_CHOOSE_PLAYERS_VIEW_H
