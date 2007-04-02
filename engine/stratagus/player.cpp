//     ____                _       __               
//    / __ )____  _____   | |     / /___ ___________
//   / __  / __ \/ ___/   | | /| / / __ `/ ___/ ___/
//  / /_/ / /_/ (__  )    | |/ |/ / /_/ / /  (__  ) 
// /_____/\____/____/     |__/|__/\__,_/_/  /____/  
//                                              
//       A futuristic real-time strategy game.
//          This file is part of Bos Wars.
//
/**@name player.cpp - The players. */
//
//      (c) Copyright 1998-2007 by Lutz Sammer, Jimmy Salmon, Nehal Mistry
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; only version 2 of the License.
//
//      This program is distributed in the hope that it will be useful,
//      but WITHOUT ANY WARRANTY; without even the implied warranty of
//      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//      GNU General Public License for more details.
//
//      You should have received a copy of the GNU General Public License
//      along with this program; if not, write to the Free Software
//      Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
//      02111-1307, USA.
//
//      $Id$

//@{

/*----------------------------------------------------------------------------
-- Includes
----------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <algorithm>

#include "stratagus.h"
#include "video.h"
#include "sound.h"
#include "unitsound.h"
#include "unittype.h"
#include "player.h"
#include "unit.h"
#include "map.h"
#include "ai.h"
#include "network.h"
#include "netconnect.h"
#include "interface.h"
#include "iolib.h"
#include "ui.h"

/*----------------------------------------------------------------------------
--  Variables
----------------------------------------------------------------------------*/

int NumPlayers;                  /// How many player slots used
CPlayer Players[PlayerMax];       /// All players in play
CPlayer *ThisPlayer;              /// Player on this computer

int NoRescueCheck;               /// Disable rescue check

/**
**  Colors used for minimap.
*/
SDL_Color *PlayerColorsRGB[PlayerMax];
Uint32 *PlayerColors[PlayerMax];

std::string PlayerColorNames[PlayerMax];

/**
**  Which indexes to replace with player color
*/
int PlayerColorIndexStart;
int PlayerColorIndexCount;

/*----------------------------------------------------------------------------
--  Functions
----------------------------------------------------------------------------*/

/**
**  Init players.
*/
void InitPlayers(void)
{
	for (int p = 0; p < PlayerMax; ++p) {
		Players[p].Index = p;
		if (!Players[p].Type) {
			Players[p].Type = PlayerNobody;
		}
		for (int x = 0; x < PlayerColorIndexCount; ++x) {
			PlayerColors[p][x] = Video.MapRGB(TheScreen->format,
				PlayerColorsRGB[p][x].r,
				PlayerColorsRGB[p][x].g, PlayerColorsRGB[p][x].b);
		}
	}
}

/**
**  Clean up players.
*/
void CleanPlayers(void)
{
	ThisPlayer = NULL;
	for (int i = 0; i < PlayerMax; i++) {
		Players[i].Clear();
	}
	NumPlayers = 0;

	NoRescueCheck = 0;
}

/**
**  Add to UnitsConsumingResources
*/
void CPlayer::AddToUnitsConsumingResources(CUnit *unit, int costs[MaxCosts])
{
	Assert(UnitsConsumingResourcesActual[unit] == NULL);
	Assert(UnitsConsumingResourcesRequested[unit] == NULL);

	int *c;
	
	c = new int[MaxCosts];
	memset(c, 0, MaxCosts * sizeof(int));
	this->UnitsConsumingResourcesActual[unit] = c;
	// no need to change ActualUtilizationRate here

	c = new int[MaxCosts];
	memcpy(c, costs, MaxCosts * sizeof(int));
	this->UnitsConsumingResourcesRequested[unit] = c;
	for (int i = 0; i < MaxCosts; ++i) {
		this->RequestedUtilizationRate[i] += costs[i];
	}
}

/**
**  Remove from UnitsConsumingResources
*/
void CPlayer::RemoveFromUnitsConsumingResources(CUnit *unit)
{
	int *cactual = UnitsConsumingResourcesActual[unit];
	int *crequested = UnitsConsumingResourcesRequested[unit];

	for (int i = 0; i < MaxCosts; ++i) {
		this->ActualUtilizationRate[i] -= cactual[i];
		this->RequestedUtilizationRate[i] -= crequested[i];
	}

	delete[] cactual;
	this->UnitsConsumingResourcesActual.erase(unit);
	delete[] crequested;
	this->UnitsConsumingResourcesRequested.erase(unit);
}

