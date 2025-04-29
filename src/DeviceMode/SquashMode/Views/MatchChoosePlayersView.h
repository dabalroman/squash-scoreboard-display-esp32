#ifndef MATCHCHOOSEPLAYERSVIEW_H
#define MATCHCHOOSEPLAYERSVIEW_H
#include "LoggerHelper.h"
#include "RemoteInputManager.h"
#include "DeviceMode/View.h"
#include "DeviceMode/SquashMode/SquashModeState.h"
#include "Display/GlyphDisplay.h"
#include "Match/Tournament.h"

class Adafruit_SSD1306;
class GlyphDisplay;
class RemoteInputManager;

class MatchChoosePlayersView final : public View {
    Tournament &tournament;
    std::vector<UserProfile *> &players;
    std::function<void(SquashModeState)> onStateChange;

    UserProfile *playerA = nullptr;
    UserProfile *playerB = nullptr;

    size_t playerAIndex = 0;
    size_t playerBIndex = 1;

public:
    MatchChoosePlayersView(
        Tournament &tournament,
        const std::function<void(SquashModeState)> &onStateChange
    )
        : tournament(tournament), players(tournament.getPlayers()), onStateChange(onStateChange) {
        if (players.size() < 2) {
            printLn("Not enough players");
            return;
        }

        playerA = players.at(0);
        playerB = players.at(1);

        const Match *match = &tournament.getActiveMatch();
        if (!match) {
            return;
        }

        playerA = &match->getPlayerA();
        playerB = &match->getPlayerB();
        playerAIndex = playerA->getId();
        playerBIndex = playerB->getId();
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        if (players.size() > 2) {
            if (remoteInputManager.buttonA.takeActionIfPossible()) {
                do {
                    playerAIndex = (playerAIndex + 1) % players.size();
                } while (players.at(playerAIndex) == playerB);

                playerA = players.at(playerAIndex);
            }

            if (remoteInputManager.buttonB.takeActionIfPossible()) {
                do {
                    playerBIndex = (playerBIndex + 1) % players.size();
                } while (players.at(playerBIndex) == playerA);

                playerB = players.at(playerBIndex);
            }
        }

        if (remoteInputManager.buttonC.takeActionIfPossible()) {
            // TODO: Go to tournament summary
        }

        if (remoteInputManager.buttonD.takeActionIfPossible()) {
            tournament.setActiveMatch(tournament.getMatchBetween(*playerA, *playerB));
            onStateChange(SquashModeState::MatchOn);
        }
    }

    void render(GlyphDisplay &glyphDisplay, Adafruit_SSD1306 &backDisplay) override {
        glyphDisplay.clear();
        glyphDisplay.setGlyphs(
            Glyph::P,
            GlyphDisplay::digitToGlyph(playerA->getId()),
            Glyph::P,
            GlyphDisplay::digitToGlyph(playerB->getId())
        );
        glyphDisplay.setGlyphColor(playerA->getColor(), playerB->getColor());
        glyphDisplay.setGlyphBlinking(true, true, true, true);
        glyphDisplay.render();
        glyphDisplay.show();
    }
};

#endif //MATCHCHOOSEPLAYERSVIEW_H
