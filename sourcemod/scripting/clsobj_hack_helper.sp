#include <sourcemod>
#include <clsobj_hack>
#include <proxysend>
#include <sdkhooks>
#include <tf2_stocks>

//#define DEBUG

native int BuilderGetNumBuildables(int entity);
native TFObjectType BuilderGetBuildableIndex(int entity, int index);
native TFObjectType BuilderIndexByRepresentative(int entity, TFObjectType type);
native TFObjectType BuilderRepresentativeByIndex(int entity, TFObjectType type);
native void BuilderSetAsBuildableInternal(int entity, TFObjectType type, bool value);

int m_hMyWeaponsLen = -1;
bool bSentByPlugin[MAXPLAYERS+1] = {false, ...};

ConVar tf_cheapobjects = null;

bool late_loaded;

public APLRes AskPluginLoad2(Handle myself, bool late, char[] error, int err_max)
{
	CreateNative("BuilderSetAsBuildable", BuilderSetAsBuildableNative);
	late_loaded = late;
	return APLRes_Success;
}

int BuilderSetAsBuildableNative(Handle plugin, int params)
{
	int entity = GetNativeCell(1);
	TFObjectType type = GetNativeCell(2);
	bool value = GetNativeCell(3);

	BuilderSetAsBuildableInternal(entity, type, value);

	proxysend_unhook(entity, "m_iObjectType", ProxyObjectTypeBuilder);
	proxysend_unhook(entity, "m_aBuildableObjectTypes", ProxyBuildableObjectTypes);

#if defined DEBUG
	PrintToServer("BuilderSetAsBuildable %i %i %i", entity, type, value);
#endif

	if(value) {
		if(type > OBJ_LAST) {
			proxysend_hook(entity, "m_iObjectType", ProxyObjectTypeBuilder, false);
			proxysend_hook(entity, "m_aBuildableObjectTypes", ProxyBuildableObjectTypes, false);
		}
	}

	return 0;
}

public void OnPluginStart()
{
	AddCommandListener(command_build, "build");
	AddCommandListener(command_build, "destroy");

	RegAdminCmd("sm_dumpobjsinfo", sm_dumpobjsinfo, ADMFLAG_GENERIC);
	RegAdminCmd("sm_refreshbuilder", sm_refreshbuilder, ADMFLAG_GENERIC);
	RegAdminCmd("sm_dumpbuilder", sm_dumpbuilder, ADMFLAG_GENERIC);

#if defined DEBUG
	HookEvent("player_builtobject", player_builtobject);
#endif

	tf_cheapobjects = FindConVar("tf_cheapobjects");

	if(late_loaded) {
		for(int i = 1; i <= MaxClients; ++i) {
			if(IsClientInGame(i)) {
				OnClientPutInServer(i);
			}
		}

		int entity = -1;
		char classname[64];
		while((entity = FindEntityByClassname(entity, "*")) != -1) {
			GetEntityClassname(entity, classname, sizeof(classname));
			OnEntityCreated(entity, classname);

			if(HasEntProp(entity, Prop_Send, "m_aBuildableObjectTypes")) {
				BuilderOnSpawnPost(entity);
			} else if(HasEntProp(entity, Prop_Send, "m_iObjectType")) {
				ObjectOnSpawnPost(entity);
			}
		}
	}
}

#if defined DEBUG
static void player_builtobject(Event event, const char[] name, bool dontBroadcast)
{
	int client = GetClientOfUserId(event.GetInt("userid"));
	TFObjectType type = view_as<TFObjectType>(event.GetInt("object"));
	int entity = event.GetInt("index");

	PrintToServer("player_builtobject %i %i %i", client, type, entity);
}
#endif

