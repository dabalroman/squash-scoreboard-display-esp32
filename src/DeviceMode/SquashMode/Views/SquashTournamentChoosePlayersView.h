#ifndef SQUASH_MODE__TOURNAMENT_CHOOSE_PLAYERS_VIEW_H
#define SQUASH_MODE__TOURNAMENT_CHOOSE_PLAYERS_VIEW_H

#include <vector>

#include "UserProfile.h"
#include "DeviceMode/View.h"
#include "DeviceMode/SquashMode/SquashModeState.h"
#include "Tournament/Tournament.h"
#include "DeviceMode/DeviceModeState.h"
#include "Display/Scrollable.h"
#include "Display/ScrollableWidget.h"
#include "Display/LedDisplay/Adapter/TournamentPlayersBarAdapter.h"

class SquashTournamentChoosePlayersView final : public View {
    Tournament &tournament;
    std::vector<UserProfile *> &users;
    std::function<void(DeviceModeState)> onDeviceModeChange;
    std::function<void(SquashModeState)> onStateChange;

    std::vector<String> menuOptions;
    std::unique_ptr<Scrollable> scrollable;
    std::unique_ptr<ScrollableWidget> scrollableWidget;

    uint8_t startOptionId;
    uint8_t exitOptionId;

public:
    explicit SquashTournamentChoosePlayersView(
        Tournament &tournament,
        std::vector<UserProfile *> &players,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        const std::function<void(SquashModeState)> &onStateChange
    )
        : tournament(tournament), users(players), onDeviceModeChange(onDeviceModeChange),
          onStateChange(std::move(onStateChange)) {

        menuOptions.reserve(players.size() + 2);
        menuOptions.push_back(F(" [Start] "));

        for (const UserProfile *user: players) {
            menuOptions.push_back(user->getName());
        }
        menuOptions.push_back(F("  [Exit]  "));

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
                onStateChange(SquashModeState::MatchStartGame);
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
        ledDisplay.resetHistoryBar();
        ledDisplay.setColonAppearance();
        ledDisplay.setPlayersIndicatorsState(true);
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
        const Glyph playerGlyph = isPlayerIn ? Glyph::UpperDot : Glyph::LowerDot;

        if (optionId == startOptionId) {
            const Color color = tournament.getPlayers().size() < 2 ? Colors::Red : Colors::Green;
            ledDisplay.setGlyphsColor(color, color);
            ledDisplay.setGlyphsGlyph(Glyph::P, Glyph::L, Glyph::A, Glyph::Y);
            ledDisplay.setIndicatorAppearancePlayerA(color);
            ledDisplay.setIndicatorAppearancePlayerB(color);
        } else if (optionId == exitOptionId) {
            ledDisplay.setGlyphsGlyph(Glyph::C, Glyph::F, Glyph::G, Glyph::Empty);
            ledDisplay.setGlyphsColor(Colors::White, Colors::White);
            ledDisplay.setIndicatorAppearancePlayerA(Colors::White);
            ledDisplay.setIndicatorAppearancePlayerB(Colors::White);
        } else {
            ledDisplay.setGlyphsGlyph(Glyph::P, LedDisplay::digitToGlyph(playerId), Glyph::Empty, playerGlyph);
            ledDisplay.setGlyphsColor(playerColor, playerStateColor);
            ledDisplay.setIndicatorAppearancePlayerA(playerColor);
            ledDisplay.setIndicatorAppearancePlayerB(playerStateColor);
        }

        ledDisplay.setLedBarState(TournamentPlayersBarAdapter::toLedBarPixels(users, tournament.getPlayers()));
        ledDisplay.display();

        shouldRenderLedDisplay = false;
    }

    void initBackDisplay(BackDisplay &backDisplay) override {
        backDisplay.initSmallFont();
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


#endif //SQUASH_MODE__TOURNAMENT_CHOOSE_PLAYERS_VIEW_H
