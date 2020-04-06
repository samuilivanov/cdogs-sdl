/*
 C-Dogs SDL
 A port of the legendary (and fun) action/arcade cdogs.
 Copyright (C) 1995 Ronny Wester
 Copyright (C) 2003 Jeremy Chin
 Copyright (C) 2003 Lucas Martin-King

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 This file incorporates work covered by the following copyright and
 permission notice:

 Copyright (c) 2013-2014, 2016, 2018-2019 Cong Xu
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.
 Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
 */
#include "automap.h"

#include <stdio.h>
#include <string.h>

#include "actors.h"
#include "config.h"
#include "draw/draw.h"
#include "draw/draw_actor.h"
#include "draw/drawtools.h"
#include "font.h"
#include "gamedata.h"
#include "map.h"
#include "mission.h"
#include "objs.h"
#include "pic_manager.h"
#include "pickup.h"

#define MAP_FACTOR 2
#define MASK_ALPHA 128;

color_t colorWall = { 72, 152, 72, 255 };
color_t colorFloor = { 12, 92, 12, 255 };
color_t colorRoom = { 24, 112, 24, 255 };
color_t colorDoor = { 172, 172, 172, 255 };
color_t colorYellowDoor = { 252, 224, 0, 255 };
color_t colorGreenDoor = { 0, 252, 0, 255 };
color_t colorBlueDoor = { 0, 252, 252, 255 };
color_t colorRedDoor = { 132, 0, 0, 255 };
color_t colorExit = { 255, 255, 255, 255 };

static void DisplayPlayer(SDL_Renderer *renderer, const TActor *player,
		struct vec2i pos, const int scale) {
	const struct vec2i playerPos = Vec2ToTile(player->thing.Pos);
	pos = svec2i_add(pos, svec2i_scale(playerPos, (float) scale));
	if (scale >= 2) {
		DrawHead(renderer, ActorGetCharacter(player), DIRECTION_DOWN, pos);
	} else {
		DrawPoint(pos, colorWhite);
	}
}

static void DisplayObjective(Thing *t, int objectiveIndex, struct vec2i pos,
		int scale, int flags) {
	const struct vec2i objectivePos = Vec2ToTile(t->Pos);
	const Objective *o = static_cast<const Objective*>(CArrayGet(
			&gMission.missionData->Objectives, objectiveIndex));
	color_t color = o->color;
	pos = svec2i_add(pos, svec2i_scale(objectivePos, (float) scale));
	if (flags & AUTOMAP_FLAGS_MASK) {
		color.a = MASK_ALPHA
		;
	}
	if (scale >= 2) {
		DrawCross(&gGraphicsDevice, pos, color);
	} else {
		DrawPoint(pos, color);
	}
}

static void DisplayExit(struct vec2i pos, int scale, int flags) {
	struct vec2i exitPos = gMap.ExitStart;
	struct vec2i exitSize = svec2i_add(svec2i_subtract(gMap.ExitEnd, exitPos),
			svec2i_one());
	color_t color = colorExit;

	if (!HasExit(gCampaign.Entry.Mode)) {
		return;
	}

	exitPos = svec2i_scale(exitPos, (float) scale);
	exitSize = svec2i_scale(exitSize, (float) scale);
	exitPos = svec2i_add(exitPos, pos);

	if (flags & AUTOMAP_FLAGS_MASK) {
		color.a = MASK_ALPHA
		;
	}
	DrawRectangle(&gGraphicsDevice, exitPos, exitSize, color, false);
}

static void DisplaySummary(void) {
	char sScore[20];
	struct vec2i pos;
	pos.y = gGraphicsDevice.cachedConfig.Res.y - 5 - FontH();

	CA_FOREACH(const Objective, o, gMission.missionData->Objectives)
		if (ObjectiveIsRequired(o) || o->done > 0) {
			color_t textColor = colorWhite;
			pos.x = 5;
			// Objective color dot
			DrawRectangle(&gGraphicsDevice, svec2i(pos.x, pos.y + 3),
					svec2i(2, 2), o->color, false);

			pos.x += 5;

			sprintf(sScore, "(%d)", o->done);

			if (!ObjectiveIsRequired(o)) {
				textColor = colorPurple;
			} else if (ObjectiveIsComplete(o)) {
				textColor = colorRed;
			}
			pos = FontStrMask(o->Description, pos, textColor);
			pos.x += 5;
			FontStrMask(sScore, pos, textColor);
			pos.y -= (FontH() + 1);
		}CA_FOREACH_END()
}

