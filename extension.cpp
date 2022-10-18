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
#include <vector>
#include <memory>
#include <icvar.h>

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
ICvar *icvar = nullptr;
CGlobalVars *gpGlobals = nullptr;

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

template <typename R, typename T, typename ...Args>
R call_vfunc(T *pThisPtr, size_t offset, Args ...args)
{
	void **vtable = *reinterpret_cast<void ***>(pThisPtr);
	void *vfunc = vtable[offset];
	
	return call_mfunc<R, T, Args...>(pThisPtr, vfunc, args...);
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

enum
{
	BUILDER_OBJECT_BITS = 8,
	BUILDER_INVALID_OBJECT = ((1 << BUILDER_OBJECT_BITS) - 1)
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
	
	int m_nBaseHealth = 100;
	
	int m_nIndex = OBJ_LAST;
	
	bool m_bCustom = false;
	
	class CObjectInfoCustom *clone();
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
void RemoveBuilderVars(int index);

struct ObjectDeleter
{
	void operator()(CObjectInfo *ptr) const;
};

using ObjectUniquePtr = std::unique_ptr<CObjectInfo, ObjectDeleter>;

class ObjectMap : private std::vector<ObjectUniquePtr>
{
public:
	using base = std::vector<ObjectUniquePtr>;
	
	void add(CObjectInfo *pInfo, bool update = true)
	{
		pInfo->m_nIndex = base::size();
		base::emplace_back(pInfo);
		if(update) {
			UpdateObjectOffsets();
		}
	}
	
	template <typename T>
	void remove(T it);
	
	void clear(bool update = true)
	{
		base::clear();
		if(update) {
			UpdateObjectOffsets();
		}
	}
	
	using base::operator[];
	using base::size;
	using base::begin;
	using base::end;
} g_ObjectInfos;

void UpdateBuilders();

class CObjectInfoCustom : public CObjectInfo
{
public:
	CObjectInfoCustom( const char *pObjectName )
		: CObjectInfo{pObjectName}
	{
		m_bCustom = true;
	}
	
	~CObjectInfoCustom()
	{
		RemoveBuilderVars(m_nIndex);
		
		if(freehndl) {
			if(hndl != BAD_HANDLE) {
				HandleSecurity security(pContext->GetIdentity(), myself->GetIdentity());
				handlesys->FreeHandle(hndl, &security);
			}
		}
		
		UpdateBuilders();
	}
	
	Handle_t hndl = BAD_HANDLE;
	IPluginContext *pContext = nullptr;
	bool freehndl = true;
};

template <typename T>
void ObjectMap::remove(T it)
{
	auto it2 = it;
	while(it2 != base::end()) {
		--(*it2)->m_nIndex;
		++it2;
	}
	base::erase(it);
	UpdateObjectOffsets();
}

void ObjectDeleter::operator()(CObjectInfo *ptr) const
{
	if(ptr->m_bCustom) {
		CObjectInfoCustom *pCustom = (CObjectInfoCustom *)ptr;
		delete pCustom;
	} else {
		delete ptr;
	}
}

CObjectInfoCustom *CObjectInfo::clone()
{
	CObjectInfoCustom *ret = new CObjectInfoCustom{m_pObjectName};
	
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
	ret->m_nBaseHealth = m_nBaseHealth;
	ret->m_bCustom = m_bCustom;
	
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
	m_bUseItemInfo = false;
	m_pIconActive = NULL;
	m_pIconInactive = NULL;
	m_pIconMenu = NULL;
	m_pHudStatusIcon = NULL;
	m_pViewModel = NULL;
	m_pPlayerModel = NULL;
	m_iDisplayPriority = 0;
	m_bVisibleInWeaponSelection = false;
	m_pExplodeSound = NULL;
	m_pUpgradeSound = NULL;
	m_pExplosionParticleEffect = NULL;
	m_bAutoSwitchTo = false;
	m_iBuildCount = 0;
	m_iNumAltModes = 0;
	m_bRequiresOwnBuilder = false;
	m_iMetalToDropInGibs = -9999;
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
	pInfo->m_nBaseHealth = pSub->GetInt( "BaseHealth", 100 );
	
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

#define OBJ_MAX_UPGRADE_LEVEL 3

void DoLoadObjectInfos(IBaseFileSystem *pFileSystem)
{
	g_ObjectInfos.clear(false);
	
	g_ObjectInfos.add(new CObjectInfo( "OBJ_DISPENSER" ), false);
	g_ObjectInfos.add(new CObjectInfo( "OBJ_TELEPORTER" ), false);
	g_ObjectInfos.add(new CObjectInfo( "OBJ_SENTRYGUN" ), false);
	g_ObjectInfos.add(new CObjectInfo( "OBJ_ATTACHMENT_SAPPER" ), false);
	
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
	
	g_ObjectInfos.add(new CObjectInfo( "OBJ_LAST" ), false);
	
	g_ObjectInfos[OBJ_LAST]->m_nBaseHealth = 0;
	
	g_ObjectInfos[OBJ_DISPENSER]->m_nRepresentative = OBJ_DISPENSER;
	g_ObjectInfos[OBJ_DISPENSER]->m_nBaseHealth = 150;
	g_ObjectInfos[OBJ_DISPENSER]->m_MaxUpgradeLevel = OBJ_MAX_UPGRADE_LEVEL;
	
	g_ObjectInfos[OBJ_TELEPORTER]->m_nRepresentative = OBJ_TELEPORTER;
	g_ObjectInfos[OBJ_TELEPORTER]->m_nBaseHealth = 150;
	g_ObjectInfos[OBJ_TELEPORTER]->m_MaxUpgradeLevel = OBJ_MAX_UPGRADE_LEVEL;
	
	g_ObjectInfos[OBJ_SENTRYGUN]->m_nRepresentative = OBJ_SENTRYGUN;
	g_ObjectInfos[OBJ_SENTRYGUN]->m_nBaseHealth = 150;
	g_ObjectInfos[OBJ_SENTRYGUN]->m_MaxUpgradeLevel = OBJ_MAX_UPGRADE_LEVEL;
	
	g_ObjectInfos[OBJ_ATTACHMENT_SAPPER]->m_nRepresentative = OBJ_ATTACHMENT_SAPPER;
	g_ObjectInfos[OBJ_ATTACHMENT_SAPPER]->m_nBaseHealth = 100;
	
	UpdateObjectOffsets();
	UpdateBuilders();

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
	const CObjectInfo *pInfo = g_ObjectInfos[iObject].get();
	return pInfo;
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

	int m_nRepresentative = TF_CLASS_UNDEFINED;

	int m_nIndex = TF_CLASS_COUNT_ALL;
	
	bool m_bCustom = false;
	
	TFPlayerClassData_t();
	const char *GetModelName() const;

	const char *GetDeathSound( int nType );

	void Parse( const char *pszClassName );
	void ParseData( KeyValues *pKeyValuesData );
	void AddAdditionalPlayerDeathSounds( void );
	
	class TFPlayerClassDataCustom *clone();
};

struct ClassDataDeleter
{
	void operator()(TFPlayerClassData_t *ptr) const;
};

using ClassDataUniquePtr = std::unique_ptr<TFPlayerClassData_t, ClassDataDeleter>;

void UpdateClassOffsets();

class ClassDataMap : private std::vector<ClassDataUniquePtr>
{
public:
	using base = std::vector<ClassDataUniquePtr>;
	
	void add(TFPlayerClassData_t *pInfo, bool update = true)
	{
		pInfo->m_nIndex = base::size();
		base::emplace_back(pInfo);
		if(update) {
			UpdateClassOffsets();
		}
	}
	
	template <typename T>
	void remove(T it);
	
	void clear(bool update = true)
	{
		base::clear();
		if(update) {
			UpdateClassOffsets();
		}
	}
	
	void resize(size_t size, bool update = true)
	{
		base::resize(size);
		if(update) {
			UpdateClassOffsets();
		}
	}
	
	using base::operator[];
	using base::size;
	using base::begin;
	using base::end;
} m_aTFPlayerClassData;

void *CTFPlayerManageBuilderWeaponsAddr = nullptr;
int m_iClassOffset = -1;

void UpdateBuilders()
{
	if(!playerhelpers || m_iClassOffset == -1) {
		return;
	}
	
	for(int i = 1; i <= playerhelpers->GetMaxClients(); ++i) {
		IGamePlayer *pPlayer = playerhelpers->GetGamePlayer(i);
		if(!pPlayer || !pPlayer->IsInGame()) {
			continue;
		}
		
		CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(pPlayer->GetIndex());
		int m_iClass = *(int *)((unsigned char *)pEntity + m_iClassOffset);
		if(m_iClass < TF_FIRST_NORMAL_CLASS || m_iClass >= TF_LAST_NORMAL_CLASS) {
			continue;
		}

		TFPlayerClassData_t *pClassData = m_aTFPlayerClassData[m_iClass].get();
		call_mfunc<void, CBaseEntity, TFPlayerClassData_t *>(pEntity, CTFPlayerManageBuilderWeaponsAddr, pClassData);
	}
}

class TFPlayerClassDataCustom : public TFPlayerClassData_t
{
public:
	TFPlayerClassDataCustom()
		: TFPlayerClassData_t{}
	{
		m_bCustom = true;
	}
	
	~TFPlayerClassDataCustom()
	{
		if(freehndl) {
			if(hndl != BAD_HANDLE) {
				HandleSecurity security(pContext->GetIdentity(), myself->GetIdentity());
				handlesys->FreeHandle(hndl, &security);
			}
		}
		
		UpdateBuilders();
	}
	
	Handle_t hndl = BAD_HANDLE;
	IPluginContext *pContext = nullptr;
	bool freehndl = true;
};

template <typename T>
void ClassDataMap::remove(T it)
{
	auto it2 = it;
	while(it2 != base::end()) {
		--(*it2)->m_nIndex;
		++it2;
	}
	base::erase(it);
	UpdateClassOffsets();
}

void ClassDataDeleter::operator()(TFPlayerClassData_t *ptr) const
{
	if(ptr->m_bCustom) {
		TFPlayerClassDataCustom *pCustom = (TFPlayerClassDataCustom *)ptr;
		delete pCustom;
	} else {
		delete ptr;
	}
}

TFPlayerClassDataCustom *TFPlayerClassData_t::clone()
{
	TFPlayerClassDataCustom *ret = new TFPlayerClassDataCustom();
	
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
	
	ret->m_nRepresentative = m_nRepresentative;
	ret->m_bCustom = m_bCustom;
	
	return ret;
}

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

void *GetWeaponIdAddr = nullptr;
void *GetAmmoNameAddr = nullptr;

template <typename T>
T void_to_func(void *ptr)
{
	union { T f; void *p; };
	p = ptr;
	return f;
}

const char *GetAmmoName( int iAmmoType )
{
	return (void_to_func<const char *(*)(int)>(GetAmmoNameAddr))(iAmmoType);
}

#define TF_WEAPON_NONE 0

int GetWeaponId( const char *pszWeaponName )
{
	return (void_to_func<int(*)(const char *)>(GetWeaponIdAddr))(pszWeaponName);
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

	m_nRepresentative = pKeyValuesData->GetInt( "Representative", TF_CLASS_UNDEFINED );

	// The file has been parsed.
	m_bParsed = true;
}

bool DoClassDataMgrInit()
{
	m_aTFPlayerClassData.clear(false);
	
	m_aTFPlayerClassData.resize(TF_CLASS_COUNT_ALL, false);
	
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
		m_aTFPlayerClassData[iClass]->Parse( s_aPlayerClassFiles[iClass] );
		m_aTFPlayerClassData[iClass]->m_nRepresentative = iClass;
	}
	
	UpdateClassOffsets();
	
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

ConVar *tf_cheapobjects = nullptr;

bool Sample::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	gpGlobals = ismm->GetCGlobals();
	GET_V_IFACE_ANY(GetEngineFactory, engine, IVEngineServer, INTERFACEVERSION_VENGINESERVER)
	GET_V_IFACE_CURRENT(GetEngineFactory, filesystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION)
	GET_V_IFACE_CURRENT(GetEngineFactory, icvar, ICvar, CVAR_INTERFACE_VERSION);
	g_pCVar = icvar;
	ConVar_Register(0, this);
	
	tf_cheapobjects = g_pCVar->FindVar("tf_cheapobjects");
	
	DoLoadObjectInfos(filesystem);
	
	return true;
}

bool Sample::RegisterConCommandBase(ConCommandBase *pCommand)
{
	META_REGCVAR(pCommand);
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
				
				CObjectInfo *pInfo = new CObjectInfo{name};
				g_ObjectInfos.add(pInfo, false);

				LoadObjectInfo(pInfo, pSub, pFilename);
			}
			
			pValues->deleteThis();
		}
		closedir(dir);
	}
	
	UpdateObjectOffsets();
	UpdateBuilders();
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
			
			TFPlayerClassData_t *pClassData = new TFPlayerClassData_t{};
			pClassData->ParseData( pValues );
			
			m_aTFPlayerClassData.add(pClassData, false);
			
			pValues->deleteThis();
		}
		closedir(dir);
	}
	
	UpdateClassOffsets();
	UpdateBuilders();
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
	
	return BUILDER_INVALID_OBJECT;
}

