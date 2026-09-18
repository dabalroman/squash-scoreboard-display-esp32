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

public:
    explicit VolleyballTournamentChoosePlayersView(
        Tournament &tournament,
        std::vector<UserProfile *> &players,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        std::function<void(VolleyballModeState)> onStateChange
    )
        : tournament(tournament), users(players), onDeviceModeChange(onDeviceModeChange),
          onStateChange(std::move(onStateChange)) {

        menuOptions.reserve(players.size() + 1);
        menuOptions.push_back(Str::PLAYERS_OPTION_START_OLED);

        for (const UserProfile *user: players) {
            menuOptions.push_back(user->getName());
        }

        startOptionId = 0;

        scrollable = std::make_unique<Scrollable>(menuOptions);
        scrollableWidget = std::make_unique<ScrollableWidget>(*scrollable);
    }

    uint8_t getPlayerIdFromOptionId(const uint8_t optionId) const {
        if (optionId == startOptionId) {
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

        // C is back to the mode selector - which is what a long press already did
        // here, and what let the KONIEC row go. D alone toggles or starts, so the
        // two buttons no longer do the same thing.
        if (remoteInputManager.buttonC.takeActionIfPossible()) {
            remoteInputManager.preventTriggerForMs();
            // The callback destroys this view; nothing may touch `this` after it.
            onDeviceModeChange(DeviceModeState::ModeSwitchingMode);
            return;
        }

        if (remoteInputManager.buttonD.takeActionIfPossible()) {
            const uint8_t selectedOptionId = scrollable->getSelectedOptionId();

            if (selectedOptionId == startOptionId) {
                if (tournament.getPlayers().size() < 2) {
                    return;
                }

                remoteInputManager.preventTriggerForMs();
                onStateChange(VolleyballModeState::MatchStartGame);
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
        } else {
            // "P" and the id's last digit, then the in/out dot in the last slot:
            // "P5 *" while the player is in, "P5 ." while they are out. The dot
            // sits high for in and low for out, so the state is readable from its
            // position alone and not only from the green/red tint that
            // setGlyphsColor gives slots C and D below.
            // Only the ones digit fits beside the dot, so ids 3, 13 and 23 share a
            // face here; the profile name on the OLED and the e-paper is what
            // picks between them.
            ledDisplay.setGlyphsGlyph(
                Glyph::P,
                LedDisplay::digitToGlyph(playerId % 10),
                Glyph::Empty,
                isPlayerIn ? Glyph::UpperDot : Glyph::LowerDot
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
        // Rows follow menuOptions: [Start], then one per user. Stack-local.
        constexpr uint8_t MAX_ROWS = 1 + PlayerRosterLimits::MAX_PLAYERS;
        EInkMenuRow rows[MAX_ROWS];
        uint8_t count = 0;

        // No tickbox on START: here the box means in/out membership and nothing
        // else. Readiness is carried by the footer count and the red/green LED word.
        const size_t playersIn = tournament.getPlayers().size();
        rows[count++] = {Str::PLAYERS_ROW_START, nullptr, -1};
        for (const UserProfile *user : users) {
            if (count >= MAX_ROWS) {
                break;
            }
            rows[count++] = {user->getName(), nullptr, static_cast<int8_t>(tournament.isPlayerIn(*user) ? 1 : 0)};
        }

        char footer[12];
        snprintf(footer, sizeof(footer), Str::PLAYERS_FOOTER_COUNT_FMT, static_cast<unsigned>(playersIn));

        einkDisplay.showMenu(Str::MODE_OPTION_VOLLEYBALL, rows, count, scrollable->getSelectedOptionId(),
                             EInkFooter(footer, nullptr, nullptr, -1));
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