/**
**  Update costs for unit in UnitsConsumingResources
*/
void CPlayer::UpdateUnitsConsumingResources(CUnit *unit, int costs[MaxCosts])
{
	int *c = UnitsConsumingResourcesActual[unit];

	for (int i = 0; i < MaxCosts; ++i) {
		this->ActualUtilizationRate[i] -= c[i];
		c[i] = costs[i];
		this->ActualUtilizationRate[i] += c[i];
	}
}

/** 
**  Go through the list of units owned by the player and rebuild
**  the UnitsConsumingResources list.
*/
void CPlayer::RebuildUnitsConsumingResourcesList()
{
	for (int i = 0; i < TotalNumUnits; ++i) {
		int costs[MaxCosts];
		CUnit *u = Units[i];

		if (u->Orders[0]->Action == UnitActionTrain && u->SubAction > 0) {
			for (int i = 0; i < MaxCosts; ++i) {
				costs[i] = std::min<int>(u->Type->MaxUtilizationRate[i],
					u->Orders[0]->Type->Stats[u->Player->Index].Costs[i]);
			}
			AddToUnitsConsumingResources(u, costs);
		} else if (u->Orders[0]->Action == UnitActionBuilt) {
			if (!u->Type->BuilderOutside) {
				for (int i = 0; i < MaxCosts; ++i) {
					costs[i] = std::min<int>(u->Type->MaxUtilizationRate[i],
						u->Orders[0]->Goal->Type->Stats[u->Orders[0]->Goal->Player->Index].Costs[i]);
				}
				u->Orders[0]->Goal->Player->AddToUnitsConsumingResources(u->Orders[0]->Goal, costs);
			} else {
				for (int i = 0; i < MaxCosts; ++i) {
					costs[i] = std::min<int>(u->Type->MaxUtilizationRate[i],
						u->Orders[0]->Goal->Type->Stats[u->Orders[0]->Goal->Player->Index].Costs[i]);
				}
				AddToUnitsConsumingResources(u, costs);
			}
		}
	}
}

/**
**  Clear all resource state variables.
*/
void CPlayer::ClearResourceVariables()
{
	std::map<CUnit *, int *>::iterator i;
	for (i = UnitsConsumingResourcesActual.begin();
			i != UnitsConsumingResourcesActual.end(); ++i) {
		delete[] (*i).second;
	}
	for (i = UnitsConsumingResourcesRequested.begin();
			i != UnitsConsumingResourcesRequested.end(); ++i) {
		delete[] (*i).second;
	}

	UnitsConsumingResourcesActual.clear();
	UnitsConsumingResourcesRequested.clear();
	memset(ProductionRate, 0, sizeof(ProductionRate));
	memset(ActualUtilizationRate, 0, sizeof(ActualUtilizationRate));
	memset(RequestedUtilizationRate, 0, sizeof(RequestedUtilizationRate));
	memset(StoredResources, 0, sizeof(StoredResources));
	memset(StorageCapacity, 0, sizeof(StorageCapacity));

}

// p(type) = total production(type) + min(storage(type), storagerate(type))
static int AvailableResourcesRate(int type, CPlayer *p)
{
	// FIXME: change 50 to rate we can get resources out of storage
	int RateFromStorage = 50;
	return p->ProductionRate[type] + std::min<int>(p->StoredResources[type], RateFromStorage);
}

// f(type) = min(p(type)/ total needs(type), 1.0)
static int SpecificEfficiency(int type, CPlayer *p)
{
	return std::min<int>((100 * AvailableResourcesRate(type, p) + 50) / p->RequestedUtilizationRate[type], 100);
}

// base efficiency = min(f(energy), f(magma))
static int BaseEfficiency(CPlayer *p)
{
	// FIXME: hardcoded 1 and 2 for resources
	return std::min<int>(SpecificEfficiency(1, p), SpecificEfficiency(2, p));
}