static cell_t CObjectInfoGet(IPluginContext *pContext, const cell_t *params)
{
	if(params[1] < 0 || params[1] >= g_ObjectInfos.size()) {
		return pContext->ThrowNativeError("Invalid Index %i", params[1]);
	}
	
	return (cell_t)g_ObjectInfos[params[1]].get();
}

HandleType_t objinfo_handle = 0;
HandleType_t classdata_handle = 0;

static cell_t CObjectInfoCloneByName(IPluginContext *pContext, const cell_t *params)
{
	char *name = nullptr;
	pContext->LocalToString(params[1], &name);
	
	auto it = g_ObjectInfos.begin();
	while(it != g_ObjectInfos.end()) {
		if(Q_stricmp((*it)->m_pObjectName, name) == 0) {
			CObjectInfoCustom *pInfo = (*it)->clone();
			g_ObjectInfos.add(pInfo);
			
			Handle_t hndl = handlesys->CreateHandle(objinfo_handle, pInfo, pContext->GetIdentity(), myself->GetIdentity(), nullptr);
			pInfo->hndl = hndl;
			pInfo->pContext = pContext;
			
			UpdateBuilders();
			
			return hndl;
		}
		++it;
	}
	
	return BAD_HANDLE;
}

static cell_t CObjectInfoCloneByIndex(IPluginContext *pContext, const cell_t *params)
{
	if(params[1] < 0 || params[1] >= g_ObjectInfos.size()) {
		return pContext->ThrowNativeError("Invalid Index %i", params[1]);
	}
	
	CObjectInfoCustom *pInfo = g_ObjectInfos[params[1]]->clone();
	g_ObjectInfos.add(pInfo);
	
	Handle_t hndl = handlesys->CreateHandle(objinfo_handle, pInfo, pContext->GetIdentity(), myself->GetIdentity(), nullptr);
	pInfo->hndl = hndl;
	pInfo->pContext = pContext;
	
	UpdateBuilders();
	
	return hndl;
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

static cell_t TFPlayerClassData_tCloneByName(IPluginContext *pContext, const cell_t *params)
{
	char *name = nullptr;
	pContext->LocalToString(params[1], &name);
	
	auto it = m_aTFPlayerClassData.begin();
	while(it != m_aTFPlayerClassData.end()) {
		if(Q_stricmp((*it)->m_szClassName, name) == 0) {
			TFPlayerClassDataCustom *pClassData = (*it)->clone();
			m_aTFPlayerClassData.add(pClassData);
			
			Handle_t hndl = handlesys->CreateHandle(classdata_handle, pClassData, pContext->GetIdentity(), myself->GetIdentity(), nullptr);
			pClassData->hndl = hndl;
			pClassData->pContext = pContext;
			
			UpdateBuilders();
			
			return hndl;
		}
		++it;
	}
	
	return BAD_HANDLE;
}

static cell_t TFPlayerClassData_tCloneByIndex(IPluginContext *pContext, const cell_t *params)
{
	if(params[1] < 0 || params[1] >= m_aTFPlayerClassData.size()) {
		return pContext->ThrowNativeError("Invalid Index %i", params[1]);
	}
	
	TFPlayerClassDataCustom *pClassData = m_aTFPlayerClassData[params[1]]->clone();
	m_aTFPlayerClassData.add(pClassData);
	
	Handle_t hndl = handlesys->CreateHandle(classdata_handle, pClassData, pContext->GetIdentity(), myself->GetIdentity(), nullptr);
	pClassData->hndl = hndl;
	pClassData->pContext = pContext;
	
	UpdateBuilders();
	
	return hndl;
}

static cell_t TFPlayerClassData_tGetString(IPluginContext *pContext, const cell_t *params)
{
	TFPlayerClassData_t *pInfo = (TFPlayerClassData_t *)params[1];
	
	char *name = nullptr;
	pContext->LocalToString(params[2], &name);

	size_t written = 0;

	if(Q_stricmp(name, "m_szClassName") == 0) {
		pContext->StringToLocalUTF8(params[3], params[4], pInfo->m_szClassName, &written);
	} else if(Q_stricmp(name, "m_szModelName") == 0) {
		pContext->StringToLocalUTF8(params[3], params[4], pInfo->m_szModelName, &written);
	} else if(Q_stricmp(name, "m_szHWMModelName") == 0) {
		pContext->StringToLocalUTF8(params[3], params[4], pInfo->m_szHWMModelName, &written);
	} else if(Q_stricmp(name, "m_szHandModelName") == 0) {
		pContext->StringToLocalUTF8(params[3], params[4], pInfo->m_szHandModelName, &written);
	} else if(Q_stricmp(name, "m_szLocalizableName") == 0) {
		pContext->StringToLocalUTF8(params[3], params[4], pInfo->m_szLocalizableName, &written);
	} else if(Q_stricmp(name, "m_szDeathSound") == 0) {
		pContext->StringToLocalUTF8(params[3], params[4], pInfo->m_szDeathSound[params[5]], &written);
	}
	
	return written;
}

static cell_t TFPlayerClassData_tIndexget(IPluginContext *pContext, const cell_t *params)
{
	TFPlayerClassData_t *pInfo = (TFPlayerClassData_t *)params[1];
	
	return pInfo->m_nIndex;
}

static cell_t CObjectInfoGetString(IPluginContext *pContext, const cell_t *params)
{
	CObjectInfo *pInfo = (CObjectInfo *)params[1];
	
	char *name = nullptr;
	pContext->LocalToString(params[2], &name);

	size_t written = 0;

#define STROREMPTY(var) var ? var : ""
	
	if(Q_stricmp(name, "m_pObjectName") == 0) {
		pContext->StringToLocalUTF8(params[3], params[4], STROREMPTY(pInfo->m_pObjectName), &written);
	} else if(Q_stricmp(name, "m_pClassName") == 0) {
		pContext->StringToLocalUTF8(params[3], params[4], STROREMPTY(pInfo->m_pClassName), &written);
	} else if(Q_stricmp(name, "m_pStatusName") == 0) {
		pContext->StringToLocalUTF8(params[3], params[4], STROREMPTY(pInfo->m_pStatusName), &written);
	} else if(Q_stricmp(name, "m_pBuilderWeaponName") == 0) {
		pContext->StringToLocalUTF8(params[3], params[4], STROREMPTY(pInfo->m_pBuilderWeaponName), &written);
	} else if(Q_stricmp(name, "m_pBuilderPlacementString") == 0) {
		pContext->StringToLocalUTF8(params[3], params[4], STROREMPTY(pInfo->m_pBuilderPlacementString), &written);
	} else if(Q_stricmp(name, "m_pViewModel") == 0) {
		pContext->StringToLocalUTF8(params[3], params[4], STROREMPTY(pInfo->m_pViewModel), &written);
	} else if(Q_stricmp(name, "m_pPlayerModel") == 0) {
		pContext->StringToLocalUTF8(params[3], params[4], STROREMPTY(pInfo->m_pPlayerModel), &written);
	} else if(Q_stricmp(name, "m_pExplodeSound") == 0) {
		pContext->StringToLocalUTF8(params[3], params[4], STROREMPTY(pInfo->m_pExplodeSound), &written);
	} else if(Q_stricmp(name, "m_pExplosionParticleEffect") == 0) {
		pContext->StringToLocalUTF8(params[3], params[4], STROREMPTY(pInfo->m_pExplosionParticleEffect), &written);
	} else if(Q_stricmp(name, "m_pUpgradeSound") == 0) {
		pContext->StringToLocalUTF8(params[3], params[4], STROREMPTY(pInfo->m_pUpgradeSound), &written);
	} else if(Q_stricmp(name, "m_pIconActive") == 0) {
		pContext->StringToLocalUTF8(params[3], params[4], STROREMPTY(pInfo->m_pIconActive), &written);
	} else if(Q_stricmp(name, "m_pIconInactive") == 0) {
		pContext->StringToLocalUTF8(params[3], params[4], STROREMPTY(pInfo->m_pIconInactive), &written);
	} else if(Q_stricmp(name, "m_pIconMenu") == 0) {
		pContext->StringToLocalUTF8(params[3], params[4], STROREMPTY(pInfo->m_pIconMenu), &written);
	} else if(Q_stricmp(name, "m_pHudStatusIcon") == 0) {
		pContext->StringToLocalUTF8(params[3], params[4], STROREMPTY(pInfo->m_pHudStatusIcon), &written);
	} else if(Q_stricmp(name, "m_AltModes") == 0) {
		#define GETOBJINFO_ALTMODE(i) \
			case i##0: { \
				pContext->StringToLocalUTF8(params[3], params[4], STROREMPTY(pInfo->m_AltModes[i].pszStatusName), &written); \
				break; \
			} \
			case i##1: { \
				pContext->StringToLocalUTF8(params[3], params[4], STROREMPTY(pInfo->m_AltModes[i].pszModeName), &written); \
				break; \
			} \
			case i##2: { \
				pContext->StringToLocalUTF8(params[3], params[4], STROREMPTY(pInfo->m_AltModes[i].pszIconMenu), &written); \
				break; \
			}
		
		switch(params[4]) {
			case 0: {
				pContext->StringToLocalUTF8(params[3], params[4], STROREMPTY(pInfo->m_AltModes[0].pszStatusName), &written);
				break;
			}
			case 1: {
				pContext->StringToLocalUTF8(params[3], params[4], STROREMPTY(pInfo->m_AltModes[0].pszModeName), &written);
				break;
			}
			case 2: {
				pContext->StringToLocalUTF8(params[3], params[4], STROREMPTY(pInfo->m_AltModes[0].pszIconMenu), &written);
				break;
			}
			GETOBJINFO_ALTMODE(1)
			GETOBJINFO_ALTMODE(2)
		}
	}
	
	return written;
}

