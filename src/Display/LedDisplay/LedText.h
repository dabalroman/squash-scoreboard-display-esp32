#ifndef LED_TEXT_H
#define LED_TEXT_H

#include "GlyphMasks.h"

/**
 * Text -> glyph adapter, so the 4-character LED words can be written as ordinary
 * strings in src/Strings.h next to the OLED and e-paper wording they have to
 * match, instead of as hand-assembled Glyph lists inside the views.
 *
 * The table has only one form of most letters, so the mapping is case-sensitive
 * where both exist (C/c, H/h, I/i, L/l, U/u) and case-folding where only one
 * does ('B' and 'b' both give the lowercase b). Write the word the way it will
 * actually look: "buZZ", "rESt", "oPCJ".
 *
 * There is no W, K, M, V, Q or X in the table - those render blank, which is
 * why Return is COFNIJ and not WSTECZ. A word shorter than 4 characters pads
 * with Empty.
 */
namespace LedText {
    constexpr Glyph toGlyph(const char c) {
        return c == '0' ? Glyph::D0
             : c == '1' ? Glyph::D1
             : c == '2' ? Glyph::D2
             : c == '3' ? Glyph::D3
             : c == '4' ? Glyph::D4
             : c == '5' ? Glyph::D5
             : c == '6' ? Glyph::D6
             : c == '7' ? Glyph::D7
             : c == '8' ? Glyph::D8
             : c == '9' ? Glyph::D9
             : c == 'A' || c == 'a' ? Glyph::A
             : c == 'B' || c == 'b' ? Glyph::b
             : c == 'C' ? Glyph::C
             : c == 'c' ? Glyph::c
             : c == 'D' || c == 'd' ? Glyph::d
             : c == 'E' || c == 'e' ? Glyph::E
             : c == 'F' || c == 'f' ? Glyph::F
             : c == 'G' || c == 'g' ? Glyph::G
             : c == 'H' ? Glyph::H
             : c == 'h' ? Glyph::h
             : c == 'I' ? Glyph::I
             : c == 'i' ? Glyph::i
             : c == 'J' || c == 'j' ? Glyph::J
             : c == 'L' ? Glyph::L
             : c == 'l' ? Glyph::l
             : c == 'N' || c == 'n' ? Glyph::n
             : c == 'O' || c == 'o' ? Glyph::o
             : c == 'P' || c == 'p' ? Glyph::P
             : c == 'R' || c == 'r' ? Glyph::r
             : c == 'S' || c == 's' ? Glyph::S
             : c == 'T' || c == 't' ? Glyph::t
             : c == 'U' ? Glyph::U
             : c == 'u' ? Glyph::u
             : c == 'Y' || c == 'y' ? Glyph::Y
             : c == 'Z' || c == 'z' ? Glyph::Z
             : c == '-' ? Glyph::Minus
             : c == '.' ? Glyph::Dot
             : c == '_' ? Glyph::Underscore
             : c == '=' ? Glyph::Equals
             : c == ']' ? Glyph::RBracket
             : c == '\'' ? Glyph::Apostrophe
             : Glyph::Empty;
    }

    // Walks rather than indexes, so a word shorter than 4 stops at the terminator
    // instead of reading past it.
    constexpr char charAt(const char *text, const uint8_t index) {
        return index == 0 || *text == '\0' ? *text : charAt(text + 1, index - 1);
    }

    constexpr LedWord toWord(const char *text) {
        return LedWord{
            toGlyph(charAt(text, 0)),
            toGlyph(charAt(text, 1)),
            toGlyph(charAt(text, 2)),
            toGlyph(charAt(text, 3)),
        };
    }
}

#endif //LED_TEXT_H
