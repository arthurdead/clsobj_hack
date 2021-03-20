/**
 * vim: set ts=4 :
 * =============================================================================
 * SourceMod Sample Extension
 * Copyright (C) 2004-2008 AlliedModders LLC.  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */

#define GAME_DLL

#include "extension.h"
#include <CDetour/detours.h>
#include <tier1/KeyValues.h>
#include <filesystem.h>
#include <vector>
#include <mathlib/IceKey.H>
#include <unordered_map>

enum PLAYER_ANIM {};

#include <tier1/checksum_md5.h>
#include <usercmd.h>

/**
 * @file extension.cpp
 * @brief Implement extension code here.
 */

Sample g_Sample;		/**< Global singleton for extension's main interface */

SMEXT_LINK(&g_Sample);

IGameConfig *g_pGameConf = nullptr;

IFileSystem *filesystem = nullptr;

CDetour *pLoadObjectInfos = nullptr;
CDetour *pGetObjectInfo = nullptr;
CDetour *pGetBuildableId = nullptr;
CDetour *pCTFPlayerClassDataMgrInit = nullptr;
CDetour *pGetPlayerClassData = nullptr;
CDetour *pCTFPlayerClassDataMgrAddAdditionalPlayerDeathSounds = nullptr;

template <typename R, typename T, typename ...Args>
R call_mfunc(T *pThisPtr, void *offset, Args ...args)
{
	class VEmptyClass {};
	
	void **this_ptr = *reinterpret_cast<void ***>(&pThisPtr);
	
	union
	{
		R (VEmptyClass::*mfpnew)(Args...);
#ifndef PLATFORM_POSIX
		void *addr;
	} u;
	u.addr = offset;
#else
		struct  
		{
			void *addr;
			intptr_t adjustor;
		} s;
	} u;
	u.s.addr = offset;
	u.s.adjustor = 0;
#endif
	
	return (R)(reinterpret_cast<VEmptyClass *>(this_ptr)->*u.mfpnew)(args...);
}

#define OBJECT_MAX_GIB_MODELS	9
#ifdef STAGING_ONLY
#define OBJECT_MAX_MODES		4
#else
#define OBJECT_MAX_MODES		3
#endif

enum ObjectType_t
{
	OBJ_DISPENSER=0,
	OBJ_TELEPORTER,
	OBJ_SENTRYGUN,

	// Attachment Objects
	OBJ_ATTACHMENT_SAPPER,

	// If you add a new object, you need to add it to the g_ObjectInfos array 
	// in tf_shareddefs.cpp, and add it's data to the scripts/object.txt

	//
	// ADD NEW ITEMS HERE TO AVOID BREAKING DEMOS
	//
	OBJ_LAST,
};

class CObjectInfo
{
public:
	CObjectInfo( const char *pObjectName );	
	~CObjectInfo();

	// This is initialized by the code and matched with a section in objects.txt
	char	*m_pObjectName;

	// This stuff all comes from objects.txt
	char	*m_pClassName;					// Code classname (in LINK_ENTITY_TO_CLASS).
	char	*m_pStatusName;					// Shows up when crosshairs are on the object.
	float	m_flBuildTime;
	int		m_nMaxObjects;					// Maximum number of objects per player
	int		m_Cost;							// Base object resource cost
	float	m_CostMultiplierPerInstance;	// Cost multiplier
	int		m_UpgradeCost;					// Base object resource cost for upgrading
	int		m_MaxUpgradeLevel;				// Max object upgrade level
	char	*m_pBuilderWeaponName;			// Names shown for each object onscreen when using the builder weapon
	char	*m_pBuilderPlacementString;		// String shown to player during placement of this object
	int		m_SelectionSlot;				// Weapon selection slots for objects
	int		m_SelectionPosition;			// Weapon selection positions for objects
	bool	m_bSolidToPlayerMovement;
	bool	m_bUseItemInfo;					// Use default item appearance info.
	char    *m_pViewModel;					// View model to show in builder weapon for this object
	char    *m_pPlayerModel;				// World model to show attached to the player
	int		m_iDisplayPriority;				// Priority for ordering in the hud display ( higher is closer to top )
	bool	m_bVisibleInWeaponSelection;	// should show up and be selectable via the weapon selection?
	char	*m_pExplodeSound;				// gamesound to play when object explodes
	char	*m_pExplosionParticleEffect;	// particle effect to play when object explodes
	bool	m_bAutoSwitchTo;				// should we let players switch back to the builder weapon representing this?
	char	*m_pUpgradeSound;				// gamesound to play when object is upgraded
	float	m_flUpgradeDuration;			// time it takes to upgrade to the next level
	int		m_iBuildCount;					// number of these that can be carried at one time
	int		m_iNumAltModes;					// whether the item has more than one mode (ex: teleporter exit/entrance)

	struct
	{
		char* pszStatusName;
		char* pszModeName;
		char* pszIconMenu;
	}		m_AltModes[OBJECT_MAX_MODES];

	// HUD weapon selection menu icon ( from hud_textures.txt )
	char	*m_pIconActive;
	char	*m_pIconInactive;
	char	*m_pIconMenu;

	// HUD building status icon
	char	*m_pHudStatusIcon;

	// gibs
	int		m_iMetalToDropInGibs;

	// unique builder
	bool	m_bRequiresOwnBuilder;			// if object needs to instantiate its' own builder
	
	int m_nRepresentative = OBJ_LAST;
	
	CObjectInfo *clone();
};

char* ReadAndAllocString( const char *pValue )
{
	if(!pValue) {
		pValue = "";
	}
	
	int len = Q_strlen( pValue ) + 1;
	char *pAlloced = new char[ len ];
	Q_strncpy( pAlloced, pValue, len );
	return pAlloced;
}

void UpdateObjectOffsets();

CObjectInfo *CObjectInfo::clone()
{
	CObjectInfo *ret = new CObjectInfo{m_pObjectName};
	
	ret->m_pClassName = ReadAndAllocString(m_pClassName);
	ret->m_pStatusName = ReadAndAllocString(m_pStatusName);
	ret->m_pBuilderWeaponName = ReadAndAllocString(m_pBuilderWeaponName);
	ret->m_pBuilderPlacementString = ReadAndAllocString(m_pBuilderPlacementString);
	ret->m_pViewModel = ReadAndAllocString(m_pViewModel);
	ret->m_pPlayerModel = ReadAndAllocString(m_pPlayerModel);
	ret->m_pExplodeSound = ReadAndAllocString(m_pExplodeSound);
	ret->m_pExplosionParticleEffect = ReadAndAllocString(m_pExplosionParticleEffect);
	ret->m_pUpgradeSound = ReadAndAllocString(m_pUpgradeSound);
	ret->m_pIconActive = ReadAndAllocString(m_pIconActive);
	ret->m_pIconInactive = ReadAndAllocString(m_pIconInactive);
	ret->m_pIconMenu = ReadAndAllocString(m_pIconMenu);
	ret->m_pHudStatusIcon = ReadAndAllocString(m_pHudStatusIcon);
	
	for(int i = 0; i < OBJECT_MAX_MODES; ++i) {
		ret->m_AltModes[i].pszStatusName = ReadAndAllocString(m_AltModes[i].pszStatusName);
		ret->m_AltModes[i].pszModeName = ReadAndAllocString(m_AltModes[i].pszModeName);
		ret->m_AltModes[i].pszIconMenu = ReadAndAllocString(m_AltModes[i].pszIconMenu);
	}
	
	ret->m_flBuildTime = m_flBuildTime;
	ret->m_nMaxObjects = m_nMaxObjects;
	ret->m_Cost = m_Cost;
	ret->m_CostMultiplierPerInstance = m_CostMultiplierPerInstance;
	ret->m_UpgradeCost = m_UpgradeCost;
	ret->m_MaxUpgradeLevel = m_MaxUpgradeLevel;
	ret->m_SelectionSlot = m_SelectionSlot;
	ret->m_SelectionPosition = m_SelectionPosition;
	ret->m_bSolidToPlayerMovement = m_bSolidToPlayerMovement;
	ret->m_bUseItemInfo = m_bUseItemInfo;
	ret->m_iDisplayPriority = m_iDisplayPriority;
	ret->m_bVisibleInWeaponSelection = m_bVisibleInWeaponSelection;
	ret->m_bAutoSwitchTo = m_bAutoSwitchTo;
	ret->m_flUpgradeDuration = m_flUpgradeDuration;
	ret->m_iBuildCount = m_iBuildCount;
	ret->m_iNumAltModes = m_iNumAltModes;
	ret->m_iMetalToDropInGibs = m_iMetalToDropInGibs;
	ret->m_bRequiresOwnBuilder = m_bRequiresOwnBuilder;
	
	ret->m_nRepresentative = m_nRepresentative;
	
	return ret;
}

CObjectInfo::CObjectInfo( const char *pObjectName )
{
	m_pObjectName = ReadAndAllocString(pObjectName);
	m_pClassName = NULL;
	m_pStatusName = NULL;
	m_flBuildTime = -9999;
	m_nMaxObjects = -9999;
	m_Cost = -9999;
	m_CostMultiplierPerInstance = -999;
	m_flUpgradeDuration = -999;
	m_UpgradeCost = -9999;
	m_MaxUpgradeLevel = -9999;
	m_pBuilderWeaponName = NULL;
	m_pBuilderPlacementString = NULL;
	m_SelectionSlot = -9999;
	m_SelectionPosition = -9999;
	m_bSolidToPlayerMovement = false;
	m_pIconActive = NULL;
	m_pIconInactive = NULL;
	m_pIconMenu = NULL;
	m_pViewModel = NULL;
	m_pPlayerModel = NULL;
	m_iDisplayPriority = 0;
	m_bVisibleInWeaponSelection = true;
	m_pExplodeSound = NULL;
	m_pUpgradeSound = NULL;
	m_pExplosionParticleEffect = NULL;
	m_bAutoSwitchTo = false;
	m_iBuildCount = 0;
	m_iNumAltModes = 0;
	m_bRequiresOwnBuilder = false;
	for(int i = 0; i < OBJECT_MAX_MODES; ++i) {
		m_AltModes[i].pszStatusName = NULL;
		m_AltModes[i].pszModeName = NULL;
		m_AltModes[i].pszIconMenu = NULL;
	}
}

CObjectInfo::~CObjectInfo()
{
	if(m_pObjectName) {
		delete [] m_pObjectName;
	}
	if(m_pClassName) {
		delete [] m_pClassName;
	}
	if(m_pStatusName) {
		delete [] m_pStatusName;
	}
	if(m_pBuilderWeaponName) {
		delete [] m_pBuilderWeaponName;
	}
	if(m_pBuilderPlacementString) {
		delete [] m_pBuilderPlacementString;
	}
	if(m_pIconActive) {
		delete [] m_pIconActive;
	}
	if(m_pIconInactive) {
		delete [] m_pIconInactive;
	}
	if(m_pIconMenu) {
		delete [] m_pIconMenu;
	}
	if(m_pViewModel) {
		delete [] m_pViewModel;
	}
	if(m_pPlayerModel) {
		delete [] m_pPlayerModel;
	}
	if(m_pExplodeSound) {
		delete [] m_pExplodeSound;
	}
	if(m_pUpgradeSound) {
		delete [] m_pUpgradeSound;
	}
	if(m_pExplosionParticleEffect) {
		delete [] m_pExplosionParticleEffect;
	}
	for(int i = 0; i < OBJECT_MAX_MODES; ++i) {
		delete [] m_AltModes[i].pszStatusName;
		delete [] m_AltModes[i].pszModeName;
		delete [] m_AltModes[i].pszIconMenu;
	}
}

static void SpewFileInfo( IBaseFileSystem *pFileSystem, const char *resourceName, const char *pathID, KeyValues *pValues )
{
	bool bFileExists = pFileSystem->FileExists( resourceName, pathID );
	bool bFileWritable = pFileSystem->IsFileWritable( resourceName, pathID );
	unsigned int nSize = pFileSystem->Size( resourceName, pathID );

	Msg( "resourceName:%s pathID:%s bFileExists:%d size:%u writeable:%d\n", resourceName, pathID, bFileExists, nSize, bFileWritable );

	unsigned int filesize = ( unsigned int )-1;
	FileHandle_t f = filesystem->Open( resourceName, "rb", pathID );
	if ( f )
	{
		filesize = filesystem->Size( f );
		filesystem->Close( f );
	}

	Msg( " FileHandle_t:%p size:%u\n", f, filesize );

	IFileSystem *pFS = 	(IFileSystem *)filesystem;

	char cwd[ MAX_PATH ];
	cwd[ 0 ] = 0;
	pFS->GetCurrentDirectory( cwd, ARRAYSIZE( cwd ) );

	bool bAvailable = pFS->IsFileImmediatelyAvailable( resourceName );

	Msg( " IsFileImmediatelyAvailable:%d cwd:%s\n", bAvailable, cwd );

	pFS->PrintSearchPaths();

	if ( pValues )
	{
		Msg( "Keys:" );
		KeyValuesDumpAsDevMsg( pValues, 2, 0 );
	}
}

std::vector<std::unique_ptr<CObjectInfo>> g_ObjectInfos;

char* ReadAndAllocStringValue( KeyValues *pSub, const char *pName, const char *pFilename )
{
	const char *pValue = pSub->GetString( pName, NULL );
	if ( !pValue )
	{
		if ( pFilename )
		{
			DevWarning( "Can't get key value	'%s' from file '%s'.\n", pName, pFilename );
		}
		pValue = "";
	}

	int len = Q_strlen( pValue ) + 1;
	char *pAlloced = new char[ len ];
	Q_strncpy( pAlloced, pValue, len );
	return pAlloced;
}