static int MaxRate(CUnit *unit, int res)
{
	if (unit->Orders[0]->Action == UnitActionTrain ||
			unit->Orders[0]->Action == UnitActionBuild ||
			unit->Orders[0]->Action == UnitActionRepair) {
		return unit->Type->MaxUtilizationRate[res];
	} else if (unit->Orders[0]->Action == UnitActionBuilt) {
		if (!unit->Type->BuilderOutside) {
			return unit->Data.Built.Worker->Type->MaxUtilizationRate[res];
		} else {
			Assert(0);
			return 0;
		}
	}
	Assert(0);
	return 0;
}

static void CalculateCosts(CUnit *unit, int costs[MaxCosts])
{
	int i;
	int usedtypes = 0;
	int *c = unit->Player->UnitsConsumingResourcesRequested[unit];

	for (i = 1; i < MaxCosts; ++i) {
		if (c[i] != 0) {
			++usedtypes;
		}
	}
	Assert(usedtypes > 0);

	// if unit requires both types:
	if (usedtypes > 1) {
		// unit effective rate(type) = unit max rate(type) * base efficiency
		int be = BaseEfficiency(unit->Player);
		costs[0] = 0;
		for (i = 1; i < MaxCosts; ++i) {
			costs[i] = MaxRate(unit, i) * be / 100;
		}
	} else {
		// unit effective rate(type) = unit max rate(type) * unique efficiency(type)
#if 0
		int ue = UniqueEfficiency(unit->Player);
		costs[0] = 0;
		for (i = 1; i < MaxCosts; ++i) {
			costs[i] = MaxRate(unit, i) * ue / 100;
		}
#endif
	}
}

