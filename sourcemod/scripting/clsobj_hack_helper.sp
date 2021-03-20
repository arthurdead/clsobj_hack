#include <sourcemod>
#include <clsobj_hack>
#include <sendproxy>
#include <sdkhooks>

int m_hMyWeaponsLen = -1;
bool bSentByPlugin[MAXPLAYERS+1] = {false, ...};

public void OnPluginStart()
{
	AddCommandListener(command_build, "build");
	AddCommandListener(command_build, "destroy");
}

public void OnClientDisconnected(int client)
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

		int num = BuilderGetNumBuildables(weapon);
		if(num == OBJ_LAST) {
			continue;
		}

		int type = GetCmdArgInt(1);

		int m_iObjectType = BUILDER_INVALID_OBJECT;

		bool valid = false;

		for(int j = OBJ_LAST; j < num; ++j) {
			m_iObjectType = BuilderGetBuildableIndex(weapon, j);

			CObjectInfo info = CObjectInfo.Get(m_iObjectType);
			int m_nRepresentative = info.GetInt("m_nRepresentative");
			if(m_nRepresentative == type) {
				valid = true;
				break;
			}
		}

		if(!valid) {
			continue;
		}

		int mode = args >= 3 ? GetCmdArgInt(2) : 0;

		bSentByPlugin[client] = true;
		ClientCommand(client, "%s %i %i", command, m_iObjectType, mode);
		return Plugin_Stop;
	}

	return Plugin_Continue;
}

public Action OnClientCommand(int client, int args)
{
	char cmd[64];
	GetCmdArg(0, cmd, sizeof(cmd));

	if(StrEqual(cmd, "build")) {
		return HandleBuildCommand(client, cmd, args);
	} else if(StrEqual(cmd, "destroy")) {
		return HandleBuildCommand(client, cmd, args);
	}

	return Plugin_Continue;
}

Action command_build(int client, const char[] command, int args)
{
	return HandleBuildCommand(client, command, args);
}

Action m_iObjectType(int iEntity, const char[] cPropName, int &iValue, int iElement)
{
	if(iValue > OBJ_LAST) {
		CObjectInfo info = CObjectInfo.Get(iValue);
		iValue = info.GetInt("m_nRepresentative");
		return Plugin_Changed;
	}
	return Plugin_Continue;
}

Action m_aBuildableObjectTypes(int iEntity, const char[] cPropName, int &iValue, int iElement)
{
	//PrintToServer("m_aBuildableObjectTypes %i, %i", iValue, iElement);
	return Plugin_Continue;
}

void ObjectOnSpawnPost(int entity)
{
	SendProxy_Hook(entity, "m_iObjectType", Prop_Int, m_iObjectType);
}

void BuilderOnSpawnPost(int entity)
{
	for(int i = 0; i < OBJ_LAST; ++i) {
		SendProxy_HookArrayProp(entity, "m_aBuildableObjectTypes", i, Prop_Int, m_aBuildableObjectTypes);
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