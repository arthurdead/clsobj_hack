#include <sourcemod>
#include <clsobj_hack>
#include <sendproxy>
#include <sdkhooks>
#include <tf2_stocks>

native int BuilderGetNumBuildables(int entity);
native int BuilderGetBuildableIndex(int entity, int index);
native int BuilderIsBuildable(int entity, int index);
native int BuilderIndexByRepresentative(int entity, int type);
native int BuilderRepresentativeByIndex(int entity, int type);
native void BuilderSetAsBuildableInternal(int entity, int type, bool value);

int m_hMyWeaponsLen = -1;
bool bSentByPlugin[MAXPLAYERS+1] = {false, ...};

ConVar tf_cheapobjects = null;

public APLRes AskPluginLoad2(Handle myself, bool late, char[] error, int err_max)
{
	CreateNative("BuilderSetAsBuildable", BuilderSetAsBuildableNative);
	return APLRes_Success;
}

int BuilderSetAsBuildableNative(Handle plugin, int params)
{
	int entity = GetNativeCell(1);
	int type = GetNativeCell(2);
	bool value = GetNativeCell(3);

	BuilderSetAsBuildableInternal(entity, type, value);

	SendProxy_Unhook(entity, "m_iObjectType", ProxyObjectType);

	for(int i = 0; i < OBJ_LAST; ++i) {
		SendProxy_UnhookArrayProp(entity, "m_aBuildableObjectTypes", i, Prop_Int, ProxyBuildableObjectTypes);
	}

	if(value) {
		if(type > OBJ_LAST) {
			SendProxy_Hook(entity, "m_iObjectType", Prop_Int, ProxyObjectType);

			CObjectInfo info = CObjectInfo.Get(type);
			int rep = info.GetInt("m_nRepresentative");

			if(rep != OBJ_LAST) {
				SendProxy_HookArrayProp(entity, "m_aBuildableObjectTypes", rep, Prop_Int, ProxyBuildableObjectTypes);
			}
		}
	}
}

public void OnPluginStart()
{
	AddCommandListener(command_build, "build");
	AddCommandListener(command_build, "destroy");

	RegAdminCmd("sm_dumpobjsinfo", sm_dumpobjsinfo, ADMFLAG_GENERIC);
	RegAdminCmd("sm_refreshbuilder", sm_refreshbuilder, ADMFLAG_GENERIC);

	tf_cheapobjects = FindConVar("tf_cheapobjects");
}

Action sm_refreshbuilder(int client, int args)
{
	if(args < 1) {
		ReplyToCommand(client, "[SM] Usage: sm_refreshbuilder <filter> [class]");
		return Plugin_Handled;
	}

	char filter[64];
	GetCmdArg(1, filter, sizeof(filter));

	char name[MAX_TARGET_LENGTH];
	bool isml = false;
	int targets[MAXPLAYERS];
	int count = ProcessTargetString(filter, client, targets, MAXPLAYERS, COMMAND_FILTER_ALIVE, name, sizeof(name), isml);
	if(count == 0) {
		ReplyToTargetError(client, count);
		return Plugin_Handled;
	}

	TFClassType class = TFClass_Unknown;

	if(args >= 2) {
		char arg[64];
		GetCmdArg(2, arg, sizeof(arg));

		class = TF2_GetClass(arg);
		if(class == TFClass_Unknown) {
			class = view_as<TFClassType>(StringToInt(arg));
		}
	}

	for(int i = 0; i < count; ++i) {
		int target = targets[i];

		TFClassType playerclass = TFClass_Unknown;

		if(class != TFClass_Unknown) {
			playerclass = class;
		} else {
			playerclass = TF2_GetPlayerClass(target);
		}

		ManageBuilderWeaponsByIndex(target, view_as<int>(playerclass));
	}

	return Plugin_Handled;
}

void PrintObjectInfoString(CObjectInfo info, const char[] name, int index = 0, const char[] prepend = "", const char[] append = "")
{
	char value[64];
	info.GetString(name, value, sizeof(value), index);

	PrintToServer("    %s%s%s = %s", prepend, name, append, value);
}

void PrintObjectInfoInt(CObjectInfo info, const char[] name)
{
	PrintToServer("    %s = %i", name, info.GetInt(name));
}

void PrintObjectInfoFloat(CObjectInfo info, const char[] name)
{
	PrintToServer("    %s = %f", name, info.GetFloat(name));
}