/**
**  Save state of players to file.
**
**  @param file  Output file.
**
**  @note FIXME: Not completely saved.
*/
void SavePlayers(CFile *file)
{
	int j;
	Uint8 r, g, b;
	CPlayer *p;

	file->printf("\n--------------------------------------------\n");
	file->printf("--- MODULE: players $Id$\n\n");

	//
	//  Dump all players
	//
	for (int i = 0; i < NumPlayers; ++i) {
		p = &Players[i];
		file->printf("Player(%d,\n", i);
		file->printf("  \"name\", \"%s\",\n", p->Name.c_str());
		file->printf("  \"type\", ");
		switch (p->Type) {
			case PlayerNeutral:       file->printf("\"neutral\",");         break;
			case PlayerNobody:        file->printf("\"nobody\",");          break;
			case PlayerComputer:      file->printf("\"computer\",");        break;
			case PlayerPerson:        file->printf("\"person\",");          break;
			case PlayerRescuePassive: file->printf("\"rescue-passive\",");break;
			case PlayerRescueActive:  file->printf("\"rescue-active\","); break;
			default:                  file->printf("%d,",p->Type);break;
		}
		file->printf(" \"ai-name\", \"%s\",\n", p->AiName.c_str());
		file->printf("  \"team\", %d,", p->Team);

		file->printf(" \"enemy\", \"");
		for (j = 0; j < PlayerMax; ++j) {
			file->printf("%c",(p->Enemy & (1 << j)) ? 'X' : '_');
		}
		file->printf("\", \"allied\", \"");
		for (j = 0; j < PlayerMax; ++j) {
			file->printf("%c", (p->Allied & (1 << j)) ? 'X' : '_');
		}
		file->printf("\", \"shared-vision\", \"");
		for (j = 0; j < PlayerMax; ++j) {
			file->printf("%c", (p->SharedVision & (1 << j)) ? 'X' : '_');
		}
		file->printf("\",\n  \"start\", {%d, %d},\n", p->StartX,
			p->StartY);

		// Resources
		file->printf("  \"resources\", {");
		for (j = 0; j < MaxCosts; ++j) {
			if (j) {
				file->printf(" ");
			}
			file->printf("\"%s\", %d,", DefaultResourceNames[j].c_str(),
				p->Resources[j]);
		}
		// Incomes
		file->printf("},\n  \"incomes\", {");
		for (j = 0; j < MaxCosts; ++j) {
			if (j) {
				file->printf(" ");
			}
			file->printf("\"%s\", %d,", DefaultResourceNames[j].c_str(),
				p->Incomes[j]);
		}

		// ProductionRate done by load units.
		// ActualUtilizationRate
		file->printf("},\n  \"actual-utilization-rate\", {");
		for (j = 0; j < MaxCosts; ++j) {
			if (j) {
				file->printf(" ");
			}
			file->printf("\"%s\", %d,", DefaultResourceNames[j].c_str(),
				p->ActualUtilizationRate[j]);
		}
		// RequestedUtilizationRate
		file->printf("},\n  \"requested-utilization-rate\", {");
		for (j = 0; j < MaxCosts; ++j) {
			if (j) {
				file->printf(" ");
			}
			file->printf("\"%s\", %d,", DefaultResourceNames[j].c_str(),
				p->RequestedUtilizationRate[j]);
		}
		// StoredResources
		file->printf("},\n  \"stored-resources\", {");
		for (j = 0; j < MaxCosts; ++j) {
			if (j) {
				file->printf(" ");
			}
			file->printf("\"%s\", %d,", DefaultResourceNames[j].c_str(),
				p->StoredResources[j]);
		}
		// StorageCapacity done by load units.

		// UnitTypesCount done by load units.

		file->printf("},\n  \"%s\",\n", p->AiEnabled ?
			"ai-enabled" : "ai-disabled");

		// Ai done by load ais.
		// Units done by load units.
		// TotalNumUnits done by load units.
		// NumBuildings done by load units.

		file->printf(" \"unit-limit\", %d,", p->UnitLimit);
		file->printf(" \"building-limit\", %d,", p->BuildingLimit);
		file->printf(" \"total-unit-limit\", %d,", p->TotalUnitLimit);

		file->printf("\n  \"score\", %d,", p->Score);
		file->printf("\n  \"total-units\", %d,", p->TotalUnits);
		file->printf("\n  \"total-buildings\", %d,", p->TotalBuildings);
		file->printf("\n  \"total-resources\", {");
		for (j = 0; j < MaxCosts; ++j) {
			if (j) {
				file->printf(" ");
			}
			file->printf("%d,", p->TotalResources[j]);
		}
		file->printf("},");
		file->printf("\n  \"total-razings\", %d,", p->TotalRazings);
		file->printf("\n  \"total-kills\", %d,", p->TotalKills);

		SDL_GetRGB(p->Color, TheScreen->format, &r, &g, &b);
		file->printf("\n  \"color\", { %d, %d, %d }", r, g, b); // no comma after last parameter

		// UnitColors done by init code.
		// Allow saved by allow.

		file->printf(")\n\n");
	}

	DebugPrint("FIXME: must save unit-stats?\n");

	//
	//  Dump local variables
	//
	file->printf("SetThisPlayer(%d)\n\n", ThisPlayer->Index);
}

