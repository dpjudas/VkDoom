#pragma once 

#include <variant>
#include "dobject.h"
#include "serializer.h"
#include "d_event.h"
#include "p_local.h"
#include "sbar.h"
#include "info.h"
#include "vm.h"

class DStaticEventHandler;
struct EventManager;
struct line_t;
struct sector_t;
struct FLevelLocals;

enum class EventHandlerType
{
	Global,
	PerMap
};

enum ENetCmd
{
	NET_INT8 = 1,
	NET_INT16,
	NET_INT,
	NET_FLOAT,
	NET_DOUBLE,
	NET_STRING,
};

struct FNetworkCommand
{
private:
	size_t _index = 0;
	TArray<uint8_t> _stream;

public:
	int Player;
	FName Command;

	FNetworkCommand(const int player, const FName& command, TArray<uint8_t>& stream) : Player(player), Command(command)
	{
		_stream.Swap(stream);
	}

	inline bool EndOfStream() const
	{
		return _index >= _stream.Size();
	}

	inline void Reset()
	{
		_index = 0;
	}

	int ReadInt8()
	{
		if (EndOfStream())
			return 0;

		return _stream[_index++];
	}

	// If a value has to cut off early, just treat the previous value as the full one.
	int ReadInt16()
	{
		if (EndOfStream())
			return 0;

		int value = _stream[_index++];
		if (!EndOfStream())
			value = (value << 8) | _stream[_index++];

		return value;
	}

	int ReadInt()
	{
		if (EndOfStream())
			return 0;

		int value = _stream[_index++];
		if (!EndOfStream())
		{
			value = (value << 8) | _stream[_index++];
			if (!EndOfStream())
			{
				value = (value << 8) | _stream[_index++];
				if (!EndOfStream())
					value = (value << 8) | _stream[_index++];
			}
		}

		return value;
	}

	// Floats without their first bits are pretty meaningless so those are done first.
	double ReadFloat()
	{
		if (EndOfStream())
			return 0.0;

		int value = _stream[_index++] << 24;
		if (!EndOfStream())
		{
			value |= _stream[_index++] << 16;
			if (!EndOfStream())
			{
				value |= _stream[_index++] << 8;
				if (!EndOfStream())
					value |= _stream[_index++];
			}
		}

		union
		{
			int32_t i;
			float f;
		} floatCaster;
		floatCaster.i = value;
		return floatCaster.f;
	}

	double ReadDouble()
	{
		if (EndOfStream())
			return 0.0;

		int64_t value = int64_t(_stream[_index++]) << 56;
		if (!EndOfStream())
		{
			value |= int64_t(_stream[_index++]) << 48;
			if (!EndOfStream())
			{
				value |= int64_t(_stream[_index++]) << 40;
				if (!EndOfStream())
				{
					value |= int64_t(_stream[_index++]) << 32;
					if (!EndOfStream())
					{
						value |= int64_t(_stream[_index++]) << 24;
						if (!EndOfStream())
						{
							value |= int64_t(_stream[_index++]) << 16;
							if (!EndOfStream())
							{
								value |= int64_t(_stream[_index++]) << 8;
								if (!EndOfStream())
									value |= int64_t(_stream[_index++]);
							}
						}
					}
				}
			}
		}

		union
		{
			int64_t i;
			double f;
		} floatCaster;
		floatCaster.i = value;
		return floatCaster.f;
	}

	const char* ReadString()
	{
		if (EndOfStream())
			return nullptr;

		const char* str = reinterpret_cast<const char*>(&_stream[_index]);
		_index += strlen(str) + 1;
		return str;
	}
};

class DNetworkBuffer final : public DObject
{
	DECLARE_CLASS(DNetworkBuffer, DObject)

public:
	struct BufferValue
	{
	private:
		ENetCmd _type;
		std::variant<int, double, FString> _message;

	public:
		BufferValue(const ENetCmd type, const int message) : _type(type), _message(message) {}
		BufferValue(const ENetCmd type, const double message) : _type(type), _message(message) {}
		BufferValue(const ENetCmd type, const FString& message) : _type(type), _message(message) {}

		inline ENetCmd GetType() const
		{
			return _type;
		}

		inline int GetInt() const
		{
			return std::get<int>(_message);
		}

		inline double GetDouble() const
		{
			return std::get<double>(_message);
		}

		inline const char* GetString() const
		{
			return std::get<FString>(_message).GetChars();
		}
	};

private:
	unsigned int _size = 0u;
	TArray<BufferValue> _buffer = {};

public:
	inline unsigned int GetBytes() const
	{
		return _size;
	}