void LoadObjectInfo(CObjectInfo *pInfo, KeyValues *pSub, const char *pFilename)
{
	// Read all the info in.
	if ( (pInfo->m_flBuildTime = pSub->GetFloat( "BuildTime", -999 )) == -999 ||
		(pInfo->m_nMaxObjects = pSub->GetInt( "MaxObjects", -999 )) == -999 ||
		(pInfo->m_Cost = pSub->GetInt( "Cost", -999 )) == -999 ||
		(pInfo->m_CostMultiplierPerInstance = pSub->GetFloat( "CostMultiplier", -999 )) == -999 ||
		(pInfo->m_flUpgradeDuration = pSub->GetFloat( "UpgradeDuration", -999 )) == -999 ||
		(pInfo->m_UpgradeCost = pSub->GetInt( "UpgradeCost", -999 )) == -999 ||
		(pInfo->m_MaxUpgradeLevel = pSub->GetInt( "MaxUpgradeLevel", -999 )) == -999 ||
		(pInfo->m_SelectionSlot = pSub->GetInt( "SelectionSlot", -999 )) == -999 ||
		(pInfo->m_iBuildCount = pSub->GetInt( "BuildCount", -999 )) == -999 ||
		(pInfo->m_SelectionPosition = pSub->GetInt( "SelectionPosition", -999 )) == -999 )
	{
		Warning( "Missing data for object '%s' in %s.\n", pInfo->m_pObjectName, pFilename );
	}

	pInfo->m_pClassName = ReadAndAllocStringValue( pSub, "ClassName", pFilename );
	pInfo->m_pStatusName = ReadAndAllocStringValue( pSub, "StatusName", pFilename );
	pInfo->m_pBuilderWeaponName = ReadAndAllocStringValue( pSub, "BuilderWeaponName", pFilename );
	pInfo->m_pBuilderPlacementString = ReadAndAllocStringValue( pSub, "BuilderPlacementString", pFilename );
	pInfo->m_bSolidToPlayerMovement = pSub->GetInt( "SolidToPlayerMovement", 0 ) ? true : false;
	pInfo->m_pIconActive = ReadAndAllocStringValue( pSub, "IconActive", pFilename );
	pInfo->m_pIconInactive = ReadAndAllocStringValue( pSub, "IconInactive", pFilename );
	pInfo->m_pIconMenu = ReadAndAllocStringValue( pSub, "IconMenu", pFilename );
	pInfo->m_bUseItemInfo = ( pSub->GetInt( "UseItemInfo", 0 ) > 0 );
	pInfo->m_pViewModel = ReadAndAllocStringValue( pSub, "Viewmodel", pFilename );
	pInfo->m_pPlayerModel = ReadAndAllocStringValue( pSub, "Playermodel", pFilename );
	pInfo->m_iDisplayPriority = pSub->GetInt( "DisplayPriority", 0 );
	pInfo->m_pHudStatusIcon = ReadAndAllocStringValue( pSub, "HudStatusIcon", pFilename );
	pInfo->m_bVisibleInWeaponSelection = ( pSub->GetInt( "VisibleInWeaponSelection", 1 ) > 0 );
	pInfo->m_pExplodeSound = ReadAndAllocStringValue( pSub, "ExplodeSound", pFilename );
	pInfo->m_pUpgradeSound = ReadAndAllocStringValue( pSub, "UpgradeSound", pFilename );
	pInfo->m_pExplosionParticleEffect = ReadAndAllocStringValue( pSub, "ExplodeEffect", pFilename );
	pInfo->m_bAutoSwitchTo = ( pSub->GetInt( "autoswitchto", 0 ) > 0 );
	pInfo->m_iMetalToDropInGibs = pSub->GetInt( "MetalToDropInGibs", 0 );
	pInfo->m_bRequiresOwnBuilder = pSub->GetBool( "RequiresOwnBuilder", 0 );
	
	pInfo->m_nRepresentative = pSub->GetInt( "Representative", OBJ_LAST );
	
	// Read the other alternate object modes.
	KeyValues *pAltModesKey = pSub->FindKey( "AltModes" );
	if ( pAltModesKey )
	{
		int iIndex = 0;
		while ( iIndex<OBJECT_MAX_MODES )
		{
			char buf[256];
			Q_snprintf( buf, sizeof(buf), "AltMode%d", iIndex );
			KeyValues *pCurrentModeKey = pAltModesKey->FindKey( buf );
			if ( !pCurrentModeKey )
				break;

			pInfo->m_AltModes[iIndex].pszStatusName = ReadAndAllocStringValue( pCurrentModeKey, "StatusName", pFilename );
			pInfo->m_AltModes[iIndex].pszModeName   = ReadAndAllocStringValue( pCurrentModeKey, "ModeName",   pFilename );
			pInfo->m_AltModes[iIndex].pszIconMenu   = ReadAndAllocStringValue( pCurrentModeKey, "IconMenu",   pFilename );

			iIndex++;
		}
		pInfo->m_iNumAltModes = iIndex-1;
	}

	// Alternate mode 0 always matches the defaults.
	pInfo->m_AltModes[0].pszStatusName = ReadAndAllocString(pInfo->m_pStatusName);
	pInfo->m_AltModes[0].pszIconMenu   = ReadAndAllocString(pInfo->m_pIconMenu);
}

void DoLoadObjectInfos(IBaseFileSystem *pFileSystem)
{
	g_ObjectInfos.clear();
	
	g_ObjectInfos.emplace_back(new CObjectInfo( "OBJ_DISPENSER" ));
	g_ObjectInfos.emplace_back(new CObjectInfo( "OBJ_TELEPORTER" ));
	g_ObjectInfos.emplace_back(new CObjectInfo( "OBJ_SENTRYGUN" ));
	g_ObjectInfos.emplace_back(new CObjectInfo( "OBJ_ATTACHMENT_SAPPER" ));
	
	const char *pFilename = "scripts/objects.txt";

	KeyValues *pValues = new KeyValues( "Object descriptions" );
	if ( !pValues->LoadFromFile( pFileSystem, pFilename, "GAME" ) )
	{
		// Getting "Can't open scripts/objects.txt for object info." errors. Spew file information
		//  before the Error() call which should show up in the minidumps.
		SpewFileInfo( pFileSystem, pFilename, "GAME", NULL );

		Error( "Can't open %s for object info.", pFilename );
		pValues->deleteThis();
		return;
	}

	// Now read each class's information in.
	for ( int iObj=0; iObj < g_ObjectInfos.size(); iObj++ )
	{
		CObjectInfo *pInfo = g_ObjectInfos[iObj].get();
		KeyValues *pSub = pValues->FindKey( pInfo->m_pObjectName );
		if ( !pSub )
		{
			// It seems that folks have corrupt files when these errors are seen in http://minidump.
			// Does it make sense to call the below Steam API so it'll force a validation next startup time?
			// Need to verify it's real corruption and not someone dorking around with their objects.txt file...
			//
			// From Martin Otten: If you have a file on disc and youre 100% sure its
			//  corrupt, call ISteamApps::MarkContentCorrupt( false ), before you shutdown
			//  the game. This will cause a content validation in Steam.

			Warning( "Missing section '%s' from %s.\n", pInfo->m_pObjectName, pFilename );
			continue;
		}

		LoadObjectInfo(pInfo, pSub, pFilename);
	}
	
	g_ObjectInfos.emplace_back(new CObjectInfo( "OBJ_LAST" ));
	
	g_ObjectInfos[OBJ_DISPENSER]->m_nRepresentative = OBJ_DISPENSER;
	g_ObjectInfos[OBJ_TELEPORTER]->m_nRepresentative = OBJ_TELEPORTER;
	g_ObjectInfos[OBJ_SENTRYGUN]->m_nRepresentative = OBJ_SENTRYGUN;
	g_ObjectInfos[OBJ_ATTACHMENT_SAPPER]->m_nRepresentative = OBJ_ATTACHMENT_SAPPER;
	
	UpdateObjectOffsets();

	pValues->deleteThis();
}

DETOUR_DECL_STATIC1( LoadObjectInfos, void, IBaseFileSystem *, pFileSystem )
{
	DoLoadObjectInfos(pFileSystem);
}

DETOUR_DECL_STATIC1(GetBuildableId, int , const char *, pszBuildableName )
{
	for ( int iBuildable = 0; iBuildable < g_ObjectInfos.size(); ++iBuildable )
	{
		if ( !Q_stricmp( pszBuildableName, g_ObjectInfos[iBuildable]->m_pObjectName ) )
			return iBuildable;
	}

	return OBJ_LAST;
}

DETOUR_DECL_STATIC1(GetObjectInfo, const CObjectInfo*, int, iObject )
{
	return g_ObjectInfos[iObject].get();
}

#define MAX_PLAYERCLASS_SOUND_LENGTH	128
#define TF_NAME_LENGTH		128


#define DEATH_SOUND_FIRST			( DEATH_SOUND_GENERIC )
#define DEATH_SOUND_LAST			( DEATH_SOUND_EXPLOSION )
#define DEATH_SOUND_MVM_FIRST		( DEATH_SOUND_GENERIC_MVM )
#define DEATH_SOUND_MVM_LAST		( DEATH_SOUND_EXPLOSION_MVM )
#define DEATH_SOUND_GIANT_MVM_FIRST	( DEATH_SOUND_GENERIC_GIANT_MVM )
#define DEATH_SOUND_GIANT_MVM_LAST	( DEATH_SOUND_EXPLOSION_GIANT_MVM )

enum DeathSoundType_t
{
	DEATH_SOUND_GENERIC = 0,
	DEATH_SOUND_CRIT,
	DEATH_SOUND_MELEE,
	DEATH_SOUND_EXPLOSION,

	DEATH_SOUND_GENERIC_MVM,
	DEATH_SOUND_CRIT_MVM,
	DEATH_SOUND_MELEE_MVM,
	DEATH_SOUND_EXPLOSION_MVM,

	DEATH_SOUND_GENERIC_GIANT_MVM,
	DEATH_SOUND_CRIT_GIANT_MVM,
	DEATH_SOUND_MELEE_GIANT_MVM,
	DEATH_SOUND_EXPLOSION_GIANT_MVM,

	DEATH_SOUND_TOTAL
};

#define TF_PLAYER_WEAPON_COUNT		6
#define TF_PLAYER_GRENADE_COUNT		2
#define TF_PLAYER_BUILDABLE_COUNT	3
#define TF_PLAYER_BLUEPRINT_COUNT	6

enum ETFAmmoType
{
	TF_AMMO_DUMMY = 0,	// Dummy index to make the CAmmoDef indices correct for the other ammo types.
	TF_AMMO_PRIMARY,
	TF_AMMO_SECONDARY,
	TF_AMMO_METAL,
	TF_AMMO_GRENADES1,
	TF_AMMO_GRENADES2,
	TF_AMMO_GRENADES3,	// Utility Slot Grenades
	TF_AMMO_COUNT,

	//
	// ADD NEW ITEMS HERE TO AVOID BREAKING DEMOS
	//
};

enum
{
	TF_CLASS_UNDEFINED = 0,

	TF_CLASS_SCOUT,			// TF_FIRST_NORMAL_CLASS
    TF_CLASS_SNIPER,
    TF_CLASS_SOLDIER,
	TF_CLASS_DEMOMAN,
	TF_CLASS_MEDIC,
	TF_CLASS_HEAVYWEAPONS,
	TF_CLASS_PYRO,
	TF_CLASS_SPY,
	TF_CLASS_ENGINEER,		

	// Add any new classes after Engineer
	TF_CLASS_CIVILIAN,		// TF_LAST_NORMAL_CLASS
	TF_CLASS_COUNT_ALL,

	TF_CLASS_RANDOM
};

#define TF_CLASS_COUNT			( TF_CLASS_COUNT_ALL )

#define TF_FIRST_NORMAL_CLASS	( TF_CLASS_UNDEFINED + 1 )
#define TF_LAST_NORMAL_CLASS	( TF_CLASS_CIVILIAN )

struct TFPlayerClassData_t
{
	char		m_szClassName[TF_NAME_LENGTH];
	char		m_szModelName[TF_NAME_LENGTH];
	char		m_szHWMModelName[TF_NAME_LENGTH];
	char		m_szHandModelName[TF_NAME_LENGTH];
	char		m_szLocalizableName[TF_NAME_LENGTH];
	float		m_flMaxSpeed;
	int			m_nMaxHealth;
	int			m_nMaxArmor;
	int			m_aWeapons[TF_PLAYER_WEAPON_COUNT];
	int			m_aGrenades[TF_PLAYER_GRENADE_COUNT];
	int			m_aAmmoMax[TF_AMMO_COUNT];
	int			m_aBuildable[TF_PLAYER_BLUEPRINT_COUNT];

	bool		m_bDontDoAirwalk;
	bool		m_bDontDoNewJump;

	bool		m_bParsed;
	Vector		m_vecThirdPersonOffset;

	// sounds
	char		m_szDeathSound[ DEATH_SOUND_TOTAL ][MAX_PLAYERCLASS_SOUND_LENGTH];

	TFPlayerClassData_t();
	const char *GetModelName() const;

	const char *GetDeathSound( int nType );

