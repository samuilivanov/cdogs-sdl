/*
    C-Dogs SDL
    A port of the legendary (and fun) action/arcade cdogs.
    Copyright (C) 1995 Ronny Wester
    Copyright (C) 2003 Jeremy Chin
    Copyright (C) 2003-2007 Lucas Martin-King

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

    Copyright (c) 2013-2014, Cong Xu
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
#ifndef __GAMEDATA
#define __GAMEDATA

#include "campaigns.h"
#include "character.h"
#include "input.h"
#include "map_new.h"
#include "map_object.h"
#include "pics.h"
#include "tile.h"
#include "weapon.h"
#include "sys_config.h"

#define MAX_WEAPONS 3


struct PlayerData
{
	char name[20];
	CharLooks looks;
	int weaponCount;
	gun_e weapons[MAX_WEAPONS];

	int score;
	int totalScore;
	int survived;
	int hp;
	int missions;
	int lastMission;
	int allTime, today;
	int kills;
	int friendlies;

	input_device_e inputDevice;
	int deviceIndex;
	int playerIndex;
};

extern struct PlayerData gPlayerDatas[MAX_PLAYERS];

struct GameOptions {
	int numPlayers;
	int badGuys;
};

struct DoorPic {
	int horzPic;
	int vertPic;
};

// Score penalty for killing a penalty character
#define PENALTY_MULTIPLIER (-3)

// Score for destroying an objective object
#define OBJECT_SCORE 50

// Score for picking up an objective
#define PICKUP_SCORE 10


struct Objective
{
	color_t color;
	int placed;
	int done;
	MapObject *blowupObject;
	int pickupItem;
};

extern struct GameOptions gOptions;
extern struct MissionOptions gMission;

struct SongDef {
	char path[255];
	struct SongDef *next;
};

extern struct SongDef *gGameSongs;
extern struct SongDef *gMenuSongs;

void AddSong(struct SongDef **songList, const char *path);
void ShiftSongs(struct SongDef **songList);
void FreeSongs(struct SongDef **songList);
void LoadSongs(void);

void PlayerDataInitialize(void);

void CampaignLoad(CampaignOptions *co, campaign_entry_t *entry);

void MissionOptionsInit(struct MissionOptions *mo);
void MissionOptionsTerminate(struct MissionOptions *mo);

int IsIntroNeeded(campaign_mode_e mode);
int IsScoreNeeded(campaign_mode_e mode);
int HasObjectives(campaign_mode_e mode);
int IsAutoMapEnabled(campaign_mode_e mode);
int IsPasswordAllowed(campaign_mode_e mode);
int IsMissionBriefingNeeded(campaign_mode_e mode);
int AreKeysAllowed(campaign_mode_e mode);
int AreHealthPickupsAllowed(campaign_mode_e mode);

int GameIsMouseUsed(struct PlayerData playerDatas[MAX_PLAYERS]);

#endif