	inline unsigned int GetBufferSize() const
	{
		return _buffer.Size();
	}

	inline const BufferValue& GetValue(unsigned int i) const
	{
		return _buffer[i];
	}

	void AddInt8(int byte);
	void AddInt16(int word);
	void AddInt(int msg);
	void AddFloat(double msg);
	void AddDouble(double msg);
	void AddString(const FString& msg);
	void OnDestroy() override;
	void Serialize(FSerializer& arc) override;
};

// ==============================================
//
//  EventHandler - base class
//
// ==============================================

class DStaticEventHandler : public DObject // make it a part of normal GC process
{
	DECLARE_CLASS(DStaticEventHandler, DObject);
	HAS_OBJECT_POINTERS
public:
	DStaticEventHandler()
	{
		prev = 0;
		next = 0;
		Order = 0;
		IsUiProcessor = false;
	}

	EventManager *owner;
	DStaticEventHandler* prev;
	DStaticEventHandler* next;
	virtual bool IsStatic() { return true; }

	//
	int Order;
	bool IsUiProcessor;
	bool RequireMouse;

	// serialization handler. let's keep it here so that I don't get lost in serialized/not serialized fields
	void Serialize(FSerializer& arc) override
	{
		Super::Serialize(arc);
		/*
		if (arc.isReading())
		{
			Printf("DStaticEventHandler::Serialize: reading object %s\n", GetClass()->TypeName.GetChars());
		}
		else
		{
			Printf("DStaticEventHandler::Serialize: store object %s\n", GetClass()->TypeName.GetChars());
		}
		*/

		arc("next", next);
		arc("prev", prev);
		arc("Order", Order);
		arc("IsUiProcessor", IsUiProcessor);
		arc("RequireMouse", RequireMouse);
	}

	// destroy handler. this unlinks EventHandler from the list automatically.
	void OnDestroy() override;

	//
	void OnRegister(); // you can set order and IsUi here.
	void OnUnregister();

	//
	void OnEngineInitialize();
	void WorldLoaded();
	void WorldUnloaded(const FString& nextmap);
	void WorldThingSpawned(AActor* actor);
	void WorldThingDied(AActor* actor, AActor* inflictor);
	void WorldThingGround(AActor* actor, FState* st);
	void WorldThingRevived(AActor* actor);
	void WorldThingDamaged(AActor* actor, AActor* inflictor, AActor* source, int damage, FName mod, int flags, DAngle angle);
	void WorldThingDestroyed(AActor* actor);
	bool WorldHitscanPreFired(AActor* actor, DAngle angle, double distance, DAngle pitch, int damage, FName damageType, PClassActor *pufftype, int flags, double sz, double offsetforward, double offsetside);
	bool WorldRailgunPreFired(FName damageType, PClassActor* pufftype, FRailParams* param);
	void WorldHitscanFired(AActor* actor, const DVector3& AttackPos, const DVector3& DamagePosition, AActor* Inflictor, int flags);
	void WorldRailgunFired(AActor* actor, const DVector3& AttackPos, const DVector3& DamagePosition, AActor* Inflictor, int flags);
	void WorldLinePreActivated(line_t* line, AActor* actor, int activationType, bool* shouldactivate);
	void WorldLineActivated(line_t* line, AActor* actor, int activationType);
	int WorldSectorDamaged(sector_t* sector, AActor* source, int damage, FName damagetype, int part, DVector3 position, bool isradius);
	int WorldLineDamaged(line_t* line, AActor* source, int damage, FName damagetype, int side, DVector3 position, bool isradius);
	void WorldLightning();
	void WorldTick();

	//
	void RenderFrame();
	void RenderOverlay(EHudState state);
	void RenderUnderlay(EHudState state);

	//
	void PlayerEntered(int num, bool fromhub);
	void PlayerSpawned(int num);
	void PlayerRespawned(int num);
	void PlayerDied(int num);
	void PlayerDisconnected(int num);

	// return true if handled.
	bool InputProcess(const event_t* ev);
	bool UiProcess(const event_t* ev);
	void UiTick();
	void PostUiTick();
	
	// 
	void ConsoleProcess(int player, FString name, int arg1, int arg2, int arg3, bool manual, bool ui);
	void NetCommandProcess(FNetworkCommand& cmd);

	//
	void CheckReplacement(PClassActor* replacee, PClassActor** replacement, bool* final);
	void CheckReplacee(PClassActor** replacee, PClassActor* replacement, bool* final);

