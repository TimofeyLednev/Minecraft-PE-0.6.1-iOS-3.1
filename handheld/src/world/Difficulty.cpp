#include "Difficulty.h"

// C++98 wants an out-of-class definition for a static const member that is
// odr-used, even when the in-class initialiser already makes it a compile-time
// constant.  Minecraft::tickInput odr-uses two of these without meaning to:
//
//     options.difficulty = (options.difficulty == Difficulty::PEACEFUL)?
//         Difficulty::NORMAL : Difficulty::PEACEFUL;
//
// Both arms of a conditional expression are const int lvalues, so the result is
// an lvalue as well and the operands are odr-used rather than just read.  An
// optimising compiler folds the whole expression and never emits the reference,
// which is why every other port links without this file -- the Windows Mobile
// debug build, at -O0, is the first one not to fold it.
//
// Defining all four rather than only the two that happen to be referenced today:
// the header offers them as ordinary constants, so any of them may end up in a
// context that odr-uses it.
const int Difficulty::PEACEFUL;
const int Difficulty::EASY;
const int Difficulty::NORMAL;
const int Difficulty::HARD;