	void Parse( const char *pszClassName );
	void ParseData( KeyValues *pKeyValuesData );
	void AddAdditionalPlayerDeathSounds( void );
	
	TFPlayerClassData_t *clone();
};

TFPlayerClassData_t *TFPlayerClassData_t::clone()
{
	TFPlayerClassData_t *ret = new TFPlayerClassData_t();
	
	Q_strncpy( ret->m_szClassName, m_szClassName, TF_NAME_LENGTH );
	Q_strncpy( ret->m_szModelName, m_szModelName, TF_NAME_LENGTH );
	Q_strncpy( ret->m_szHWMModelName, m_szHWMModelName, TF_NAME_LENGTH );
	Q_strncpy( ret->m_szHandModelName, m_szHandModelName, TF_NAME_LENGTH );
	Q_strncpy( ret->m_szLocalizableName, m_szLocalizableName, TF_NAME_LENGTH );
	
	for(int i = 0; i < DEATH_SOUND_TOTAL; ++i) {
		Q_strncpy( ret->m_szDeathSound[i], m_szDeathSound[i], MAX_PLAYERCLASS_SOUND_LENGTH );
	}
	
	ret->m_flMaxSpeed = m_flMaxSpeed;
	ret->m_nMaxHealth = m_nMaxHealth;
	ret->m_nMaxArmor = m_nMaxArmor;
	ret->m_bDontDoAirwalk = m_bDontDoAirwalk;
	ret->m_bDontDoNewJump = m_bDontDoNewJump;
	ret->m_bParsed = m_bParsed;
	
	for(int i = 0; i < TF_PLAYER_WEAPON_COUNT; ++i) {
		ret->m_aWeapons[i] = m_aWeapons[i];
	}
	
	for(int i = 0; i < TF_PLAYER_GRENADE_COUNT; ++i) {
		ret->m_aGrenades[i] = m_aGrenades[i];
	}
	
	for(int i = 0; i < TF_AMMO_COUNT; ++i) {
		ret->m_aAmmoMax[i] = m_aAmmoMax[i];
	}
	
	for(int i = 0; i < TF_PLAYER_BLUEPRINT_COUNT; ++i) {
		ret->m_aBuildable[i] = m_aBuildable[i];
	}
	
	ret->m_vecThirdPersonOffset = m_vecThirdPersonOffset;
	
	return ret;
}

void UpdateClassOffsets();

std::vector<std::unique_ptr<TFPlayerClassData_t>> m_aTFPlayerClassData;

#define TF_CLASS_UNDEFINED_FILE			""
#define TF_CLASS_SCOUT_FILE				"scripts/playerclasses/scout"
#define TF_CLASS_SNIPER_FILE			"scripts/playerclasses/sniper"
#define TF_CLASS_SOLDIER_FILE			"scripts/playerclasses/soldier"
#define TF_CLASS_DEMOMAN_FILE			"scripts/playerclasses/demoman"
#define TF_CLASS_MEDIC_FILE				"scripts/playerclasses/medic"
#define TF_CLASS_HEAVYWEAPONS_FILE		"scripts/playerclasses/heavyweapons"
#define TF_CLASS_PYRO_FILE				"scripts/playerclasses/pyro"
#define TF_CLASS_SPY_FILE				"scripts/playerclasses/spy"
#define TF_CLASS_ENGINEER_FILE			"scripts/playerclasses/engineer"
#define TF_CLASS_CIVILIAN_FILE			"scripts/playerclasses/civilian"

const char *s_aPlayerClassFiles[] =
{
	TF_CLASS_UNDEFINED_FILE,
	TF_CLASS_SCOUT_FILE,
	TF_CLASS_SNIPER_FILE,
	TF_CLASS_SOLDIER_FILE,
	TF_CLASS_DEMOMAN_FILE,
	TF_CLASS_MEDIC_FILE,
	TF_CLASS_HEAVYWEAPONS_FILE,
	TF_CLASS_PYRO_FILE,
	TF_CLASS_SPY_FILE,
	TF_CLASS_ENGINEER_FILE,
	TF_CLASS_CIVILIAN_FILE
};

const unsigned char *GetTFEncryptionKey( void )
{ 
	return (unsigned char *)"E2NcUkG2"; 
}

void UTIL_DecodeICE( unsigned char * buffer, int size, const unsigned char *key)
{
	if ( !key )
		return;

	IceKey ice( 0 ); // level 0 = 64bit key
	ice.set( key ); // set key

	int blockSize = ice.blockSize();

	unsigned char *temp = (unsigned char *)alloca( PAD_NUMBER( size, blockSize ) );
	unsigned char *p1 = buffer;
	unsigned char *p2 = temp;
				
	// encrypt data in 8 byte blocks
	int bytesLeft = size;
	while ( bytesLeft >= blockSize )
	{
		ice.decrypt( p1, p2 );
		bytesLeft -= blockSize;
		p1+=blockSize;
		p2+=blockSize;
	}

	// copy encrypted data back to original buffer
	Q_memcpy( buffer, temp, size-bytesLeft );
}

KeyValues* ReadEncryptedKVFile( IFileSystem *pFilesystem, const char *szFilenameWithoutExtension, const unsigned char *pICEKey, bool bForceReadEncryptedFile = false )
{
	char szFullName[512];

	const char *pSearchPath = "MOD";

	if ( pICEKey == NULL )
	{
		pSearchPath = "GAME";
	}

	// Open the weapon data file, and abort if we can't
	KeyValues *pKV = new KeyValues( "WeaponDatafile" );

	Q_snprintf(szFullName,sizeof(szFullName), "%s.txt", szFilenameWithoutExtension);

	if ( bForceReadEncryptedFile || !pKV->LoadFromFile( pFilesystem, szFullName, pSearchPath ) ) // try to load the normal .txt file first
	{
#ifndef _XBOX
		if ( pICEKey )
		{
			Q_snprintf(szFullName,sizeof(szFullName), "%s.ctx", szFilenameWithoutExtension); // fall back to the .ctx file

			FileHandle_t f = pFilesystem->Open( szFullName, "rb", pSearchPath );

			if (!f)
			{
				pKV->deleteThis();
				return NULL;
			}
			// load file into a null-terminated buffer
			int fileSize = pFilesystem->Size(f);
			char *buffer = (char*)MemAllocScratch(fileSize + 1);
		
			pFilesystem->Read(buffer, fileSize, f); // read into local buffer
			buffer[fileSize] = 0; // null terminate file as EOF
			pFilesystem->Close( f );	// close file after reading

			UTIL_DecodeICE( (unsigned char*)buffer, fileSize, pICEKey );
			
			bool retOK = pKV->LoadFromBuffer( szFullName, buffer, pFilesystem );

			MemFreeScratch();

			if ( !retOK )
			{
				pKV->deleteThis();
				return NULL;
			}
		}
		else
		{
			pKV->deleteThis();
			return NULL;
		}
#else
		pKV->deleteThis();
		return NULL;
#endif
	}

	return pKV;
}

void TFPlayerClassData_t::Parse( const char *szName )
{
	// Have we parsed this file already?
	if ( m_bParsed )
		return;

	// Parse class file.
	const unsigned char *pKey = GetTFEncryptionKey();
	KeyValues *pKV = ReadEncryptedKVFile( filesystem, szName, pKey );
	if ( pKV )
	{
		ParseData( pKV );
		pKV->deleteThis();
	}
}

const char *g_aAmmoNames[] =
{
	"DUMMY AMMO",
	"TF_AMMO_PRIMARY",
	"TF_AMMO_SECONDARY",
	"TF_AMMO_METAL",
	"TF_AMMO_GRENADES1",
	"TF_AMMO_GRENADES2",
	"TF_AMMO_GRENADES3"
};

const char *GetAmmoName( int iAmmoType )
{
	ETFAmmoType eAmmoType = (ETFAmmoType)iAmmoType;
	return g_aAmmoNames[ eAmmoType ];
}

const char *g_aWeaponNames[] =
{
	"TF_WEAPON_NONE",
	"TF_WEAPON_BAT",
	"TF_WEAPON_BAT_WOOD",
	"TF_WEAPON_BOTTLE", 
	"TF_WEAPON_FIREAXE",
	"TF_WEAPON_CLUB",
	"TF_WEAPON_CROWBAR",
	"TF_WEAPON_KNIFE",
	"TF_WEAPON_FISTS",
	"TF_WEAPON_SHOVEL",
	"TF_WEAPON_WRENCH",
	"TF_WEAPON_BONESAW",
	"TF_WEAPON_SHOTGUN_PRIMARY",
	"TF_WEAPON_SHOTGUN_SOLDIER",
	"TF_WEAPON_SHOTGUN_HWG",
	"TF_WEAPON_SHOTGUN_PYRO",
	"TF_WEAPON_SCATTERGUN",
	"TF_WEAPON_SNIPERRIFLE",
	"TF_WEAPON_MINIGUN",
	"TF_WEAPON_SMG",
	"TF_WEAPON_SYRINGEGUN_MEDIC",
	"TF_WEAPON_TRANQ",
	"TF_WEAPON_ROCKETLAUNCHER",
	"TF_WEAPON_GRENADELAUNCHER",
	"TF_WEAPON_PIPEBOMBLAUNCHER",
	"TF_WEAPON_FLAMETHROWER",
	"TF_WEAPON_GRENADE_NORMAL",
	"TF_WEAPON_GRENADE_CONCUSSION",
	"TF_WEAPON_GRENADE_NAIL",
	"TF_WEAPON_GRENADE_MIRV",
	"TF_WEAPON_GRENADE_MIRV_DEMOMAN",
	"TF_WEAPON_GRENADE_NAPALM",
	"TF_WEAPON_GRENADE_GAS",
	"TF_WEAPON_GRENADE_EMP",
	"TF_WEAPON_GRENADE_CALTROP",
	"TF_WEAPON_GRENADE_PIPEBOMB",
	"TF_WEAPON_GRENADE_SMOKE_BOMB",
	"TF_WEAPON_GRENADE_HEAL",
	"TF_WEAPON_GRENADE_STUNBALL",
	"TF_WEAPON_GRENADE_JAR",
	"TF_WEAPON_GRENADE_JAR_MILK",
	"TF_WEAPON_PISTOL",
	"TF_WEAPON_PISTOL_SCOUT",
	"TF_WEAPON_REVOLVER",
	"TF_WEAPON_NAILGUN",
	"TF_WEAPON_PDA",
	"TF_WEAPON_PDA_ENGINEER_BUILD",
	"TF_WEAPON_PDA_ENGINEER_DESTROY",
	"TF_WEAPON_PDA_SPY",
	"TF_WEAPON_BUILDER",
	"TF_WEAPON_MEDIGUN",
	"TF_WEAPON_GRENADE_MIRVBOMB",
	"TF_WEAPON_FLAMETHROWER_ROCKET",
	"TF_WEAPON_GRENADE_DEMOMAN",
	"TF_WEAPON_SENTRY_BULLET",
	"TF_WEAPON_SENTRY_ROCKET",
	"TF_WEAPON_DISPENSER",
	"TF_WEAPON_INVIS",
	"TF_WEAPON_FLAREGUN",
	"TF_WEAPON_LUNCHBOX",
	"TF_WEAPON_JAR",
	"TF_WEAPON_COMPOUND_BOW",
	"TF_WEAPON_BUFF_ITEM",
	"TF_WEAPON_PUMPKIN_BOMB",
	"TF_WEAPON_SWORD",
	"TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT",
	"TF_WEAPON_LIFELINE",
	"TF_WEAPON_LASER_POINTER",
	"TF_WEAPON_DISPENSER_GUN",
	"TF_WEAPON_SENTRY_REVENGE",
	"TF_WEAPON_JAR_MILK",
	"TF_WEAPON_HANDGUN_SCOUT_PRIMARY",
	"TF_WEAPON_BAT_FISH",
	"TF_WEAPON_CROSSBOW",
	"TF_WEAPON_STICKBOMB",
	"TF_WEAPON_HANDGUN_SCOUT_SECONDARY",
	"TF_WEAPON_SODA_POPPER",
	"TF_WEAPON_SNIPERRIFLE_DECAP",
	"TF_WEAPON_RAYGUN",
	"TF_WEAPON_PARTICLE_CANNON",
	"TF_WEAPON_MECHANICAL_ARM",
	"TF_WEAPON_DRG_POMSON",
	"TF_WEAPON_BAT_GIFTWRAP",
	"TF_WEAPON_GRENADE_ORNAMENT_BALL",
	"TF_WEAPON_FLAREGUN_REVENGE",
	"TF_WEAPON_PEP_BRAWLER_BLASTER",
	"TF_WEAPON_CLEAVER",
	"TF_WEAPON_GRENADE_CLEAVER",
	"TF_WEAPON_STICKY_BALL_LAUNCHER",
	"TF_WEAPON_GRENADE_STICKY_BALL",
	"TF_WEAPON_SHOTGUN_BUILDING_RESCUE",
	"TF_WEAPON_CANNON",
	"TF_WEAPON_THROWABLE",
	"TF_WEAPON_GRENADE_THROWABLE",
	"TF_WEAPON_PDA_SPY_BUILD",
	"TF_WEAPON_GRENADE_WATERBALLOON",
	"TF_WEAPON_HARVESTER_SAW",
	"TF_WEAPON_SPELLBOOK",
	"TF_WEAPON_SPELLBOOK_PROJECTILE",
	"TF_WEAPON_SNIPERRIFLE_CLASSIC",
	"TF_WEAPON_PARACHUTE",
	"TF_WEAPON_GRAPPLINGHOOK",
	"TF_WEAPON_PASSTIME_GUN",
	"TF_WEAPON_CHARGED_SMG",
};

