#ifndef TOURNAMETCHOOSEPLAYERSVIEW_H
#define TOURNAMETCHOOSEPLAYERSVIEW_H

#include <utility>
#include <vector>

#include "UserProfile.h"
#include "DeviceMode/View.h"

class TournamentChoosePlayersView final : public View {
    Tournament &tournament;
    std::vector<UserProfile *> &players;
    std::function<void(SquashModeState)> onStateChange;
    uint8_t playerIndex = 0;

public:
    explicit TournamentChoosePlayersView(
        Tournament &tournament,
        std::vector<UserProfile *> &players,
        std::function<void(SquashModeState)> onStateChange
    )
        : tournament(tournament), players(players), onStateChange(std::move(onStateChange)) {
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        if (players.empty()) {
            return;
        }

        if (remoteInputManager.buttonA.takeActionIfPossible()) {
            playerIndex++;
            printLn("Player index: %d", playerIndex);
        }

        if (playerIndex == players.size()) {
            playerIndex = 0;
        }

        if (remoteInputManager.buttonB.takeActionIfPossible()) {
            if (tournament.isPlayerIn(*players.at(playerIndex))) {
                tournament.removePlayer(*players.at(playerIndex));
            } else {
                tournament.addPlayer(*players.at(playerIndex));
            }

            printLn("Added/removed player");
        }

        if (remoteInputManager.buttonC.takeActionIfPossible()) {
            onStateChange(SquashModeState::MatchChoosePlayers);
        }

        if (remoteInputManager.buttonD.takeActionIfPossible()) {
            // exit squash mode
        }
    }

    void render(GlyphDisplay &glyphDisplay, Adafruit_SSD1306 &backDisplay) override {
        const bool isPlayerIn = tournament.isPlayerIn(*players.at(playerIndex));
        const Color playerColor = players.at(playerIndex)->getColor();

        glyphDisplay.clear();
        glyphDisplay.setGlyphs(Glyph::P, GlyphDisplay::digitToGlyph(playerIndex), Glyph::Minus,
                               isPlayerIn ? Glyph::UpperDot : Glyph::LowerDot);
        glyphDisplay.setGlyphColor(playerColor, playerColor, Colors::Black, isPlayerIn ? Colors::Green : Colors::Red);
        glyphDisplay.render();
        glyphDisplay.show();
    }
};


#endif //TOURNAMETCHOOSEPLAYERSVIEW_H