void PrintObjectInfoRepresentative(CObjectInfo info)
{
	int m_nRepresentative = info.GetInt("m_nRepresentative");

	CObjectInfo rep_info = CObjectInfo.Get(m_nRepresentative);

	char m_pObjectName_rep[64];
	rep_info.GetString("m_pObjectName", m_pObjectName_rep, sizeof(m_pObjectName_rep));

	PrintToServer("    m_nRepresentative = %i, %s", m_nRepresentative, m_pObjectName_rep);
}

void PrintObjectInfoName(CObjectInfo info)
{
	char m_pObjectName[64];
	info.GetString("m_pObjectName", m_pObjectName, sizeof(m_pObjectName));

	char m_pClassName[64];
	info.GetString("m_pClassName", m_pClassName, sizeof(m_pClassName));

	PrintToServer("%i - %s, %s", info.Index, m_pObjectName, m_pClassName);
}

void PrintObjectAltModes(CObjectInfo info)
{
	PrintObjectInfoInt(info, "m_iNumAltModes");

	PrintObjectInfoString(info, "m_AltModes", 0, "  ", "[0].pszStatusName");
	PrintObjectInfoString(info, "m_AltModes", 1, "  ", "[0].pszModeName");
	PrintObjectInfoString(info, "m_AltModes", 2, "  ", "[0].pszIconMenu");

	PrintObjectInfoString(info, "m_AltModes", 10, "  ", "[1].pszStatusName");
	PrintObjectInfoString(info, "m_AltModes", 11, "  ", "[1].pszModeName");
	PrintObjectInfoString(info, "m_AltModes", 12, "  ", "[1].pszIconMenu");

	PrintObjectInfoString(info, "m_AltModes", 20, "  ", "[2].pszStatusName");
	PrintObjectInfoString(info, "m_AltModes", 21, "  ", "[2].pszModeName");
	PrintObjectInfoString(info, "m_AltModes", 22, "  ", "[2].pszIconMenu");
}

void DumpObjectInfo(CObjectInfo info)
{
	PrintObjectInfoName(info);
	PrintObjectInfoRepresentative(info);

	PrintObjectInfoString(info, "m_pStatusName");
	PrintObjectInfoFloat(info, "m_flBuildTime");
	PrintObjectInfoInt(info, "m_nMaxObjects");
	PrintObjectInfoInt(info, "m_Cost");
	PrintObjectInfoFloat(info, "m_CostMultiplierPerInstance");
	PrintObjectInfoInt(info, "m_UpgradeCost");
	PrintObjectInfoInt(info, "m_MaxUpgradeLevel");
	PrintObjectInfoString(info, "m_pBuilderWeaponName");
	PrintObjectInfoString(info, "m_pBuilderPlacementString");
	PrintObjectInfoInt(info, "m_SelectionSlot");
	PrintObjectInfoInt(info, "m_SelectionPosition");
	PrintObjectInfoInt(info, "m_bSolidToPlayerMovement");
	PrintObjectInfoInt(info, "m_bUseItemInfo");
	PrintObjectInfoString(info, "m_pViewModel");
	PrintObjectInfoString(info, "m_pPlayerModel");
	PrintObjectInfoInt(info, "m_iDisplayPriority");
	PrintObjectInfoInt(info, "m_bVisibleInWeaponSelection");
	PrintObjectInfoString(info, "m_pExplodeSound");
	PrintObjectInfoString(info, "m_pExplosionParticleEffect");
	PrintObjectInfoInt(info, "m_bAutoSwitchTo");
	PrintObjectInfoString(info, "m_pUpgradeSound");
	PrintObjectInfoFloat(info, "m_flUpgradeDuration");
	PrintObjectInfoInt(info, "m_iBuildCount");
	PrintObjectAltModes(info);
	PrintObjectInfoString(info, "m_pIconActive");
	PrintObjectInfoString(info, "m_pIconInactive");
	PrintObjectInfoString(info, "m_pIconMenu");
	PrintObjectInfoString(info, "m_pHudStatusIcon");
	PrintObjectInfoInt(info, "m_iMetalToDropInGibs");
	PrintObjectInfoInt(info, "m_bRequiresOwnBuilder");
	PrintObjectInfoInt(info, "m_nBaseHealth");
}

