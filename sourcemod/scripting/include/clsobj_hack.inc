#if defined clsobjhack_included
	#endinput
#endif
#define clsobjhack_included

#include <tf2>

enum
{
	TF_AMMO_DUMMY = 0,	// Dummy index to make the CAmmoDef indices correct for the other ammo types.
	TF_AMMO_PRIMARY,
	TF_AMMO_SECONDARY,
	TF_AMMO_METAL,
	TF_AMMO_GRENADES1,
	TF_AMMO_GRENADES2,
	TF_AMMO_GRENADES3,	// Utility Slot Grenades
	TF_AMMO_COUNT,
};

#define TF_CLASS_RANDOM TF_CLASS_COUNT_ALL+1
#define TF_CLASS_COUNT_ALL TF_CLASS_CIVILIAN+1
#define TF_CLASS_CIVILIAN TFClass_Engineer+1

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

#define OBJ_LAST (view_as<int>(TFObject_Sapper)+1)

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
#define BUILDER_INVALID_OBJECT ((1 << BUILDER_OBJECT_BITS) - 1)

#define MAX_OBJECTS_PER_PLAYER	6

methodmap CObjectInfo
{
	public static native CObjectInfo Find(const char[] name);
	public static native CObjectInfo Get(int index);
	public static native CObjectInfo Create(KeyValues kv);
	public static native CObjectInfo CloneByName(const char[] name);
	public static native CObjectInfo CloneByIndex(int index);
	public static native int Count();
	public static native int GetIndex(const char[] name);
	public static native void RemoveByName(const char[] name);
	public static native void RemoveByIndex(int index);

	public native CObjectInfo Clone();

	public native void SetString(const char[] name, const char[] value, int index = 0);
	public native void SetFloat(const char[] name, float value);
	public native void SetInt(const char[] name, int value);

	public native void GetString(const char[] name, const char[] value, int length, int index = 0);
	public native float GetFloat(const char[] name);
	public native int GetInt(const char[] name);
};

#define CObjectInfo_Null view_as<CObjectInfo>(Address_Null)

methodmap TFPlayerClassData_t
{
	public static native TFPlayerClassData_t Find(const char[] name);
	public static native TFPlayerClassData_t Get(int index);
	public static native TFPlayerClassData_t Create(KeyValues kv);
	public static native TFPlayerClassData_t CloneByName(const char[] name);
	public static native TFPlayerClassData_t CloneByIndex(int index);
	public static native int Count();
	public static native int GetIndex(const char[] name);
	public static native void RemoveByName(const char[] name);
	public static native void RemoveByIndex(int index);

	public native TFPlayerClassData_t Clone();

	public native void SetString(const char[] name, const char[] value, int index = 0);
	public native void SetFloat(const char[] name, float value);
	public native void SetInt(const char[] name, int value, int index = 0);
	public native void SetVector(const char[] name, float value[3]);

	public native void GetString(const char[] name, const char[] value, int length, int index = 0);
	public native float GetFloat(const char[] name, int index = 0);
	public native int GetInt(const char[] name, int index = 0);
	public native void GetVector(const char[] name, float value[3]);
};

#define TFPlayerClassData_Null view_as<TFPlayerClassData_t>(Address_Null)

native int BuilderGetNumBuildables(int entity);
native int BuilderGetBuildableIndex(int entity, int index);
native int BuilderIsBuildable(int entity, int index);

#if !defined REQUIRE_EXTENSIONS
public void __ext_clsobj_hack_SetNTVOptional()
{
	
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