	//
	void NewGame();
};
class DEventHandler : public DStaticEventHandler
{
	DECLARE_CLASS(DEventHandler, DStaticEventHandler) // TODO: make sure this does not horribly break anything
public:
	bool IsStatic() override { return false; }
};

struct FRenderEvent
{
	// these are for all render events
	DVector3 ViewPos;
	DAngle ViewAngle;
	DAngle ViewPitch;
	DAngle ViewRoll;
	double FracTic = 0; // 0..1 value that describes where we are inside the current gametic, render-wise.
	AActor* Camera = nullptr;
	int HudState;
};

struct FWorldEvent
{
	// for loaded/unloaded
	bool IsSaveGame = false;
	bool IsReopen = false;
	FString NextMap;
	// for thingspawned, thingdied, thingdestroyed
	AActor* Thing = nullptr; // for thingdied
	AActor* Inflictor = nullptr; // can be null - for damagemobj
	AActor* DamageSource = nullptr; // can be null
	int Damage = 0; // thingdamaged, sector/line damaged
	FName DamageType = NAME_None; // thingdamaged, sector/line damaged
	int DamageFlags = 0; // thingdamaged
	DAngle DamageAngle; // thingdamaged
	// for line(pre)activated
	line_t* ActivatedLine = nullptr;
	int ActivationType = 0;
	bool ShouldActivate = true;
	// for line/sector damaged
	int DamageSectorPart = 0;
	line_t* DamageLine = nullptr;
	sector_t* DamageSector = nullptr;
	int DamageLineSide = -1;
	DVector3 DamagePosition;
	bool DamageIsRadius; // radius damage yes/no
	int NewDamage = 0; // sector/line damaged. allows modifying damage
	FState* CrushedState = nullptr; // custom crush state set in thingground
	DVector3 AttackPos; //hitscan point of origin
	DAngle AttackAngle;
	DAngle AttackPitch;
	double AttackDistance = 0;
	double AttackOffsetForward = 0;
	double AttackOffsetSide = 0;
	double AttackZ = 0;
	PClassActor* AttackPuffType = nullptr;
	FRailParams RailParams;
	int AttackLineFlags = 0;
};

struct FPlayerEvent
{
	// we currently have only one member: player index
	// in ZScript, we have global players[] array from which we can
	//  get both the player itself and player's body,
	//  so no need to pass it here.
	int PlayerNumber;
	// we set this to true if level was reopened (RETURN scripts)
	bool IsReturn;
};

struct FConsoleEvent 
{
	// player that activated this event. note that it's always -1 for non-playsim events (i.e. these not called with netevent)
	int Player;
	//
	FString Name;
	int Args[3];
	//
	bool IsManual;
};

struct FReplaceEvent
{
	PClassActor* Replacee;
	PClassActor* Replacement;
	bool IsFinal;
};

struct FReplacedEvent
{
	PClassActor* Replacee;
	PClassActor* Replacement;
	bool IsFinal;
};

struct EventManager
{
	FLevelLocals *Level = nullptr;
	DStaticEventHandler* FirstEventHandler = nullptr;
	DStaticEventHandler* LastEventHandler = nullptr;

	EventManager() = default;
	EventManager(FLevelLocals *l) { Level = l; }
	~EventManager() { Shutdown(); }
	bool ShouldCallStatic(bool forplay);

	// for use after loading a savegame. The old handler explicitly reinstalled all handlers instead of doing a list deserialization which resulted in OnRegister being called even when a save was loaded.
	void CallOnRegister();
	// register
	bool RegisterHandler(DStaticEventHandler* handler);
	// unregister
	bool UnregisterHandler(DStaticEventHandler* handler);
	// find
	bool CheckHandler(DStaticEventHandler* handler);
	// check type
	bool IsStaticType(PClass* type);
	// init static handlers
	void InitStaticHandlers(FLevelLocals *l, bool map);
	// shutdown handlers
	void Shutdown();