Action sm_dumpobjsinfo(int client, int args)
{
	int num = CObjectInfo.Count();

	if(args == 0) {
		for(int i = 0; i < num; ++i) {
			CObjectInfo info = CObjectInfo.Get(i);
			DumpObjectInfo(info);
		}
	} else if(args == 1) {
		char arg[64];
		GetCmdArg(1, arg, sizeof(arg));

		if(StrEqual(arg, "custom")) {
			for(int i = OBJ_LAST+1; i < num; ++i) {
				CObjectInfo info = CObjectInfo.Get(i);
				DumpObjectInfo(info);
			}
		} else {
			int i = -1;
			int ret = StringToIntEx(arg, i);
			if(ret != 0) {
				if(i >= 0 && i < num) {
					CObjectInfo info = CObjectInfo.Get(i);
					DumpObjectInfo(info);
				} else {
					ReplyToCommand(client, "invalid index %i", i);
				}
			} else {
				CObjectInfo info = CObjectInfo.Find(arg);
				if(info != CObjectInfo_Null) {
					DumpObjectInfo(info);
				} else {
					ReplyToCommand(client, "unknown object %s", arg);
				}
			}
		}
	} else {
		ReplyToCommand(client, "sm_dumpobjsinfo [name/index|custom]");
	}

	return Plugin_Handled;
}

public void OnClientDisconnect(int client)
{
	bSentByPlugin[client] = false;
}

Action HandleBuildCommand(int client, const char[] command, int args)
{
	if(m_hMyWeaponsLen == -1) {
		m_hMyWeaponsLen = GetEntPropArraySize(client, Prop_Send, "m_hMyWeapons");
	}

	if(bSentByPlugin[client]) {
		bSentByPlugin[client] = false;
		return Plugin_Continue;
	}

	if(args < 2) {
		return Plugin_Continue;
	}

	for(int i = 0; i < m_hMyWeaponsLen; ++i) {
		int weapon = GetEntPropEnt(client, Prop_Send, "m_hMyWeapons", i);
		if(weapon == -1) {
			continue;
		}

		if(!HasEntProp(weapon, Prop_Send, "m_iObjectType")) {
			continue;
		}

		int type = GetCmdArgInt(1);

		int m_iObjectType = BuilderIndexByRepresentative(weapon, type);
		if(m_iObjectType == -1) {
			continue;
		}

		int mode = args >= 3 ? GetCmdArgInt(2) : 0;

		bSentByPlugin[client] = true;
		ClientCommand(client, "%s %i %i", command, m_iObjectType, mode);
		return Plugin_Stop;
	}

	return Plugin_Continue;
}

Action command_build(int client, const char[] command, int args)
{
	return HandleBuildCommand(client, command, args);
}

Action ProxyObjectType(int iEntity, const char[] cPropName, int &iValue, int iElement)
{
	if(iValue >= CObjectInfo.Count()) {
		iValue = 0;
		return Plugin_Changed;
	} else {
		CObjectInfo info = CObjectInfo.Get(iValue);
		iValue = info.GetInt("m_nRepresentative");
		return Plugin_Changed;
	}
}

void ObjectOnSpawnPost(int entity)
{
	int m_iObjectType = GetEntProp(entity, Prop_Send, "m_iObjectType");
	if(m_iObjectType > OBJ_LAST) {
		SendProxy_Hook(entity, "m_iObjectType", Prop_Int, ProxyObjectType);
	}

	SDKUnhook(entity, SDKHook_SpawnPost, ObjectOnSpawnPost);
}

Action ProxyBuildableObjectTypes(int iEntity, const char[] cPropName, int &iValue, int iElement)
{
	iValue = 1;
	return Plugin_Changed;
}

void BuilderOnSpawnPost(int entity)
{
	for(int i = 0; i < OBJ_LAST; ++i) {
		if(BuilderIndexByRepresentative(entity, i) == -1) {
			continue;
		}

		SendProxy_HookArrayProp(entity, "m_aBuildableObjectTypes", i, Prop_Int, ProxyBuildableObjectTypes);
	}
}

public void OnEntityCreated(int entity, const char[] classname)
{
	if(HasEntProp(entity, Prop_Send, "m_iObjectType")) {
		SDKHook(entity, SDKHook_SpawnPost, ObjectOnSpawnPost);
	}
	if(HasEntProp(entity, Prop_Send, "m_aBuildableObjectTypes")) {
		SDKHook(entity, SDKHook_SpawnPost, BuilderOnSpawnPost);
	}
}
