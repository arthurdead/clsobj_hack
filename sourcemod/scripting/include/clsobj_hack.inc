#if defined clsobjhack_included
	#endinput
#endif
#define clsobjhack_included

#include <tf2>
#include <tf2_stocks>
#tryinclude <wpnhack>
#tryinclude <datamaps>

#define TF_CLASS_RANDOM TF_CLASS_COUNT_ALL+1
#define TF_CLASS_COUNT_ALL (view_as<int>(TF_CLASS_CIVILIAN)+1)
#define TF_CLASS_CIVILIAN view_as<TFClassType>(view_as<int>(TFClass_Engineer)+1)

#define TF_CLASS_COUNT			( TF_CLASS_COUNT_ALL )

#define TF_FIRST_NORMAL_CLASS	( TFClass_Unknown + 1 )
#define TF_LAST_NORMAL_CLASS	( TF_CLASS_CIVILIAN )

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

#define OBJECT_MAX_GIB_MODELS	9
#define OBJECT_MAX_MODES		3

#define OBJ_LAST (view_as<TFObjectType>(view_as<int>(TFObject_Sapper)+1))

enum
{
	OF_ALLOW_REPEAT_PLACEMENT				= 0x01,
	OF_MUST_BE_BUILT_ON_ATTACHMENT			= 0x02,
	OF_DOESNT_HAVE_A_MODEL					= 0x04,
	OF_PLAYER_DESTRUCTION					= 0x08,

	//
	// ADD NEW ITEMS HERE TO AVOID BREAKING DEMOS
	//

	OF_BIT_COUNT	= 4
};

enum
{
	CB_CAN_BUILD,			// Player is allowed to build this object
	CB_CANNOT_BUILD,		// Player is not allowed to build this object
	CB_LIMIT_REACHED,		// Player has reached the limit of the number of these objects allowed
	CB_NEED_RESOURCES,		// Player doesn't have enough resources to build this object
	CB_NEED_ADRENALIN,		// Commando doesn't have enough adrenalin to build a rally flag
	CB_UNKNOWN_OBJECT,		// Error message, tried to build unknown object
};

#define BUILDER_OBJECT_BITS 8
#define BUILDER_INVALID_OBJECT view_as<TFObjectType>((1 << BUILDER_OBJECT_BITS) - 1)

#define MAX_OBJECTS_PER_PLAYER	6

methodmap CObjectInfo
{
	public static native CObjectInfo Find(const char[] name);
	public static native CObjectInfo Get(TFObjectType index);
	public static native int Count();
	public static native TFObjectType GetIndex(const char[] name);

	public native int GetString(const char[] name, char[] value, int length, int index = 0);
	public native float GetFloat(const char[] name);
	public native int GetInt(const char[] name);

	property TFObjectType Index
	{
		public native get();
	}
};

#define CObjectInfo_Null view_as<CObjectInfo>(Address_Null)

methodmap CObjectInfoCustom < Handle
{
	public static native CObjectInfoCustom Create(KeyValues kv);
	public static native CObjectInfoCustom CloneByName(const char[] name);
	public static native CObjectInfoCustom CloneByIndex(TFObjectType index);

	public native void SetString(const char[] name, const char[] value, int index = 0);
	public native void SetFloat(const char[] name, float value);
	public native void SetInt(const char[] name, int value);

	property CObjectInfo BaseInfo
	{
		public native get();
	}

	public int GetString(const char[] name, char[] value, int length, int index = 0)
	{ return this.BaseInfo.GetString(name, value, length, index); }
	public float GetFloat(const char[] name)
	{ return this.BaseInfo.GetFloat(name); }
	public int GetInt(const char[] name)
	{ return this.BaseInfo.GetInt(name); }

	property TFObjectType Index
	{
		public get()
		{ return this.BaseInfo.Index; }
	}
};

methodmap TFPlayerClassData
{
	public static native TFPlayerClassData Find(const char[] name);
	public static native TFPlayerClassData Get(TFClassType index);
	public static native int Count();
	public static native TFClassType GetIndex(const char[] name);

	public native int GetString(const char[] name, char[] value, int length, int index = 0);
	public native float GetFloat(const char[] name, int index = 0);
	public native int GetInt(const char[] name, int index = 0);

	property TFClassType Index
	{
		public native get();
	}
};

#define TFPlayerClassData_Null view_as<TFPlayerClassData>(Address_Null)

