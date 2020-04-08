/*
 C-Dogs SDL
 A port of the legendary (and fun) action/arcade cdogs.
 Copyright (c) 2014, 2016, 2018-2020 Cong Xu
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
#include "map_static.h"

#include "actors.h"
#include "campaigns.h"
#include "events.h"
#include "game_events.h"
#include "gamedata.h"
#include "handle_game_events.h"
#include "log.h"
#include "map_build.h"
#include "net_util.h"

static int AddTileClass(any_t data, any_t item);
void MapStaticLoad(MapBuilder *mb) {
	// Tile classes
	if (hashmap_iterate(mb->mission->u.Static.TileClasses, AddTileClass,
			NULL) != MAP_OK) {
		CASSERT(false, "failed to add static tile classes");
	}

	// Tiles
	RECT_FOREACH(Rect2iNew(svec2i_zero(), mb->Map->Size))
				MapStaticLoadTile(mb, _v);
			RECT_FOREACH_END()

			// Exit area
	if (!svec2i_is_zero(mb->mission->u.Static.Exit.Start)
			&& !svec2i_is_zero(mb->mission->u.Static.Exit.End)) {
		mb->Map->ExitStart = mb->mission->u.Static.Exit.Start;
		mb->Map->ExitEnd = mb->mission->u.Static.Exit.End;
	}
}
static int AddTileClass(any_t data, any_t item) {
	UNUSED(data);
	TileClass *t = static_cast<TileClass*>(item);
	// Attach base style to tile class for convenience in editors etc
	t->StyleType = static_cast<char*>(malloc(
			strlen(TileClassBaseStyleType(t->Type)) + 1));
	if (t->StyleType == NULL
			&& (strlen(TileClassBaseStyleType(t->Type)) + 1) > 0) {
		exit(1);
	}
	strcpy(t->StyleType, TileClassBaseStyleType(t->Type));
//	CSTRDUP(t->StyleType, TileClassBaseStyleType(t->Type));
	TileClassesAdd(&gTileClasses, &gPicManager, t, t->Style, t->StyleType,
			t->Mask, t->MaskAlt);
	switch (t->Type) {
	case TILE_CLASS_DOOR:
		SetupDoorTileClasses(&gPicManager, t);
		break;
	case TILE_CLASS_WALL:
		SetupWallTileClasses(&gPicManager, t);
		break;
	case TILE_CLASS_FLOOR:
		SetupFloorTileClasses(&gPicManager, t);
		break;
	default:
		break;
	}
	return MAP_OK;
}

void MapStaticLoadTile(MapBuilder *mb, const struct vec2i v) {
	if (!MapIsTileIn(mb->Map, v))
		return;
	const int idx = v.y * mb->Map->Size.x + v.x;
	int tileAccess = *(int*) CArrayGet(&mb->mission->u.Static.Access, idx);
	if (!AreKeysAllowed(gCampaign.Entry.Mode)) {
		tileAccess = 0;
	}
	const TileClass *tc = MissionStaticGetTileClass(&mb->mission->u.Static,
			mb->Map->Size, v);
	MapBuilderSetTile(mb, v, tc);
	MapBuildSetAccess(mb, v, (uint16_t) tileAccess);
}

static void AddCharacters(const CArray *characters);
static void AddObjectives(MapBuilder *mb, const CArray *objectives);
static void AddKeys(MapBuilder *mb, const CArray *keys);
void MapStaticLoadDynamic(MapBuilder *mb) {
	// Map objects
	CA_FOREACH(const MapObjectPositions, mop, mb->mission->u.Static.Items)
		for (int j = 0; j < (int) mop->Positions.size; j++) {
			const struct vec2i *pos =
					static_cast<const struct vec2i*>(CArrayGet(&mop->Positions,
							j));
			MapTryPlaceOneObject(mb, *pos, mop->M, 0, false);
		}CA_FOREACH_END()

	if (ModeHasNPCs(gCampaign.Entry.Mode)) {
		AddCharacters(&mb->mission->u.Static.Characters);
	}

	if (HasObjectives(gCampaign.Entry.Mode)) {
		AddObjectives(mb, &mb->mission->u.Static.Objectives);
	}

	if (AreKeysAllowed(gCampaign.Entry.Mode)) {
		AddKeys(mb, &mb->mission->u.Static.Keys);
	}

	// Process the events to place dynamic objects
	HandleGameEvents(&gGameEvents, NULL, NULL, NULL);
}
static void AddCharacter(const CharacterPositions *cp);
static void AddCharacters(const CArray *characters) {
	CA_FOREACH(const CharacterPositions, cp, *characters)
		AddCharacter(cp);
	CA_FOREACH_END()
}
static void AddCharacter(const CharacterPositions *cp) {
	NActorAdd aa = NActorAdd_init_default;
	aa.CharId = cp->Index;
	const Character *c = static_cast<const Character*>(CArrayGet(
			&gCampaign.Setting.characters.OtherChars, aa.CharId));
	aa.Health = CharacterGetStartingHealth(c, true);
	CA_FOREACH(const struct vec2i, pos, cp->Positions)
		aa.UID = ActorsGetNextUID();
		aa.Direction = rand() % DIRECTION_COUNT;
		aa.Pos = Vec2ToNet(Vec2CenterOfTile(*pos));

		GameEvent e = GameEventNew(GAME_EVENT_ACTOR_ADD);
		e.u.ActorAdd = aa;
		GameEventsEnqueue(&gGameEvents, e);
	CA_FOREACH_END()
}
static void AddObjective(MapBuilder *mb, const ObjectivePositions *op);
static void AddObjectives(MapBuilder *mb, const CArray *objectives) {
	CA_FOREACH(const ObjectivePositions, op, *objectives)
		AddObjective(mb, op);
	CA_FOREACH_END()
}
static void AddObjective(MapBuilder *mb, const ObjectivePositions *op) {
	if (op->Index >= (int) mb->mission->Objectives.size) {
		LOG(LM_MAP, LL_ERROR, "cannot add objective; objective #%d missing",
				op->Index);
		return;
	}
	Objective *o = static_cast<Objective*>(CArrayGet(&mb->mission->Objectives,
			op->Index));
	CA_FOREACH(const struct vec2i, tilePos, op->Positions)
		const int *idx = static_cast<const int*>(CArrayGet(&op->Indices,
				_ca_index));
		const struct vec2 pos = Vec2CenterOfTile(*tilePos);
		switch (o->Type) {
		case OBJECTIVE_KILL: {
			NActorAdd aa = NActorAdd_init_default;
			aa.UID = ActorsGetNextUID();
			aa.CharId = CharacterStoreGetSpecialId(&mb->co->Setting.characters,
					*idx);
			aa.ThingFlags = ObjectiveToThing(op->Index);
			aa.Direction = rand() % DIRECTION_COUNT;
			const Character *c = static_cast<const Character*>(CArrayGet(
					&gCampaign.Setting.characters.OtherChars, aa.CharId));
			aa.Health = CharacterGetStartingHealth(c, true);
			aa.Pos = Vec2ToNet(pos);
			GameEvent e = GameEventNew(GAME_EVENT_ACTOR_ADD);
			e.u.ActorAdd = aa;
			GameEventsEnqueue(&gGameEvents, e);
		}
			break;
		case OBJECTIVE_COLLECT:
			MapPlaceCollectible(mb->mission, op->Index, pos);
			break;
		case OBJECTIVE_DESTROY:
			MapTryPlaceOneObject(mb, *tilePos, o->u.MapObject,
					ObjectiveToThing(op->Index), false);
			break;
		case OBJECTIVE_RESCUE: {
			NActorAdd aa = NActorAdd_init_default;
			aa.UID = ActorsGetNextUID();
			aa.CharId = CharacterStoreGetPrisonerId(&mb->co->Setting.characters,
					*idx);
			aa.ThingFlags = ObjectiveToThing(op->Index);
			aa.Direction = rand() % DIRECTION_COUNT;
			const Character *c = static_cast<const Character*>(CArrayGet(
					&gCampaign.Setting.characters.OtherChars, aa.CharId));
			aa.Health = CharacterGetStartingHealth(c, true);
			aa.Pos = Vec2ToNet(pos);
			GameEvent e = GameEventNew(GAME_EVENT_ACTOR_ADD);
			e.u.ActorAdd = aa;
			GameEventsEnqueue(&gGameEvents, e);
		}
			break;
		default:
			// do nothing
			break;
		}
		o->placed++;
	CA_FOREACH_END()
}
static void AddKey(MapBuilder *mb, const KeyPositions *kp);
static void AddKeys(MapBuilder *mb, const CArray *keys) {
	CA_FOREACH(const KeyPositions, kp, *keys)
		AddKey(mb, kp);
	CA_FOREACH_END()
}
static void AddKey(MapBuilder *mb, const KeyPositions *kp) {
	CA_FOREACH(const struct vec2i, pos, kp->Positions)
		MapPlaceKey(mb, *pos, kp->Index);
	CA_FOREACH_END()
}