#define TF_WEAPON_NONE 0

int GetWeaponId( const char *pszWeaponName )
{
	for ( int iWeapon = 0; iWeapon < ARRAYSIZE( g_aWeaponNames ); ++iWeapon )
	{
		if ( !Q_stricmp( pszWeaponName, g_aWeaponNames[iWeapon] ) )
			return iWeapon;
	}

	return TF_WEAPON_NONE;
}

TFPlayerClassData_t::TFPlayerClassData_t()
{
	m_szClassName[0] = '\0';
	m_szModelName[0] = '\0';
	m_szHWMModelName[0] = '\0';
	m_szHandModelName[0] = '\0';
	m_szLocalizableName[0] = '\0';
	m_flMaxSpeed = 0.0f;
	m_nMaxHealth = 0;
	m_nMaxArmor = 0;

	for ( int i = 0; i < ARRAYSIZE( m_szDeathSound ); ++i )
	{
		m_szDeathSound[ i ][ 0 ] = '\0';
	}

	for ( int iWeapon = 0; iWeapon < TF_PLAYER_WEAPON_COUNT; ++iWeapon )
	{
		m_aWeapons[iWeapon] = TF_WEAPON_NONE;
	}

	for ( int iGrenade = 0; iGrenade < TF_PLAYER_GRENADE_COUNT; ++iGrenade )
	{
		m_aGrenades[iGrenade] = TF_WEAPON_NONE;
	}

	for ( int iAmmo = 0; iAmmo < TF_AMMO_COUNT; ++iAmmo )
	{
		m_aAmmoMax[iAmmo] = TF_AMMO_DUMMY;
	}

	for ( int iBuildable = 0; iBuildable < TF_PLAYER_BLUEPRINT_COUNT; ++iBuildable )
	{
		m_aBuildable[iBuildable] = OBJ_LAST;
	}

	m_bParsed = false;
}

void TFPlayerClassData_t::ParseData( KeyValues *pKeyValuesData )
{
	// Attributes.
	Q_strncpy( m_szClassName, pKeyValuesData->GetString( "name" ), TF_NAME_LENGTH );

	// Load the high res model or the lower res model.
	if ( !IsX360() )
	{
		Q_strncpy( m_szHWMModelName, pKeyValuesData->GetString( "model_hwm" ), TF_NAME_LENGTH );
	}
	Q_strncpy( m_szModelName, pKeyValuesData->GetString( "model" ), TF_NAME_LENGTH );
	Q_strncpy( m_szHandModelName, pKeyValuesData->GetString( "model_hands" ), TF_NAME_LENGTH );
	Q_strncpy( m_szLocalizableName, pKeyValuesData->GetString( "localize_name" ), TF_NAME_LENGTH );

	m_flMaxSpeed = pKeyValuesData->GetFloat( "speed_max" );
	m_nMaxHealth = pKeyValuesData->GetInt( "health_max" );
	m_nMaxArmor = pKeyValuesData->GetInt( "armor_max" );

	// Weapons.
	int i;
	char buf[32];
	for ( i=0;i<TF_PLAYER_WEAPON_COUNT;i++ )
	{
		Q_snprintf( buf, sizeof(buf), "weapon%d", i+1 );		
		m_aWeapons[i] = GetWeaponId( pKeyValuesData->GetString( buf ) );
	}

	// Grenades.
	m_aGrenades[0] = GetWeaponId( pKeyValuesData->GetString( "grenade1" ) );
	m_aGrenades[1] = GetWeaponId( pKeyValuesData->GetString( "grenade2" ) );

	// Ammo Max.
	KeyValues *pAmmoKeyValuesData = pKeyValuesData->FindKey( "AmmoMax" );
	if ( pAmmoKeyValuesData )
	{
		for ( int iAmmo = 1; iAmmo < TF_AMMO_COUNT; ++iAmmo )
		{
			m_aAmmoMax[iAmmo] = pAmmoKeyValuesData->GetInt( GetAmmoName( iAmmo ), 0 );
		}
	}

	// Buildables
	for ( i=0;i<TF_PLAYER_BLUEPRINT_COUNT;i++ )
	{
		Q_snprintf( buf, sizeof(buf), "buildable%d", i+1 );		
		m_aBuildable[i] = GetBuildableId( pKeyValuesData->GetString( buf ) );		
	}

	// Temp animation flags
	m_bDontDoAirwalk = ( pKeyValuesData->GetInt( "DontDoAirwalk", 0 ) > 0 );
	m_bDontDoNewJump = ( pKeyValuesData->GetInt( "DontDoNewJump", 0 ) > 0 );

	m_vecThirdPersonOffset.x = pKeyValuesData->GetFloat( "cameraoffset_forward" );
	m_vecThirdPersonOffset.y = pKeyValuesData->GetFloat( "cameraoffset_right" );
	m_vecThirdPersonOffset.z = pKeyValuesData->GetFloat( "cameraoffset_up" );

	// Death Sounds
	Q_strncpy( m_szDeathSound[ DEATH_SOUND_GENERIC ], pKeyValuesData->GetString( "sound_death", "Player.Death" ), MAX_PLAYERCLASS_SOUND_LENGTH );
	Q_strncpy( m_szDeathSound[ DEATH_SOUND_CRIT ], pKeyValuesData->GetString( "sound_crit_death", "TFPlayer.CritDeath" ), MAX_PLAYERCLASS_SOUND_LENGTH );
	Q_strncpy( m_szDeathSound[ DEATH_SOUND_MELEE ], pKeyValuesData->GetString( "sound_melee_death", "Player.MeleeDeath" ), MAX_PLAYERCLASS_SOUND_LENGTH );
	Q_strncpy( m_szDeathSound[ DEATH_SOUND_EXPLOSION ], pKeyValuesData->GetString( "sound_explosion_death", "Player.ExplosionDeath" ), MAX_PLAYERCLASS_SOUND_LENGTH );

	// The file has been parsed.
	m_bParsed = true;
}

bool DoClassDataMgrInit()
{
	m_aTFPlayerClassData.clear();
	
	m_aTFPlayerClassData.resize(TF_CLASS_COUNT_ALL);
	
	UpdateClassOffsets();
	
	// Special case the undefined class.
	m_aTFPlayerClassData[TF_CLASS_UNDEFINED].reset(new TFPlayerClassData_t{});
	
	TFPlayerClassData_t *pClassData = m_aTFPlayerClassData[TF_CLASS_UNDEFINED].get();
	Q_strncpy( pClassData->m_szClassName, "undefined", TF_NAME_LENGTH );
	Q_strncpy( pClassData->m_szModelName, "models/player/scout.mdl", TF_NAME_LENGTH );	// Undefined players still need a model
	Q_strncpy( pClassData->m_szLocalizableName, "undefined", TF_NAME_LENGTH );

	// Initialize the classes.
	for ( int iClass = 1; iClass < TF_CLASS_COUNT_ALL; ++iClass )
	{
		m_aTFPlayerClassData[iClass].reset(new TFPlayerClassData_t{});
		pClassData = m_aTFPlayerClassData[iClass].get();
		pClassData->Parse( s_aPlayerClassFiles[iClass] );
	}
	
	return true;
}

DETOUR_DECL_MEMBER0(CTFPlayerClassDataMgrInit, bool)
{
	return DoClassDataMgrInit();
}

DETOUR_DECL_STATIC1(GetPlayerClassData, TFPlayerClassData_t *, unsigned int, iClass )
{
	return m_aTFPlayerClassData[ iClass ].get();
}

void CopySoundNameWithModifierToken( char *pchDest, const char *pchSource, int nMaxLenInChars, const char *pchToken )
{
	// Copy the sound name
	int nSource = 0;
	int nDest = 0;
	bool bFoundPeriod = false;

	while ( pchSource[ nSource ] != '\0' && nDest < nMaxLenInChars - 2 )
	{
		pchDest[ nDest ] = pchSource[ nSource ];
		nDest++;
		nSource++;

		if ( !bFoundPeriod && pchSource[ nSource - 1 ] == '.' )
		{
			// Insert special token after the period
			bFoundPeriod = true;

			int nToken = 0;

			while ( pchToken[ nToken ] != '\0' && nDest < nMaxLenInChars - 2 )
			{
				pchDest[ nDest ] = pchToken[ nToken ];
				nDest++;
				nToken++;
			}
		}
	}

	pchDest[ nDest ] = '\0';
}

void TFPlayerClassData_t::AddAdditionalPlayerDeathSounds( void )
{
	for ( int i = DEATH_SOUND_FIRST; i <= DEATH_SOUND_LAST; ++i )
	{
		CopySoundNameWithModifierToken( m_szDeathSound[ i + DEATH_SOUND_MVM_FIRST ], m_szDeathSound[ i ], ARRAYSIZE( m_szDeathSound[0] ), "MVM_" );
		CopySoundNameWithModifierToken( m_szDeathSound[ i + DEATH_SOUND_GIANT_MVM_FIRST ], m_szDeathSound[ i ], ARRAYSIZE( m_szDeathSound[0] ), "M_MVM_" );
	}
}

DETOUR_DECL_MEMBER0(CTFPlayerClassDataMgrAddAdditionalPlayerDeathSounds, void)
{
	for ( int iClass = 1; iClass < m_aTFPlayerClassData.size(); ++iClass )
	{
		TFPlayerClassData_t *pClassData = m_aTFPlayerClassData[iClass].get();
		pClassData->AddAdditionalPlayerDeathSounds();
	}
}

bool Sample::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	GET_V_IFACE_CURRENT(GetEngineFactory, filesystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION)
	
	DoLoadObjectInfos(filesystem);
	DoClassDataMgrInit();
	
	return true;
}

void LoadExtraObjects()
{
	char objdir[PLATFORM_MAX_PATH];
	g_pSM->BuildPath(Path_SM, objdir, sizeof(objdir), "data/objects/");

	DIR *dir = opendir(objdir);
	if(dir) {
		struct dirent *entry = nullptr;
		while(true) {
			entry = readdir(dir);
			if(!entry) {
				break;
			}
			
			if(entry->d_name[0] == '.' ||
				entry->d_name[1] == '.') {
				continue;
			}
			
			char pFilename[PLATFORM_MAX_PATH];
			Q_strncpy( pFilename, objdir, PLATFORM_MAX_PATH );
			strcat(pFilename, entry->d_name);
			
			KeyValues *pValues = new KeyValues( "Object descriptions" );
			if ( !pValues->LoadFromFile( filesystem, pFilename, "GAME" ) )
			{
				Warning( "Can't open %s for object info.\n", pFilename );
				pValues->deleteThis();
				continue;
			}
			
			FOR_EACH_SUBKEY(pValues, pSub)
			{
				const char *name = pSub->GetName();
				
				g_ObjectInfos.emplace_back(new CObjectInfo{name});
				CObjectInfo *pInfo = g_ObjectInfos.back().get();

				LoadObjectInfo(pInfo, pSub, pFilename);
			}
			
			pValues->deleteThis();
		}
		closedir(dir);
	}
	
	UpdateObjectOffsets();
}

void LoadExtraClasses()
{
	char plrclsdir[PLATFORM_MAX_PATH];
	g_pSM->BuildPath(Path_SM, plrclsdir, sizeof(plrclsdir), "data/playerclasses/");

	DIR *dir = opendir(plrclsdir);
	if(dir) {
		struct dirent *entry = nullptr;
		while(true) {
			entry = readdir(dir);
			if(!entry) {
				break;
			}
			
			if(entry->d_name[0] == '.' ||
				entry->d_name[1] == '.') {
				continue;
			}
			
			char pFilename[PLATFORM_MAX_PATH];
			Q_strncpy( pFilename, plrclsdir, PLATFORM_MAX_PATH );
			strcat(pFilename, entry->d_name);
			
			KeyValues *pValues = new KeyValues( "WeaponDatafile" );
			if ( !pValues->LoadFromFile( filesystem, pFilename, "GAME" ) )
			{
				Warning( "Can't open %s for class info.\n", pFilename );
				pValues->deleteThis();
				continue;
			}
			
			m_aTFPlayerClassData.emplace_back(new TFPlayerClassData_t{});
			TFPlayerClassData_t *pClassData = m_aTFPlayerClassData.back().get();
			pClassData->ParseData( pValues );
			
			pValues->deleteThis();
		}
		closedir(dir);
	}
	
	UpdateClassOffsets();
}

static cell_t CObjectInfoFind(IPluginContext *pContext, const cell_t *params)
{
	char *name = nullptr;
	pContext->LocalToString(params[1], &name);
	
	for(auto &it : g_ObjectInfos) {
		if(Q_stricmp(it->m_pObjectName, name) == 0) {
			return (cell_t)it.get();
		}
	}
	
	return 0;
}

static cell_t CObjectInfoGetIndex(IPluginContext *pContext, const cell_t *params)
{
	char *name = nullptr;
	pContext->LocalToString(params[1], &name);
	
	for(int i = 0; i < g_ObjectInfos.size(); ++i) {
		auto &it = g_ObjectInfos[i];
		if(Q_stricmp(it->m_pObjectName, name) == 0) {
			return i;
		}
	}
	
	return -1;
}

static cell_t CObjectInfoGet(IPluginContext *pContext, const cell_t *params)
{
	if(params[1] < 0 || params[1] >= g_ObjectInfos.size()) {
		return pContext->ThrowNativeError("Invalid Index %i", params[1]);
	}
	
	return (cell_t)g_ObjectInfos[params[1]].get();
}