/**
**  Create a new player.
**
**  @param type  Player type (Computer,Human,...).
*/
void CreatePlayer(int type)
{
	int team;
	int i;
	CPlayer *player;

	if (NumPlayers == PlayerMax) { // already done for bigmaps!
		return;
	}
	player = &Players[NumPlayers];
	player->Index = NumPlayers;

	//  Allocate memory for the "list" of this player's units.
	//  FIXME: brutal way, as we won't need UnitMax for this player...
	//  FIXME: ARI: is this needed for 'PlayerNobody' ??
	//  FIXME: A: Johns: currently we need no init for the nobody player.
	memset(player->Units, 0, sizeof(player->Units));

	//
	//  Take first slot for person on this computer,
	//  fill other with computer players.
	//
	if (type == PlayerPerson && !NetPlayers) {
		if (!ThisPlayer) {
			ThisPlayer = player;
		} else {
			type = PlayerComputer;
		}
	}
	if (NetPlayers && NumPlayers == NetLocalPlayerNumber) {
		ThisPlayer = &Players[NetLocalPlayerNumber];
	}

	if (NumPlayers == PlayerMax) {
		static int already_warned;

		if (!already_warned) {
			DebugPrint("Too many players\n");
			already_warned = 1;
		}
		return;
	}

	//
	//  Make simple teams:
	//  All person players are enemies.
	//
	switch (type) {
		case PlayerNeutral:
		case PlayerNobody:
		default:
			team = 0;
			player->SetName("Neutral");
			break;
		case PlayerComputer:
			team = 1;
			player->SetName("Computer");
			break;
		case PlayerPerson:
			team = 2 + NumPlayers;
			player->SetName("Person");
			break;
		case PlayerRescuePassive:
		case PlayerRescueActive:
			// FIXME: correct for multiplayer games?
			player->SetName("Computer");
			team = 2 + NumPlayers;
			break;
	}
	DebugPrint("CreatePlayer name %s\n" _C_ player->Name.c_str());

	player->Type = type;
	player->Team = team;
	player->Enemy = 0;
	player->Allied = 0;
	player->AiName = "ai-passive";

	//
	//  Calculate enemy/allied mask.
	//
	for (i = 0; i < NumPlayers; ++i) {
		switch (type) {
			case PlayerNeutral:
			case PlayerNobody:
			default:
				break;
			case PlayerComputer:
				// Computer allied with computer and enemy of all persons.
				if (Players[i].Type == PlayerComputer) {
					player->Allied |= (1 << i);
					Players[i].Allied |= (1 << NumPlayers);
				} else if (Players[i].Type == PlayerPerson ||
						Players[i].Type == PlayerRescueActive) {
					player->Enemy |= (1 << i);
					Players[i].Enemy |= (1 << NumPlayers);
				}
				break;
			case PlayerPerson:
				// Humans are enemy of all?
				if (Players[i].Type == PlayerComputer ||
						Players[i].Type == PlayerPerson) {
					player->Enemy |= (1 << i);
					Players[i].Enemy |= (1 << NumPlayers);
				} else if (Players[i].Type == PlayerRescueActive ||
						Players[i].Type == PlayerRescuePassive) {
					player->Allied |= (1 << i);
					Players[i].Allied |= (1 << NumPlayers);
				}
				break;
			case PlayerRescuePassive:
				// Rescue passive are allied with persons
				if (Players[i].Type == PlayerPerson) {
					player->Allied |= (1 << i);
					Players[i].Allied |= (1 << NumPlayers);
				}
				break;
			case PlayerRescueActive:
				// Rescue active are allied with persons and enemies of computer
				if (Players[i].Type == PlayerComputer) {
					player->Enemy |= (1 << i);
					Players[i].Enemy |= (1 << NumPlayers);
				} else if (Players[i].Type == PlayerPerson) {
					player->Allied |= (1 << i);
					Players[i].Allied |= (1 << NumPlayers);
				}
				break;
		}
	}

	//
	//  Initial default incomes.
	//
	for (i = 0; i < MaxCosts; ++i) {
		player->Incomes[i] = DefaultIncomes[i];
	}

	memset(player->UnitTypesCount, 0, sizeof(player->UnitTypesCount));

	player->NumBuildings = 0;
	player->TotalNumUnits = 0;
	player->Score = 0;

	player->Color = PlayerColors[NumPlayers][0];

	if (Players[NumPlayers].Type == PlayerComputer ||
			Players[NumPlayers].Type == PlayerRescueActive) {
		player->AiEnabled = 1;
	} else {
		player->AiEnabled = 0;
	}

	++NumPlayers;
}

/**
**  Change player name.
**
**  @param name    New name.
*/
void CPlayer::SetName(const std::string &name)
{
	Name = name;
}

/**
**  Clear all player data excepts members which don't change.
**
**  The fields that are not cleared are 
**  UnitLimit, BuildingLimit, TotalUnitLimit and Allow.
*/
void CPlayer::Clear() 
{
	Index = 0;
	Name.clear();
	Type = 0;
	AiName.clear();
	Team = 0;
	Enemy = 0;
	Allied = 0;
	SharedVision = 0;
	StartX = 0;
	StartY = 0;
	memset(Resources, 0, sizeof(Resources));
	memset(Incomes, 0, sizeof(Incomes));
	memset(UnitTypesCount, 0, sizeof(UnitTypesCount));
	AiEnabled = 0;
	Ai = 0;
	memset(Units, 0, sizeof(Units));
	TotalNumUnits = 0;
	NumBuildings = 0;
	// FIXME: can't clear limits since it's initialized already
//	UnitLimit = 0;
//	BuildingLimit = 0;
//	TotalUnitLimit = 0;
	Score = 0;
	TotalUnits = 0;
	TotalBuildings = 0;
	memset(TotalResources, 0, sizeof(TotalResources));
	TotalRazings = 0;
	TotalKills = 0;
	Color = 0;
	ClearResourceVariables();
}