static cell_t TFPlayerClassData_tSetString(IPluginContext *pContext, const cell_t *params)
{
	HandleSecurity security(pContext->GetIdentity(), myself->GetIdentity());
	
	TFPlayerClassDataCustom *pInfo = nullptr;
	HandleError err = handlesys->ReadHandle(params[1], classdata_handle, &security, (void **)&pInfo);
	if(err != HandleError_None)
	{
		return pContext->ThrowNativeError("Invalid Handle %x (error: %d)", params[1], err);
	}
	
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

static cell_t TFPlayerClassDataCustomBaseGet(IPluginContext *pContext, const cell_t *params)
{
	HandleSecurity security(pContext->GetIdentity(), myself->GetIdentity());
	
	TFPlayerClassDataCustom *pInfo = nullptr;
	HandleError err = handlesys->ReadHandle(params[1], classdata_handle, &security, (void **)&pInfo);
	if(err != HandleError_None)
	{
		return pContext->ThrowNativeError("Invalid Handle %x (error: %d)", params[1], err);
	}
	
	return (cell_t)pInfo;
}

static cell_t CObjectInfoSetString(IPluginContext *pContext, const cell_t *params)
{
	HandleSecurity security(pContext->GetIdentity(), myself->GetIdentity());
	
	CObjectInfoCustom *pInfo = nullptr;
	HandleError err = handlesys->ReadHandle(params[1], objinfo_handle, &security, (void **)&pInfo);
	if(err != HandleError_None)
	{
		return pContext->ThrowNativeError("Invalid Handle %x (error: %d)", params[1], err);
	}
	
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

static cell_t CObjectInfoCustomBaseGet(IPluginContext *pContext, const cell_t *params)
{
	HandleSecurity security(pContext->GetIdentity(), myself->GetIdentity());
	
	CObjectInfoCustom *pInfo = nullptr;
	HandleError err = handlesys->ReadHandle(params[1], objinfo_handle, &security, (void **)&pInfo);
	if(err != HandleError_None)
	{
		return pContext->ThrowNativeError("Invalid Handle %x (error: %d)", params[1], err);
	}
	
	return (cell_t)pInfo;
}

static cell_t CObjectInfoSetFloat(IPluginContext *pContext, const cell_t *params)
{
	HandleSecurity security(pContext->GetIdentity(), myself->GetIdentity());
	
	CObjectInfoCustom *pInfo = nullptr;
	HandleError err = handlesys->ReadHandle(params[1], objinfo_handle, &security, (void **)&pInfo);
	if(err != HandleError_None)
	{
		return pContext->ThrowNativeError("Invalid Handle %x (error: %d)", params[1], err);
	}
	
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
	HandleSecurity security(pContext->GetIdentity(), myself->GetIdentity());
	
	TFPlayerClassDataCustom *pInfo = nullptr;
	HandleError err = handlesys->ReadHandle(params[1], classdata_handle, &security, (void **)&pInfo);
	if(err != HandleError_None)
	{
		return pContext->ThrowNativeError("Invalid Handle %x (error: %d)", params[1], err);
	}
	
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
	
	return sp_ftoc(0.0f);
}

static cell_t CObjectInfoIndexget(IPluginContext *pContext, const cell_t *params)
{
	CObjectInfo *pInfo = (CObjectInfo *)params[1];
	return pInfo->m_nIndex;
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
	
	return sp_ftoc(0.0f);
}

static cell_t CObjectInfoSetInt(IPluginContext *pContext, const cell_t *params)
{
	HandleSecurity security(pContext->GetIdentity(), myself->GetIdentity());
	
	CObjectInfoCustom *pInfo = nullptr;
	HandleError err = handlesys->ReadHandle(params[1], objinfo_handle, &security, (void **)&pInfo);
	if(err != HandleError_None)
	{
		return pContext->ThrowNativeError("Invalid Handle %x (error: %d)", params[1], err);
	}
	
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
	} else if(Q_stricmp(name, "m_nBaseHealth") == 0) {
		pInfo->m_nBaseHealth = params[3];
	}
	
	return 0;
}

static cell_t TFPlayerClassData_tSetInt(IPluginContext *pContext, const cell_t *params)
{
	HandleSecurity security(pContext->GetIdentity(), myself->GetIdentity());
	
	TFPlayerClassDataCustom *pInfo = nullptr;
	HandleError err = handlesys->ReadHandle(params[1], classdata_handle, &security, (void **)&pInfo);
	if(err != HandleError_None)
	{
		return pContext->ThrowNativeError("Invalid Handle %x (error: %d)", params[1], err);
	}
	
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
		UpdateBuilders();
	} else if(Q_stricmp(name, "m_bDontDoAirwalk") == 0) {
		pInfo->m_bDontDoAirwalk = params[3];
	} else if(Q_stricmp(name, "m_bDontDoNewJump") == 0) {
		pInfo->m_bDontDoNewJump = params[3];
	} else if(Q_stricmp(name, "m_nRepresentative") == 0) {
		pInfo->m_nRepresentative = params[3];
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
	} else if(Q_stricmp(name, "m_nBaseHealth") == 0) {
		return pInfo->m_nBaseHealth;
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
	} else if(Q_stricmp(name, "m_nRepresentative") == 0) {
		return pInfo->m_nRepresentative;
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

static cell_t CObjectInfoCreate(IPluginContext *pContext, const cell_t *params)
{
	KeyValues *pSub = g_pSM->ReadKeyValuesHandle(params[1], nullptr, false);
	if(!pSub) {
		return pContext->ThrowNativeError("Invalid KV Handle");
	}
	
	const char *name = pSub->GetName();
	
	CObjectInfoCustom *pInfo = new CObjectInfoCustom{name};
	LoadObjectInfo(pInfo, pSub, "");
	g_ObjectInfos.add(pInfo);
	
	Handle_t hndl = handlesys->CreateHandle(objinfo_handle, pInfo, pContext->GetIdentity(), myself->GetIdentity(), nullptr);
	pInfo->hndl = hndl;
	pInfo->pContext = pContext;
	
	UpdateBuilders();
	
	return hndl;
}

static cell_t TFPlayerClassData_tCreate(IPluginContext *pContext, const cell_t *params)
{
	KeyValues *pSub = g_pSM->ReadKeyValuesHandle(params[1], nullptr, false);
	if(!pSub) {
		return pContext->ThrowNativeError("Invalid KV Handle");
	}
	
	TFPlayerClassDataCustom *pClassData = new TFPlayerClassDataCustom{};
	pClassData->ParseData( pSub );
	m_aTFPlayerClassData.add(pClassData);
	
	Handle_t hndl = handlesys->CreateHandle(classdata_handle, pClassData, pContext->GetIdentity(), myself->GetIdentity(), nullptr);
	pClassData->hndl = hndl;
	pClassData->pContext = pContext;
	
	UpdateBuilders();
	
	return hndl;
}

struct builder_vars_t
{
	std::unordered_map<int, int> m_aBuildableObjectTypes{};
};

std::unordered_map<int, builder_vars_t> buildervarsmap{};

void RemoveBuilderVars(int index)
{
	auto it1 = buildervarsmap.begin();
	while(it1 != buildervarsmap.end()) {
		auto &map1 = it1->second.m_aBuildableObjectTypes;
		auto it2 = map1.begin();
		while(it2 != map1.end()) {
			if(it2->first == index) {
				it2 = map1.erase(it2);
				continue;
			}
			++it2;
		}
		if(map1.empty()) {
			it1 = buildervarsmap.erase(it1);
			continue;
		}
		++it1;
	}
}

static cell_t BuilderGetNumBuildables(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}
	
	cell_t ret = OBJ_LAST;

	int ref = gamehelpers->EntityToReference(pEntity);
	
	auto it = buildervarsmap.find(ref);
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
		int ref = gamehelpers->EntityToReference(pEntity);

		auto it = buildervarsmap.find(ref);
		if(it != buildervarsmap.end()) {
			int idx = params[2]-OBJ_LAST;
			auto &map = it->second.m_aBuildableObjectTypes;
			if(idx < map.size()) {
				auto it2 = map.begin();
				std::advance(it2, idx);
				return it2->first;
			}
		}
	}

	return BUILDER_INVALID_OBJECT;
}