static cell_t CObjectInfoRemoveByIndex(IPluginContext *pContext, const cell_t *params)
{
	if(params[1] < 0 || params[1] >= g_ObjectInfos.size()) {
		return pContext->ThrowNativeError("Invalid Index %i", params[1]);
	}
	
	g_ObjectInfos.erase(g_ObjectInfos.begin() + params[1]);
	
	UpdateObjectOffsets();
	
	return 0;
}

static cell_t CObjectInfoRemoveByName(IPluginContext *pContext, const cell_t *params)
{
	char *name = nullptr;
	pContext->LocalToString(params[1], &name);
	
	auto it = g_ObjectInfos.begin();
	while(it != g_ObjectInfos.end()) {
		if(Q_stricmp((*it)->m_pObjectName, name) == 0) {
			g_ObjectInfos.erase(it);
			break;
		}
		++it;
	}
	
	UpdateObjectOffsets();
	
	return 0;
}

static cell_t CObjectInfoCloneByName(IPluginContext *pContext, const cell_t *params)
{
	char *name = nullptr;
	pContext->LocalToString(params[1], &name);
	
	cell_t ret = 0;
	
	auto it = g_ObjectInfos.begin();
	while(it != g_ObjectInfos.end()) {
		if(Q_stricmp((*it)->m_pObjectName, name) == 0) {
			g_ObjectInfos.emplace_back((*it)->clone());
			ret = (cell_t)g_ObjectInfos.back().get();
			break;
		}
		++it;
	}
	
	UpdateObjectOffsets();
	
	return ret;
}

static cell_t CObjectInfoCloneByIndex(IPluginContext *pContext, const cell_t *params)
{
	if(params[1] < 0 || params[1] >= g_ObjectInfos.size()) {
		return pContext->ThrowNativeError("Invalid Index %i", params[1]);
	}
	
	g_ObjectInfos.emplace_back(g_ObjectInfos[params[1]].get()->clone());
	
	UpdateObjectOffsets();
	
	return (cell_t)g_ObjectInfos.back().get();
}

static cell_t TFPlayerClassData_tFind(IPluginContext *pContext, const cell_t *params)
{
	char *name = nullptr;
	pContext->LocalToString(params[1], &name);
	
	for(auto &it : m_aTFPlayerClassData) {
		if(Q_stricmp(it->m_szClassName, name) == 0) {
			return (cell_t)it.get();
		}
	}
	
	return 0;
}

static cell_t TFPlayerClassData_tGetIndex(IPluginContext *pContext, const cell_t *params)
{
	char *name = nullptr;
	pContext->LocalToString(params[1], &name);
	
	for(int i = 0; i < m_aTFPlayerClassData.size(); ++i) {
		auto &it = m_aTFPlayerClassData[i];
		if(Q_stricmp(it->m_szClassName, name) == 0) {
			return i;
		}
	}
	
	return -1;
}

static cell_t TFPlayerClassData_tGet(IPluginContext *pContext, const cell_t *params)
{
	if(params[1] < 0 || params[1] >= m_aTFPlayerClassData.size()) {
		return pContext->ThrowNativeError("Invalid Index %i", params[1]);
	}
	
	return (cell_t)m_aTFPlayerClassData[params[1]].get();
}

static cell_t TFPlayerClassData_tRemoveByIndex(IPluginContext *pContext, const cell_t *params)
{
	if(params[1] < 0 || params[1] >= m_aTFPlayerClassData.size()) {
		return pContext->ThrowNativeError("Invalid Index %i", params[1]);
	}
	
	m_aTFPlayerClassData.erase(m_aTFPlayerClassData.begin() + params[1]);
	
	UpdateClassOffsets();
	
	return 0;
}

static cell_t TFPlayerClassData_tRemoveByName(IPluginContext *pContext, const cell_t *params)
{
	char *name = nullptr;
	pContext->LocalToString(params[1], &name);
	
	auto it = m_aTFPlayerClassData.begin();
	while(it != m_aTFPlayerClassData.end()) {
		if(Q_stricmp((*it)->m_szClassName, name) == 0) {
			m_aTFPlayerClassData.erase(it);
			break;
		}
		++it;
	}
	
	UpdateClassOffsets();
	
	return 0;
}

static cell_t TFPlayerClassData_tCloneByName(IPluginContext *pContext, const cell_t *params)
{
	char *name = nullptr;
	pContext->LocalToString(params[1], &name);
	
	cell_t ret = 0;
	
	auto it = m_aTFPlayerClassData.begin();
	while(it != m_aTFPlayerClassData.end()) {
		if(Q_stricmp((*it)->m_szClassName, name) == 0) {
			m_aTFPlayerClassData.emplace_back((*it)->clone());
			ret = (cell_t)m_aTFPlayerClassData.back().get();
			break;
		}
		++it;
	}
	
	UpdateClassOffsets();
	
	return ret;
}

static cell_t TFPlayerClassData_tCloneByIndex(IPluginContext *pContext, const cell_t *params)
{
	if(params[1] < 0 || params[1] >= m_aTFPlayerClassData.size()) {
		return pContext->ThrowNativeError("Invalid Index %i", params[1]);
	}
	
	m_aTFPlayerClassData.emplace_back(m_aTFPlayerClassData[params[1]].get()->clone());
	
	UpdateClassOffsets();
	
	return (cell_t)m_aTFPlayerClassData.back().get();
}

static cell_t TFPlayerClassData_tGetString(IPluginContext *pContext, const cell_t *params)
{
	TFPlayerClassData_t *pInfo = (TFPlayerClassData_t *)params[1];
	
	char *name = nullptr;
	pContext->LocalToString(params[2], &name);
	
	if(Q_stricmp(name, "m_szClassName") == 0) {
		pContext->StringToLocal(params[3], params[4], pInfo->m_szClassName);
	} else if(Q_stricmp(name, "m_szModelName") == 0) {
		pContext->StringToLocal(params[3], params[4], pInfo->m_szModelName);
	} else if(Q_stricmp(name, "m_szHWMModelName") == 0) {
		pContext->StringToLocal(params[3], params[4], pInfo->m_szHWMModelName);
	} else if(Q_stricmp(name, "m_szHandModelName") == 0) {
		pContext->StringToLocal(params[3], params[4], pInfo->m_szHandModelName);
	} else if(Q_stricmp(name, "m_szLocalizableName") == 0) {
		pContext->StringToLocal(params[3], params[4], pInfo->m_szLocalizableName);
	} else if(Q_stricmp(name, "m_szDeathSound") == 0) {
		pContext->StringToLocal(params[3], params[4], pInfo->m_szDeathSound[params[5]]);
	}
	
	return 0;
}

static cell_t CObjectInfoGetString(IPluginContext *pContext, const cell_t *params)
{
	CObjectInfo *pInfo = (CObjectInfo *)params[1];
	
	char *name = nullptr;
	pContext->LocalToString(params[2], &name);
	
#define STROREMPTY(var) var ? var : ""
	
	if(Q_stricmp(name, "m_pObjectName") == 0) {
		pContext->StringToLocal(params[3], params[4], STROREMPTY(pInfo->m_pObjectName));
	} else if(Q_stricmp(name, "m_pClassName") == 0) {
		pContext->StringToLocal(params[3], params[4], STROREMPTY(pInfo->m_pClassName));
	} else if(Q_stricmp(name, "m_pStatusName") == 0) {
		pContext->StringToLocal(params[3], params[4], STROREMPTY(pInfo->m_pStatusName));
	} else if(Q_stricmp(name, "m_pBuilderWeaponName") == 0) {
		pContext->StringToLocal(params[3], params[4], STROREMPTY(pInfo->m_pBuilderWeaponName));
	} else if(Q_stricmp(name, "m_pBuilderPlacementString") == 0) {
		pContext->StringToLocal(params[3], params[4], STROREMPTY(pInfo->m_pBuilderPlacementString));
	} else if(Q_stricmp(name, "m_pViewModel") == 0) {
		pContext->StringToLocal(params[3], params[4], STROREMPTY(pInfo->m_pViewModel));
	} else if(Q_stricmp(name, "m_pPlayerModel") == 0) {
		pContext->StringToLocal(params[3], params[4], STROREMPTY(pInfo->m_pPlayerModel));
	} else if(Q_stricmp(name, "m_pExplodeSound") == 0) {
		pContext->StringToLocal(params[3], params[4], STROREMPTY(pInfo->m_pExplodeSound));
	} else if(Q_stricmp(name, "m_pExplosionParticleEffect") == 0) {
		pContext->StringToLocal(params[3], params[4], STROREMPTY(pInfo->m_pExplosionParticleEffect));
	} else if(Q_stricmp(name, "m_pUpgradeSound") == 0) {
		pContext->StringToLocal(params[3], params[4], STROREMPTY(pInfo->m_pUpgradeSound));
	} else if(Q_stricmp(name, "m_pIconActive") == 0) {
		pContext->StringToLocal(params[3], params[4], STROREMPTY(pInfo->m_pIconActive));
	} else if(Q_stricmp(name, "m_pIconInactive") == 0) {
		pContext->StringToLocal(params[3], params[4], STROREMPTY(pInfo->m_pIconInactive));
	} else if(Q_stricmp(name, "m_pIconMenu") == 0) {
		pContext->StringToLocal(params[3], params[4], STROREMPTY(pInfo->m_pIconMenu));
	} else if(Q_stricmp(name, "m_pHudStatusIcon") == 0) {
		pContext->StringToLocal(params[3], params[4], STROREMPTY(pInfo->m_pHudStatusIcon));
	} else if(Q_stricmp(name, "m_AltModes") == 0) {
		#define GETOBJINFO_ALTMODE(i) \
			case i##0: { \
				pContext->StringToLocal(params[3], params[4], STROREMPTY(pInfo->m_AltModes[i].pszStatusName)); \
				break; \
			} \
			case i##1: { \
				pContext->StringToLocal(params[3], params[4], STROREMPTY(pInfo->m_AltModes[i].pszModeName)); \
				break; \
			} \
			case i##2: { \
				pContext->StringToLocal(params[3], params[4], STROREMPTY(pInfo->m_AltModes[i].pszIconMenu)); \
				break; \
			}
		
		switch(params[4]) {
			case 0: {
				pContext->StringToLocal(params[3], params[4], STROREMPTY(pInfo->m_AltModes[0].pszStatusName));
				break;
			}
			case 1: {
				pContext->StringToLocal(params[3], params[4], STROREMPTY(pInfo->m_AltModes[0].pszModeName));
				break;
			}
			case 2: {
				pContext->StringToLocal(params[3], params[4], STROREMPTY(pInfo->m_AltModes[0].pszIconMenu));
				break;
			}
			GETOBJINFO_ALTMODE(1)
			GETOBJINFO_ALTMODE(2)
		}
	}
	
	return 0;
}

static cell_t TFPlayerClassData_tSetString(IPluginContext *pContext, const cell_t *params)
{
	TFPlayerClassData_t *pInfo = (TFPlayerClassData_t *)params[1];
	
	char *name = nullptr;
	pContext->LocalToString(params[2], &name);
	
	char *value = nullptr;
	pContext->LocalToString(params[3], &value);
	
	if(Q_stricmp(name, "m_szClassName") == 0) {
		Q_strncpy(pInfo->m_szClassName, value, TF_NAME_LENGTH);
	} else if(Q_stricmp(name, "m_szModelName") == 0) {
		Q_strncpy(pInfo->m_szModelName, value, TF_NAME_LENGTH);
	} else if(Q_stricmp(name, "m_szHWMModelName") == 0) {
		Q_strncpy(pInfo->m_szHWMModelName, value, TF_NAME_LENGTH);
	} else if(Q_stricmp(name, "m_szHandModelName") == 0) {
		Q_strncpy(pInfo->m_szHandModelName, value, TF_NAME_LENGTH);
	} else if(Q_stricmp(name, "m_szLocalizableName") == 0) {
		Q_strncpy(pInfo->m_szLocalizableName, value, TF_NAME_LENGTH);
	} else if(Q_stricmp(name, "m_szDeathSound") == 0) {
		Q_strncpy(pInfo->m_szDeathSound[params[4]], value, MAX_PLAYERCLASS_SOUND_LENGTH);
	}
	
	return 0;
}