Action sm_dumpbuilder(int client, int args)
{
	if(m_hMyWeaponsLen == -1) {
		m_hMyWeaponsLen = GetEntPropArraySize(client, Prop_Send, "m_hMyWeapons");
	}

	char classname[64];
	for(int i = 0; i < m_hMyWeaponsLen; ++i) {
		int weapon = GetEntPropEnt(client, Prop_Send, "m_hMyWeapons", i);
		if(weapon == -1) {
			continue;
		}

		if(!HasEntProp(weapon, Prop_Send, "m_iObjectType")) {
			continue;
		}

		GetEntityClassname(weapon, classname, sizeof(classname));

		int m_iObjectType = GetEntProp(weapon, Prop_Send, "m_iObjectType");
		PrintToConsole(client, "slot: %i - ent: %i - %s:\n  m_iObjectType = %i", i, weapon, classname, m_iObjectType);
		for(int j = 0, len = BuilderGetNumBuildables(weapon); j < len; ++j) {
			TFObjectType type = BuilderGetBuildableIndex(weapon, j);
			bool buildable = BuilderIsBuildable(weapon, view_as<TFObjectType>(j));
			PrintToConsole(client, "  m_aBuildableObjectTypes[%i, %i] = %i", j, type, buildable);
		}
	}

	return Plugin_Handled;
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

		ManageBuilderWeaponsByIndex(target, playerclass);
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
	TFObjectType m_nRepresentative = view_as<TFObjectType>(info.GetInt("m_nRepresentative"));

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
			CObjectInfo info = CObjectInfo.Get(view_as<TFObjectType>(i));
			DumpObjectInfo(info);
		}
	} else if(args == 1) {
		char arg[64];
		GetCmdArg(1, arg, sizeof(arg));

		if(StrEqual(arg, "custom")) {
			for(int i = (view_as<int>(OBJ_LAST)+1); i < num; ++i) {
				CObjectInfo info = CObjectInfo.Get(view_as<TFObjectType>(i));
				DumpObjectInfo(info);
			}
		} else {
			int i = -1;
			int ret = StringToIntEx(arg, i);
			if(ret != 0) {
				if(i >= 0 && i < num) {
					CObjectInfo info = CObjectInfo.Get(view_as<TFObjectType>(i));
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

		TFObjectType type = view_as<TFObjectType>(GetCmdArgInt(1));

		TFObjectType m_iObjectType = BuilderIndexByRepresentative(weapon, type);

		int mode = args >= 3 ? GetCmdArgInt(2) : 0;

	#if defined DEBUG
		PrintToServer("HandleBuildCommand %i %s %i %i %i", client, command, type, mode, m_iObjectType);
	#endif

		if(m_iObjectType == BUILDER_INVALID_OBJECT) {
			continue;
		}

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

Action ProxyObjectType(int &iValue)
{
	if(iValue >= CObjectInfo.Count()) {
		iValue = 0;
		return Plugin_Changed;
	} else {
		CObjectInfo info = CObjectInfo.Get(view_as<TFObjectType>(iValue));
		iValue = info.GetInt("m_nRepresentative");
		return Plugin_Changed;
	}
}

Action ProxyObjectTypeObject(int iEntity, const char[] cPropName, int &iValue, int iElement, int client)
{
#if defined DEBUG
	PrintToServer("ProxyObjectTypeObject %i %i", iEntity, iValue);
#endif
	return ProxyObjectType(iValue);
}

Action ProxyObjectTypeBuilder(int iEntity, const char[] cPropName, int &iValue, int iElement, int client)
{
#if defined DEBUG && 0
	PrintToServer("ProxyObjectTypeBuilder %i %i", iEntity, iValue);
#endif
	return ProxyObjectType(iValue);
}

void ObjectOnSpawnPost(int entity)
{
	TFObjectType m_iObjectType = view_as<TFObjectType>(GetEntProp(entity, Prop_Send, "m_iObjectType"));
	if(m_iObjectType > OBJ_LAST) {
		proxysend_hook(entity, "m_iObjectType", ProxyObjectTypeObject, false);
	}

#if defined DEBUG
	PrintToServer("ObjectOnSpawnPost %i %i", entity, m_iObjectType);
#endif

	SDKUnhook(entity, SDKHook_SpawnPost, ObjectOnSpawnPost);
}

Action ProxyBuildableObjectTypes(int iEntity, const char[] cPropName, bool &iValue, int iElement, int client)
{
	Action ret = Plugin_Continue;

	if(BuilderIndexByRepresentative(iEntity, view_as<TFObjectType>(iElement)) != BUILDER_INVALID_OBJECT) {
		iValue = true;
		ret = Plugin_Changed;
	}

#if defined DEBUG && 0
	PrintToServer("ProxyBuildableObjectTypes %i %i", iElement, iValue);
	if(iElement == 3) {
		PrintToServer("\n");
	}
#endif

	return ret;
}

void BuilderOnSpawnPost(int entity)
{
	proxysend_hook(entity, "m_iObjectType", ProxyObjectTypeBuilder, false);
	proxysend_hook(entity, "m_aBuildableObjectTypes", ProxyBuildableObjectTypes, false);

#if defined DEBUG
	PrintToServer("BuilderOnSpawnPost %i", entity);
#endif
}

Action ProxyPlayerClass(int iEntity, const char[] cPropName, int &iValue, int iElement, int client)
{
	if(iValue >= TFPlayerClassData.Count()) {
		iValue = view_as<int>(TFClass_Unknown);
		return Plugin_Changed;
	} else {
		TFPlayerClassData info = TFPlayerClassData.Get(view_as<TFClassType>(iValue));
		iValue = info.GetInt("m_nRepresentative");
	#if defined DEBUG && 0
		PrintToServer("ProxyPlayerClass %i", iValue);
	#endif
		return Plugin_Changed;
	}
}

public void OnClientPutInServer(int client)
{
	proxysend_hook(client, "m_iClass", ProxyPlayerClass, false);
	proxysend_hook(client, "m_iDesiredPlayerClass", ProxyPlayerClass, false);

#if defined DEBUG
	PrintToServer("OnClientPutInServer %i", client);
#endif
}

public void OnEntityCreated(int entity, const char[] classname)
{
	if(HasEntProp(entity, Prop_Send, "m_aBuildableObjectTypes")) {
		SDKHook(entity, SDKHook_SpawnPost, BuilderOnSpawnPost);
	} else if(HasEntProp(entity, Prop_Send, "m_iObjectType")) {
		SDKHook(entity, SDKHook_SpawnPost, ObjectOnSpawnPost);
	}
}
