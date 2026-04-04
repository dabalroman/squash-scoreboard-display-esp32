#ifndef VOLLEYBALL_MODE__MATCHCHOOSEPLAYERSVIEW_H
#define VOLLEYBALL_MODE__MATCHCHOOSEPLAYERSVIEW_H

#include "DeviceMode/View.h"
#include "DeviceMode/VolleyballMode/VolleyballModeState.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Match/Tournament.h"
#include "RemoteDevelopmentService/LoggerHelper.h"

class Adafruit_SSD1306;
class LedDisplay;
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

        const Match *match = tournament.getActiveMatch();
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

        if (
            players.size() == 2
            && (remoteInputManager.buttonA.takeActionIfPossible() || remoteInputManager.buttonB.takeActionIfPossible())
        ) {
            const size_t tempIndex = playerBIndex;
            playerBIndex = playerAIndex;
            playerAIndex = tempIndex;

            playerA = players.at(playerAIndex);
            playerB = players.at(playerBIndex);
            queueRender();
        }

        if (remoteInputManager.buttonD.takeActionIfPossible()) {
            remoteInputManager.preventTriggerForMs();

            const Match& match = tournament.getMatchBetween(*playerA, *playerB);
            tournament.setActiveMatch(match);
            tournament.matchOrderKeeper->confirmMatchBetweenPlayers({playerA->getId(), playerB->getId()});

            onStateChange(VolleyballModeState::MatchPlaying);
            queueRender();
        }
    }

    void initLedDisplay(LedDisplay &ledDisplay) override {
        ledDisplay.resetHistoryBar();
        ledDisplay.setColonAppearance();
        ledDisplay.setPlayersIndicatorsState(true);
    }

    // Blinking, so always should render
    void renderLedDisplay(LedDisplay &ledDisplay) override {
        if (!shouldRenderLedDisplay) {
            return;
        }

        ledDisplay.setGlyphsGlyph(
            Glyph::P,
            LedDisplay::digitToGlyph(playerA->getId()),
            Glyph::P,
            LedDisplay::digitToGlyph(playerB->getId())
        );

        ledDisplay.setGlyphsAppearance(playerA->getColor(), playerB->getColor());
        ledDisplay.setIndicatorAppearancePlayerA(playerA->getColor());
        ledDisplay.setIndicatorAppearancePlayerB(playerB->getColor());

        ledDisplay.display();

        shouldRenderLedDisplay = false;
    }

    void initBackDisplay(BackDisplay &backDisplay) override {
        backDisplay.initBigFont();
    }

    void renderBackDisplay(BackDisplay &backDisplay) override {
        if (!shouldRenderBack) {
            return;
        }

        backDisplay.clear();
        backDisplay.renderPlayerWidget(
            String(static_cast<char>(playerA->getId() + 65)),
            String(static_cast<char>(playerB->getId() + 65))
        );
        backDisplay.display();
    }
};

#endif //VOLLEYBALL_MODE__MATCHCHOOSEPLAYERSVIEW_H