static cell_t BuilderIndexByRepresentative(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}

	int ref = gamehelpers->EntityToReference(pEntity);
	
	auto it = buildervarsmap.find(ref);
	if(it != buildervarsmap.end()) {
		auto &map = it->second.m_aBuildableObjectTypes;
		auto it2 = map.begin();
		while(it2 != map.end()) {
			if(it2->second == params[2]) {
				return it2->first;
			}
			++it2;
		}
	}
	
	return BUILDER_INVALID_OBJECT;
}

static cell_t BuilderRepresentativeByIndex(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}

	int ref = gamehelpers->EntityToReference(pEntity);
	
	auto it = buildervarsmap.find(ref);
	if(it != buildervarsmap.end()) {
		auto &map = it->second.m_aBuildableObjectTypes;
		auto it2 = map.find(params[2]);
		if(it2 != map.end()) {
			return it2->second;
		}
	}
	
	return BUILDER_INVALID_OBJECT;
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
		int ref = gamehelpers->EntityToReference(pEntity);

		auto it = buildervarsmap.find(ref);
		if(it != buildervarsmap.end()) {
			int idx = params[2]-OBJ_LAST;
			auto &map = it->second.m_aBuildableObjectTypes;
			return (idx < map.size());
		}
	}

	return 0;
}

static cell_t BuilderSetAsBuildable(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}
	
	if(params[2] < OBJ_LAST) {
		*(bool *)((unsigned char *)pEntity + m_aBuildableObjectTypesOffset + params[2]) = params[3];
	} else {
		int ref = gamehelpers->EntityToReference(pEntity);

		auto it = buildervarsmap.find(ref);
		if(it != buildervarsmap.end()) {
			auto &map = it->second.m_aBuildableObjectTypes;
			auto it2 = map.find(params[2]);
			if(params[3]) {
				if(it2 == map.end()) {
					CObjectInfo *pInfo = g_ObjectInfos[params[2]].get();
					
					if(pInfo->m_nRepresentative != OBJ_LAST) {
						map[params[2]] = pInfo->m_nRepresentative;
					} else {
						map[params[2]] = BUILDER_INVALID_OBJECT;
					}
				}
			} else {
				if(it2 != map.end()) {
					map.erase(it2);
				}
			}
		}
	}
	
	return 0;
}

static cell_t ManageBuilderWeaponsEx(IPluginContext *pContext, const cell_t *params)
{
	IGamePlayer *pPlayer = playerhelpers->GetGamePlayer(params[1]);
	if(!pPlayer) {
		return pContext->ThrowNativeError("Client index %d is invalid", params[1]);
	} else if(!pPlayer->IsInGame()) {
		return pContext->ThrowNativeError("Client %d is not in game", params[1]);
	}
	
	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(pPlayer->GetIndex());
	TFPlayerClassData_t *pClassData = (TFPlayerClassData_t *)params[2];
	call_mfunc<void, CBaseEntity, TFPlayerClassData_t *>(pEntity, CTFPlayerManageBuilderWeaponsAddr, pClassData);
	return 0;
}

int m_iMaxHealthOffset = -1;
int m_iHealthOffset = -1;

void SetEdictStateChanged(CBaseEntity *pEntity, int offset);

int CBaseEntityIsBaseObject = -1;

class CBaseEntity : public IServerEntity
{
public:
	void SetHealth(int hp)
	{
		if(m_iHealthOffset == -1) {
			sm_datatable_info_t info{};
			datamap_t *pMap = gamehelpers->GetDataMap(this);
			gamehelpers->FindDataMapInfo(pMap, "m_iHealth", &info);
			m_iHealthOffset = info.actual_offset;
		}
		
		*(int *)((unsigned char *)this + m_iHealthOffset) = hp;
		SetEdictStateChanged(this, m_iHealthOffset);
	}
	
	void SetMaxHealth(int hp)
	{
		if(m_iMaxHealthOffset == -1) {
			sm_datatable_info_t info{};
			datamap_t *pMap = gamehelpers->GetDataMap(this);
			gamehelpers->FindDataMapInfo(pMap, "m_iMaxHealth", &info);
			m_iMaxHealthOffset = info.actual_offset;
		}
		
		*(int *)((unsigned char *)this + m_iMaxHealthOffset) = hp;
		SetEdictStateChanged(this, m_iMaxHealthOffset);
	}

	bool IsBaseObject()
	{
		return call_vfunc<bool, CBaseEntity>(this, CBaseEntityIsBaseObject);
	}
};

void SetEdictStateChanged(CBaseEntity *pEntity, int offset)
{
	IServerNetworkable *pNet = pEntity->GetNetworkable();
	if(!pNet) {
		return;
	}

	edict_t *edict = pNet->GetEdict();
	if(!edict) {
		return;
	}
	
	gamehelpers->SetEdictStateChanged(edict, offset);
}

SH_DECL_MANUALHOOK0_void(GenericDtor, 1, 0, 0)

void *CBaseObjectCTOR = nullptr;
void *CBaseObjectUpgradeCTOR = nullptr;

SH_DECL_MANUALHOOK0(GetBaseHealth, 0, 0, 0, int)

int m_iObjectTypeOffset = -1;
int sizeofCBaseObject = -1;
int sizeofCBaseObjectUpgrade = -1;

class CBaseObject : public CBaseEntity
{
public:
	static CBaseObject *create(void *ctor, size_t siz, size_t size_modifier)
	{
		CBaseObject *bytes = (CBaseObject *)engine->PvAllocEntPrivateData(siz + size_modifier);
		call_mfunc<void>(bytes, ctor);
		SH_ADD_MANUALHOOK(GenericDtor, bytes, SH_MEMBER(bytes, &CBaseObject::dtor), false);
		SH_ADD_MANUALHOOK(GetBaseHealth, bytes, SH_MEMBER(bytes, &CBaseObject::HookGetBaseHealth), false);
		bytes->SetHealth(100);
		bytes->SetMaxHealth(100);
		return bytes;
	}

	static CBaseObject *create(size_t size_modifier)
	{
		return create(CBaseObjectCTOR, sizeofCBaseObject, size_modifier);
	}
	
	int GetObjectType()
	{
		return *(int *)((unsigned char *)this + m_iObjectTypeOffset);
	}
	
	void dtor()
	{
		CBaseEntity *pEntity = META_IFACEPTR(CBaseEntity);
		
		SH_REMOVE_MANUALHOOK(GenericDtor, pEntity, SH_MEMBER(this, &CBaseObject::dtor), false);
		SH_REMOVE_MANUALHOOK(GetBaseHealth, pEntity, SH_MEMBER(this, &CBaseObject::HookGetBaseHealth), false);
		
		RETURN_META(MRES_HANDLED);
	}
	
	int HookGetBaseHealth()
	{
		int m_iObjectType = GetObjectType();
		if(m_iObjectType < 0 || m_iObjectType >= g_ObjectInfos.size()) {
			RETURN_META_VALUE(MRES_SUPERCEDE, 100);
		} else {
			CObjectInfo *pInfo = g_ObjectInfos[m_iObjectType].get();
			RETURN_META_VALUE(MRES_SUPERCEDE, pInfo->m_nBaseHealth);
		}
	}
};

class CBaseObjectUpgrade : public CBaseObject
{
public:
	static CBaseObjectUpgrade *create(size_t size_modifier)
	{
		return (CBaseObjectUpgrade *)CBaseObject::create(CBaseObjectUpgradeCTOR, sizeofCBaseObjectUpgrade, size_modifier);
	}
};

static cell_t GetBaseObjectSize(IPluginContext *pContext, const cell_t *params)
{
	return sizeofCBaseObject;
}

static cell_t EntityIsBaseObject(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pSubject = gamehelpers->ReferenceToEntity(params[1]);
	if(!pSubject)
	{
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}

	return pSubject->IsBaseObject();
}

static cell_t AllocateBaseObject(IPluginContext *pContext, const cell_t *params)
{
	return (cell_t)CBaseObject::create(params[1]);
}

static cell_t GetBaseObjectUpgradeSize(IPluginContext *pContext, const cell_t *params)
{
	return sizeofCBaseObjectUpgrade;
}

static cell_t AllocateBaseObjectUpgrade(IPluginContext *pContext, const cell_t *params)
{
	return (cell_t)CBaseObjectUpgrade::create(params[1]);
}

