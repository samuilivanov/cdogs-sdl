/*
 C-Dogs SDL
 A port of the legendary (and fun) action/arcade cdogs.
 Copyright (c) 2014-2017, 2019-2020 Cong Xu
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
#include "map_new.h"

#include <assert.h>
#include <locale.h>
#include <stdio.h>

#include "door.h"
#include "files.h"
#include "json_utils.h"
#include "log.h"
#include "map_archive.h"
#include "mission.h"

int MapNewScan(const char *filename, char **title, int *numMissions) {
	int err = 0;
	json_t *root = NULL;
	FILE *f = NULL;

	if (strcmp(StrGetFileExt(filename), "cdogscpn") == 0
			|| strcmp(StrGetFileExt(filename), "CDOGSCPN") == 0) {
		return MapNewScanArchive(filename, title, numMissions);
	}
	if (IsCampaignOldFile(filename)) {
		return ScanCampaignOld(filename, title, numMissions);
	}
	f = fopen(filename, "r");
	if (f == NULL) {
		err = -1;
		goto bail;
	}
	if (json_stream_parse(f, &root) != JSON_OK) {
		err = -1;
		goto bail;
	}
	err = MapNewScanJSON(root, title, numMissions);
	if (err < 0) {
		goto bail;
	}

	bail: if (f) {
		fclose(f);
	}
	json_free_value(&root);
	return err;
}
int MapNewScanJSON(json_t *root, char **title, int *numMissions) {
	int err = 0;
	int version;
	LoadInt(&version, root, "Version");
	if (version > MAP_VERSION || version <= 0) {
		err = -1;
		goto bail;
	}
	*title = GetString(root, "Title");
	*numMissions = 0;
	if (version < 3) {
		for (json_t *missionNode =
				json_find_first_label(root, "Missions")->child->child;
				missionNode; missionNode = missionNode->next) {
			(*numMissions)++;
		}
	} else {
		LoadInt(numMissions, root, "Missions");
	}

	bail: return err;
}

int MapNewLoad(const char *filename, CampaignSetting *c) {
	int err = 0;

	if (IsCampaignOldFile(filename)) {
		CampaignSettingOld cOld;
		memset(&cOld, 0, sizeof cOld);
		err = LoadCampaignOld(filename, &cOld);
		if (!err) {
			ConvertCampaignSetting(c, &cOld);
		}
		CFREE(cOld.missions);
		CFREE(cOld.characters);
		return err;
	}

	if (strcmp(StrGetFileExt(filename), "cdogscpn") == 0
			|| strcmp(StrGetFileExt(filename), "CDOGSCPN") == 0) {
		return MapNewLoadArchive(filename, c);
	}

	// try to load the new map format
	json_t *root = NULL;
	int version;
	FILE *f = fopen(filename, "r");
	if (f == NULL) {
		err = -1;
		goto bail;
	}
	if (json_stream_parse(f, &root) != JSON_OK) {
		printf("Error parsing campaign '%s'\n", filename);
		err = -1;
		goto bail;
	}
	LoadInt(&version, root, "Version");
	if (version > 2 || version <= 0) {
		assert(0 && "not implemented or unknown campaign");
		err = -1;
		goto bail;
	}
	MapNewLoadCampaignJSON(root, c);
	LoadMissions(&c->Missions, json_find_first_label(root, "Missions")->child,
			version);
	CharacterLoadJSON(&c->characters, root, version);

	bail: json_free_value(&root);
	if (f != NULL) {
		fclose(f);
	}
	return err;
}

void MapNewLoadCampaignJSON(json_t *root, CampaignSetting *c) {
	CFREE(c->Title);
	c->Title = GetString(root, "Title");
	CFREE(c->Author);
	c->Author = GetString(root, "Author");
	CFREE(c->Description);
	c->Description = GetString(root, "Description");
}

static void LoadMissionObjectives(CArray *objectives, json_t *objectivesNode,
		const int version);
static void LoadWeapons(CArray *weapons, json_t *weaponsNode);
static void LoadRooms(RoomParams *r, json_t *roomsNode);
static void LoadClassicDoors(Mission *m, json_t *node, char *name);
static void LoadClassicPillars(Mission *m, json_t *node, char *name);
void LoadMissions(CArray *missions, json_t *missionsNode, int version) {
	json_t *child;
	for (child = missionsNode->child; child; child = child->next) {
		Mission m;
		MissionInit(&m);
		m.Title = GetString(child, "Title");
		m.Description = GetString(child, "Description");
		JSON_UTILS_LOAD_ENUM(m.Type, child, "Type", StrMapType);
		LoadInt(&m.Size.x, child, "Width");
		LoadInt(&m.Size.y, child, "Height");
		if (version <= 9) {
			int style;
			LoadInt(&style, child, "ExitStyle");
			strcpy(m.ExitStyle, IntExitStyle(style));
		} else {
			char *tmp = GetString(child, "ExitStyle");
			strcpy(m.ExitStyle, tmp);
			CFREE(tmp);
		}
		if (version <= 8) {
			int keyStyle;
			LoadInt(&keyStyle, child, "KeyStyle");
			strcpy(m.KeyStyle, IntKeyStyle(keyStyle));
		} else {
			char *tmp = GetString(child, "KeyStyle");
			strcpy(m.KeyStyle, tmp);
			CFREE(tmp);
		}
		LoadMissionObjectives(&m.Objectives,
				json_find_first_label(child, "Objectives")->child, version);
		LoadIntArray(&m.Enemies, child, "Enemies");
		LoadIntArray(&m.SpecialChars, child, "SpecialChars");
		if (version <= 3) {
			CArray items;
			CArrayInit(&items, sizeof(int));
			LoadIntArray(&items, child, "Items");
			CArray densities;
			CArrayInit(&densities, sizeof(int));
			LoadIntArray(&densities, child, "ItemDensities");
			for (int i = 0; i < (int) items.size; i++) {
				MapObjectDensity mod;
				mod.M = IntMapObject(*(int*) CArrayGet(&items, i));
				mod.Density = *(int*) CArrayGet(&densities, i);
				CArrayPushBack(&m.MapObjectDensities, &mod);
			}
		} else {
			json_t *modsNode = json_find_first_label(child,
					"MapObjectDensities");
			if (modsNode && modsNode->child) {
				modsNode = modsNode->child;
				for (json_t *modNode = modsNode->child; modNode; modNode =
						modNode->next) {
					MapObjectDensity mod;
					mod.M =
							StrMapObject(
									json_find_first_label(modNode, "MapObject")->child->text);
					LoadInt(&mod.Density, modNode, "Density");
					CArrayPushBack(&m.MapObjectDensities, &mod);
				}
			}
		}
		LoadInt(&m.EnemyDensity, child, "EnemyDensity");
		LoadWeapons(&m.Weapons, json_find_first_label(child, "Weapons")->child);
		strcpy(m.Song, json_find_first_label(child, "Song")->child->text);
		switch (m.Type) {
		case MAPTYPE_CLASSIC:
			LoadMissionTileClasses(&m.u.Classic.TileClasses, child, version);
			LoadInt(&m.u.Classic.Walls, child, "Walls");
			LoadInt(&m.u.Classic.WallLength, child, "WallLength");
			LoadInt(&m.u.Classic.CorridorWidth, child, "CorridorWidth");
			LoadRooms(&m.u.Classic.Rooms,
					json_find_first_label(child, "Rooms")->child);
			LoadInt(&m.u.Classic.Squares, child, "Squares");
			LoadClassicDoors(&m, child, "Doors");
			LoadClassicPillars(&m, child, "Pillars");
			break;
		case MAPTYPE_STATIC:
			if (!MissionStaticTryLoadJSON(&m.u.Static, child, version)) {
				continue;
			}
			break;
		case MAPTYPE_CAVE: {
			LoadMissionTileClasses(&m.u.Cave.TileClasses, child, version);
			LoadInt(&m.u.Cave.FillPercent, child, "FillPercent");
			LoadInt(&m.u.Cave.Repeat, child, "Repeat");
			LoadInt(&m.u.Cave.R1, child, "R1");
			LoadInt(&m.u.Cave.R2, child, "R2");
			json_t *roomsNode = json_find_first_label(child, "Rooms");
			if (roomsNode != NULL && roomsNode->child != NULL) {
				LoadRooms(&m.u.Cave.Rooms, roomsNode->child);
			}
			LoadInt(&m.u.Cave.Squares, child, "Squares");
			if (version < 14) {
				m.u.Cave.DoorsEnabled = true;
			} else {
				LoadBool(&m.u.Cave.DoorsEnabled, child, "DoorsEnabled");
			}
		}
			break;
		default:
			assert(0 && "unknown map type");
			continue;
		}
		CArrayPushBack(missions, &m);
	}
}

void MissionLoadTileClass(TileClass *tc, json_t *node) {
	memset(tc, 0, sizeof *tc);
	LoadStr(&tc->Name, node, "Name");
	JSON_UTILS_LOAD_ENUM(tc->Type, node, "Type", StrTileClassType);
	LoadStr(&tc->Style, node, "Style");
	LoadColor(&tc->Mask, node, "Mask");
	LoadColor(&tc->MaskAlt, node, "MaskAlt");
	LoadBool(&tc->canWalk, node, "CanWalk");
	LoadBool(&tc->isOpaque, node, "IsOpaque");
	LoadBool(&tc->shootable, node, "Shootable");
	LoadBool(&tc->IsRoom, node, "IsRoom");
}

void LoadMissionTileClasses(MissionTileClasses *mtc, json_t *node,
		const int version) {
	if (version <= 14) {
		char wallStyle[CDOGS_FILENAME_MAX];
		char floorStyle[CDOGS_FILENAME_MAX];
		char roomStyle[CDOGS_FILENAME_MAX];
		if (version <= 10) {
			int style;
			LoadInt(&style, node, "WallStyle");
			strcpy(wallStyle, IntWallStyle(style));
			LoadInt(&style, node, "FloorStyle");
			strcpy(floorStyle, IntFloorStyle(style));
			LoadInt(&style, node, "RoomStyle");
			strcpy(roomStyle, IntRoomStyle(style));
		} else {
			char *tmp = GetString(node, "WallStyle");
			strcpy(wallStyle, tmp);
			CFREE(tmp);
			tmp = GetString(node, "FloorStyle");
			strcpy(floorStyle, tmp);
			CFREE(tmp);
			tmp = GetString(node, "RoomStyle");
			strcpy(roomStyle, tmp);
			CFREE(tmp);
		}
		char doorStyle[CDOGS_FILENAME_MAX];
		if (version <= 5) {
			int doorStyleInt;
			LoadInt(&doorStyleInt, node, "DoorStyle");
			strcpy(doorStyle, IntDoorStyle(doorStyleInt));
		} else {
			char *tmp = GetString(node, "DoorStyle");
			strcpy(doorStyle, tmp);
			CFREE(tmp);
		}
		color_t wallMask;
		color_t floorMask;
		color_t roomMask;
		color_t altMask;
		if (version <= 4) {
			// Load colour indices
			int wc, fc, rc, ac;
			LoadInt(&wc, node, "WallColor");
			LoadInt(&fc, node, "FloorColor");
			LoadInt(&rc, node, "RoomColor");
			LoadInt(&ac, node, "AltColor");
			wallMask = RangeToColor(wc);
			floorMask = RangeToColor(fc);
			roomMask = RangeToColor(rc);
			altMask = RangeToColor(ac);
		} else {
			LoadColor(&wallMask, node, "WallMask");
			LoadColor(&floorMask, node, "FloorMask");
			LoadColor(&roomMask, node, "RoomMask");
			LoadColor(&altMask, node, "AltMask");
		}
		TileClassInit(&mtc->Wall, &gPicManager, &gTileWall, wallStyle,
				TileClassBaseStyleType(TILE_CLASS_WALL), wallMask, altMask);
		TileClassInit(&mtc->Floor, &gPicManager, &gTileFloor, floorStyle,
				TileClassBaseStyleType(TILE_CLASS_FLOOR), floorMask, altMask);
		TileClassInit(&mtc->Room, &gPicManager, &gTileRoom, roomStyle,
				TileClassBaseStyleType(TILE_CLASS_FLOOR), roomMask, altMask);
		TileClassInit(&mtc->Door, &gPicManager, &gTileDoor, doorStyle,
				TileClassBaseStyleType(TILE_CLASS_DOOR), colorWhite,
				colorWhite);
	} else {
		MissionLoadTileClass(&mtc->Wall,
				json_find_first_label(node, "Wall")->child);
		MissionLoadTileClass(&mtc->Floor,
				json_find_first_label(node, "Floor")->child);
		MissionLoadTileClass(&mtc->Room,
				json_find_first_label(node, "Room")->child);
		MissionLoadTileClass(&mtc->Door,
				json_find_first_label(node, "Door")->child);
	}
}
static void LoadMissionObjectives(CArray *objectives, json_t *objectivesNode,
		const int version) {
	json_t *child;
	for (child = objectivesNode->child; child; child = child->next) {
		Objective o;
		ObjectiveLoadJSON(&o, child, version);
		CArrayPushBack(objectives, &o);
	}
}
static void AddWeapon(CArray *weapons, const CArray *guns);
static void LoadWeapons(CArray *weapons, json_t *weaponsNode) {
	if (!weaponsNode->child) {
		// enable all weapons
		AddWeapon(weapons, &gWeaponClasses.Guns);
		AddWeapon(weapons, &gWeaponClasses.CustomGuns);
	} else {
		for (json_t *child = weaponsNode->child; child; child = child->next) {
			const WeaponClass *wc = StrWeaponClass(child->text);
			if (wc == NULL) {
				continue;
			}
			CArrayPushBack(weapons, &wc);
		}
	}
}
static void AddWeapon(CArray *weapons, const CArray *guns) {
	for (int i = 0; i < (int) guns->size; i++) {
		const WeaponClass *wc = static_cast<const WeaponClass*>(CArrayGet(guns,
				i));
		if (wc->IsRealGun) {
			CArrayPushBack(weapons, &wc);
		}
	}
}
static void LoadRooms(RoomParams *r, json_t *roomsNode) {
	LoadInt(&r->Count, roomsNode, "Count");
	LoadInt(&r->Min, roomsNode, "Min");
	LoadInt(&r->Max, roomsNode, "Max");
	LoadBool(&r->Edge, roomsNode, "Edge");
	LoadBool(&r->Overlap, roomsNode, "Overlap");
	LoadInt(&r->Walls, roomsNode, "Walls");
	LoadInt(&r->WallLength, roomsNode, "WallLength");
	LoadInt(&r->WallPad, roomsNode, "WallPad");
}
static void LoadClassicPillars(Mission *m, json_t *node, char *name) {
	json_t *child = json_find_first_label(node, name);
	if (!child || !child->child) {
		return;
	}
	child = child->child;
	LoadInt(&m->u.Classic.Pillars.Count, child, "Count");
	LoadInt(&m->u.Classic.Pillars.Min, child, "Min");
	LoadInt(&m->u.Classic.Pillars.Max, child, "Max");
}
static void LoadClassicDoors(Mission *m, json_t *node, char *name) {
	json_t *child = json_find_first_label(node, name);
	if (!child || !child->child) {
		return;
	}
	child = child->child;
	LoadBool(&m->u.Classic.Doors.Enabled, child, "Enabled");
	LoadInt(&m->u.Classic.Doors.Min, child, "Min");
	LoadInt(&m->u.Classic.Doors.Max, child, "Max");
}
