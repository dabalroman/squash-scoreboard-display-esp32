#ifndef VOLLEYBALL_MODE__MATCHCHOOSEPLAYERSVIEW_H
#define VOLLEYBALL_MODE__MATCHCHOOSEPLAYERSVIEW_H

#include "DeviceMode/View.h"
#include "DeviceMode/VolleyballMode/VolleyballModeState.h"
#include "Display/GlyphDisplay.h"
#include "Match/Tournament.h"
#include "RemoteDevelopmentService/LoggerHelper.h"

class Adafruit_SSD1306;
class GlyphDisplay;
class RemoteInputManager;

class VolleyballMatchChoosePlayersView final : public View {
    Tournament &tournament;
    std::vector<UserProfile *> &players;
    std::function<void(DeviceModeState)> onDeviceModeChange;
    std::function<void(VolleyballModeState)> onStateChange;

    UserProfile *playerA = nullptr;
    UserProfile *playerB = nullptr;

    size_t playerAIndex = 0;
    size_t playerBIndex = 1;

public:
    VolleyballMatchChoosePlayersView(
        Tournament &tournament,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        const std::function<void(VolleyballModeState)> &onStateChange
    )
        : tournament(tournament), players(tournament.getPlayers()), onDeviceModeChange(onDeviceModeChange),
          onStateChange(onStateChange) {
        if (players.size() < 2) {
            printLn("Not enough players");
            return;
        }

        // const MatchPlayersPair pair = tournament.matchOrderKeeper->getPlayersForNextMatch();

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
                queueRender();
            }

            if (remoteInputManager.buttonB.takeActionIfPossible()) {
                do {
                    playerBIndex = (playerBIndex + 1) % players.size();
                } while (players.at(playerBIndex) == playerA);

                playerB = players.at(playerBIndex);
                queueRender();
            }
        }

        if (remoteInputManager.buttonC.takeActionIfPossible()) {
            remoteInputManager.preventTriggerForMs();
            onDeviceModeChange(DeviceModeState::ModeSwitchingMode);
            return;
        }

        if (remoteInputManager.buttonD.takeActionIfPossible()) {
            remoteInputManager.preventTriggerForMs();
            tournament.setActiveMatch(tournament.getMatchBetween(*playerA, *playerB));
            tournament.matchOrderKeeper->confirmMatchBetweenPlayers({playerA->getId(), playerB->getId()});
            onStateChange(VolleyballModeState::MatchPlaying);
            queueRender();
        }
    }

    // Blinking, so always should render
    void renderGlyphs(GlyphDisplay &glyphDisplay) override {
        glyphDisplay.setColonAppearance();

        glyphDisplay.setGlyphsGlyph(
            Glyph::P,
            GlyphDisplay::digitToGlyph(playerA->getId()),
            Glyph::P,
            GlyphDisplay::digitToGlyph(playerB->getId())
        );
        glyphDisplay.setGlyphsAppearance(playerA->getColor(), playerB->getColor());

        glyphDisplay.setPlayersIndicatorsState(true);
        glyphDisplay.setIndicatorAppearancePlayerA(playerA->getColor());
        glyphDisplay.setIndicatorAppearancePlayerB(playerB->getColor());

        glyphDisplay.display();
    }

    void renderScreen(BackDisplay &backDisplay) override {
        if (!shouldRenderBack) {
            return;
        }

        backDisplay.clear();
        backDisplay.renderPlayerWidget(
            String(static_cast<char>(playerA->getId() + 65)),
            String(static_cast<char>(playerB->getId() + 65))
        );
        backDisplay.display();

        shouldRenderBack = false;
    }
};

#endif //VOLLEYBALL_MODE__MATCHCHOOSEPLAYERSVIEW_H