static const sp_nativeinfo_t g_sNativesInfo[] =
{
	{"CObjectInfo.Find", CObjectInfoFind},
	{"CObjectInfo.GetIndex", CObjectInfoGetIndex},
	{"CObjectInfo.Get", CObjectInfoGet},
	{"CObjectInfoCustom.CloneByName", CObjectInfoCloneByName},
	{"CObjectInfoCustom.CloneByIndex", CObjectInfoCloneByIndex},
	{"CObjectInfoCustom.SetString", CObjectInfoSetString},
	{"CObjectInfoCustom.SetFloat", CObjectInfoSetFloat},
	{"CObjectInfoCustom.SetInt", CObjectInfoSetInt},
	{"CObjectInfo.GetString", CObjectInfoGetString},
	{"CObjectInfo.GetFloat", CObjectInfoGetFloat},
	{"CObjectInfo.GetInt", CObjectInfoGetInt},
	{"CObjectInfo.Count", CObjectInfoCount},
	{"CObjectInfo.Index.get", CObjectInfoIndexget},
	{"CObjectInfoCustom.Create", CObjectInfoCreate},
	{"CObjectInfoCustom.BaseInfo.get", CObjectInfoCustomBaseGet},
	{"TFPlayerClassData.Find", TFPlayerClassData_tFind},
	{"TFPlayerClassData.GetIndex", TFPlayerClassData_tGetIndex},
	{"TFPlayerClassData.Get", TFPlayerClassData_tGet},
	{"TFPlayerClassDataCustom.CloneByName", TFPlayerClassData_tCloneByName},
	{"TFPlayerClassDataCustom.CloneByIndex", TFPlayerClassData_tCloneByIndex},
	{"TFPlayerClassDataCustom.SetString", TFPlayerClassData_tSetString},
	{"TFPlayerClassDataCustom.SetFloat", TFPlayerClassData_tSetFloat},
	{"TFPlayerClassDataCustom.SetInt", TFPlayerClassData_tSetInt},
	{"TFPlayerClassData.GetString", TFPlayerClassData_tGetString},
	{"TFPlayerClassData.GetFloat", TFPlayerClassData_tGetFloat},
	{"TFPlayerClassData.GetInt", TFPlayerClassData_tGetInt},
	{"TFPlayerClassData.Count", TFPlayerClassData_tCount},
	{"TFPlayerClassData.Index.get", TFPlayerClassData_tIndexget},
	{"TFPlayerClassDataCustom.Create", TFPlayerClassData_tCreate},
	{"TFPlayerClassDataCustom.BaseData.get", TFPlayerClassDataCustomBaseGet},
	{"BuilderGetNumBuildables", BuilderGetNumBuildables},
	{"BuilderGetBuildableIndex", BuilderGetBuildableIndex},
	{"BuilderIsBuildable", BuilderIsBuildable},
	{"BuilderIndexByRepresentative", BuilderIndexByRepresentative},
	{"BuilderRepresentativeByIndex", BuilderRepresentativeByIndex},
	{"AllocateBaseObject", AllocateBaseObject},
	{"GetBaseObjectSize", GetBaseObjectSize},
	{"AllocateBaseObjectUpgrade", AllocateBaseObjectUpgrade},
	{"GetBaseObjectUpgradeSize", GetBaseObjectUpgradeSize},
	{"ManageBuilderWeaponsEx", ManageBuilderWeaponsEx},
	{"BuilderSetAsBuildableInternal", BuilderSetAsBuildable},
	{"EntityIsBaseObject", EntityIsBaseObject},
	{nullptr, nullptr},
};

CDetour *pClassCanBuild = nullptr;

#include <sourcehook/sh_memory.h>

void *CTFPlayerCanBuildAddr = nullptr;
void *CTFPlayerPrecachePlayerModelsAddr = nullptr;

int CTFPlayerManageBuilderWeaponsOBJ_LAST = -1;
int CTFPlayerPrecachePlayerModelsTF_CLASS_COUNT_ALL1 = -1;
int CTFPlayerPrecachePlayerModelsTF_CLASS_COUNT_ALL2 = -1;
int CTFPlayerPrecachePlayerModelsTF_CLASS_COUNT_ALL3 = -1;
int CTFPlayerCanBuildOBJ_ATTACHMENT_SAPPER = -1;

bool g_bOffsetsInited = false;

void UpdateObjectOffsets()
{
	if(!g_bOffsetsInited) {
		return;
	}
	
	size_t size = g_ObjectInfos.size();

	*(unsigned char *)((unsigned char *)CTFPlayerManageBuilderWeaponsAddr + CTFPlayerManageBuilderWeaponsOBJ_LAST) = size;
	*(unsigned char *)((unsigned char *)CTFPlayerCanBuildAddr + CTFPlayerCanBuildOBJ_ATTACHMENT_SAPPER) = size;
}

void UpdateClassOffsets()
{
	if(!g_bOffsetsInited) {
		return;
	}
	
	size_t size = m_aTFPlayerClassData.size();
	
	*(unsigned char *)((unsigned char *)CTFPlayerPrecachePlayerModelsAddr + CTFPlayerPrecachePlayerModelsTF_CLASS_COUNT_ALL1) = size;
	*(unsigned char *)((unsigned char *)CTFPlayerPrecachePlayerModelsAddr + CTFPlayerPrecachePlayerModelsTF_CLASS_COUNT_ALL2) = size;
	*(unsigned char *)((unsigned char *)CTFPlayerPrecachePlayerModelsAddr + CTFPlayerPrecachePlayerModelsTF_CLASS_COUNT_ALL3) = size;
}

CDetour *pCTFWeaponBuilderCanBuildObjectType = nullptr;
CDetour *pCTFWeaponBuilderSetObjectTypeAsBuildable = nullptr;
CDetour *pCTFWeaponBuilderCTOR = nullptr;

void *CTFWeaponBasePrecache = nullptr;
void *CBaseEntityPrecacheModel = nullptr;

SH_DECL_MANUALHOOK0_void(Precache, 0, 0, 0)
SH_DECL_MANUALHOOK0_void(Spawn, 0, 0, 0)

void PrecacheModel(const char *mdl)
{
	((void(*)(const char *))CBaseEntityPrecacheModel)(mdl);
}

void HookBuilderPrecache()
{
	CBaseEntity *pEntity = META_IFACEPTR(CBaseEntity);

	call_mfunc<void, CBaseEntity>((CBaseEntity *)pEntity, CTFWeaponBasePrecache);

	for ( int iObj=0, len = g_ObjectInfos.size(); iObj < len; ++iObj )
	{
		const CObjectInfo *pInfo = g_ObjectInfos[ iObj ].get();

		if ( pInfo->m_pViewModel )
		{
			PrecacheModel( pInfo->m_pViewModel );
		}

		if ( pInfo->m_pPlayerModel )
		{
			PrecacheModel( pInfo->m_pPlayerModel );
		}
	}

	RETURN_META(MRES_SUPERCEDE);
}

void HookBuilderSpawn()
{
	CBaseEntity *pEntity = META_IFACEPTR(CBaseEntity);

	int ref = gamehelpers->EntityToReference(pEntity);

	buildervarsmap.emplace(ref, builder_vars_t{});

	RETURN_META(MRES_HANDLED);
}

void HookBuilderDtor()
{
	CBaseEntity *pEntity = META_IFACEPTR(CBaseEntity);
	
	int ref = gamehelpers->EntityToReference(pEntity);

	buildervarsmap.erase(ref);
	
	SH_REMOVE_MANUALHOOK(GenericDtor, pEntity, SH_STATIC(HookBuilderDtor), false);
	SH_REMOVE_MANUALHOOK(Precache, pEntity, SH_STATIC(HookBuilderPrecache), false);
	SH_REMOVE_MANUALHOOK(Spawn, pEntity, SH_STATIC(HookBuilderSpawn), true);
	
	RETURN_META(MRES_HANDLED);
}

DETOUR_DECL_MEMBER0(CTFWeaponBuilderCTOR, void)
{
	DETOUR_MEMBER_CALL(CTFWeaponBuilderCTOR)();
	
	SH_ADD_MANUALHOOK(GenericDtor, this, SH_STATIC(HookBuilderDtor), false);
	SH_ADD_MANUALHOOK(Spawn, this, SH_STATIC(HookBuilderSpawn), true);
	//SH_ADD_MANUALHOOK(Precache, this, SH_STATIC(HookBuilderPrecache), false);
}

DETOUR_DECL_MEMBER1(CTFWeaponBuilderCanBuildObjectType, bool, int, iObjectType)
{
	if ( iObjectType < 0 || iObjectType >= g_ObjectInfos.size() ) {
		return false;
	}
	
	bool value = false;
	
	if( iObjectType < OBJ_LAST ) {
		value = *(bool *)((unsigned char *)this + m_aBuildableObjectTypesOffset + iObjectType);
	} else {
		int ref = gamehelpers->EntityToReference((CBaseEntity *)this);

		auto it = buildervarsmap.find(ref);
		if(it != buildervarsmap.end()) {
			auto &map = it->second.m_aBuildableObjectTypes;
			value = (map.find(iObjectType) != map.end());
		}
	}
	
	return value;
}

void *CTFWeaponBuilderSetSubType = nullptr;