color_t DoorColor(int x, int y) {
	int l = MapGetDoorKeycardFlag(&gMap, svec2i(x, y));

	switch (l) {
	case FLAGS_KEYCARD_YELLOW:
		return colorYellowDoor;
	case FLAGS_KEYCARD_GREEN:
		return colorGreenDoor;
	case FLAGS_KEYCARD_BLUE:
		return colorBlueDoor;
	case FLAGS_KEYCARD_RED:
		return colorRedDoor;
	default:
		return colorDoor;
	}
}

void DrawDot(Thing *t, color_t color, struct vec2i pos, int scale) {
	const struct vec2i dotPos = Vec2ToTile(t->Pos);
	pos = svec2i_add(pos, svec2i_scale(dotPos, (float) scale));
	DrawRectangle(&gGraphicsDevice, pos, svec2i(scale, scale), color, false);
}

static void DrawMap(Map *map, struct vec2i center, struct vec2i centerOn,
		struct vec2i size, int scale, int flags) {
	int x, y;
	struct vec2i mapPos = svec2i_add(center,
			svec2i_scale(centerOn, (float) -scale));
	for (y = 0; y < gMap.Size.y; y++) {
		int i;
		for (i = 0; i < scale; i++) {
			for (x = 0; x < gMap.Size.x; x++) {
				Tile *tile = MapGetTile(map, svec2i(x, y));
				if (tile->Class->Pic != NULL
						&& (tile->isVisited || (flags & AUTOMAP_FLAGS_SHOWALL))) {
					int j;
					for (j = 0; j < scale; j++) {
						struct vec2i drawPos = svec2i(mapPos.x + x * scale + j,
								mapPos.y + y * scale + i);
						color_t color = colorTransparent;
						switch (tile->Class->Type) {
						case TILE_CLASS_WALL:
							color = colorWall;
							break;
						case TILE_CLASS_DOOR:
							color = DoorColor(x, y);
							break;
						case TILE_CLASS_FLOOR:
							color = tile->Class->IsRoom ?
									colorRoom : colorFloor;
							break;
						default:
							CASSERT(false, "Unknown tile class type")
							;
							break;
						}
						if (!ColorEquals(color, colorTransparent)) {
							if (flags & AUTOMAP_FLAGS_MASK) {
								color.a = MASK_ALPHA
								;
							}
							DrawPoint(drawPos, color);
						}
					}
				}
			}
		}
	}
	if (flags & AUTOMAP_FLAGS_MASK) {
		const color_t color = { 255, 255, 255, 128 };
		DrawRectangle(&gGraphicsDevice,
				svec2i_subtract(center, svec2i_scale_divide(size, 2)), size,
				color, false);
	}
}

static void DrawThing(Thing *t, Tile *tile, struct vec2i pos, int scale,
		int flags);
