/*
    C-Dogs SDL
    A port of the legendary (and fun) action/arcade cdogs.
    Copyright (c) 2013-2018 Cong Xu
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
#include "prep_equip.h"

#include <cdogs/player.h>
#include <cdogs/net_client.h>
#include <cdogs/net_server.h>

#include "prep.h"
#include "weapon_menu.h"


static void RemoveUnavailableWeapons(PlayerData *data, const CArray *weapons);
static void AddDefaultGuns(
	PlayerData *p, const int idx, const CArray *guns, const bool isGrenade);
typedef struct
{
	WeaponMenu menus[MAX_LOCAL_PLAYERS];
	EventWaitResult waitResult;
} PlayerEquipData;
static void PlayerEquipTerminate(GameLoopData *data);
static void PlayerEquipOnExit(GameLoopData *data);
static GameLoopResult PlayerEquipUpdate(GameLoopData *data, LoopRunner *l);
static void PlayerEquipDraw(GameLoopData *data);
GameLoopData *PlayerEquip(void)
{
	PlayerEquipData *data;
	CCALLOC(data, sizeof *data);
	data->waitResult = EVENT_WAIT_CONTINUE;
	for (int i = 0, idx = 0; i < (int)gPlayerDatas.size; i++, idx++)
	{
		PlayerData *p = static_cast<PlayerData*>(CArrayGet(&gPlayerDatas, i));
		if (!p->IsLocal)
		{
			idx--;
			continue;
		}

		// Remove unavailable weapons from players inventories
		RemoveUnavailableWeapons(p, &gMission.Weapons);

		// Add default guns if the player has no weapons
		if (PlayerGetNumWeapons(p) == 0)
		{
			AddDefaultGuns(p, idx, &gMission.Weapons, false);
			AddDefaultGuns(p, idx, &gMission.Weapons, true);
		}

		WeaponMenuCreate(
			&data->menus[idx], GetNumPlayers(PLAYER_ANY, false, true),
			idx, p->UID,
			&gEventHandlers, &gGraphicsDevice);
	}

	return GameLoopDataNew(
		data, PlayerEquipTerminate, NULL, PlayerEquipOnExit,
		NULL, PlayerEquipUpdate, PlayerEquipDraw);
}
static bool HasWeapon(const CArray *weapons, const WeaponClass *wc);
static void RemoveUnavailableWeapons(PlayerData *data, const CArray *weapons)
{
	for (int i = 0; i < MAX_WEAPONS; i++)
	{
		if (!HasWeapon(weapons, data->guns[i]))
		{
			data->guns[i] = NULL;
		}
	}
}
static void AddDefaultGuns(
	PlayerData *p, const int idx, const CArray *guns, const bool isGrenade)
{
	const char *defaultGuns[MAX_LOCAL_PLAYERS][MAX_GUNS] =
	{
		{ "Shotgun", "Machine gun" },
		{ "Powergun", "Flamer" },
		{ "Sniper rifle", "Knife" },
		{ "Machine gun", "Flamer" },
	};
	const char *defaultGrenades[MAX_LOCAL_PLAYERS][MAX_GRENADES] =
	{
		{ "Shrapnel bombs" },
		{ "Grenades" },
		{ "Molotovs" },
		{ "Dynamite" },
	};

	const int first = isGrenade ? MAX_GUNS : 0;
	const int max = isGrenade ? MAX_WEAPONS : MAX_GUNS;
	const int size = isGrenade ? MAX_GRENADES : MAX_GUNS;
	// Attempt to give the player default guns; if the guns are missing, find
	// guns available in the mission
	int defaultIdx = 0;
	// Look for the default guns
	int i;
	for (i = first; i < max && defaultIdx < size; i++)
	{
		const WeaponClass *defaultWC = NULL;
		CA_FOREACH(const WeaponClass *, wc, *guns)
			const char *gunName = isGrenade ?
				defaultGrenades[idx][defaultIdx] :
				defaultGuns[idx][defaultIdx];
			if (strcmp((*wc)->name, gunName) == 0)
			{
				defaultWC = *wc;
			}
		CA_FOREACH_END()
			if (defaultWC != NULL)
			{
				p->guns[i] = defaultWC;
			}
			else
			{
				i--;
			}
		defaultIdx++;
	}
	// Look for guns available in the mission
	int j = 0;
	for (; i < max; i++)
	{
		for (; j < (int)guns->size; j++)
		{
			const int missionGunIdx = ((idx * size) + j) % guns->size;
			const WeaponClass **wc = static_cast<const WeaponClass **>(CArrayGet(guns, missionGunIdx));
			if ((*wc)->IsGrenade != isGrenade)
			{
				continue;
			}
			p->guns[i] = *wc;
			j++;
			break;
		}
	}
}
static bool HasWeapon(const CArray *weapons, const WeaponClass *wc)
{
	for (int i = 0; i < (int)weapons->size; i++)
	{
		const WeaponClass **wc2 = static_cast<const WeaponClass **>(CArrayGet(weapons, i));
		if (wc == *wc2)
		{
			return true;
		}
	}
	return false;
}
static void PlayerEquipTerminate(GameLoopData *data)
{
	PlayerEquipData *pData = static_cast<PlayerEquipData*>(data->Data);

	for (int i = 0; i < GetNumPlayers(PLAYER_ANY, false, true); i++)
	{
		WeaponMenuTerminate(&pData->menus[i]);
	}
	CFREE(pData);
}
static void PlayerEquipOnExit(GameLoopData *data)
{
	PlayerEquipData *pData = static_cast<PlayerEquipData*>(data->Data);

	if (pData->waitResult == EVENT_WAIT_OK)
	{
		for (int i = 0, idx = 0; i < (int)gPlayerDatas.size; i++, idx++)
		{
			const PlayerData *p = static_cast<const PlayerData*>(CArrayGet(&gPlayerDatas, i));
			if (!p->IsLocal)
			{
				idx--;
				continue;
			}
			NPlayerData pd = NMakePlayerData(p);
			// Update player definitions
			if (gCampaign.IsClient)
			{
				NetClientSendMsg(&gNetClient, GAME_EVENT_PLAYER_DATA, &pd);
			}
			else
			{
				NetServerSendMsg(
					&gNetServer, NET_SERVER_BCAST, GAME_EVENT_PLAYER_DATA, &pd);
			}
		}
	}
	else
	{
		CampaignUnload(&gCampaign);
	}
}
static GameLoopResult PlayerEquipUpdate(GameLoopData *data, LoopRunner *l)
{
	PlayerEquipData *pData = static_cast<PlayerEquipData*>(data->Data);
	bool isDone;
	// If no human players, don't show equip screen
	if (GetNumPlayers(PLAYER_ANY, false, true) == 0)
	{
		pData->waitResult = EVENT_WAIT_OK;
		goto bail;
	}

	// Check if anyone pressed escape
	int cmds[MAX_LOCAL_PLAYERS];
	memset(cmds, 0, sizeof cmds);
	GetPlayerCmds(&gEventHandlers, &cmds);
	if (EventIsEscape(&gEventHandlers, cmds, GetMenuCmd(&gEventHandlers)))
	{
		pData->waitResult = EVENT_WAIT_CANCEL;
		goto bail;
	}

	// Update menus
	isDone = true;
	for (int i = 0; i < GetNumPlayers(PLAYER_ANY, false, true); i++)
	{
		WeaponMenuUpdate(&pData->menus[i], cmds[i]);
		isDone = isDone && WeaponMenuIsDone(&pData->menus[i]);
	}
	if (isDone)
	{
		pData->waitResult = EVENT_WAIT_OK;
		goto bail;
	}

	return UPDATE_RESULT_DRAW;

bail:
	if (pData->waitResult == EVENT_WAIT_OK)
	{
		LoopRunnerChange(l, ScreenWaitForGameStart());
	}
	else
	{
		LoopRunnerPop(l);
	}
	return UPDATE_RESULT_OK;
}
static void PlayerEquipDraw(GameLoopData *data)
{
	const PlayerEquipData *pData = static_cast<const PlayerEquipData*>(data->Data);
	BlitClearBuf(&gGraphicsDevice);
	for (int i = 0; i < GetNumPlayers(PLAYER_ANY, false, true); i++)
	{
		WeaponMenuDraw(&pData->menus[i]);
	}
	BlitUpdateFromBuf(&gGraphicsDevice, gGraphicsDevice.screen);
}