static cell_t CObjectInfoSetString(IPluginContext *pContext, const cell_t *params)
{
	CObjectInfo *pInfo = (CObjectInfo *)params[1];
	
	char *name = nullptr;
	pContext->LocalToString(params[2], &name);
	
	char *value = nullptr;
	pContext->LocalToString(params[3], &value);
	
	if(Q_stricmp(name, "m_pObjectName") == 0) {
		if(pInfo->m_pObjectName) {
			delete[] pInfo->m_pObjectName;
		}
		pInfo->m_pObjectName = ReadAndAllocString(value);
	} else if(Q_stricmp(name, "m_pClassName") == 0) {
		if(pInfo->m_pClassName) {
			delete[] pInfo->m_pClassName;
		}
		pInfo->m_pClassName = ReadAndAllocString(value);
	} else if(Q_stricmp(name, "m_pStatusName") == 0) {
		if(pInfo->m_pStatusName) {
			delete[] pInfo->m_pStatusName;
		}
		pInfo->m_pStatusName = ReadAndAllocString(value);
	} else if(Q_stricmp(name, "m_pBuilderWeaponName") == 0) {
		if(pInfo->m_pBuilderWeaponName) {
			delete[] pInfo->m_pBuilderWeaponName;
		}
		pInfo->m_pBuilderWeaponName = ReadAndAllocString(value);
	} else if(Q_stricmp(name, "m_pBuilderPlacementString") == 0) {
		if(pInfo->m_pBuilderPlacementString) {
			delete[] pInfo->m_pBuilderPlacementString;
		}
		pInfo->m_pBuilderPlacementString = ReadAndAllocString(value);
	} else if(Q_stricmp(name, "m_pViewModel") == 0) {
		if(pInfo->m_pViewModel) {
			delete[] pInfo->m_pViewModel;
		}
		pInfo->m_pViewModel = ReadAndAllocString(value);
	} else if(Q_stricmp(name, "m_pPlayerModel") == 0) {
		if(pInfo->m_pPlayerModel) {
			delete[] pInfo->m_pPlayerModel;
		}
		pInfo->m_pPlayerModel = ReadAndAllocString(value);
	} else if(Q_stricmp(name, "m_pExplodeSound") == 0) {
		if(pInfo->m_pExplodeSound) {
			delete[] pInfo->m_pExplodeSound;
		}
		pInfo->m_pExplodeSound = ReadAndAllocString(value);
	} else if(Q_stricmp(name, "m_pExplosionParticleEffect") == 0) {
		if(pInfo->m_pExplosionParticleEffect) {
			delete[] pInfo->m_pExplosionParticleEffect;
		}
		pInfo->m_pExplosionParticleEffect = ReadAndAllocString(value);
	} else if(Q_stricmp(name, "m_pUpgradeSound") == 0) {
		if(pInfo->m_pUpgradeSound) {
			delete[] pInfo->m_pUpgradeSound;
		}
		pInfo->m_pUpgradeSound = ReadAndAllocString(value);
	} else if(Q_stricmp(name, "m_pIconActive") == 0) {
		if(pInfo->m_pIconActive) {
			delete[] pInfo->m_pIconActive;
		}
		pInfo->m_pIconActive = ReadAndAllocString(value);
	} else if(Q_stricmp(name, "m_pIconInactive") == 0) {
		if(pInfo->m_pIconInactive) {
			delete[] pInfo->m_pIconInactive;
		}
		pInfo->m_pIconInactive = ReadAndAllocString(value);
	} else if(Q_stricmp(name, "m_pIconMenu") == 0) {
		if(pInfo->m_pIconMenu) {
			delete[] pInfo->m_pIconMenu;
		}
		pInfo->m_pIconMenu = ReadAndAllocString(value);
	} else if(Q_stricmp(name, "m_pHudStatusIcon") == 0) {
		if(pInfo->m_pHudStatusIcon) {
			delete[] pInfo->m_pHudStatusIcon;
		}
		pInfo->m_pHudStatusIcon = ReadAndAllocString(value);
	} else if(Q_stricmp(name, "m_AltModes") == 0) {
		#define SETOBJINFO_STR(var1, var2) \
			if(pInfo->var1.var2) { \
				delete[] pInfo->var1.var2; \
			} \
			pInfo->var1.var2 = ReadAndAllocString(value);
			
		#define SETOBJINFO_ALTMODE(i) \
			case i##0: { \
				SETOBJINFO_STR(m_AltModes[i], pszStatusName) \
				break; \
			} \
			case i##1: { \
				SETOBJINFO_STR(m_AltModes[i], pszModeName) \
				break; \
			} \
			case i##2: { \
				SETOBJINFO_STR(m_AltModes[i], pszIconMenu) \
				break; \
			}
		
		switch(params[4]) {
			case 0: {
				SETOBJINFO_STR(m_AltModes[0], pszStatusName)
				break;
			}
			case 1: {
				SETOBJINFO_STR(m_AltModes[0], pszModeName)
				break;
			}
			case 2: {
				SETOBJINFO_STR(m_AltModes[0], pszIconMenu)
				break;
			}
			SETOBJINFO_ALTMODE(1)
			SETOBJINFO_ALTMODE(2)
		}
	}
	
	return 0;
}

static cell_t CObjectInfoSetFloat(IPluginContext *pContext, const cell_t *params)
{
	CObjectInfo *pInfo = (CObjectInfo *)params[1];
	
	char *name = nullptr;
	pContext->LocalToString(params[2], &name);
	
	float value = sp_ctof(params[3]);
	
	if(Q_stricmp(name, "m_CostMultiplierPerInstance") == 0) {
		pInfo->m_CostMultiplierPerInstance = value;
	} else if(Q_stricmp(name, "m_flUpgradeDuration") == 0) {
		pInfo->m_flUpgradeDuration = value;
	} else if(Q_stricmp(name, "m_flBuildTime") == 0) {
		pInfo->m_flBuildTime = value;
	}
	
	return 0;
}

static cell_t TFPlayerClassData_tSetFloat(IPluginContext *pContext, const cell_t *params)
{
	TFPlayerClassData_t *pInfo = (TFPlayerClassData_t *)params[1];
	
	char *name = nullptr;
	pContext->LocalToString(params[2], &name);
	
	float value = sp_ctof(params[3]);
	
	if(Q_stricmp(name, "m_flMaxSpeed") == 0) {
		pInfo->m_flMaxSpeed = value;
	} else if(Q_stricmp(name, "m_vecThirdPersonOffset") == 0) {
		pInfo->m_vecThirdPersonOffset[params[4]] = value;
	}
	
	return 0;
}

static cell_t CObjectInfoGetFloat(IPluginContext *pContext, const cell_t *params)
{
	CObjectInfo *pInfo = (CObjectInfo *)params[1];
	
	char *name = nullptr;
	pContext->LocalToString(params[2], &name);
	
	if(Q_stricmp(name, "m_CostMultiplierPerInstance") == 0) {
		return sp_ftoc(pInfo->m_CostMultiplierPerInstance);
	} else if(Q_stricmp(name, "m_flUpgradeDuration") == 0) {
		return sp_ftoc(pInfo->m_flUpgradeDuration);
	} else if(Q_stricmp(name, "m_flBuildTime") == 0) {
		return sp_ftoc(pInfo->m_flBuildTime);
	}
	
	return 0;
}

static cell_t TFPlayerClassData_tGetFloat(IPluginContext *pContext, const cell_t *params)
{
	TFPlayerClassData_t *pInfo = (TFPlayerClassData_t *)params[1];
	
	char *name = nullptr;
	pContext->LocalToString(params[2], &name);
	
	if(Q_stricmp(name, "m_flMaxSpeed") == 0) {
		return sp_ftoc(pInfo->m_flMaxSpeed);
	} else if(Q_stricmp(name, "m_vecThirdPersonOffset") == 0) {
		return sp_ftoc(pInfo->m_vecThirdPersonOffset[params[3]]);
	}
	
	return 0;
}

static cell_t CObjectInfoSetInt(IPluginContext *pContext, const cell_t *params)
{
	CObjectInfo *pInfo = (CObjectInfo *)params[1];
	
	char *name = nullptr;
	pContext->LocalToString(params[2], &name);
	
	if(Q_stricmp(name, "m_nMaxObjects") == 0) {
		pInfo->m_nMaxObjects = params[3];
	} else if(Q_stricmp(name, "m_Cost") == 0) {
		pInfo->m_Cost = params[3];
	} else if(Q_stricmp(name, "m_UpgradeCost") == 0) {
		pInfo->m_UpgradeCost = params[3];
	} else if(Q_stricmp(name, "m_MaxUpgradeLevel") == 0) {
		pInfo->m_MaxUpgradeLevel = params[3];
	} else if(Q_stricmp(name, "m_SelectionSlot") == 0) {
		pInfo->m_SelectionSlot = params[3];
	} else if(Q_stricmp(name, "m_SelectionPosition") == 0) {
		pInfo->m_SelectionPosition = params[3];
	} else if(Q_stricmp(name, "m_bSolidToPlayerMovement") == 0) {
		pInfo->m_bSolidToPlayerMovement = params[3];
	} else if(Q_stricmp(name, "m_bUseItemInfo") == 0) {
		pInfo->m_bUseItemInfo = params[3];
	} else if(Q_stricmp(name, "m_iDisplayPriority") == 0) {
		pInfo->m_iDisplayPriority = params[3];
	} else if(Q_stricmp(name, "m_bVisibleInWeaponSelection") == 0) {
		pInfo->m_bVisibleInWeaponSelection = params[3];
	} else if(Q_stricmp(name, "m_bAutoSwitchTo") == 0) {
		pInfo->m_bAutoSwitchTo = params[3];
	} else if(Q_stricmp(name, "m_iBuildCount") == 0) {
		pInfo->m_iBuildCount = params[3];
	} else if(Q_stricmp(name, "m_iNumAltModes") == 0) {
		pInfo->m_iNumAltModes = params[3];
	} else if(Q_stricmp(name, "m_iMetalToDropInGibs") == 0) {
		pInfo->m_iMetalToDropInGibs = params[3];
	} else if(Q_stricmp(name, "m_bRequiresOwnBuilder") == 0) {
		pInfo->m_bRequiresOwnBuilder = params[3];
	} else if(Q_stricmp(name, "m_nRepresentative") == 0) {
		pInfo->m_nRepresentative = params[3];
	}
	
	return 0;
}

static cell_t TFPlayerClassData_tSetInt(IPluginContext *pContext, const cell_t *params)
{
	TFPlayerClassData_t *pInfo = (TFPlayerClassData_t *)params[1];
	
	char *name = nullptr;
	pContext->LocalToString(params[2], &name);
	
	if(Q_stricmp(name, "m_nMaxHealth") == 0) {
		pInfo->m_nMaxHealth = params[3];
	} else if(Q_stricmp(name, "m_nMaxArmor") == 0) {
		pInfo->m_nMaxArmor = params[3];
	} else if(Q_stricmp(name, "m_aWeapons") == 0) {
		pInfo->m_aWeapons[params[4]] = params[3];
	} else if(Q_stricmp(name, "m_aGrenades") == 0) {
		pInfo->m_aGrenades[params[4]] = params[3];
	} else if(Q_stricmp(name, "m_aAmmoMax") == 0) {
		pInfo->m_aAmmoMax[params[4]] = params[3];
	} else if(Q_stricmp(name, "m_aBuildable") == 0) {
		pInfo->m_aBuildable[params[4]] = params[3];
	} else if(Q_stricmp(name, "m_bDontDoAirwalk") == 0) {
		pInfo->m_bDontDoAirwalk = params[3];
	} else if(Q_stricmp(name, "m_bDontDoNewJump") == 0) {
		pInfo->m_bDontDoNewJump = params[3];
	} else if(Q_stricmp(name, "m_bParsed") == 0) {
		pInfo->m_bParsed = params[3];
	}
	
	return 0;
}

static cell_t CObjectInfoGetInt(IPluginContext *pContext, const cell_t *params)
{
	CObjectInfo *pInfo = (CObjectInfo *)params[1];
	
	char *name = nullptr;
	pContext->LocalToString(params[2], &name);
	
	if(Q_stricmp(name, "m_nMaxObjects") == 0) {
		return pInfo->m_nMaxObjects;
	} else if(Q_stricmp(name, "m_Cost") == 0) {
		return pInfo->m_Cost;
	} else if(Q_stricmp(name, "m_UpgradeCost") == 0) {
		return pInfo->m_UpgradeCost;
	} else if(Q_stricmp(name, "m_MaxUpgradeLevel") == 0) {
		return pInfo->m_MaxUpgradeLevel;
	} else if(Q_stricmp(name, "m_SelectionSlot") == 0) {
		return pInfo->m_SelectionSlot;
	} else if(Q_stricmp(name, "m_SelectionPosition") == 0) {
		return pInfo->m_SelectionPosition;
	} else if(Q_stricmp(name, "m_bSolidToPlayerMovement") == 0) {
		return pInfo->m_bSolidToPlayerMovement;
	} else if(Q_stricmp(name, "m_bUseItemInfo") == 0) {
		return pInfo->m_bUseItemInfo;
	} else if(Q_stricmp(name, "m_iDisplayPriority") == 0) {
		return pInfo->m_iDisplayPriority;
	} else if(Q_stricmp(name, "m_bVisibleInWeaponSelection") == 0) {
		return pInfo->m_bVisibleInWeaponSelection;
	} else if(Q_stricmp(name, "m_bAutoSwitchTo") == 0) {
		return pInfo->m_bAutoSwitchTo;
	} else if(Q_stricmp(name, "m_iBuildCount") == 0) {
		return pInfo->m_iBuildCount;
	} else if(Q_stricmp(name, "m_iNumAltModes") == 0) {
		return pInfo->m_iNumAltModes;
	} else if(Q_stricmp(name, "m_iMetalToDropInGibs") == 0) {
		return pInfo->m_iMetalToDropInGibs;
	} else if(Q_stricmp(name, "m_bRequiresOwnBuilder") == 0) {
		return pInfo->m_bRequiresOwnBuilder;
	} else if(Q_stricmp(name, "m_nRepresentative") == 0) {
		return pInfo->m_nRepresentative;
	}
	
	return 0;
}

