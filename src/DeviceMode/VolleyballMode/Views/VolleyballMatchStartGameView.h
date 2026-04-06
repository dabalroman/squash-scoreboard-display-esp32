#ifndef VOLLEYBALL_MODE__MATCH_START_GAME_VIEW_H
#define VOLLEYBALL_MODE__MATCH_START_GAME_VIEW_H

#include "DeviceMode/DeviceModeState.h"
#include "DeviceMode/View.h"
#include "DeviceMode/VolleyballMode/VolleyballModeState.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/Adapter/MatchResultBarAdapter.h"
#include "Tournament/Tournament.h"

class Adafruit_SSD1306;
class LedDisplay;
class RemoteInputManager;

class VolleyballMatchStartGameView final : public View {
    Tournament &tournament;
    std::vector<UserProfile *> &players;
    std::function<void(DeviceModeState)> onDeviceModeChange;
    std::function<void(VolleyballModeState)> onStateChange;

    UserProfile *playerLeft = nullptr;
    UserProfile *playerRight = nullptr;

    Match *match = nullptr;
    MatchResult matchResult;

    bool shouldUpdateLedBarState = true;

    UserProfile *getNextPlayer(const UserProfile *current, const UserProfile *excluded) const {
        if (players.size() < 2) {
            return nullptr;
        }

        const auto it = std::find(players.begin(), players.end(), current);
        if (it == players.end()) {
            return nullptr;
        }

        const size_t startIndex = static_cast<size_t>(std::distance(players.begin(), it));
        const size_t count = players.size();

        for (size_t step = 1; step < count; ++step) {
            const size_t index = (startIndex + step) % count;
            if (players[index] != excluded) {
                return players[index];
            }
        }

        return nullptr;
    }

    void setupMatchData() {
        playerLeft = &match->getLeftCourtSidePlayer();
        playerRight = &match->getRightCourtSidePlayer();

        matchResult = match->getMatchResult();
        shouldUpdateLedBarState = true;
    }

    void setMatchByPlayers(UserProfile *_playerA, UserProfile *_playerB) {
        match = &tournament.chooseMatchBetween(*_playerA, *_playerB);
        setupMatchData();
    }

public:
    VolleyballMatchStartGameView(
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

        match = tournament.getActiveMatch();
        if (match != nullptr) {
            setupMatchData();
        } else {
            setMatchByPlayers(players.at(0), players.at(1));
        }
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        if (players.size() > 2) {
            if (remoteInputManager.buttonA.takeActionIfPossible()) {
                if (UserProfile *nextPlayer = getNextPlayer(playerLeft, playerRight)) {
                    setMatchByPlayers(nextPlayer, playerRight);
                    queueRender();
                }
            }

            if (remoteInputManager.buttonB.takeActionIfPossible()) {
                if (UserProfile *nextPlayer = getNextPlayer(playerRight, playerLeft)) {
                    setMatchByPlayers(playerLeft, nextPlayer);
                    queueRender();
                }
            }
        }

        if (
            players.size() == 2
            && (remoteInputManager.buttonA.takeActionIfPossible() || remoteInputManager.buttonB.takeActionIfPossible())
        ) {
            setMatchByPlayers(playerRight, playerLeft);
            queueRender();
        }

        if (remoteInputManager.buttonC.takeActionIfPossible()) {
            setMatchByPlayers(playerRight, playerLeft);
            queueRender();
        }

        if (remoteInputManager.buttonD.takeActionIfPossible()) {
            remoteInputManager.preventTriggerForMs();
            // tournament.matchOrderKeeper->confirmMatchBetweenPlayers({playerA->getId(), playerB->getId()});
            onStateChange(VolleyballModeState::GamePlaying);
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
            LedDisplay::digitToGlyph(playerLeft->getId()),
            Glyph::P,
            LedDisplay::digitToGlyph(playerRight->getId())
        );

        ledDisplay.setGlyphsAppearance(playerLeft->getColor(), playerRight->getColor());
        ledDisplay.setIndicatorAppearancePlayerA(playerLeft->getColor());
        ledDisplay.setIndicatorAppearancePlayerB(playerRight->getColor());

        if (shouldUpdateLedBarState) {
            ledDisplay.setLedBarState(MatchResultBarAdapter::toLedBarPixels(
                playerLeft->getColor(),
                playerRight->getColor(),
                matchResult,
                match->getPlayersSwappedCourtSides()
            ));

            shouldUpdateLedBarState = false;
        }

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
            String(static_cast<char>(playerLeft->getId() + 65)),
            String(static_cast<char>(playerRight->getId() + 65))
        );
        backDisplay.display();

        shouldRenderBack = false;
    }
};

#endif //VOLLEYBALL_MODE__MATCH_START_GAME_VIEW_H