/*----------------------------------------------------------------------------
--  Resource management
----------------------------------------------------------------------------*/

/**
**  Change the player resource.
**
**  @param resource  Resource to change.
**  @param value     How many of this resource.
*/
void CPlayer::SetResource(int resource, int value)
{
	this->Resources[resource] = value;
}

/**
**  Check if the unit-type didn't break any unit limits.
**
**  @param type    Type of unit.
**
**  @return        True if enough, negative on problem.
**
**  @note The return values of the PlayerCheck functions are inconsistent.
*/
int CPlayer::CheckLimits(const CUnitType *type) const
{
	//
	//  Check game limits.
	//
	if (NumUnits < UnitMax) {
		if (type->Building && this->NumBuildings >= this->BuildingLimit) {
			Notify(NotifyYellow, -1, -1, _("Building Limit Reached"));
			return -1;
		}
		if (!type->Building && (this->TotalNumUnits - this->NumBuildings) >= this->UnitLimit) {
			Notify(NotifyYellow, -1, -1, _("Unit Limit Reached"));
			return -2;
		}
		if (this->TotalNumUnits >= this->TotalUnitLimit) {
			Notify(NotifyYellow, -1, -1, _("Total Unit Limit Reached"));
			return -4;
		}
		if (this->UnitTypesCount[type->Slot] >=  this->Allow.Units[type->Slot]) {
			Notify(NotifyYellow, -1, -1, _("Limit of %d reached for this unit type"),
				this->Allow.Units[type->Slot]);
			return -6;
		}
		return 1;
	} else {
		Notify(NotifyYellow, -1, -1, _("Cannot create more units."));
		if (AiEnabled) {
			// AiNoMoreUnits(player, type);
		}
		return -5;
	}
}

/**
**  Check if enough resources for are available.
**
**  @param costs   How many costs.
**
**  @return        False if all enough, otherwise a bit mask.
**
**  @note The return values of the PlayerCheck functions are inconsistent.
*/
int CPlayer::CheckCosts(const int *costs) const
{
	int err = 0;
	for (int i = 1; i < MaxCosts; ++i) {
		if (this->Resources[i] < costs[i]) {
			Notify(NotifyYellow, -1, -1, "Not enough %s...%s more %s.",
				DefaultResourceNames[i].c_str(), DefaultActions[i].c_str(), DefaultResourceNames[i].c_str());

			err |= 1 << i;
		}
	}
	return err;
}

/**
**  Check if enough resources for new unit is available.
**
**  @param type    Type of unit.
**
**  @return        False if all enough, otherwise a bit mask.
*/
int CPlayer::CheckUnitType(const CUnitType *type) const
{
	return this->CheckCosts(type->Stats[this->Index].Costs);
}

/**
**  Add costs to the resources
**
**  @param costs   How many costs.
*/
void CPlayer::AddCosts(const int *costs)
{
	for (int i = 1; i < MaxCosts; ++i) {
		this->Resources[i] += costs[i];
	}
}

/**
**  Add the costs of a unit type to resources
**
**  @param type    Type of unit.
*/
void CPlayer::AddUnitType(const CUnitType *type)
{
	// FIXME: a player could make money by upgrading and than cancel
	AddCosts(type->Stats[this->Index].Costs);
}

/**
**  Add a factor of costs to the resources
**
**  @param costs   How many costs.
**  @param factor  Factor of the costs to apply.
*/
void CPlayer::AddCostsFactor(const int *costs, int factor)
{
	for (int i = 1; i < MaxCosts; ++i) {
		this->Resources[i] += costs[i] * factor / 100;
	}
}

/**
**  Subtract costs from the resources
**
**  @param costs   How many costs.
*/
void CPlayer::SubCosts(const int *costs)
{
	for (int i = 1; i < MaxCosts; ++i) {
		this->Resources[i] -= costs[i];
	}
}

/**
**  Substract the costs of new unit from resources
**
**  @param type    Type of unit.
*/
void CPlayer::SubUnitType(const CUnitType *type)
{
	this->SubCosts(type->Stats[this->Index].Costs);
}

