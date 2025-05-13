#ifndef TOURNAMENT_CHOOSE_PLAYERS_VIEW_H
#define TOURNAMENT_CHOOSE_PLAYERS_VIEW_H

#include <utility>
#include <vector>

#include "UserProfile.h"
#include "DeviceMode/View.h"

class TournamentChoosePlayersView final : public View {
    Tournament &tournament;
    std::vector<UserProfile *> &users;
    std::function<void(DeviceModeState)> onDeviceModeChange;
    std::function<void(SquashModeState)> onStateChange;

    std::vector<String> menuOptions;
    Scrollable *scrollable = nullptr;
    ScrollableWidget *scrollableWidget = nullptr;

    uint8_t startOptionId;
    uint8_t settingsOptionId;

public:
    explicit TournamentChoosePlayersView(
        Tournament &tournament,
        std::vector<UserProfile *> &players,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        std::function<void(SquashModeState)> onStateChange
    )
        : tournament(tournament), users(players), onDeviceModeChange(onDeviceModeChange),
          onStateChange(std::move(onStateChange)) {

        menuOptions.reserve(players.size() + 2);
        menuOptions.push_back(F(" [Start] "));

        for (const UserProfile *user: players) {
            menuOptions.push_back(user->getName());
        }
        menuOptions.push_back(F("[Settings]"));

        startOptionId = 0;
        settingsOptionId = menuOptions.size() - 1;

        scrollable = new Scrollable(menuOptions);
        scrollableWidget = new ScrollableWidget(*scrollable);
    }

    uint8_t getPlayerIdFromOptionId(const uint8_t optionId) const {
        if (optionId == startOptionId || optionId == settingsOptionId) {
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

            printLn("%d", selectedOptionId);

            if (selectedOptionId == startOptionId) {
                if (tournament.getPlayers().size() < 2) {
                    return;
                }

                remoteInputManager.preventTriggerForMs();
                onStateChange(SquashModeState::MatchChoosePlayers);
                return;
            }

            if (selectedOptionId == settingsOptionId) {
                remoteInputManager.preventTriggerForMs();
                onDeviceModeChange(DeviceModeState::ConfigMode);
                return;
            }

            const uint8_t playerId = getPlayerIdFromOptionId(selectedOptionId);
            if (tournament.isPlayerIn(*users.at(playerId))) {
                tournament.removePlayer(*users.at(playerId));
            } else {
                tournament.addPlayer(*users.at(playerId));
            }

            queueRender();

            // for (uint8_t i = 0; i < 20; i++) {
            //     const MatchPlayersPair pair = tournament.matchOrderKeeper->getPlayersForNextMatch();
            //     tournament.matchOrderKeeper->confirmMatchBetweenPlayers(pair);
            // }
        }
    }

    void renderGlyphs(GlyphDisplay &glyphDisplay) override {
        if (!shouldRenderGlyphs) {
            return;
        }

        const uint8_t optionId = scrollable->getSelectedOptionId();
        const uint8_t playerId = getPlayerIdFromOptionId(optionId);

        const bool isPlayerIn = tournament.isPlayerIn(*users.at(playerId));
        const Color playerColor = users.at(playerId)->getColor();
        const Color playerStateColor = isPlayerIn ? Colors::Green : Colors::Red;
        const Glyph playerGlyph = isPlayerIn ? Glyph::UpperDot : Glyph::LowerDot;

        glyphDisplay.setColonAppearance();
        glyphDisplay.setPlayersIndicatorsState(true);

        if (optionId == startOptionId) {
            const Color color = tournament.getPlayers().size() < 2 ? Colors::Red : Colors::Green;
            glyphDisplay.setGlyphsColor(color, color);
            glyphDisplay.setGlyphsGlyph(Glyph::P, Glyph::L, Glyph::A, Glyph::Y);
            glyphDisplay.setIndicatorAppearancePlayerA(color);
            glyphDisplay.setIndicatorAppearancePlayerB(color);
        } else if (optionId == settingsOptionId) {
            glyphDisplay.setGlyphsGlyph(Glyph::C, Glyph::F, Glyph::G, Glyph::Empty);
            glyphDisplay.setGlyphsColor(Colors::White, Colors::White);
            glyphDisplay.setIndicatorAppearancePlayerA(Colors::White);
            glyphDisplay.setIndicatorAppearancePlayerB(Colors::White);
        } else {
            glyphDisplay.setGlyphsGlyph(Glyph::P, GlyphDisplay::digitToGlyph(playerId), Glyph::Empty, playerGlyph);
            glyphDisplay.setGlyphsColor(playerColor, playerStateColor);
            glyphDisplay.setIndicatorAppearancePlayerA(playerColor);
            glyphDisplay.setIndicatorAppearancePlayerB(playerStateColor);
        }

        glyphDisplay.display();
    }

    void renderScreen(BackDisplay &backDisplay) override {
        backDisplay.clear();
        scrollableWidget->render(backDisplay);
        backDisplay.display();
    }
};


#endif //TOURNAMENT_CHOOSE_PLAYERS_VIEW_H
