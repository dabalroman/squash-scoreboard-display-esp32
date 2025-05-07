#ifndef TOURNAMENTCHOOSEPLAYERSVIEW_H
#define TOURNAMENTCHOOSEPLAYERSVIEW_H

#include <utility>
#include <vector>

#include "UserProfile.h"
#include "DeviceMode/View.h"
#include "Fonts/FreeMonoBold18pt7b.h"

class TournamentChoosePlayersView final : public View {
    Tournament &tournament;
    std::vector<UserProfile *> &users;
    std::function<void(SquashModeState)> onStateChange;
    uint8_t playerIndex = 0;
    uint32_t playerIndexChangeAtMs = 0;

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
            // TODO: exit squash mode
        }

        if (remoteInputManager.buttonD.takeActionIfPossible()) {
            if (tournament.getPlayers().size() < 2) {
                return;
            }

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
        constexpr uint16_t spaceWidth = 2 * backDisplay.ONE_CHAR_WIDTH_2x_24p7b;
        const String playerName = users.at(playerIndex)->getName();
        const uint16_t textWidth = playerName.length() * backDisplay.ONE_CHAR_WIDTH_2x_24p7b;
        const uint16_t segmentWidth = textWidth + spaceWidth;
        const uint16_t totalWidth = (textWidth + spaceWidth) * 2;

        const int scrollX =
            ((millis() - playerIndexChangeAtMs) / 25 % segmentWidth) * -1 + backDisplay.ONE_CHAR_WIDTH_2x_24p7b / 2;

        GFXcanvas1 canvas(totalWidth, 64);
        canvas.setFont(&FreeMonoBold24pt7b);
        canvas.setTextSize(2);
        canvas.setCursor(0, backDisplay.VERTICAL_CURSOR_OFFSET_2x_24p7b);
        canvas.print(playerName);
        canvas.setCursor(segmentWidth, backDisplay.VERTICAL_CURSOR_OFFSET_2x_24p7b);
        canvas.print(playerName);

        backDisplay.clear();
        backDisplay.screen->drawBitmap(scrollX, 0, canvas.getBuffer(), totalWidth, 64, WHITE);
        backDisplay.display();
    }
};


#endif //TOURNAMENTCHOOSEPLAYERSVIEW_H