	// after the engine is done creating data
	void OnEngineInitialize();
	// called right after the map has loaded (approximately same time as OPEN ACS scripts)
	void WorldLoaded();
	// called when the map is about to unload (approximately same time as UNLOADING ACS scripts)
	void WorldUnloaded(const FString& nextmap);
	// called around PostBeginPlay of each actor.
	void WorldThingSpawned(AActor* actor);
	// called after AActor::Die of each actor.
	void WorldThingDied(AActor* actor, AActor* inflictor);
	// called when a hitscan attack is fired (can be overridden to block it)
	bool WorldHitscanPreFired(AActor* actor, DAngle angle, double distance, DAngle pitch, int damage, FName damageType, PClassActor *pufftype, int flags, double sz, double offsetforward, double offsetside);
	// called when a hitscan attack has been fired
	void WorldHitscanFired(AActor* actor, const DVector3& AttackPos, const DVector3& DamagePosition, AActor* Inflictor, int flags);
	// called when a railgun attack has been fired
	void WorldRailgunFired(AActor* actor, const DVector3& AttackPos, const DVector3& DamagePosition, AActor* Inflictor, int flags);
	// called when a railgun attack has been fired (can be overridden to block it)
	bool WorldRailgunPreFired(FName damageType, PClassActor* pufftype, FRailParams* param);
	// called inside AActor::Grind just before the corpse is destroyed
	void WorldThingGround(AActor* actor, FState* st);
	// called after AActor::Revive.
	void WorldThingRevived(AActor* actor);
	// called before P_DamageMobj and before AActor::DamageMobj virtuals.
	void WorldThingDamaged(AActor* actor, AActor* inflictor, AActor* source, int damage, FName mod, int flags, DAngle angle);
	// called before AActor::Destroy of each actor.
	void WorldThingDestroyed(AActor* actor);
	// called in P_ActivateLine before executing special, set shouldactivate to false to prevent activation.
	void WorldLinePreActivated(line_t* line, AActor* actor, int activationType, bool* shouldactivate);
	// called in P_ActivateLine after successful special execution.
	void WorldLineActivated(line_t* line, AActor* actor, int activationType);
	// called in P_DamageSector and P_DamageLinedef before receiving damage to the sector. returns actual damage
	int WorldSectorDamaged(sector_t* sector, AActor* source, int damage, FName damagetype, int part, DVector3 position, bool isradius);
	// called in P_DamageLinedef before receiving damage to the linedef. returns actual damage
	int WorldLineDamaged(line_t* line, AActor* source, int damage, FName damagetype, int side, DVector3 position, bool isradius);
	// same as ACS SCRIPT_Lightning
	void WorldLightning();
	// this executes on every tick, before everything, only when in valid level and not paused
	void WorldTick();
	// this executes on every tick on UI side, always
	void UiTick();
	// this executes on every tick on UI side, always AND immediately after everything else
	void PostUiTick();
	// called on each render frame once.
	void RenderFrame();
	// called after everything's been rendered, but before console/menus
	void RenderOverlay(EHudState state);
	// called after everything's been rendered, but before console/menus/huds
	void RenderUnderlay(EHudState state);
	// this executes when a player enters the level (once). PlayerEnter+inhub = RETURN
	void PlayerEntered(int num, bool fromhub);
	// this executes at the same time as ENTER scripts
	void PlayerSpawned(int num);
	// this executes when a player respawns. includes resurrect cheat.
	void PlayerRespawned(int num);
	// this executes when a player dies (partially duplicating worldthingdied, but whatever)
	void PlayerDied(int num);
	// this executes when a player leaves the game
	void PlayerDisconnected(int num);
	// this executes on events.
	bool Responder(const event_t* ev); // splits events into InputProcess and UiProcess
	// this executes on console/net events.
	void Console(int player, FString name, int arg1, int arg2, int arg3, bool manual, bool ui);
	// This reads from ZScript network commands.
	void NetCommand(FNetworkCommand& cmd);

	// called when looking up the replacement for an actor class
	bool CheckReplacement(PClassActor* replacee, PClassActor** replacement);
	// called when looking up the replaced for an actor class
	bool CheckReplacee(PClassActor** replacee, PClassActor* replacement);

	// called on new game
	void NewGame();

	// send networked event. unified function.
	bool SendNetworkEvent(FString name, int arg1, int arg2, int arg3, bool manual);
	// Send a custom network command from ZScript.
	bool SendNetworkCommand(const FName& cmd, VMVa_List& args);
	// Send a pre-built command buffer over.
	bool SendNetworkBuffer(const FName& cmd, const DNetworkBuffer* buffer);

	// check if there is anything that should receive GUI events
	bool CheckUiProcessors();
	// check if we need native mouse due to UiProcessors
	bool CheckRequireMouse();

	void InitHandler(PClass* type);
	FWorldEvent SetupWorldEvent();
	FRenderEvent SetupRenderEvent();

	void SetOwnerForHandlers()
	{
		for (DStaticEventHandler* existinghandler = FirstEventHandler; existinghandler; existinghandler = existinghandler->next)
		{
			existinghandler->owner = this;
		}
	}

};

 extern EventManager staticEventManager;
