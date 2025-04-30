#ifndef TOURNAMENTCHOOSEPLAYERSVIEW_H
#define TOURNAMENTCHOOSEPLAYERSVIEW_H

#include <utility>
#include <vector>

#include "UserProfile.h"
#include "DeviceMode/View.h"

class TournamentChoosePlayersView final : public View {
    Tournament &tournament;
    std::vector<UserProfile *> &users;
    std::function<void(SquashModeState)> onStateChange;
    uint8_t playerIndex = 0;

public:
    explicit TournamentChoosePlayersView(
        Tournament &tournament,
        std::vector<UserProfile *> &players,
        std::function<void(SquashModeState)> onStateChange
    )
        : tournament(tournament), users(players), onStateChange(std::move(onStateChange)) {
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        if (users.empty()) {
            return;
        }

        if (remoteInputManager.buttonA.takeActionIfPossible()) {
            playerIndex++;
            printLn("Player index: %d", playerIndex % users.size());
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

            printLn("Added/removed player");
        }

        if (remoteInputManager.buttonC.takeActionIfPossible()) {
            // TODO: exit squash mode
        }

        if (remoteInputManager.buttonD.takeActionIfPossible()) {
            if (tournament.getPlayers().size() < 2) {
                printLn("Not enough players");
                return;
            }

            onStateChange(SquashModeState::MatchChoosePlayers);
        }
    }

    void renderGlyphs(GlyphDisplay &glyphDisplay) override {
        const bool isPlayerIn = tournament.isPlayerIn(*users.at(playerIndex));
        const Color playerColor = users.at(playerIndex)->getColor();

        glyphDisplay.clear();
        glyphDisplay.setGlyphs(Glyph::P, GlyphDisplay::digitToGlyph(playerIndex), Glyph::Minus,
                               isPlayerIn ? Glyph::UpperDot : Glyph::LowerDot);
        glyphDisplay.setGlyphColor(playerColor, playerColor, Colors::Black, isPlayerIn ? Colors::Green : Colors::Red);
        glyphDisplay.render();
        glyphDisplay.show();
    }

    void renderBack(Adafruit_SSD1306 &backDisplay) override {
        if (!shouldRenderBack) {
            return;
        }

        backDisplay.clearDisplay();
        backDisplay.setFont(&FreeMonoBold24pt7b);
        backDisplay.setCursor(0, 24);
        backDisplay.print("B/C");
        backDisplay.display();

        shouldRenderBack = false;
    }
};


#endif //TOURNAMENTCHOOSEPLAYERSVIEW_H