/**
**  Substract a factor of costs from the resources
**
**  @param costs   How many costs.
**  @param factor  Factor of the costs to apply.
*/
void CPlayer::SubCostsFactor(const int *costs, int factor)
{
	for (int i = 1; i < MaxCosts; ++i) {
		this->Resources[i] -= costs[i] * 100 / factor;
	}
}

/**
**  Have unit of type.
**
**  @param type    Type of unit.
**
**  @return        How many exists, false otherwise.
*/
int CPlayer::HaveUnitTypeByType(const CUnitType *type) const
{
	return this->UnitTypesCount[type->Slot];
}

/**
**  Have unit of type.
**
**  @param ident   Identifier of unit-type that should be lookuped.
**
**  @return        How many exists, false otherwise.
**
**  @note This function should not be used during run time.
*/
int CPlayer::HaveUnitTypeByIdent(const std::string &ident) const
{
	return this->UnitTypesCount[UnitTypeByIdent(ident)->Slot];
}

/**
**  Initialize the Ai for all players.
*/
void PlayersInitAi(void)
{
	for (int player = 0; player < NumPlayers; ++player) {
		if (Players[player].AiEnabled) {
			AiInit(&Players[player]);
		}
	}
}

/**
**  Handle AI of all players each game cycle.
*/
void PlayersEachCycle(void)
{
	for (int player = 0; player < NumPlayers; ++player) {
		CPlayer *p = &Players[player];

		// Update rate based economy
		for (int res = 0; res < MaxCosts; ++res) {
			int rate = p->ProductionRate[res] - p->ActualUtilizationRate[res];
			if (rate > 0) {
				if (p->StoredResources[res] < p->StorageCapacity[res]) {
					p->StoredResources[res] += rate;
					if (p->StoredResources[res] > p->StorageCapacity[res]) {
						p->StoredResources[res] = p->StorageCapacity[res];
					}
				}
			} else if (rate < 0) {
				rate = -rate;
//				Assert(p->StoredResources[res] >= rate);
				p->StoredResources[res] -= rate;
				if (p->StoredResources[res] < 0) {
					p->StoredResources[res] = 0;
				}
			}
		}

		// Recalculate costs
		std::map<CUnit *, int *>::iterator it;
		for (it = p->UnitsConsumingResourcesActual.begin(); it != p->UnitsConsumingResourcesActual.end(); ++it) {
			int costs[MaxCosts];
			CalculateCosts((*it).first, costs);
			p->UpdateUnitsConsumingResources((*it).first, costs);
		}

		// Ai
		if (p->AiEnabled) {
			AiEachCycle(p);
		}
	}
}

/**
**  Handle AI of a player each second.
**
**  @param player  the player to update AI
*/
void PlayersEachSecond(int player)
{
	if (Players[player].AiEnabled) {
		AiEachSecond(&Players[player]);
	}
}

#ifndef USE_OPENGL
/**
**  Change current color set to new player.
**
**  FIXME: use function pointer here.
**
**  @param player  Pointer to player.
**  @param sprite  The sprite in which the colors should be changed.
*/
void GraphicPlayerPixels(CPlayer *player, const CGraphic *sprite)
{
	Assert(PlayerColorIndexCount);

	SDL_LockSurface(sprite->Surface);
	SDL_SetColors(sprite->Surface, player->UnitColors.Colors,
		PlayerColorIndexStart, PlayerColorIndexCount);
	if (sprite->SurfaceFlip) {
		SDL_SetColors(sprite->SurfaceFlip,
			player->UnitColors.Colors, PlayerColorIndexStart, PlayerColorIndexCount);
	}
	SDL_UnlockSurface(sprite->Surface);
}
#endif

/**
**  Setup the player colors for the current palette.
**
**  @todo  FIXME: could be called before PixelsXX is setup.
*/
void SetPlayersPalette(void)
{
	for (int i = 0; i < PlayerMax; ++i) {
		delete[] Players[i].UnitColors.Colors;
		Players[i].UnitColors.Colors = new SDL_Color[PlayerColorIndexCount];
		memcpy(Players[i].UnitColors.Colors, PlayerColorsRGB[i],
			sizeof(SDL_Color) * PlayerColorIndexCount);
	}
}