DETOUR_DECL_MEMBER1(CTFWeaponBuilderSetObjectTypeAsBuildable, void, int, iObjectType)
{
	if ( iObjectType < 0 || iObjectType >= g_ObjectInfos.size() )
		return;
	
	if( iObjectType < OBJ_LAST ) {
		*(bool *)((unsigned char *)this + m_aBuildableObjectTypesOffset + iObjectType) = true;
		SetEdictStateChanged((CBaseEntity *)this, m_aBuildableObjectTypesOffset + iObjectType);
	} else {
		CObjectInfo *pInfo = g_ObjectInfos[iObjectType].get();

		int ref = gamehelpers->EntityToReference((CBaseEntity *)this);

		auto it = buildervarsmap.find(ref);
		if(it != buildervarsmap.end()) {
			if(pInfo->m_nRepresentative != OBJ_LAST) {
				it->second.m_aBuildableObjectTypes[iObjectType] = pInfo->m_nRepresentative;
				SetEdictStateChanged((CBaseEntity *)this, m_aBuildableObjectTypesOffset + pInfo->m_nRepresentative);
			} else {
				it->second.m_aBuildableObjectTypes[iObjectType] = BUILDER_INVALID_OBJECT;
			}
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

enum
{
	CB_CAN_BUILD,			// Player is allowed to build this object
	CB_CANNOT_BUILD,		// Player is not allowed to build this object
	CB_LIMIT_REACHED,		// Player has reached the limit of the number of these objects allowed
	CB_NEED_RESOURCES,		// Player doesn't have enough resources to build this object
	CB_NEED_ADRENALIN,		// Commando doesn't have enough adrenalin to build a rally flag
	CB_UNKNOWN_OBJECT,		// Error message, tried to build unknown object
};

void Sample::OnHandleDestroy(HandleType_t type, void *object)
{
	if(type == objinfo_handle) {
		CObjectInfoCustom *obj = (CObjectInfoCustom *)object;
		obj->freehndl = false;
		g_ObjectInfos.remove(g_ObjectInfos.begin() + obj->m_nIndex);
	} else if(type == classdata_handle) {
		TFPlayerClassDataCustom *obj = (TFPlayerClassDataCustom *)object;
		obj->freehndl = false;
		m_aTFPlayerClassData.remove(m_aTFPlayerClassData.begin() + obj->m_nIndex);
	}
}

CDetour *pCTFPlayerClassSharedCanBuildObject = nullptr;

int m_PlayerClassOffset = -1;
int m_iClassLocalOffset = -1;

IForward *ClassCanBuildObject = nullptr;

CDetour *pCTFPlayerManageBuilderWeapons = nullptr;

bool bInManageBuilderWeapons = false;
int iLastObjectType = BUILDER_INVALID_OBJECT;

DETOUR_DECL_MEMBER1(CTFPlayerManageBuilderWeapons, void, TFPlayerClassData_t *, pClassData)
{
	bInManageBuilderWeapons = true;
	DETOUR_MEMBER_CALL(CTFPlayerManageBuilderWeapons)(pClassData);
	bInManageBuilderWeapons = false;
	iLastObjectType = BUILDER_INVALID_OBJECT;
}

CDetour *pCBaseObjectCanBeUpgraded = nullptr;
CDetour *pCBaseObjectDoQuickBuild = nullptr;
CDetour *pCBaseObjectGetMaxUpgradeLevel = nullptr;

int CBaseObjectCanBeUpgradedOBJ_MAX_UPGRADE_LEVEL = -1;
int CBaseObjectDoQuickBuildOBJ_MAX_UPGRADE_LEVEL = -1;

int CBaseObjectGetMaxUpgradeLevelOffset = -1;

CBaseEntity *pLastPlayer = nullptr;

void *CBaseObjectCanBeUpgradedAddr = nullptr;
void *CBaseObjectDoQuickBuildAddr = nullptr;

DETOUR_DECL_MEMBER1(CBaseObjectCanBeUpgraded, bool, CBaseEntity *, pPlayer)
{
	pLastPlayer = pPlayer;
	int max = call_vfunc<int, CBaseEntity>((CBaseEntity *)this, CBaseObjectGetMaxUpgradeLevelOffset);
	*(unsigned char *)((unsigned char *)CBaseObjectCanBeUpgradedAddr + CBaseObjectCanBeUpgradedOBJ_MAX_UPGRADE_LEVEL) = (max-1);
	bool ret = DETOUR_MEMBER_CALL(CBaseObjectCanBeUpgraded)(pPlayer);
	*(unsigned char *)((unsigned char *)CBaseObjectCanBeUpgradedAddr + CBaseObjectCanBeUpgradedOBJ_MAX_UPGRADE_LEVEL) = (OBJ_MAX_UPGRADE_LEVEL-1);
	pLastPlayer = nullptr;
	return ret;
}

DETOUR_DECL_MEMBER1(CBaseObjectDoQuickBuild, bool, bool, bForceMax)
{
	int max = call_vfunc<int, CBaseEntity>((CBaseEntity *)this, CBaseObjectGetMaxUpgradeLevelOffset);
	*(unsigned char *)((unsigned char *)CBaseObjectDoQuickBuildAddr + CBaseObjectDoQuickBuildOBJ_MAX_UPGRADE_LEVEL) = max;
	bool ret = DETOUR_MEMBER_CALL(CBaseObjectDoQuickBuild)(bForceMax);
	*(unsigned char *)((unsigned char *)CBaseObjectDoQuickBuildAddr + CBaseObjectDoQuickBuildOBJ_MAX_UPGRADE_LEVEL) = OBJ_MAX_UPGRADE_LEVEL;
	return ret;
}

DETOUR_DECL_MEMBER0(CBaseObjectGetMaxUpgradeLevel, int)
{
	CBaseObject *obj{(CBaseObject *)this};

	int m_iObjectType = obj->GetObjectType();
	if(m_iObjectType < 0 || m_iObjectType >= g_ObjectInfos.size()) {
		return OBJ_MAX_UPGRADE_LEVEL;
	} else {
		CObjectInfo *pInfo = g_ObjectInfos[m_iObjectType].get();
		return pInfo->m_MaxUpgradeLevel;
	}
}

DETOUR_DECL_STATIC2(ClassCanBuild, bool, int, iClass, int, iObjectType)
{
	if(iObjectType == OBJ_LAST) {
		return false;
	}
	
	bool ret = false;
	
	for ( int i = 0; i < TF_PLAYER_BLUEPRINT_COUNT; i++ )
	{
		if ( m_aTFPlayerClassData[iClass]->m_aBuildable[i] == iObjectType ) {
			ret = true;
			break;
		}
	}

	if(ClassCanBuildObject->GetFunctionCount() > 0) {
		ClassCanBuildObject->PushCell(pLastPlayer ? gamehelpers->EntityToBCompatRef(pLastPlayer) : -1);
		ClassCanBuildObject->PushCell(iClass);
		ClassCanBuildObject->PushCell(iObjectType);
		cell_t can = ret;
		ClassCanBuildObject->PushCellByRef(&can);
		cell_t res = 0;
		ClassCanBuildObject->Execute(&res);
		
		switch(res) {
			case Pl_Changed: {
				return can;
			}
			case Pl_Handled:
			case Pl_Stop: {
				return false;
			}
		}
	}
	
	return ret;
}

DETOUR_DECL_MEMBER1(CTFPlayerClassSharedCanBuildObject, bool, int, iObjectType)
{
	if(bInManageBuilderWeapons) {
		iLastObjectType = iObjectType;
	}
	
	if(iObjectType == OBJ_LAST) {
		return false;
	}

	bool ret = DETOUR_MEMBER_CALL(CTFPlayerClassSharedCanBuildObject)(iObjectType);

	if(ClassCanBuildObject->GetFunctionCount() > 0) {
		int m_iClass = *(int *)((unsigned char *)this + m_iClassLocalOffset);
		CBaseEntity *pPlayer = (CBaseEntity *)((unsigned char *)this - m_PlayerClassOffset);

		ClassCanBuildObject->PushCell(gamehelpers->EntityToBCompatRef(pPlayer));
		ClassCanBuildObject->PushCell(m_iClass);
		ClassCanBuildObject->PushCell(iObjectType);
		cell_t can = ret;
		ClassCanBuildObject->PushCellByRef(&can);
		cell_t res = 0;
		ClassCanBuildObject->Execute(&res);
		
		switch(res) {
			case Pl_Changed: {
				return can;
			}
			case Pl_Handled:
			case Pl_Stop: {
				return false;
			}
		}
	}
	
	return ret;
}

CDetour *pCTFPlayerGetLoadoutItem = nullptr;
CDetour *pCTFItemDefinitionGetLoadoutSlot{nullptr};
CDetour *pCTFInventoryManagerGetBaseItemForClass{nullptr};
CDetour *pCTFPlayerInventoryGetItemInLoadout{nullptr};
CDetour *pCEconItemViewGetPlayerDisplayModel{nullptr};
CDetour *pCTFItemDefinitionGetPlayerDisplayModelAlt{nullptr};
CDetour *pCTFPlayerGetClassEyeHeight{nullptr};
CDetour *TranslateWeaponEntForClassDetour = nullptr;

//TODO!!!!!!!!! CTFTauntInfo

enum loadout_positions_t
{
	LOADOUT_POSITION_INVALID = -1,

	// Weapons & Equipment
	LOADOUT_POSITION_PRIMARY = 0,
	LOADOUT_POSITION_SECONDARY,
	LOADOUT_POSITION_MELEE,
	LOADOUT_POSITION_UTILITY,
	LOADOUT_POSITION_BUILDING,
	LOADOUT_POSITION_PDA,
	LOADOUT_POSITION_PDA2,

	// Wearables. If you add new wearable slots, make sure you add them to IsWearableSlot() below this.
	LOADOUT_POSITION_HEAD,
	LOADOUT_POSITION_MISC,

	// other
	LOADOUT_POSITION_ACTION,

	// More wearables, yay!
	LOADOUT_POSITION_MISC2,

	// taunts
	LOADOUT_POSITION_TAUNT,
	LOADOUT_POSITION_TAUNT2,
	LOADOUT_POSITION_TAUNT3,
	LOADOUT_POSITION_TAUNT4,
	LOADOUT_POSITION_TAUNT5,
	LOADOUT_POSITION_TAUNT6,
	LOADOUT_POSITION_TAUNT7,
	LOADOUT_POSITION_TAUNT8,

	CLASS_LOADOUT_POSITION_COUNT,
};

DETOUR_DECL_MEMBER3(CTFPlayerGetLoadoutItem, void *, int, iClass, int, iSlot, bool, bReportWhitelistFails)
{
	iClass = m_aTFPlayerClassData[iClass]->m_nRepresentative;

	if(iLastObjectType != BUILDER_INVALID_OBJECT) {
		if(g_ObjectInfos[iLastObjectType]->m_bRequiresOwnBuilder && iSlot == LOADOUT_POSITION_BUILDING) {
			//iSlot = LOADOUT_POSITION_BUILDING2;
		}

		switch(g_ObjectInfos[iLastObjectType]->m_nRepresentative) {
			case OBJ_ATTACHMENT_SAPPER:
			iClass = TF_CLASS_SPY;
			break;
			case OBJ_DISPENSER:
			case OBJ_TELEPORTER:
			case OBJ_SENTRYGUN:
			iClass = TF_CLASS_ENGINEER;
			break;
		}
	}

	return DETOUR_MEMBER_CALL(CTFPlayerGetLoadoutItem)(iClass, iSlot, bReportWhitelistFails);
}

DETOUR_DECL_MEMBER1(CTFItemDefinitionGetLoadoutSlot, int, int, iLoadoutClass)
{
	iLoadoutClass = m_aTFPlayerClassData[iLoadoutClass]->m_nRepresentative;

	return DETOUR_MEMBER_CALL(CTFItemDefinitionGetLoadoutSlot)(iLoadoutClass);
}

DETOUR_DECL_MEMBER2(CTFInventoryManagerGetBaseItemForClass, void *, int, iClass, int, iSlot)
{
	iClass = m_aTFPlayerClassData[iClass]->m_nRepresentative;

	return DETOUR_MEMBER_CALL(CTFInventoryManagerGetBaseItemForClass)(iClass, iSlot);
}

DETOUR_DECL_MEMBER2(CTFPlayerInventoryGetItemInLoadout, void *, int, iClass, int, iSlot)
{
	iClass = m_aTFPlayerClassData[iClass]->m_nRepresentative;

	return DETOUR_MEMBER_CALL(CTFPlayerInventoryGetItemInLoadout)(iClass, iSlot);
}

DETOUR_DECL_MEMBER2(CEconItemViewGetPlayerDisplayModel, const char *, int, iClass, int, iTeam)
{
	iClass = m_aTFPlayerClassData[iClass]->m_nRepresentative;

	return DETOUR_MEMBER_CALL(CEconItemViewGetPlayerDisplayModel)(iClass, iTeam);
}

DETOUR_DECL_MEMBER1(CTFItemDefinitionGetPlayerDisplayModelAlt, const char *, int, iClass)
{
	iClass = m_aTFPlayerClassData[iClass]->m_nRepresentative;

	return DETOUR_MEMBER_CALL(CTFItemDefinitionGetPlayerDisplayModelAlt)(iClass);
}

DETOUR_DECL_STATIC2(TranslateWeaponEntForClass, const char *, const char *, pszName, int, iClass)
{
	iClass = m_aTFPlayerClassData[iClass]->m_nRepresentative;

	return DETOUR_STATIC_CALL(TranslateWeaponEntForClass)(pszName, iClass);
}

Vector g_TFClassViewVectors[11] =
{
	Vector( 0, 0, 72 ),		// TF_CLASS_UNDEFINED

	Vector( 0, 0, 65 ),		// TF_CLASS_SCOUT,			// TF_FIRST_NORMAL_CLASS
	Vector( 0, 0, 75 ),		// TF_CLASS_SNIPER,
	Vector( 0, 0, 68 ),		// TF_CLASS_SOLDIER,
	Vector( 0, 0, 68 ),		// TF_CLASS_DEMOMAN,
	Vector( 0, 0, 75 ),		// TF_CLASS_MEDIC,
	Vector( 0, 0, 75 ),		// TF_CLASS_HEAVYWEAPONS,
	Vector( 0, 0, 68 ),		// TF_CLASS_PYRO,
	Vector( 0, 0, 75 ),		// TF_CLASS_SPY,
	Vector( 0, 0, 68 ),		// TF_CLASS_ENGINEER,

	Vector( 0, 0, 65 ),		// TF_CLASS_CIVILIAN,		// TF_LAST_NORMAL_CLASS
};

int m_flModelScaleOffset{-1};

DETOUR_DECL_MEMBER0(CTFPlayerGetClassEyeHeight, Vector)
{
	CBaseEntity *pThis{(CBaseEntity *)this};

	float modelscale = *(float *)((unsigned char *)pThis + m_flModelScaleOffset);

	int m_iClass = *(int *)((unsigned char *)pThis + m_iClassOffset);
	m_iClass = m_aTFPlayerClassData[m_iClass]->m_nRepresentative;

	if(m_iClass < 0 || m_iClass >= sizeof(g_TFClassViewVectors)) {
		return g_TFClassViewVectors[0] * modelscale;
	} else {
		return g_TFClassViewVectors[m_iClass] * modelscale;
	}
}

CDetour *pCTFPlayerCanBuild = nullptr;

DETOUR_DECL_MEMBER2(CTFPlayerCanBuild, int, int, iObjectType, int, iObjectMode)
{
	int ret = DETOUR_MEMBER_CALL(CTFPlayerCanBuild)(iObjectType, iObjectMode);
	return ret;
}

CDetour *pInternalCalculateObjectCost = nullptr;

int CalculateObjectCost( int iObjectType, int iNumberOfObjects, bool bLast )
{
	if ( tf_cheapobjects->GetBool() )
	{
		return 0;
	}

	// Find out how much the next object should cost
	if ( bLast )
	{
		iNumberOfObjects = MAX(0,iNumberOfObjects-1);
	}

	int iCost = g_ObjectInfos[ iObjectType ]->m_Cost;

	// If a cost is negative, it means the first object of that type is free, and then
	// it counts up as normal, using the negative value.
	if ( iCost < 0 )
	{
		if ( iNumberOfObjects == 0 )
			return 0;
		iCost *= -1;
		iNumberOfObjects--;
	}

	// Calculate the cost based upon the number of objects
	for ( int i = 0; i < iNumberOfObjects; i++ )
	{
		iCost *= g_ObjectInfos[ iObjectType ]->m_CostMultiplierPerInstance;
	}

	return iCost;
}

void *CTFPlayerGetNumObjectsAddr = nullptr;

CBaseEntity *last_player = nullptr;

#define BUILDING_MODE_ANY -1

DETOUR_DECL_STATIC1(InternalCalculateObjectCost, int, int, iObjectType)
{
	int iNumberOfObjects{0};
	if(last_player != nullptr) {
		iNumberOfObjects = call_mfunc<int, CBaseEntity, int, int>(last_player, CTFPlayerGetNumObjectsAddr, iObjectType, BUILDING_MODE_ANY);
	}
	return CalculateObjectCost(iObjectType, iNumberOfObjects, false);
}

CDetour *pCTFPlayerSharedCalculateObjectCost = nullptr;

DETOUR_DECL_MEMBER2(CTFPlayerSharedCalculateObjectCost, int, CBaseEntity *, pBuilder, int, iObjectType)
{
	CBaseEntity *pPlayer = (CBaseEntity *)((unsigned char *)this - m_PlayerClassOffset);

	last_player = pPlayer;
	int ret = DETOUR_MEMBER_CALL(CTFPlayerSharedCalculateObjectCost)(pBuilder, iObjectType);
	last_player = nullptr;
	return ret;
}

CDetour *pCTFPlayerManageRegularWeapons = nullptr;

void *CTFPlayerManageRegularWeaponsLegacyAddr = nullptr;

DETOUR_DECL_MEMBER1(CTFPlayerManageRegularWeapons, void, TFPlayerClassData_t *, pData)
{
	CBaseEntity *pPlayer{(CBaseEntity *)this};
	int m_iClass = *(int *)((unsigned char *)pPlayer + m_iClassOffset);

	DETOUR_MEMBER_CALL(CTFPlayerManageRegularWeapons)(pData);

#if 0
	if(m_iClass >= TF_CLASS_COUNT) {
		call_mfunc<void, CBaseEntity, TFPlayerClassData_t *>(pPlayer, CTFPlayerManageRegularWeaponsLegacyAddr, pData);
	}
#endif
}

bool Sample::SDK_OnLoad(char *error, size_t maxlen, bool late)
{
	gameconfs->LoadGameConfigFile("clsobj_hack", &g_pGameConf, error, maxlen);
	
	CDetourManager::Init(g_pSM->GetScriptingEngine(), g_pGameConf);

	g_pGameConf->GetMemSig("GetWeaponId", &GetWeaponIdAddr);
	g_pGameConf->GetMemSig("GetAmmoName", &GetAmmoNameAddr);

	g_pGameConf->GetMemSig("CTFPlayer::ManageBuilderWeapons", &CTFPlayerManageBuilderWeaponsAddr);
	g_pGameConf->GetMemSig("CTFPlayer::CanBuild", &CTFPlayerCanBuildAddr);
	g_pGameConf->GetMemSig("CTFPlayer::PrecachePlayerModels", &CTFPlayerPrecachePlayerModelsAddr);
	g_pGameConf->GetMemSig("CBaseObject::CanBeUpgraded", &CBaseObjectCanBeUpgradedAddr);
	g_pGameConf->GetMemSig("CBaseObject::DoQuickBuild", &CBaseObjectDoQuickBuildAddr);

	g_pGameConf->GetOffset("CTFPlayer::ManageBuilderWeapons::OBJ_LAST", &CTFPlayerManageBuilderWeaponsOBJ_LAST);
	g_pGameConf->GetOffset("CTFPlayer::PrecachePlayerModels::TF_CLASS_COUNT_ALL::1", &CTFPlayerPrecachePlayerModelsTF_CLASS_COUNT_ALL1);
	g_pGameConf->GetOffset("CTFPlayer::PrecachePlayerModels::TF_CLASS_COUNT_ALL::2", &CTFPlayerPrecachePlayerModelsTF_CLASS_COUNT_ALL2);
	g_pGameConf->GetOffset("CTFPlayer::PrecachePlayerModels::TF_CLASS_COUNT_ALL::3", &CTFPlayerPrecachePlayerModelsTF_CLASS_COUNT_ALL3);
	g_pGameConf->GetOffset("CTFPlayer::CanBuild::OBJ_ATTACHMENT_SAPPER", &CTFPlayerCanBuildOBJ_ATTACHMENT_SAPPER);

	g_pGameConf->GetOffset("CBaseObject::CanBeUpgraded::OBJ_MAX_UPGRADE_LEVEL", &CBaseObjectCanBeUpgradedOBJ_MAX_UPGRADE_LEVEL);
	g_pGameConf->GetOffset("CBaseObject::DoQuickBuild::OBJ_MAX_UPGRADE_LEVEL", &CBaseObjectDoQuickBuildOBJ_MAX_UPGRADE_LEVEL);

	SourceHook::SetMemAccess(CTFPlayerManageBuilderWeaponsAddr, CTFPlayerManageBuilderWeaponsOBJ_LAST + 4, SH_MEM_READ|SH_MEM_WRITE|SH_MEM_EXEC);
	SourceHook::SetMemAccess(CTFPlayerCanBuildAddr, CTFPlayerCanBuildOBJ_ATTACHMENT_SAPPER + 4, SH_MEM_READ|SH_MEM_WRITE|SH_MEM_EXEC);
	SourceHook::SetMemAccess(CTFPlayerPrecachePlayerModelsAddr, CTFPlayerPrecachePlayerModelsTF_CLASS_COUNT_ALL1 + 4, SH_MEM_READ|SH_MEM_WRITE|SH_MEM_EXEC);
	SourceHook::SetMemAccess(CTFPlayerPrecachePlayerModelsAddr, CTFPlayerPrecachePlayerModelsTF_CLASS_COUNT_ALL2 + 4, SH_MEM_READ|SH_MEM_WRITE|SH_MEM_EXEC);
	SourceHook::SetMemAccess(CTFPlayerPrecachePlayerModelsAddr, CTFPlayerPrecachePlayerModelsTF_CLASS_COUNT_ALL3 + 4, SH_MEM_READ|SH_MEM_WRITE|SH_MEM_EXEC);
	SourceHook::SetMemAccess(CBaseObjectCanBeUpgradedAddr, CBaseObjectCanBeUpgradedOBJ_MAX_UPGRADE_LEVEL + 4, SH_MEM_READ|SH_MEM_WRITE|SH_MEM_EXEC);
	SourceHook::SetMemAccess(CBaseObjectDoQuickBuildAddr, CBaseObjectDoQuickBuildOBJ_MAX_UPGRADE_LEVEL + 4, SH_MEM_READ|SH_MEM_WRITE|SH_MEM_EXEC);

	g_bOffsetsInited = true;

	UpdateObjectOffsets();
	UpdateClassOffsets();

	DoClassDataMgrInit();
	
	LoadExtraObjects();
	LoadExtraClasses();

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
	
	pCTFPlayerClassSharedCanBuildObject = DETOUR_CREATE_MEMBER(CTFPlayerClassSharedCanBuildObject, "CTFPlayerClassShared::CanBuildObject")
	pCTFPlayerClassSharedCanBuildObject->EnableDetour();
	
	pCTFPlayerGetLoadoutItem = DETOUR_CREATE_MEMBER(CTFPlayerGetLoadoutItem, "CTFPlayer::GetLoadoutItem")
	pCTFPlayerGetLoadoutItem->EnableDetour();

	pCTFItemDefinitionGetLoadoutSlot = DETOUR_CREATE_MEMBER(CTFItemDefinitionGetLoadoutSlot, "CTFItemDefinition::GetLoadoutSlot")
	pCTFItemDefinitionGetLoadoutSlot->EnableDetour();

	pCTFInventoryManagerGetBaseItemForClass = DETOUR_CREATE_MEMBER(CTFInventoryManagerGetBaseItemForClass, "CTFInventoryManager::GetBaseItemForClass")
	pCTFInventoryManagerGetBaseItemForClass->EnableDetour();

	pCTFPlayerInventoryGetItemInLoadout = DETOUR_CREATE_MEMBER(CTFPlayerInventoryGetItemInLoadout, "CTFPlayerInventory::GetItemInLoadout")
	pCTFPlayerInventoryGetItemInLoadout->EnableDetour();

	pCEconItemViewGetPlayerDisplayModel = DETOUR_CREATE_MEMBER(CEconItemViewGetPlayerDisplayModel, "CEconItemView::GetPlayerDisplayModel")
	pCEconItemViewGetPlayerDisplayModel->EnableDetour();

	pCTFItemDefinitionGetPlayerDisplayModelAlt = DETOUR_CREATE_MEMBER(CTFItemDefinitionGetPlayerDisplayModelAlt, "CTFItemDefinition::GetPlayerDisplayModelAlt")
	pCTFItemDefinitionGetPlayerDisplayModelAlt->EnableDetour();

	TranslateWeaponEntForClassDetour = DETOUR_CREATE_STATIC(TranslateWeaponEntForClass, "TranslateWeaponEntForClass")
	TranslateWeaponEntForClassDetour->EnableDetour();

	pCTFPlayerGetClassEyeHeight = DETOUR_CREATE_MEMBER(CTFPlayerGetClassEyeHeight, "CTFPlayer::GetClassEyeHeight")
	pCTFPlayerGetClassEyeHeight->EnableDetour();
	
	pCTFPlayerManageBuilderWeapons = DETOUR_CREATE_MEMBER(CTFPlayerManageBuilderWeapons, "CTFPlayer::ManageBuilderWeapons")
	pCTFPlayerManageBuilderWeapons->EnableDetour();
	
	pCBaseObjectCanBeUpgraded = DETOUR_CREATE_MEMBER(CBaseObjectCanBeUpgraded, "CBaseObject::CanBeUpgraded")
	pCBaseObjectCanBeUpgraded->EnableDetour();

	pCBaseObjectDoQuickBuild = DETOUR_CREATE_MEMBER(CBaseObjectDoQuickBuild, "CBaseObject::DoQuickBuild")
	pCBaseObjectDoQuickBuild->EnableDetour();

	pCBaseObjectGetMaxUpgradeLevel = DETOUR_CREATE_MEMBER(CBaseObjectGetMaxUpgradeLevel, "CBaseObject::GetMaxUpgradeLevel")
	pCBaseObjectGetMaxUpgradeLevel->EnableDetour();
	
	pCTFPlayerCanBuild = DETOUR_CREATE_MEMBER(CTFPlayerCanBuild, "CTFPlayer::CanBuild")
	pCTFPlayerCanBuild->EnableDetour();
	
	pCTFPlayerSharedCalculateObjectCost = DETOUR_CREATE_MEMBER(CTFPlayerSharedCalculateObjectCost, "CTFPlayerShared::CalculateObjectCost")
	pCTFPlayerSharedCalculateObjectCost->EnableDetour();
	
	pWriteUsercmd = DETOUR_CREATE_STATIC(WriteUsercmd, "WriteUsercmd")
	pWriteUsercmd->EnableDetour();
	
	pReadUsercmd = DETOUR_CREATE_STATIC(ReadUsercmd, "ReadUsercmd")
	pReadUsercmd->EnableDetour();
	
	pInternalCalculateObjectCost = DETOUR_CREATE_STATIC(InternalCalculateObjectCost, "InternalCalculateObjectCost")
	pInternalCalculateObjectCost->EnableDetour();

	pCTFPlayerManageRegularWeapons = DETOUR_CREATE_MEMBER(CTFPlayerManageRegularWeapons, "CTFPlayer::ManageRegularWeapons")
	pCTFPlayerManageRegularWeapons->EnableDetour();
	
	sm_sendprop_info_t info{};
	gamehelpers->FindSendPropInfo("CTFWeaponBuilder", "m_aBuildableObjectTypes", &info);
	m_aBuildableObjectTypesOffset = info.actual_offset;
	
	gamehelpers->FindSendPropInfo("CBaseObject", "m_iObjectType", &info);
	m_iObjectTypeOffset = info.actual_offset;
	
	gamehelpers->FindSendPropInfo("CTFPlayer", "m_PlayerClass", &info);
	m_PlayerClassOffset = info.actual_offset;

	gamehelpers->FindSendPropInfo("CBaseAnimating", "m_flModelScale", &info);
	m_flModelScaleOffset = info.actual_offset;
	
	gamehelpers->FindSendPropInfo("CTFPlayer", "m_iClass", &info);
	m_iClassOffset = info.actual_offset;
	m_iClassLocalOffset = m_iClassOffset - m_PlayerClassOffset;
	
	g_pGameConf->GetOffset("sizeof(CBaseObject)", &sizeofCBaseObject);
	g_pGameConf->GetOffset("sizeof(CBaseObjectUpgrade)", &sizeofCBaseObjectUpgrade);
	
	int offset = -1;
	g_pGameConf->GetOffset("CBaseObject::GetBaseHealth", &offset);
	SH_MANUALHOOK_RECONFIGURE(GetBaseHealth, offset, 0, 0);

	g_pGameConf->GetOffset("CBaseEntity::Precache", &offset);
	SH_MANUALHOOK_RECONFIGURE(Precache, offset, 0, 0);

	g_pGameConf->GetOffset("CBaseEntity::Spawn", &offset);
	SH_MANUALHOOK_RECONFIGURE(Spawn, offset, 0, 0);

	g_pGameConf->GetOffset("CBaseObject::GetMaxUpgradeLevel", &CBaseObjectGetMaxUpgradeLevelOffset);

	g_pGameConf->GetOffset("CBaseEntity::IsBaseObject", &CBaseEntityIsBaseObject);

	g_pGameConf->GetMemSig("CTFWeaponBuilder::SetSubType", &CTFWeaponBuilderSetSubType);
	g_pGameConf->GetMemSig("CBaseObject::CBaseObject", &CBaseObjectCTOR);
	g_pGameConf->GetMemSig("CBaseObjectUpgrade::CBaseObjectUpgrade", &CBaseObjectUpgradeCTOR);
	g_pGameConf->GetMemSig("CTFWeaponBase::Precache", &CTFWeaponBasePrecache);
	g_pGameConf->GetMemSig("CBaseEntity::PrecacheModel", &CBaseEntityPrecacheModel);

	g_pGameConf->GetMemSig("CTFPlayer::ManageRegularWeaponsLegacy", &CTFPlayerManageRegularWeaponsLegacyAddr);

	g_pGameConf->GetMemSig("CTFPlayer::GetNumObjects", &CTFPlayerGetNumObjectsAddr);
	
	sharesys->AddNatives(myself, g_sNativesInfo);
	
	ClassCanBuildObject = forwards->CreateForward("ClassCanBuildObject", ET_Hook, 4, nullptr, Param_Cell, Param_Cell, Param_Cell, Param_CellByRef);
	
	objinfo_handle = handlesys->CreateType("CObjectInfo", this, 0, nullptr, nullptr, myself->GetIdentity(), nullptr);
	classdata_handle = handlesys->CreateType("TFPlayerClassData", this, 0, nullptr, nullptr, myself->GetIdentity(), nullptr);
	
	sharesys->RegisterLibrary(myself, "clsobj_hack");
	
	return true;
}

void Sample::SDK_OnUnload()
{
	pCTFPlayerManageRegularWeapons->Destroy();
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
	pCTFPlayerClassSharedCanBuildObject->Destroy();
	pCTFPlayerGetLoadoutItem->Destroy();
	pCTFItemDefinitionGetLoadoutSlot->Destroy();
	pCTFInventoryManagerGetBaseItemForClass->Destroy();
	pCTFPlayerInventoryGetItemInLoadout->Destroy();
	pCEconItemViewGetPlayerDisplayModel->Destroy();
	pCTFItemDefinitionGetPlayerDisplayModelAlt->Destroy();
	pCTFPlayerGetClassEyeHeight->Destroy();
	TranslateWeaponEntForClassDetour->Destroy();
	pCTFPlayerManageBuilderWeapons->Destroy();
	pCBaseObjectCanBeUpgraded->Destroy();
	pCBaseObjectDoQuickBuild->Destroy();
	pCBaseObjectGetMaxUpgradeLevel->Destroy();
	pCTFPlayerCanBuild->Destroy();
	pInternalCalculateObjectCost->Destroy();
	pCTFPlayerSharedCalculateObjectCost->Destroy();
	handlesys->RemoveType(objinfo_handle, myself->GetIdentity());
	handlesys->RemoveType(classdata_handle, myself->GetIdentity());
	forwards->ReleaseForward(ClassCanBuildObject);
	gameconfs->CloseGameConfigFile(g_pGameConf);
}
