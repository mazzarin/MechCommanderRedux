#pragma once

// Port-only: the game's cursor shown as the system (hardware) cursor instead of drawn into the frame, so it follows
// the mouse at the desktop's rate. PREFS "SoftwareCursor" (gSoftwareCursor) keeps the original's drawn cursor.

#include "platform/MCCursor.h"
#include "platform/MCRegisteredBlock.h"

/// <summary>
/// A cursor shape as a cursor picture: shape <paramref name="shapeNum"/> of <paramref name="shapeTable"/> as
/// <c>AG_shape_draw</c> draws it, its skipped pixels transparent, its hot spot the shape's origin.
/// </summary>
MCCursorImage MCCursorImageFromShape(std::span<const uint8_t> shapeTable, int32_t shapeNum);

/// <summary>
/// Sets the system cursors' shapes to the loaded shapes in <paramref name="shapes"/> (the GUI's, kept while it runs), each
/// one's picture and cursor made the first time it is shown, its HD frame read ahead by then: the shape shown first now,
/// the rest by HD art's readers as a set (SPEC-first-use.md); <c>aSystem::init</c> calls it.
/// </summary>
void MCHardwareCursorPreload(std::span<const MCRegisteredBlock> shapes);

/// <summary>Shows the current cursor shape (<c>application->cursorShape</c>, -1 for none) as the system cursor.</summary>
void MCHardwareCursorUpdate();