/**
**  Output debug informations for players.
*/
void DebugPlayers(void)
{
#ifdef DEBUG
	int i;
	const char *playertype;

	DebugPrint("Nr   Color   I Name     Type         Ai\n");
	DebugPrint("--  -------- - -------- ------------ -----\n");
	for (i = 0; i < PlayerMax; ++i) {
		if (Players[i].Type == PlayerNobody) {
			continue;
		}
		switch (Players[i].Type) {
			case 0: playertype = "Don't know 0"; break;
			case 1: playertype = "Don't know 1"; break;
			case 2: playertype = "neutral     "; break;
			case 3: playertype = "nobody      "; break;
			case 4: playertype = "computer    "; break;
			case 5: playertype = "person      "; break;
			case 6: playertype = "rescue pas. "; break;
			case 7: playertype = "rescue akt. "; break;
			default : playertype = "?unknown?   "; break;
		}
		DebugPrint("%2d: %8.8s %c %-8.8s %s %s\n" _C_ i _C_ PlayerColorNames[i].c_str() _C_
			ThisPlayer == &Players[i] ? '*' :
				Players[i].AiEnabled ? '+' : ' ' _C_
			Players[i].Name.c_str() _C_ playertype _C_
			Players[i].AiName.c_str());
	}
#endif
}

/**
**  Notify player about a problem.
**
**  @param type    Problem type
**  @param x       Map X tile position
**  @param y       Map Y tile position
**  @param fmt     Message format
**  @param ...     Message varargs
**
**  @note The parameter type, isn't yet used.
**  @todo FIXME: We must also notfiy allied players.
*/
void CPlayer::Notify(int type, int x, int y, const char *fmt, ...) const
{
	char temp[128];
	va_list va;

	// Notify me, and my TEAM members
	if (this != ThisPlayer && !this->IsTeamed(ThisPlayer)) {
		return;
	}

	va_start(va, fmt);
	temp[sizeof(temp) - 1] = '\0';
	vsnprintf(temp, sizeof(temp) - 1, fmt, va);
	va_end(va);

	if (x != -1) {
		UI.Minimap.AddEvent(x, y);
	}
	if (this == ThisPlayer) {
		SetMessageEvent(x, y, "%s", temp);
	} else {
		SetMessageEvent(x, y, "(%s): %s", Name.c_str(), temp);
	}

}

/**
**  Check if the player is an enemy
*/
bool CPlayer::IsEnemy(const CPlayer *x) const
{
	return (this->Enemy & (1 << x->Index)) != 0;
}

/**
**  Check if the unit is an enemy
*/
bool CPlayer::IsEnemy(const CUnit *x) const
{
	return this->IsEnemy(x->Player);
}

/**
**  Check if the player is an ally
*/
bool CPlayer::IsAllied(const CPlayer *x) const
{
	return (this->Allied & (1 << x->Index)) != 0;
}

/**
**  Check if the unit is an ally
*/
bool CPlayer::IsAllied(const CUnit *x) const
{
	return this->IsAllied(x->Player);
}

/**
**  Check if the player shares vision with the player
*/
bool CPlayer::IsSharedVision(const CPlayer *x) const
{
	return (this->SharedVision & (1 << x->Index)) != 0;
}

/**
**  Check if the player shares vision with the unit
*/
bool CPlayer::IsSharedVision(const CUnit *x) const
{
	return this->IsSharedVision(x->Player);
}

/**
**  Check if the both players share vision
*/
bool CPlayer::IsBothSharedVision(const CPlayer *x) const
{
	return (this->SharedVision & (1 << x->Index)) != 0 &&
		(x->SharedVision & (1 << this->Index)) != 0;
}

/**
**  Check if the player and the unit share vision
*/
bool CPlayer::IsBothSharedVision(const CUnit *x) const
{
	return this->IsBothSharedVision(x->Player);
}

/**
**  Check if the player is teamed
*/
bool CPlayer::IsTeamed(const CPlayer *x) const
{
	return this->Team == x->Team;
}

/**
**  Check if the unit is teamed
*/
bool CPlayer::IsTeamed(const CUnit *x) const
{
	return this->IsTeamed(x->Player);
}

//@}