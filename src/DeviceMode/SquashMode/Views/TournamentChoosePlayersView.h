#ifndef TOURNAMENTCHOOSEPLAYERSVIEW_H
#define TOURNAMENTCHOOSEPLAYERSVIEW_H

#include <utility>
#include <vector>

#include "UserProfile.h"
#include "DeviceMode/View.h"

class TournamentChoosePlayersView final : public View {
    Tournament &tournament;
    std::vector<UserProfile *> &users;
    std::function<void(DeviceModeState)> onDeviceModeChange;
    std::function<void(SquashModeState)> onStateChange;
    uint8_t playerIndex = 0;
    uint32_t playerIndexChangeAtMs = 0;

public:
    explicit TournamentChoosePlayersView(
        Tournament &tournament,
        std::vector<UserProfile *> &players,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        std::function<void(SquashModeState)> onStateChange
    )
        : tournament(tournament), users(players), onDeviceModeChange(onDeviceModeChange), onStateChange(std::move(onStateChange)) {
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        if (users.empty()) {
            return;
        }

        if (remoteInputManager.buttonA.takeActionIfPossible()) {
            playerIndex++;
            playerIndexChangeAtMs = millis();
            queueRender();
        }

        if (playerIndex == users.size()) {
            playerIndex = 0;
        }

        if (remoteInputManager.buttonB.takeActionIfPossible()) {
            if (tournament.isPlayerIn(*users.at(playerIndex))) {
                tournament.removePlayer(*users.at(playerIndex));
            } else {
                tournament.addPlayer(*users.at(playerIndex));
            }

            queueRender();
        }

        if (remoteInputManager.buttonC.takeActionIfPossible()) {
            onDeviceModeChange(DeviceModeState::ConfigMode);
        }

        if (remoteInputManager.buttonD.takeActionIfPossible()) {
            if (tournament.getPlayers().size() < 2) {
                return;
            }

            tournament.matchOrderKeeper->confirmMatchBetweenPlayers({1, 3});
            tournament.matchOrderKeeper->confirmMatchBetweenPlayers({3, 2});
            tournament.matchOrderKeeper->confirmMatchBetweenPlayers({0, 2});

            for (uint8_t i = 0; i < 20; i++) {
                MatchPlayersPair pair = tournament.matchOrderKeeper->getPlayersForNextMatch();
                tournament.matchOrderKeeper->confirmMatchBetweenPlayers(pair);
            }

            remoteInputManager.preventTriggerForMs();
            onStateChange(SquashModeState::MatchChoosePlayers);

            queueRender();
        }
    }

    void renderGlyphs(GlyphDisplay &glyphDisplay) override {
        if (!shouldRenderGlyphs) {
            return;
        }

        const bool isPlayerIn = tournament.isPlayerIn(*users.at(playerIndex));
        const Color playerColor = users.at(playerIndex)->getColor();
        const Color playerStateColor = isPlayerIn ? Colors::Green : Colors::Red;
        const Glyph playerGlyph = isPlayerIn ? Glyph::UpperDot : Glyph::LowerDot;

        glyphDisplay.setColonAppearance();

        glyphDisplay.setGlyphsGlyph(Glyph::P, GlyphDisplay::digitToGlyph(playerIndex), Glyph::Empty, playerGlyph);
        glyphDisplay.setGlyphsColor(playerColor, playerColor, Colors::Black, playerStateColor);

        glyphDisplay.setPlayersIndicatorsState(true);
        glyphDisplay.setPlayerAIndicatorAppearance(playerColor);
        glyphDisplay.setPlayerBIndicatorAppearance(playerStateColor);

        glyphDisplay.display();
    }

    void renderScreen(BackDisplay &backDisplay) override {
        const String playerName = users.at(playerIndex)->getName();

        backDisplay.clear();
        backDisplay.renderBigScrollingText(playerName, playerIndexChangeAtMs);
        backDisplay.display();
    }
};


#endif //TOURNAMENTCHOOSEPLAYERSVIEW_H