static void DrawObjectivesAndKeys(Map *map, struct vec2i pos, int scale,
		int flags) {
	for (int y = 0; y < map->Size.y; y++) {
		for (int x = 0; x < map->Size.x; x++) {
			Tile *tile = MapGetTile(map, svec2i(x, y));
			CA_FOREACH(ThingId, tid, tile->things)
				DrawThing(ThingIdGetThing(tid), tile, pos, scale, flags);
			CA_FOREACH_END()
		}
	}
}
static void DrawThing(Thing *t, Tile *tile, struct vec2i pos, int scale,
		int flags) {
	if ((t->flags & THING_OBJECTIVE) != 0) {
		const int obj = ObjectiveFromThing(t->flags);
		const Objective *o = static_cast<const Objective*>(CArrayGet(
				&gMission.missionData->Objectives, obj));
		if (!(o->Flags & OBJECTIVE_HIDDEN) || (flags & AUTOMAP_FLAGS_SHOWALL)) {
			if ((o->Flags & OBJECTIVE_POSKNOWN) || tile->isVisited
					|| (flags & AUTOMAP_FLAGS_SHOWALL)) {
				DisplayObjective(t, obj, pos, scale, flags);
			}
		}
	} else if (t->kind == KIND_PICKUP && tile->isVisited) {
		const Pickup *p =
				static_cast<const Pickup*>(CArrayGet(&gPickups, t->id));
		if (p->pickupClass->Type == PICKUP_KEYCARD) {
			color_t dotColor = colorBlack;
			switch (((const Pickup*) CArrayGet(&gPickups, t->id))->pickupClass->u.Keys) {
			case FLAGS_KEYCARD_RED:
				dotColor = colorRedDoor;
				break;
			case FLAGS_KEYCARD_BLUE:
				dotColor = colorBlueDoor;
				break;
			case FLAGS_KEYCARD_GREEN:
				dotColor = colorGreenDoor;
				break;
			case FLAGS_KEYCARD_YELLOW:
				dotColor = colorYellowDoor;
				break;
			default:
				CASSERT(false, "Unknown key color")
				;
				break;
			}
			DrawDot(t, dotColor, pos, scale);
		}
	}
}

void AutomapDraw(SDL_Renderer *renderer, const int flags, const bool showExit) {
	struct vec2i mapCenter = svec2i(gGraphicsDevice.cachedConfig.Res.x / 2,
			gGraphicsDevice.cachedConfig.Res.y / 2);
	struct vec2i centerOn = svec2i(gMap.Size.x / 2, gMap.Size.y / 2);
	struct vec2i pos = svec2i_add(mapCenter,
			svec2i_scale(centerOn, -MAP_FACTOR));

	// Draw faded green overlay
	const color_t mask = { 0, 128, 0, 128 };
	DrawRectangle(&gGraphicsDevice, svec2i_zero(),
			gGraphicsDevice.cachedConfig.Res, mask, true);

	DrawMap(&gMap, mapCenter, centerOn, gMap.Size, MAP_FACTOR, flags);
	DrawObjectivesAndKeys(&gMap, pos, MAP_FACTOR, flags);

	CA_FOREACH(const PlayerData, p, gPlayerDatas)
		if (!IsPlayerAlive(p)) {
			continue;
		}
		DisplayPlayer(renderer, ActorGetByUID(p->ActorUID), pos, MAP_FACTOR);
	CA_FOREACH_END()

	if (showExit) {
		DisplayExit(pos, MAP_FACTOR, flags);
	}
	DisplaySummary();
}

// Draw mini automap
void AutomapDrawRegion(SDL_Renderer *renderer, Map *map, struct vec2i pos,
		const struct vec2i size, const struct vec2i mapCenter, const int flags,
		const bool showExit) {
	const int scale = 1;
	const Rect2i oldClip = GraphicsGetClip(renderer);
	GraphicsSetClip(renderer, Rect2iNew(pos, size));
	pos = svec2i_add(pos, svec2i_scale_divide(size, 2));
	DrawMap(map, pos, mapCenter, size, scale, flags);
	const struct vec2i centerOn = svec2i_add(pos,
			svec2i_scale(mapCenter, (float) -scale));
	CA_FOREACH(const PlayerData, p, gPlayerDatas)
		if (!IsPlayerAlive(p)) {
			continue;
		}
		const TActor *player = ActorGetByUID(p->ActorUID);
		DisplayPlayer(renderer, player, centerOn, scale);
	CA_FOREACH_END()
	DrawObjectivesAndKeys(&gMap, centerOn, scale, flags);
	if (showExit) {
		DisplayExit(centerOn, scale, flags);
	}
	GraphicsSetClip(renderer, oldClip);
}
