#ifndef MATCH_INTRO_VIEW_H
#define MATCH_INTRO_VIEW_H

#include <functional>
#include "DeviceMode/View.h"
#include "Display/LedDisplay/LedDisplay.h"
#include "Tournament/Tournament.h"

/**
 * Between MatchStartGame and GamePlaying, shared by every sport mode: an inward
 * wipe paints the front's halves in the two players' colours, then hands over.
 * The OLED and e-paper already show exactly GamePlaying's first frame, so the
 * e-paper refreshes on entry and not again at the hand-over. No game exists yet
 * (GamePlaying creates it), so skipping or going back loses nothing.
 */
template <typename StateEnum>
class MatchIntroView final : public View {
    const Match *match = nullptr;
    UserProfile *playerLeft = nullptr, *playerRight = nullptr;
    std::function<void(StateEnum)> onStateChange;
    const char *einkLabel;
    bool einkShowsMatchSets;
    bool started = false;

public:
    MatchIntroView(
        Tournament &tournament,
        std::function<void(StateEnum)> onStateChange,
        const char *einkLabel,
        const bool einkShowsMatchSets
    )
        : onStateChange(std::move(onStateChange)), einkLabel(einkLabel), einkShowsMatchSets(einkShowsMatchSets) {
        match = tournament.getActiveMatch();

        if (match == nullptr) {
            this->onStateChange(StateEnum::MatchStartGame);
            return;
        }

        playerLeft  = &match->getLeftCourtSidePlayer();
        playerRight = &match->getRightCourtSidePlayer();
    }

    void handleInput(RemoteInputManager &remoteInputManager) override {
        // Evaluated one by one, so every pending press is consumed, not just the first.
        bool pressed = remoteInputManager.buttonA.takeActionIfPossible();
        pressed = remoteInputManager.buttonB.takeActionIfPossible() || pressed;
        pressed = remoteInputManager.buttonC.takeActionIfPossible() || pressed;
        pressed = remoteInputManager.buttonD.takeActionIfPossible() || pressed;

        if (pressed) {
            // Otherwise the same press scores or undoes on GamePlaying.
            remoteInputManager.preventTriggerForMs();
            skip();
        }
    }

    void skip() {
        onStateChange(StateEnum::GamePlaying);
    }

    Garmin::AckStatus handleWatchCommand(const WatchCommand &command) override {
        if (match == nullptr) {
            return Garmin::AckStatus::Busy;
        }

        if (command.id != Garmin::CommandId::Skip) {
            return Garmin::AckStatus::WrongScreen;
        }

        skip();
        return Garmin::AckStatus::Applied;
    }

    void initLedDisplay(LedDisplay &ledDisplay) override {
        if (match == nullptr) return;

        // A second init is restoreView() after an Overlay: no replay.
        if (started) {
            onStateChange(StateEnum::GamePlaying);
            return;
        }
        started = true;

        ledDisplay.resetAnimations();
        ledDisplay.setGlyphsGlyph(Glyph::Empty, Glyph::Empty, Glyph::Empty, Glyph::Empty);
        ledDisplay.setColonAppearance();
        ledDisplay.setBorderEnabled(false);
        ledDisplay.setPlayersIndicatorsState(true);
        ledDisplay.setIndicatorAppearancePlayerA(playerLeft->getColor());
        ledDisplay.setIndicatorAppearancePlayerB(playerRight->getColor());
        ledDisplay.startIntro(playerLeft->getColor(), playerRight->getColor());
    }

    void renderLedDisplay(LedDisplay &ledDisplay) override {
        // Also keeps a constructor bail to MatchStartGame from being overwritten.
        if (match == nullptr) return;

        // Hand over without drawing: an expired layer leaves only the dark base,
        // a black frame before 0:0. The LEDs keep the painted hold meanwhile.
        if (!ledDisplay.introActive()) {
            onStateChange(StateEnum::GamePlaying);
            return;
        }

        ledDisplay.display();
    }

    void initBackDisplay(BackDisplay &backDisplay) override {
        if (match == nullptr) return;

        backDisplay.initBigFont();
    }

    void renderBackDisplay(BackDisplay &backDisplay) override {
        if (match == nullptr || !shouldRenderBack) return;

        backDisplay.clear();
        backDisplay.renderScoreWidget(0, 0);
        backDisplay.display();

        shouldRenderBack = false;
    }

    void renderEInkDisplay(EInkDisplay &einkDisplay) override {
        if (!einkDisplay.available()) return;

        if (match == nullptr) {
            einkDisplay.showBlank();
            return;
        }

        // Padel's GamePlaying shows the fresh set's gems, 0:0.
        if (!einkShowsMatchSets) {
            einkDisplay.showMatchScore(playerLeft->getName(), 0, playerRight->getName(), 0, einkLabel);
            return;
        }

        const MatchResult result = match->getMatchResult();
        einkDisplay.showMatchScore(
            playerLeft->getName(), result.scoreOf(playerLeft->getId()),
            playerRight->getName(), result.scoreOf(playerRight->getId()),
            einkLabel
        );
    }

    void describeForWatch(WatchState &state) const override {
        if (match == nullptr) {
            View::describeForWatch(state);
            return;
        }

        state.setIntro(playerLeft->getUid(), playerRight->getUid());
    }
};

#endif //MATCH_INTRO_VIEW_H
