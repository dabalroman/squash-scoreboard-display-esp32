#ifndef SQUASH_MODE__MATCH_START_GAME_VIEW_H
#define SQUASH_MODE__MATCH_START_GAME_VIEW_H

#include "Strings.h"
#include "DeviceMode/DeviceModeState.h"
#include "DeviceMode/View.h"
#include "DeviceMode/WatchSupport.h"
#include "DeviceMode/SquashMode/SquashModeState.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Display/LedDisplay/Renderer/MatchResultBarRenderer.h"
#include "Tournament/Tournament.h"

class Adafruit_SSD1306;
class LedDisplay;
class RemoteInputManager;

class SquashMatchStartGameView final : public View {
    Tournament &tournament;
    std::vector<UserProfile *> &players;
    std::function<void(DeviceModeState)> onDeviceModeChange;
    std::function<void(SquashModeState)> onStateChange;

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
    SquashMatchStartGameView(
        Tournament &tournament,
        const std::function<void(DeviceModeState)> &onDeviceModeChange,
        const std::function<void(SquashModeState)> &onStateChange
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
                cycleLeft();
            }

            if (remoteInputManager.buttonB.takeActionIfPossible()) {
                cycleRight();
            }
        }

        if (
            players.size() == 2
            && (remoteInputManager.buttonA.takeActionIfPossible() || remoteInputManager.buttonB.takeActionIfPossible())
        ) {
            swapSides();
        }

        if (remoteInputManager.buttonC.takeActionIfPossible()) {
            swapSides();
        }

        if (remoteInputManager.buttonD.takeActionIfPossible()) {
            remoteInputManager.preventTriggerForMs();
            startMatch();
        }
    }

    void cycleLeft() {
        printLn("Swapping left player");
        if (UserProfile *nextPlayer = getNextPlayer(playerLeft, playerRight)) {
            setMatchByPlayers(nextPlayer, playerRight);
            queueRender();
        }
    }

    void cycleRight() {
        printLn("Swapping right player");
        if (UserProfile *nextPlayer = getNextPlayer(playerRight, playerLeft)) {
            setMatchByPlayers(playerLeft, nextPlayer);
            queueRender();
        }
    }

    void swapSides() {
        setMatchByPlayers(playerRight, playerLeft);
        queueRender();
    }

    void startMatch() {
        // tournament.matchOrderKeeper->confirmMatchBetweenPlayers({playerA->getId(), playerB->getId()});
        onStateChange(SquashModeState::MatchIntro);
        queueRender();
    }

    // Both must be in the tournament and distinct; `leftUid` takes the left court side.
    bool setPair(const uint32_t leftUid, const uint32_t rightUid) {
        const int left = WatchSupport::indexOfUid(players, leftUid);
        const int right = WatchSupport::indexOfUid(players, rightUid);
        if (left < 0 || right < 0 || left == right) {
            return false;
        }

        setMatchByPlayers(players[left], players[right]);
        queueRender();
        return true;
    }

    Garmin::AckStatus handleWatchCommand(const WatchCommand &command) override {
        if (match == nullptr) {
            return Garmin::AckStatus::Invalid;
        }

        switch (command.id) {
            case Garmin::CommandId::SetPair:
                return setPair(command.leftUid, command.rightUid) ? Garmin::AckStatus::Applied
                                                                  : Garmin::AckStatus::Invalid;
            case Garmin::CommandId::SwapSides:
                swapSides();
                return Garmin::AckStatus::Applied;
            case Garmin::CommandId::StartMatch:
                startMatch();
                return Garmin::AckStatus::Applied;
            default:
                return Garmin::AckStatus::WrongScreen;
        }
    }

    void initLedDisplay(LedDisplay &ledDisplay) override {
        ledDisplay.resetAnimations();
        ledDisplay.setColonAppearance();
        ledDisplay.setPlayersIndicatorsState(true);
        ledDisplay.setBorderEnabled(true);
    }

    // Blinking, so always should render
    void renderLedDisplay(LedDisplay &ledDisplay) override {
        if (!shouldRenderLedDisplay) {
            return;
        }

        // Two slots per player, so an id of 10 or more shows only its last digit.
        // Without the % it hit digitToGlyph's "above 9 is Empty" and the slot went
        // blank, which looked like a missing player rather than a truncated id.
        // Players 5 and 15 do read alike here; their colours are what separate them.
        ledDisplay.setGlyphsGlyph(
            Glyph::P,
            LedDisplay::digitToGlyph(playerLeft->getId() % 10),
            Glyph::P,
            LedDisplay::digitToGlyph(playerRight->getId() % 10)
        );

        ledDisplay.setGlyphsAppearance(playerLeft->getColor(), playerRight->getColor());
        ledDisplay.setIndicatorAppearancePlayerA(playerLeft->getColor());
        ledDisplay.setIndicatorAppearancePlayerB(playerRight->getColor());
        ledDisplay.setBorderAppearance(playerLeft->getColor(), playerRight->getColor());

        if (shouldUpdateLedBarState) {
            ledDisplay.setLedBarState([&] { return MatchResultBarRenderer::toLedBarPixels(
                playerLeft->getColor(),
                playerRight->getColor(),
                matchResult,
                match->getPlayersSwappedCourtSides()
            ); });

            shouldUpdateLedBarState = false;
        }

        ledDisplay.display();

        shouldRenderLedDisplay = false;
    }

    void initBackDisplay(BackDisplay &backDisplay) override {
        backDisplay.initBigFont();
    }

    void renderEInkDisplay(EInkDisplay &einkDisplay) override {
        if (!einkDisplay.available()) {
            return;   // V1: constant false, the score lookups below fold away
        }

        if (match == nullptr || playerLeft == nullptr || playerRight == nullptr) {
            einkDisplay.showBlank();
            return;
        }

        const MatchResult result = match->getMatchResult();
        einkDisplay.showMatchScore(
            playerLeft->getName(), result.scoreOf(playerLeft->getId()),
            playerRight->getName(), result.scoreOf(playerRight->getId()),
            Str::MODE_OPTION_SQUASH
        );
    }

    void renderBackDisplay(BackDisplay &backDisplay) override {
        if (!shouldRenderBack) {
            return;
        }

        backDisplay.clear();
        backDisplay.renderPlayerWidget(playerLeft->getName(), playerRight->getName());
        backDisplay.display();

        shouldRenderBack = false;
    }

    void describeForWatch(WatchState &state) const override {
        if (playerLeft == nullptr || playerRight == nullptr) {
            View::describeForWatch(state);
            return;
        }

        state.setMatchStart(WatchSupport::selectedMask(players), playerLeft->getUid(), playerRight->getUid());
    }
};

#endif //SQUASH_MODE__MATCH_START_GAME_VIEW_H