static cell_t TFPlayerClassData_tGetInt(IPluginContext *pContext, const cell_t *params)
{
	TFPlayerClassData_t *pInfo = (TFPlayerClassData_t *)params[1];
	
	char *name = nullptr;
	pContext->LocalToString(params[2], &name);
	
	if(Q_stricmp(name, "m_nMaxHealth") == 0) {
		return pInfo->m_nMaxHealth;
	} else if(Q_stricmp(name, "m_nMaxArmor") == 0) {
		return pInfo->m_nMaxArmor;
	} else if(Q_stricmp(name, "m_aWeapons") == 0) {
		return pInfo->m_aWeapons[params[3]];
	} else if(Q_stricmp(name, "m_aGrenades") == 0) {
		return pInfo->m_aGrenades[params[3]];
	} else if(Q_stricmp(name, "m_aAmmoMax") == 0) {
		return pInfo->m_aAmmoMax[params[3]];
	} else if(Q_stricmp(name, "m_aBuildable") == 0) {
		return pInfo->m_aBuildable[params[3]];
	} else if(Q_stricmp(name, "m_bDontDoAirwalk") == 0) {
		return pInfo->m_bDontDoAirwalk;
	} else if(Q_stricmp(name, "m_bDontDoNewJump") == 0) {
		return pInfo->m_bDontDoNewJump;
	} else if(Q_stricmp(name, "m_bParsed") == 0) {
		return pInfo->m_bParsed;
	}
	
	return 0;
}

static cell_t TFPlayerClassData_tCount(IPluginContext *pContext, const cell_t *params)
{
	return m_aTFPlayerClassData.size();
}

static cell_t CObjectInfoCount(IPluginContext *pContext, const cell_t *params)
{
	return g_ObjectInfos.size();
}

static cell_t TFPlayerClassData_tClone(IPluginContext *pContext, const cell_t *params)
{
	TFPlayerClassData_t *pInfo = (TFPlayerClassData_t *)params[1];
	
	m_aTFPlayerClassData.emplace_back(pInfo->clone());
	
	UpdateClassOffsets();
	
	return (cell_t)m_aTFPlayerClassData.back().get();
}

static cell_t CObjectInfoClone(IPluginContext *pContext, const cell_t *params)
{
	CObjectInfo *pInfo = (CObjectInfo *)params[1];
	
	g_ObjectInfos.emplace_back(pInfo->clone());
	
	UpdateObjectOffsets();
	
	return (cell_t)g_ObjectInfos.back().get();
}

static cell_t CObjectInfoCreate(IPluginContext *pContext, const cell_t *params)
{
	KeyValues *pSub = g_pSM->ReadKeyValuesHandle(params[1], nullptr, false);
	if(!pSub) {
		return pContext->ThrowNativeError("Invalid KV Handle");
	}
	
	const char *name = pSub->GetName();
	
	g_ObjectInfos.emplace_back(new CObjectInfo{name});
	CObjectInfo *pInfo = g_ObjectInfos.back().get();

	LoadObjectInfo(pInfo, pSub, "");
	
	UpdateObjectOffsets();
	
	return (cell_t)pInfo;
}

static cell_t TFPlayerClassData_tCreate(IPluginContext *pContext, const cell_t *params)
{
	KeyValues *pSub = g_pSM->ReadKeyValuesHandle(params[1], nullptr, false);
	if(!pSub) {
		return pContext->ThrowNativeError("Invalid KV Handle");
	}
	
	m_aTFPlayerClassData.emplace_back(new TFPlayerClassData_t{});
	TFPlayerClassData_t *pClassData = m_aTFPlayerClassData.back().get();
	pClassData->ParseData( pSub );
	
	UpdateClassOffsets();
	
	return (cell_t)pClassData;
}

struct builder_vars_t
{
	std::unordered_map<int, bool> m_aBuildableObjectTypes{};
};

std::unordered_map<void *, builder_vars_t> buildervarsmap{};

static cell_t BuilderGetNumBuildables(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}
	
	cell_t ret = OBJ_LAST;
	
	auto it = buildervarsmap.find(pEntity);
	if(it != buildervarsmap.end()) {
		ret += it->second.m_aBuildableObjectTypes.size();
	}
	
	return ret;
}

static cell_t BuilderGetBuildableIndex(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}
	
	if(params[2] < OBJ_LAST) {
		return params[2];
	} else {
		auto it = buildervarsmap.find(pEntity);
		if(it != buildervarsmap.end()) {
			auto it2 = it->second.m_aBuildableObjectTypes.begin();
			std::advance(it2, params[2]-OBJ_LAST);
			return it2->first;
		} else {
			return -1;
		}
	}
}

int m_aBuildableObjectTypesOffset = -1;

static cell_t BuilderIsBuildable(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}
	
	if(params[2] < OBJ_LAST) {
		return *(bool *)((unsigned char *)pEntity + m_aBuildableObjectTypesOffset + params[2]);
	} else {
		auto it = buildervarsmap.find(pEntity);
		if(it != buildervarsmap.end()) {
			auto it2 = it->second.m_aBuildableObjectTypes.begin();
			std::advance(it2, params[2]-OBJ_LAST);
			return it2->second;
		} else {
			return 0;
		}
	}
}

static const sp_nativeinfo_t g_sNativesInfo[] =
{
	{"CObjectInfo.Find", CObjectInfoFind},
	{"CObjectInfo.GetIndex", CObjectInfoGetIndex},
	{"CObjectInfo.Get", CObjectInfoGet},
	{"CObjectInfo.RemoveByName", CObjectInfoRemoveByName},
	{"CObjectInfo.RemoveByIndex", CObjectInfoRemoveByIndex},
	{"CObjectInfo.CloneByName", CObjectInfoCloneByName},
	{"CObjectInfo.CloneByIndex", CObjectInfoCloneByIndex},
	{"CObjectInfo.SetString", CObjectInfoSetString},
	{"CObjectInfo.SetFloat", CObjectInfoSetFloat},
	{"CObjectInfo.SetInt", CObjectInfoSetInt},
	{"CObjectInfo.GetString", CObjectInfoGetString},
	{"CObjectInfo.GetFloat", CObjectInfoGetFloat},
	{"CObjectInfo.GetInt", CObjectInfoGetInt},
	{"CObjectInfo.Count", CObjectInfoCount},
	{"CObjectInfo.Create", CObjectInfoCreate},
	{"TFPlayerClassData_t.Find", TFPlayerClassData_tFind},
	{"TFPlayerClassData_t.GetIndex", TFPlayerClassData_tGetIndex},
	{"TFPlayerClassData_t.Get", TFPlayerClassData_tGet},
	{"TFPlayerClassData_t.RemoveByName", TFPlayerClassData_tRemoveByName},
	{"TFPlayerClassData_t.RemoveByIndex", TFPlayerClassData_tRemoveByIndex},
	{"TFPlayerClassData_t.CloneByName", TFPlayerClassData_tCloneByName},
	{"TFPlayerClassData_t.CloneByIndex", TFPlayerClassData_tCloneByIndex},
	{"TFPlayerClassData_t.SetString", TFPlayerClassData_tSetString},
	{"TFPlayerClassData_t.SetFloat", TFPlayerClassData_tSetFloat},
	{"TFPlayerClassData_t.SetInt", TFPlayerClassData_tSetInt},
	{"TFPlayerClassData_t.GetString", TFPlayerClassData_tGetString},
	{"TFPlayerClassData_t.GetFloat", TFPlayerClassData_tGetFloat},
	{"TFPlayerClassData_t.GetInt", TFPlayerClassData_tGetInt},
	{"TFPlayerClassData_t.Count", TFPlayerClassData_tCount},
	{"TFPlayerClassData_t.Clone", TFPlayerClassData_tClone},
	{"TFPlayerClassData_t.Create", TFPlayerClassData_tCreate},
	{"BuilderGetNumBuildables", BuilderGetNumBuildables},
	{"BuilderGetBuildableIndex", BuilderGetBuildableIndex},
	{"BuilderIsBuildable", BuilderIsBuildable},
	{nullptr, nullptr},
};

CDetour *pClassCanBuild = nullptr;

#include <sourcehook/sh_memory.h>

void *CTFWeaponBuilderPrecache = nullptr;
void *CTFPlayerManageBuilderWeapons = nullptr;
void *CTFPlayerCanBuild = nullptr;
void *CTFPlayerPrecachePlayerModels = nullptr;

int CTFWeaponBuilderPrecacheOBJ_LAST = -1;
int CTFPlayerManageBuilderWeaponsOBJ_LAST = -1;
int CTFPlayerPrecachePlayerModelsTF_CLASS_COUNT_ALL1 = -1;
int CTFPlayerPrecachePlayerModelsTF_CLASS_COUNT_ALL2 = -1;

bool g_bOffsetsInited = false;

void UpdateObjectOffsets()
{
	if(!g_bOffsetsInited) {
		return;
	}
	
	size_t size = g_ObjectInfos.size();
	
	*(unsigned char *)((unsigned char *)CTFWeaponBuilderPrecache + CTFWeaponBuilderPrecacheOBJ_LAST) = size;
	*(unsigned char *)((unsigned char *)CTFPlayerManageBuilderWeapons + CTFPlayerManageBuilderWeaponsOBJ_LAST) = size;
}

void UpdateClassOffsets()
{
	if(!g_bOffsetsInited) {
		return;
	}
	
	size_t size = m_aTFPlayerClassData.size();
	
	*(unsigned char *)((unsigned char *)CTFPlayerPrecachePlayerModels + CTFPlayerPrecachePlayerModelsTF_CLASS_COUNT_ALL1) = size;
	*(unsigned char *)((unsigned char *)CTFPlayerPrecachePlayerModels + CTFPlayerPrecachePlayerModelsTF_CLASS_COUNT_ALL2) = size;
}

DETOUR_DECL_STATIC2(ClassCanBuild, bool, int, iClass, int, iObjectType)
{
	if(iClass == TF_CLASS_ENGINEER) {
		return true;
	}
	
	for ( int i = 0; i < TF_PLAYER_BLUEPRINT_COUNT; i++ )
	{
		if ( m_aTFPlayerClassData[iClass]->m_aBuildable[i] == iObjectType )
			return true;
	}
	
	return false;
}

CDetour *pCTFWeaponBuilderCanBuildObjectType = nullptr;
CDetour *pCTFWeaponBuilderSetObjectTypeAsBuildable = nullptr;
CDetour *pCTFWeaponBuilderCTOR = nullptr;

SH_DECL_MANUALHOOK0_void(GenericDtor, 0, 0, 0)

void HookBuilderDtor()
{
	void *ptr = META_IFACEPTR(void);
	
	buildervarsmap.erase(ptr);
	
	SH_REMOVE_MANUALHOOK(GenericDtor, ptr, SH_STATIC(HookBuilderDtor), false);
	
	RETURN_META(MRES_IGNORED);
}

DETOUR_DECL_MEMBER0(CTFWeaponBuilderCTOR, void)
{
	DETOUR_MEMBER_CALL(CTFWeaponBuilderCTOR)();
	
	buildervarsmap.emplace(this, builder_vars_t{});
	
	SH_ADD_MANUALHOOK(GenericDtor, this, SH_STATIC(HookBuilderDtor), false);
}

DETOUR_DECL_MEMBER1(CTFWeaponBuilderCanBuildObjectType, bool, int, iObjectType)
{
	if ( iObjectType < 0 || iObjectType >= g_ObjectInfos.size() )
		return false;
	
	bool value = false;
	
	if( iObjectType < OBJ_LAST ) {
		value = *(bool *)((unsigned char *)this + m_aBuildableObjectTypesOffset + iObjectType);
	} else {
		value = buildervarsmap[this].m_aBuildableObjectTypes[iObjectType];
	}
	
	return value;
}

void *CTFWeaponBuilderSetSubType = nullptr;

class CBaseEntity : public IServerEntity {};

void SetEdictStateChanged(CBaseEntity *pEntity, int offset)
{
	IServerNetworkable *pNet = pEntity->GetNetworkable();
	edict_t *edict = pNet->GetEdict();
	
	gamehelpers->SetEdictStateChanged(edict, offset);
}

DETOUR_DECL_MEMBER1(CTFWeaponBuilderSetObjectTypeAsBuildable, void, int, iObjectType)
{
	if ( iObjectType < 0 || iObjectType >= g_ObjectInfos.size() )
		return;
	
	if( iObjectType < OBJ_LAST ) {
		*(bool *)((unsigned char *)this + m_aBuildableObjectTypesOffset + iObjectType) = true;
		SetEdictStateChanged((CBaseEntity *)this, m_aBuildableObjectTypesOffset + iObjectType);
	} else {
		buildervarsmap[this].m_aBuildableObjectTypes[iObjectType] = true;
		
		CObjectInfo *pInfo = g_ObjectInfos[iObjectType].get();
		if(pInfo->m_nRepresentative != OBJ_LAST) {
			SetEdictStateChanged((CBaseEntity *)this, m_aBuildableObjectTypesOffset + pInfo->m_nRepresentative);
		}
	}
	
	call_mfunc<void, CBaseEntity, int>((CBaseEntity *)this, CTFWeaponBuilderSetSubType, iObjectType);
}

CDetour *pWriteUsercmd = nullptr;
CDetour *pReadUsercmd = nullptr;

#define WEAPON_SUBTYPE_BITS 6