methodmap TFPlayerClassDataCustom < Handle
{
	public static native TFPlayerClassDataCustom Create(KeyValues kv);
	public static native TFPlayerClassDataCustom CloneByName(const char[] name);
	public static native TFPlayerClassDataCustom CloneByIndex(TFClassType index);

	public native void SetString(const char[] name, const char[] value, int index = 0);
	public native void SetFloat(const char[] name, float value, int index = 0);
	public native void SetInt(const char[] name, int value, int index = 0);

	property TFPlayerClassData BaseData
	{
		public native get();
	}

	public int GetString(const char[] name, char[] value, int length, int index = 0)
	{ return this.BaseData.GetString(name, value, length, index); }
	public float GetFloat(const char[] name, int index = 0)
	{ return this.BaseData.GetFloat(name, index); }
	public int GetInt(const char[] name, int index = 0)
	{ return this.BaseData.GetInt(name, index); }

	property TFClassType Index
	{
		public get()
		{ return this.BaseData.Index; }
	}
};

stock TFClassType TF2_GetPlayerClassEx(int client)
{
	TFClassType type = TF2_GetPlayerClass(client);
	if(view_as<int>(type) >= TF_CLASS_COUNT_ALL) {
		TFPlayerClassData data = TFPlayerClassData.Get(type);
		type = view_as<TFClassType>(data.GetInt("m_nRepresentative"));
	}
	return type;
}

stock TFClassType TF2_GetClassEx(const char[] name)
{
	TFClassType type = TF2_GetClass(name);
	if(type == TFClass_Unknown) {
		char tmp_name[TF_NAME_LENGTH];
		int len = TFPlayerClassData.Count();
		for(int i = (view_as<int>(TFClass_Engineer)+1); i < len; ++i) {
			TFPlayerClassData data = TFPlayerClassData.Get(view_as<TFClassType>(i));
			data.GetString("m_szClassName", tmp_name, sizeof(tmp_name));
			if(StrEqual(tmp_name, name)) {
				return view_as<TFClassType>(i);
			}
		}
	}
	return TFClass_Unknown;
}

native void ManageBuilderWeaponsEx(int client, TFPlayerClassData class);
native void BuilderSetAsBuildable(int entity, TFObjectType type, bool value);
native bool BuilderIsBuildable(int entity, TFObjectType type);

stock int GetBuilderForObjectType(int client, TFObjectType type)
{
	static int m_hMyWeaponsLen = -1;
	if(m_hMyWeaponsLen == -1) {
		m_hMyWeaponsLen = GetEntPropArraySize(client, Prop_Send, "m_hMyWeapons");
	}

	for(int i = 0; i < m_hMyWeaponsLen; ++i) {
		int weapon = GetEntPropEnt(client, Prop_Send, "m_hMyWeapons", i);
		if(weapon == -1) {
			continue;
		}

		if(!HasEntProp(weapon, Prop_Send, "m_iObjectType")) {
			continue;
		}

		if(type == BUILDER_INVALID_OBJECT || BuilderIsBuildable(weapon, type)) {
			return weapon;
		}
	}

	return -1;
}

stock void ManageBuilderWeaponsByIndex(int client, TFClassType class)
{
	TFPlayerClassData data = TFPlayerClassData.Get(class);

	ManageBuilderWeaponsEx(client, data);
}

stock void ManageBuilderWeapons(int client)
{
	TFClassType class = TF2_GetPlayerClass(client);

	ManageBuilderWeaponsByIndex(client, class);
}

forward Action ClassCanBuildObject(int client, TFClassType class, TFObjectType type, bool &can);

native Address AllocateBaseObject(int size_modifier);
native int GetBaseObjectSize();

native Address AllocateBaseObjectUpgrade(int size_modifier);
native int GetBaseObjectUpgradeSize();

#if defined datamaps_included
static Address datamaps_allocatebaseobject(int size_modifier, any data)
{
	return AllocateBaseObject(size_modifier);
}

static Address datamaps_allocatebaseobjectupgrade(int size_modifier, any data)
{
	return AllocateBaseObjectUpgrade(size_modifier);
}

static CustomDatamap __register_object(CustomEntityFactory factory, const char[] name, TFObjectType clienttype = BUILDER_INVALID_OBJECT)
{
	char netname[64];
	netname[0] = 'C';
	strcopy(netname[1], sizeof(netname)-1, name);

	CustomDatamap datamap = CustomDatamap.from_factory(factory);
	datamap.set_name(netname);

	CustomSendtable sendtable = CustomSendtable.from_factory(factory);
	if(clienttype != BUILDER_INVALID_OBJECT) {
		switch(clienttype) {
			case TFObject_Sentry: sendtable.override_with("CObjectSentrygun");
			case TFObject_Dispenser: sendtable.override_with("CObjectDispenser");
			case TFObject_Teleporter: sendtable.override_with("CObjectTeleporter");
			case TFObject_Sapper: sendtable.override_with("CObjectSapper");
			default: SetFailState("invalid object type %i", clienttype);
		}
	}
	sendtable.set_network_name(netname);

	return datamap;
}

stock CustomDatamap register_baseobject(const char[] classname, const char[] name, TFObjectType clienttype = BUILDER_INVALID_OBJECT)
{
	CustomEntityFactory factory = EntityFactoryDictionary.register_function(classname, datamaps_allocatebaseobject, GetBaseObjectSize());
	return __register_object(factory, name, clienttype);
}

stock CustomDatamap register_baseobjectupgrade(const char[] classname, const char[] name, TFObjectType clienttype = BUILDER_INVALID_OBJECT)
{
	CustomEntityFactory factory = EntityFactoryDictionary.register_function(classname, datamaps_allocatebaseobjectupgrade, GetBaseObjectUpgradeSize());
	return __register_object(factory, name, clienttype);
}

stock CustomDatamap register_object(const char[] classname, const char[] name, TFObjectType based, TFObjectType clienttype = BUILDER_INVALID_OBJECT)
{
	CustomEntityFactory factory = null;

	switch(based) {
		case TFObject_Sentry: factory = EntityFactoryDictionary.register_based_name(classname, "obj_sentrygun");
		case TFObject_Dispenser: factory = EntityFactoryDictionary.register_based_name(classname, "obj_dispenser");
		case TFObject_Teleporter: factory = EntityFactoryDictionary.register_based_name(classname, "obj_teleporter");
		case TFObject_Sapper: factory = EntityFactoryDictionary.register_based_name(classname, "obj_attachment_sapper");
		default: SetFailState("invalid object type %i", based);
	}

	return __register_object(factory, name, clienttype);
}
#endif

#if !defined REQUIRE_EXTENSIONS
public void __ext_clsobj_hack_SetNTVOptional()
{
	MarkNativeAsOptional("CObjectInfo.Find");
	MarkNativeAsOptional("CObjectInfo.Get");
	MarkNativeAsOptional("CObjectInfo.Count");
	MarkNativeAsOptional("CObjectInfo.GetIndex");
	MarkNativeAsOptional("CObjectInfo.GetString");
	MarkNativeAsOptional("CObjectInfo.GetFloat");
	MarkNativeAsOptional("CObjectInfo.GetInt");
	MarkNativeAsOptional("CObjectInfo.Index.get");

	MarkNativeAsOptional("CObjectInfoCustom.Create");
	MarkNativeAsOptional("CObjectInfoCustom.CloneByName");
	MarkNativeAsOptional("CObjectInfoCustom.CloneByIndex");
	MarkNativeAsOptional("CObjectInfoCustom.SetString");
	MarkNativeAsOptional("CObjectInfoCustom.SetFloat");
	MarkNativeAsOptional("CObjectInfoCustom.SetInt");
	MarkNativeAsOptional("CObjectInfoCustom.BaseInfo.get");

	MarkNativeAsOptional("TFPlayerClassData.Find");
	MarkNativeAsOptional("TFPlayerClassData.Get");
	MarkNativeAsOptional("TFPlayerClassData.Count");
	MarkNativeAsOptional("TFPlayerClassData.GetIndex");
	MarkNativeAsOptional("TFPlayerClassData.GetString");
	MarkNativeAsOptional("TFPlayerClassData.GetFloat");
	MarkNativeAsOptional("TFPlayerClassData.GetInt");
	MarkNativeAsOptional("TFPlayerClassData.Index.get");

	MarkNativeAsOptional("TFPlayerClassDataCustom.Create");
	MarkNativeAsOptional("TFPlayerClassDataCustom.CloneByName");
	MarkNativeAsOptional("TFPlayerClassDataCustom.CloneByIndex");
	MarkNativeAsOptional("TFPlayerClassDataCustom.SetString");
	MarkNativeAsOptional("TFPlayerClassDataCustom.SetFloat");
	MarkNativeAsOptional("TFPlayerClassDataCustom.SetInt");
	MarkNativeAsOptional("TFPlayerClassDataCustom.BaseData.get");

	MarkNativeAsOptional("ManageBuilderWeaponsEx");
	MarkNativeAsOptional("BuilderSetAsBuildable");
	MarkNativeAsOptional("BuilderIsBuildable");

	MarkNativeAsOptional("AllocateBaseObject");
	MarkNativeAsOptional("GetBaseObjectSize");
}
#endif

public Extension __ext_clsobj_hack =
{
	name = "clsobj_hack", 
	file = "clsobj_hack.ext", 
	
#if defined AUTOLOAD_EXTENSIONS
	autoload=1,
#else
	autoload=0,
#endif

#if defined REQUIRE_EXTENSIONS
	required=1,
#else
	required=0,
#endif
};

public SharedPlugin __pl_clsobj_hack =
{
	name = "clsobj_hack",
	file = "clsobj_hack.smx",
#if defined REQUIRE_PLUGIN
	required = 1,
#else
	required = 0,
#endif
};