DETOUR_DECL_STATIC3(WriteUsercmd, void, bf_write *, buf, const CUserCmd *, to, const CUserCmd *, from)
{
	if ( to->command_number != ( from->command_number + 1 ) )
	{
		buf->WriteOneBit( 1 );
		buf->WriteUBitLong( to->command_number, 32 );
	}
	else
	{
		buf->WriteOneBit( 0 );
	}

	if ( to->tick_count != ( from->tick_count + 1 ) )
	{
		buf->WriteOneBit( 1 );
		buf->WriteUBitLong( to->tick_count, 32 );
	}
	else
	{
		buf->WriteOneBit( 0 );
	}


	if ( to->viewangles[ 0 ] != from->viewangles[ 0 ] )
	{
		buf->WriteOneBit( 1 );
		buf->WriteFloat( to->viewangles[ 0 ] );
	}
	else
	{
		buf->WriteOneBit( 0 );
	}

	if ( to->viewangles[ 1 ] != from->viewangles[ 1 ] )
	{
		buf->WriteOneBit( 1 );
		buf->WriteFloat( to->viewangles[ 1 ] );
	}
	else
	{
		buf->WriteOneBit( 0 );
	}

	if ( to->viewangles[ 2 ] != from->viewangles[ 2 ] )
	{
		buf->WriteOneBit( 1 );
		buf->WriteFloat( to->viewangles[ 2 ] );
	}
	else
	{
		buf->WriteOneBit( 0 );
	}

	if ( to->forwardmove != from->forwardmove )
	{
		buf->WriteOneBit( 1 );
		buf->WriteFloat( to->forwardmove );
	}
	else
	{
		buf->WriteOneBit( 0 );
	}

	if ( to->sidemove != from->sidemove )
	{
		buf->WriteOneBit( 1 );
		buf->WriteFloat( to->sidemove );
	}
	else
	{
		buf->WriteOneBit( 0 );
	}

	if ( to->upmove != from->upmove )
	{
		buf->WriteOneBit( 1 );
		buf->WriteFloat( to->upmove );
	}
	else
	{
		buf->WriteOneBit( 0 );
	}

	if ( to->buttons != from->buttons )
	{
		buf->WriteOneBit( 1 );
	  	buf->WriteUBitLong( to->buttons, 32 );
 	}
	else
	{
		buf->WriteOneBit( 0 );
	}

	if ( to->impulse != from->impulse )
	{
		buf->WriteOneBit( 1 );
	    buf->WriteUBitLong( to->impulse, 8 );
	}
	else
	{
		buf->WriteOneBit( 0 );
	}


	if ( to->weaponselect != from->weaponselect )
	{
		buf->WriteOneBit( 1 );
		buf->WriteUBitLong( to->weaponselect, MAX_EDICT_BITS );

		if ( to->weaponsubtype != from->weaponsubtype )
		{
			buf->WriteOneBit( 1 );
			buf->WriteUBitLong( to->weaponsubtype, WEAPON_SUBTYPE_BITS );
		}
		else
		{
			buf->WriteOneBit( 0 );
		}
	}
	else
	{
		buf->WriteOneBit( 0 );
	}


	// TODO: Can probably get away with fewer bits.
	if ( to->mousedx != from->mousedx )
	{
		buf->WriteOneBit( 1 );
		buf->WriteShort( to->mousedx );
	}
	else
	{
		buf->WriteOneBit( 0 );
	}

	if ( to->mousedy != from->mousedy )
	{
		buf->WriteOneBit( 1 );
		buf->WriteShort( to->mousedy );
	}
	else
	{
		buf->WriteOneBit( 0 );
	}
}

DETOUR_DECL_STATIC3(ReadUsercmd, void, bf_read *, buf, CUserCmd *, move, CUserCmd *, from)
{
	*move = *from;

	if ( buf->ReadOneBit() )
	{
		move->command_number = buf->ReadUBitLong( 32 );
	}
	else
	{
		// Assume steady increment
		move->command_number = from->command_number + 1;
	}

	if ( buf->ReadOneBit() )
	{
		move->tick_count = buf->ReadUBitLong( 32 );
	}
	else
	{
		// Assume steady increment
		move->tick_count = from->tick_count + 1;
	}

	// Read direction
	if ( buf->ReadOneBit() )
	{
		move->viewangles[0] = buf->ReadFloat();
	}

	if ( buf->ReadOneBit() )
	{
		move->viewangles[1] = buf->ReadFloat();
	}

	if ( buf->ReadOneBit() )
	{
		move->viewangles[2] = buf->ReadFloat();
	}

	// Moved value validation and clamping to CBasePlayer::ProcessUsercmds()

	// Read movement
	if ( buf->ReadOneBit() )
	{
		move->forwardmove = buf->ReadFloat();
	}

	if ( buf->ReadOneBit() )
	{
		move->sidemove = buf->ReadFloat();
	}

	if ( buf->ReadOneBit() )
	{
		move->upmove = buf->ReadFloat();
	}

	// read buttons
	if ( buf->ReadOneBit() )
	{
		move->buttons = buf->ReadUBitLong( 32 );
	}

	if ( buf->ReadOneBit() )
	{
		move->impulse = buf->ReadUBitLong( 8 );
	}


	if ( buf->ReadOneBit() )
	{
		move->weaponselect = buf->ReadUBitLong( MAX_EDICT_BITS );
		if ( buf->ReadOneBit() )
		{
			move->weaponsubtype = buf->ReadUBitLong( WEAPON_SUBTYPE_BITS );
		}
	}


	move->random_seed = MD5_PseudoRandom( move->command_number ) & 0x7fffffff;

	if ( buf->ReadOneBit() )
	{
		move->mousedx = buf->ReadShort();
	}

	if ( buf->ReadOneBit() )
	{
		move->mousedy = buf->ReadShort();
	}
}

CDetour *pCTFPlayerCanBuild = nullptr;

enum
{
	CB_CAN_BUILD,			// Player is allowed to build this object
	CB_CANNOT_BUILD,		// Player is not allowed to build this object
	CB_LIMIT_REACHED,		// Player has reached the limit of the number of these objects allowed
	CB_NEED_RESOURCES,		// Player doesn't have enough resources to build this object
	CB_NEED_ADRENALIN,		// Commando doesn't have enough adrenalin to build a rally flag
	CB_UNKNOWN_OBJECT,		// Error message, tried to build unknown object
};

DETOUR_DECL_MEMBER2(CTFPlayerCanBuild, int, int, iObjectType, int, iObjectMode)
{
	//TODO!!! actually fix this instead of a workaround
	if(iObjectType > OBJ_LAST && iObjectType < g_ObjectInfos.size()) {
		CObjectInfo *pInfo = g_ObjectInfos[ iObjectType ].get();
		return DETOUR_MEMBER_CALL(CTFPlayerCanBuild)(pInfo->m_nRepresentative, iObjectMode);
	} else {
		return DETOUR_MEMBER_CALL(CTFPlayerCanBuild)(iObjectType, iObjectMode);
	}
}

bool Sample::SDK_OnLoad(char *error, size_t maxlen, bool late)
{
	gameconfs->LoadGameConfigFile("clsobj_hack", &g_pGameConf, error, maxlen);
	
	CDetourManager::Init(g_pSM->GetScriptingEngine(), g_pGameConf);
	
	pLoadObjectInfos = DETOUR_CREATE_STATIC(LoadObjectInfos, "LoadObjectInfos")
	pLoadObjectInfos->EnableDetour();
	
	pGetObjectInfo = DETOUR_CREATE_STATIC(GetObjectInfo, "GetObjectInfo")
	pGetObjectInfo->EnableDetour();
	
	pGetBuildableId = DETOUR_CREATE_STATIC(GetBuildableId, "GetBuildableId")
	pGetBuildableId->EnableDetour();
	
	pCTFPlayerClassDataMgrInit = DETOUR_CREATE_MEMBER(CTFPlayerClassDataMgrInit, "CTFPlayerClassDataMgr::Init")
	pCTFPlayerClassDataMgrInit->EnableDetour();
	
	pGetPlayerClassData = DETOUR_CREATE_STATIC(GetPlayerClassData, "GetPlayerClassData")
	pGetPlayerClassData->EnableDetour();
	
	pClassCanBuild = DETOUR_CREATE_STATIC(ClassCanBuild, "ClassCanBuild")
	pClassCanBuild->EnableDetour();
	
	pCTFPlayerClassDataMgrAddAdditionalPlayerDeathSounds = DETOUR_CREATE_MEMBER(CTFPlayerClassDataMgrAddAdditionalPlayerDeathSounds, "CTFPlayerClassDataMgr::AddAdditionalPlayerDeathSounds")
	pCTFPlayerClassDataMgrAddAdditionalPlayerDeathSounds->EnableDetour();
	
	pCTFWeaponBuilderCanBuildObjectType = DETOUR_CREATE_MEMBER(CTFWeaponBuilderCanBuildObjectType, "CTFWeaponBuilder::CanBuildObjectType")
	pCTFWeaponBuilderCanBuildObjectType->EnableDetour();
	
	pCTFWeaponBuilderSetObjectTypeAsBuildable = DETOUR_CREATE_MEMBER(CTFWeaponBuilderSetObjectTypeAsBuildable, "CTFWeaponBuilder::SetObjectTypeAsBuildable")
	pCTFWeaponBuilderSetObjectTypeAsBuildable->EnableDetour();
	
	pCTFWeaponBuilderCTOR = DETOUR_CREATE_MEMBER(CTFWeaponBuilderCTOR, "CTFWeaponBuilder::CTFWeaponBuilder")
	pCTFWeaponBuilderCTOR->EnableDetour();
	
	pCTFPlayerCanBuild = DETOUR_CREATE_MEMBER(CTFPlayerCanBuild, "CTFPlayer::CanBuild")
	pCTFPlayerCanBuild->EnableDetour();
	
	pWriteUsercmd = DETOUR_CREATE_STATIC(WriteUsercmd, "WriteUsercmd")
	pWriteUsercmd->EnableDetour();
	
	pReadUsercmd = DETOUR_CREATE_STATIC(ReadUsercmd, "ReadUsercmd")
	pReadUsercmd->EnableDetour();
	
	sm_sendprop_info_t info{};
	gamehelpers->FindSendPropInfo("CTFWeaponBuilder", "m_aBuildableObjectTypes", &info);
	m_aBuildableObjectTypesOffset = info.actual_offset;
	
	g_pGameConf->GetOffset("CTFWeaponBuilder::Precache::OBJ_LAST", &CTFWeaponBuilderPrecacheOBJ_LAST);
	g_pGameConf->GetOffset("CTFPlayer::ManageBuilderWeapons::OBJ_LAST", &CTFPlayerManageBuilderWeaponsOBJ_LAST);
	g_pGameConf->GetOffset("CTFPlayer::PrecachePlayerModels::TF_CLASS_COUNT_ALL::1", &CTFPlayerPrecachePlayerModelsTF_CLASS_COUNT_ALL1);
	g_pGameConf->GetOffset("CTFPlayer::PrecachePlayerModels::TF_CLASS_COUNT_ALL::2", &CTFPlayerPrecachePlayerModelsTF_CLASS_COUNT_ALL2);
	
	g_pGameConf->GetMemSig("CTFWeaponBuilder::Precache", &CTFWeaponBuilderPrecache);
	g_pGameConf->GetMemSig("CTFPlayer::ManageBuilderWeapons", &CTFPlayerManageBuilderWeapons);
	g_pGameConf->GetMemSig("CTFPlayer::CanBuild", &CTFPlayerCanBuild);
	g_pGameConf->GetMemSig("CTFPlayer::PrecachePlayerModels", &CTFPlayerPrecachePlayerModels);
	g_pGameConf->GetMemSig("CTFWeaponBuilder::SetSubType", &CTFWeaponBuilderSetSubType);
	
	SourceHook::SetMemAccess(CTFWeaponBuilderPrecache, CTFWeaponBuilderPrecacheOBJ_LAST + 4, SH_MEM_READ|SH_MEM_WRITE|SH_MEM_EXEC);
	SourceHook::SetMemAccess(CTFPlayerManageBuilderWeapons, CTFPlayerManageBuilderWeaponsOBJ_LAST + 4, SH_MEM_READ|SH_MEM_WRITE|SH_MEM_EXEC);
	SourceHook::SetMemAccess(CTFPlayerCanBuild, sizeof(void *), SH_MEM_READ|SH_MEM_WRITE|SH_MEM_EXEC);
	SourceHook::SetMemAccess(CTFPlayerPrecachePlayerModels, CTFPlayerPrecachePlayerModelsTF_CLASS_COUNT_ALL2 + 4, SH_MEM_READ|SH_MEM_WRITE|SH_MEM_EXEC);
	
	g_bOffsetsInited = true;
	
	UpdateObjectOffsets();
	UpdateClassOffsets();
	
	LoadExtraObjects();
	LoadExtraClasses();
	
	sharesys->AddNatives(myself, g_sNativesInfo);
	
	sharesys->RegisterLibrary(myself, "clsobj_hack");
	
	return true;
}

void Sample::SDK_OnUnload()
{
	pLoadObjectInfos->Destroy();
	pGetObjectInfo->Destroy();
	pGetBuildableId->Destroy();
	pCTFPlayerClassDataMgrInit->Destroy();
	pGetPlayerClassData->Destroy();
	pCTFPlayerClassDataMgrAddAdditionalPlayerDeathSounds->Destroy();
	pClassCanBuild->Destroy();
	pCTFWeaponBuilderCanBuildObjectType->Destroy();
	pCTFWeaponBuilderSetObjectTypeAsBuildable->Destroy();
	pCTFWeaponBuilderCTOR->Destroy();
	pWriteUsercmd->Destroy();
	pReadUsercmd->Destroy();
	pCTFPlayerCanBuild->Destroy();
	gameconfs->CloseGameConfigFile(g_pGameConf);
}
