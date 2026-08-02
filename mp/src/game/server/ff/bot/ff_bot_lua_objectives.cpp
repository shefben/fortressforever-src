//========= Fortress Forever Bot =============================================//
//
// FFBotLuaObjectives — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_lua_objectives.h"
#include "ff_bot_gamemode.h"
#include "ff_bot_tagger.h"
#include "ff_nav_mesh.h"
#include "ff_nav_area.h"
#include "ff_item_flag.h"
#include "ff_player.h"
#include "triggers.h"
#include "entitylist.h"
#include "nav_mesh.h"

#include "omnibot_interface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


ConVar ff_bot_lua_objectives( "ff_bot_lua_objectives", "1", FCVAR_NONE,
	"Track Lua-declared goal entities and their live state. "
	"0 = off (level-init snapshot only, the old behaviour), "
	"1 = on, 2 = on and log every state change." );


// How often the reconcile pass runs. Goal state changes at human timescales —
// a flag is picked up, a phase ends — so four times a second is ample and keeps
// the cost off the frame budget.
#define FFLUA_RECONCILE_INTERVAL	0.25f

// How often we sweep gEntList for goals that never announced themselves. This
// is the safety net, not the mechanism, so it can be slow.
#define FFLUA_RESCAN_INTERVAL		5.0f

// Minimum gap between nav re-tags. Re-deriving tags walks every area and
// re-runs the incursion flood fill; on an AvD phase change that's exactly what
// we want, but it must not be possible to trigger it every frame.
#define FFLUA_RETAG_MIN_INTERVAL	1.0f


//-----------------------------------------------------------------------------
static CUtlVector< FFBotLuaGoal > s_goals;

static float s_nextReconcileTime = 0.0f;
static float s_nextRescanTime    = 0.0f;
static float s_nextRetagTime     = 0.0f;
static bool  s_dirty             = false;

// Map identity, so a level change can be detected from inside OnGoalInfo —
// which is where the first thing that happens on a new map arrives, long before
// any of our normal lifecycle hooks run.
static char  s_currentMap[ 64 ] = { 0 };


//-----------------------------------------------------------------------------
// Goal-type helpers.
//
// Omnibot::BotGoalTypes is a fixed set of nine values inherited from Omnibot,
// and it is the entire vocabulary Lua has for describing a goal. That is a real
// constraint on how much a map can tell us — see the note at the bottom of
// PrintReport.
//-----------------------------------------------------------------------------
static const char *GoalTypeName( int goalType )
{
	switch ( goalType )
	{
	case Omnibot::kNone:				return "none";
	case Omnibot::kBackPack_Ammo:		return "ammo";
	case Omnibot::kBackPack_Armor:		return "armor";
	case Omnibot::kBackPack_Health:		return "health";
	case Omnibot::kBackPack_Grenades:	return "grenades";
	case Omnibot::kFlag:				return "flag";
	case Omnibot::kFlagCap:				return "cap";
	case Omnibot::kHuntedEscape:		return "hunted-escape";
	case Omnibot::kTrainerSpawn:		return "trainer-spawn";
	}
	return "unknown";
}


//=============================================================================
// Classification.
//
// Priority: what Lua declared > what the model says > what the name says.
// A declared botgoaltype is never second-guessed; inference only ever fills
// gaps.
//=============================================================================

ConVar ff_bot_classify_entities( "ff_bot_classify_entities", "1", FCVAR_NONE,
	"Infer what an undeclared Lua entity is from its model and name. "
	"0 = off (only entities with an explicit botgoaltype are goals), "
	"1 = on, 2 = on and log every inference." );


const char *FFBotLuaObjectives::GoalClassName( int goalClass )
{
	switch ( goalClass )
	{
	case FFGOALCLASS_FLAG:				return "flag";
	case FFGOALCLASS_CAP:				return "cap";
	case FFGOALCLASS_AMMO:				return "ammo";
	case FFGOALCLASS_ARMOR:				return "armor";
	case FFGOALCLASS_HEALTH:			return "health";
	case FFGOALCLASS_GRENADES:			return "grenades";
	case FFGOALCLASS_HUNTED_ESCAPE:		return "hunted-escape";
	case FFGOALCLASS_TRAINER_SPAWN:		return "trainer-spawn";
	case FFGOALCLASS_KEYCARD:			return "keycard";
	case FFGOALCLASS_BALL:				return "ball";
	case FFGOALCLASS_HAZARD_GEAR:		return "hazard-gear";
	case FFGOALCLASS_BACKPACK:			return "backpack";
	}
	return "unknown";
}


bool FFBotLuaObjectives::IsFlagLikeClass( int goalClass )
{
	return goalClass == FFGOALCLASS_FLAG
		|| goalClass == FFGOALCLASS_KEYCARD
		|| goalClass == FFGOALCLASS_BALL;
}


//-----------------------------------------------------------------------------
static int ClassFromDeclaredGoalType( int goalType )
{
	switch ( goalType )
	{
	case Omnibot::kFlag:				return FFGOALCLASS_FLAG;
	case Omnibot::kFlagCap:				return FFGOALCLASS_CAP;
	case Omnibot::kBackPack_Ammo:		return FFGOALCLASS_AMMO;
	case Omnibot::kBackPack_Armor:		return FFGOALCLASS_ARMOR;
	case Omnibot::kBackPack_Health:		return FFGOALCLASS_HEALTH;
	case Omnibot::kBackPack_Grenades:	return FFGOALCLASS_GRENADES;
	case Omnibot::kHuntedEscape:		return FFGOALCLASS_HUNTED_ESCAPE;
	case Omnibot::kTrainerSpawn:		return FFGOALCLASS_TRAINER_SPAWN;
	}
	return FFGOALCLASS_UNKNOWN;
}


//-----------------------------------------------------------------------------
// Model whitelist.
//
// Counts are from a sweep of all 599 shipped map scripts plus the base
// includes, so these are the models that actually occur, not a guess.
//
// models/items/healthkit.mdl is DELIBERATELY ABSENT despite 60 occurrences.
// includes/base.lua declares
//
//     info_ff_script = baseclass:new({ model = "models/items/healthkit.mdl" })
//
// so it is the DEFAULT model for every info_ff_script that doesn't set one.
// By the time it reaches C++ there is no way to distinguish "the author chose
// a healthkit" from "the author chose nothing". Classifying on it would label
// every unconfigured script entity in the game as a health pickup.
//-----------------------------------------------------------------------------
struct ModelClassRule
{
	const char *modelSubstring;
	int         goalClass;
};

static const ModelClassRule s_modelRules[] =
{
	// Carriable objectives. FoxBot's whitelist was exactly these three
	// concepts; FF just spells the paths differently.
	{ "/flag/flag.mdl",					FFGOALCLASS_FLAG },
	{ "/keycard/keycard.mdl",			FFGOALCLASS_KEYCARD },
	{ "/ball/ball.mdl",					FFGOALCLASS_BALL },

	// Protective equipment. rock2's gas suit — the map dresses it as a
	// Half-Life 1 helmet faceplate, which is unmistakable precisely because
	// nothing else in FF uses an HL1 character prop as a pickup.
	{ "barneyhelmet_faceplate.mdl",		FFGOALCLASS_HAZARD_GEAR },
	{ "/hazmat",						FFGOALCLASS_HAZARD_GEAR },
	{ "/gasmask",						FFGOALCLASS_HAZARD_GEAR },

	// Pickups.
	{ "/armour/armour.mdl",				FFGOALCLASS_ARMOR },
	{ "/cells/cell.mdl",				FFGOALCLASS_AMMO },
	{ "boxbuckshot.mdl",				FFGOALCLASS_AMMO },
	{ "/grenades/",						FFGOALCLASS_GRENADES },

	// Generic pack — mixed contents, so it resolves to BACKPACK rather than
	// any one resource. Last in the list: it's the loosest match and the most
	// common model in the game by an order of magnitude (961 occurrences).
	{ "/backpack/backpack.mdl",			FFGOALCLASS_BACKPACK },
	{ "backpack.mdl",					FFGOALCLASS_BACKPACK },
};


//-----------------------------------------------------------------------------
// Name keywords, consulted only when the model says nothing.
//
// Weaker than the model test and ordered accordingly: "key" would match
// "monkey", so the more specific strings come first and the loose ones are
// checked as whole-ish tokens. Lua sets these names for the HUD, so they're
// human-facing text and reasonably consistent across the map pool
// ("Red Key", "gas suit", "base suit").
//-----------------------------------------------------------------------------
static const ModelClassRule s_nameRules[] =
{
	{ "gas suit",		FFGOALCLASS_HAZARD_GEAR },
	{ "gas_suit",		FFGOALCLASS_HAZARD_GEAR },
	{ "gasmask",		FFGOALCLASS_HAZARD_GEAR },
	{ "gas mask",		FFGOALCLASS_HAZARD_GEAR },
	{ "hazmat",			FFGOALCLASS_HAZARD_GEAR },
	{ "biosuit",		FFGOALCLASS_HAZARD_GEAR },
	{ "suit",			FFGOALCLASS_HAZARD_GEAR },

	{ "keycard",		FFGOALCLASS_KEYCARD },
	{ "key",			FFGOALCLASS_KEYCARD },

	{ "flag",			FFGOALCLASS_FLAG },
	{ "ball",			FFGOALCLASS_BALL },
};


//-----------------------------------------------------------------------------
static int ClassifyByModel( CBaseEntity *ent )
{
	CFFInfoScript *script = dynamic_cast< CFFInfoScript * >( ent );
	if ( !script )
		return FFGOALCLASS_UNKNOWN;

	const char *model = script->LUA_GetModel();
	if ( !model || !*model )
		return FFGOALCLASS_UNKNOWN;

	for ( int i = 0; i < (int)ARRAYSIZE( s_modelRules ); ++i )
	{
		if ( Q_stristr( model, s_modelRules[ i ].modelSubstring ) != NULL )
			return s_modelRules[ i ].goalClass;
	}
	return FFGOALCLASS_UNKNOWN;
}


//-----------------------------------------------------------------------------
static int ClassifyByName( CBaseEntity *ent )
{
	if ( !ent )
		return FFGOALCLASS_UNKNOWN;

	// Two names to try: the Hammer targetname, and whatever Lua called it.
	// They're often different — rock2 has targetname "gas_suit" and Lua name
	// "gas suit" — and either is a legitimate hint.
	const char *names[ 2 ] = { NULL, NULL };
	if ( ent->GetEntityName() != NULL_STRING )
		names[ 0 ] = STRING( ent->GetEntityName() );
	names[ 1 ] = ent->GetClassname();

	for ( int n = 0; n < 2; ++n )
	{
		if ( !names[ n ] || !*names[ n ] )
			continue;
		for ( int i = 0; i < (int)ARRAYSIZE( s_nameRules ); ++i )
		{
			if ( Q_stristr( names[ n ], s_nameRules[ i ].modelSubstring ) != NULL )
				return s_nameRules[ i ].goalClass;
		}
	}
	return FFGOALCLASS_UNKNOWN;
}


//-----------------------------------------------------------------------------
// Full classification for one entity. declaredType is the Lua botgoaltype,
// which wins outright when it says anything at all.
//
// outInferred reports whether the answer came from guessing, so diagnostics can
// show the author which of their entities the bots are only assuming about.
//-----------------------------------------------------------------------------
static int ClassifyEntity( CBaseEntity *ent, int declaredType, bool *outInferred )
{
	if ( outInferred )
		*outInferred = false;

	const int declared = ClassFromDeclaredGoalType( declaredType );
	if ( declared != FFGOALCLASS_UNKNOWN )
		return declared;

	if ( ff_bot_classify_entities.GetInt() <= 0 )
		return FFGOALCLASS_UNKNOWN;

	int inferred = ClassifyByModel( ent );
	const char *how = "model";

	if ( inferred == FFGOALCLASS_UNKNOWN )
	{
		inferred = ClassifyByName( ent );
		how = "name";
	}

	if ( inferred == FFGOALCLASS_UNKNOWN )
		return FFGOALCLASS_UNKNOWN;

	if ( outInferred )
		*outInferred = true;

	if ( ff_bot_classify_entities.GetInt() >= 2 )
	{
		CFFInfoScript *script = dynamic_cast< CFFInfoScript * >( ent );
		Msg( "[FFBotLuaObjectives] inferred '%s' from %s: name='%s' model='%s'\n",
			FFBotLuaObjectives::GoalClassName( inferred ), how,
			ent->GetEntityName() != NULL_STRING ? STRING( ent->GetEntityName() ) : "(unnamed)",
			( script && script->LUA_GetModel() ) ? script->LUA_GetModel() : "(none)" );
	}

	return inferred;
}


//-----------------------------------------------------------------------------
// Bot team flags are a bitfield in Omnibot's team numbering, not FF's. Convert
// to an FF team mask test.
//-----------------------------------------------------------------------------
bool FFBotLuaObjectives::GoalIsForTeam( const FFBotLuaGoal *goal, int ffTeam )
{
	if ( !goal )
		return false;

	// Zero means "no team declared", which every consumer treats as "all
	// teams" — that's how CFFBotTagger has always read it.
	if ( goal->teamFlags == 0 )
		return true;

	if ( ffTeam < TEAM_BLUE || ffTeam > TEAM_GREEN )
		return false;

	int obTeam = Omnibot::obUtilGetBotTeamFromGameTeam( ffTeam );
	if ( obTeam <= 0 )
		return false;

	return ( goal->teamFlags & ( 1 << obTeam ) ) != 0;
}


static bool GoalIsForTeam( const FFBotLuaGoal &goal, int ffTeam )
{
	return FFBotLuaObjectives::GoalIsForTeam( &goal, ffTeam );
}


//-----------------------------------------------------------------------------
// Read current state straight off the entity. Both CFFInfoScript and
// CFuncFFScript expose the same three-state goal model; only CFFInfoScript has
// a position state as well (triggers don't move or get carried).
//-----------------------------------------------------------------------------
static void RefreshGoalState( FFBotLuaGoal &goal )
{
	CBaseEntity *ent = goal.entity.Get();
	if ( !ent )
	{
		goal.isLive = false;
		return;
	}

	goal.worldPos = ent->GetAbsOrigin();

	if ( goal.isTrigger )
	{
		CFuncFFScript *trigger = dynamic_cast< CFuncFFScript * >( ent );
		if ( trigger )
		{
			goal.isLive    = trigger->IsActive();
			goal.isCarried = false;
			goal.isDropped = false;
			return;
		}
		goal.isLive = false;
		return;
	}

	CFFInfoScript *script = dynamic_cast< CFFInfoScript * >( ent );
	if ( !script )
	{
		goal.isLive = false;
		return;
	}

	goal.isCarried = script->IsCarried();
	goal.isDropped = script->IsDropped();

	// "Live" means a player could meaningfully act on this thing right now.
	// Removed is Lua saying it doesn't exist; inactive is Lua saying it exists
	// but isn't in play. Both mean don't path to it. A carried flag is still
	// live — chasing the carrier is a legitimate objective.
	goal.isLive = !script->IsRemoved() && !script->IsInactive();
}


//-----------------------------------------------------------------------------
static int FindGoalIndex( CBaseEntity *ent )
{
	if ( !ent )
		return -1;

	for ( int i = 0; i < s_goals.Count(); ++i )
	{
		if ( s_goals[ i ].entity.Get() == ent )
			return i;
	}
	return -1;
}


//-----------------------------------------------------------------------------
static void CheckForMapChange( void )
{
	const char *mapName = STRING( gpGlobals->mapname );
	if ( !mapName )
		mapName = "";

	if ( Q_stricmp( mapName, s_currentMap ) == 0 )
		return;

	// New map. Entity handles from the previous one are meaningless.
	Q_strncpy( s_currentMap, mapName, sizeof( s_currentMap ) );
	s_goals.RemoveAll();
	s_dirty = false;
	s_nextReconcileTime = 0.0f;
	s_nextRescanTime    = 0.0f;
	s_nextRetagTime     = 0.0f;
}


//-----------------------------------------------------------------------------
// Record or update a goal. Lua can re-declare the same entity (a base class
// spawn handler followed by a derived one), so this updates in place rather
// than accumulating duplicates.
//-----------------------------------------------------------------------------
static void RecordGoal( CBaseEntity *ent, int goalType, int teamFlags, bool isTrigger )
{
	if ( !ent )
		return;

	// A declared type of kNone is no longer an automatic rejection: the whole
	// point of the classifier is that an entity Lua never labelled can still
	// be something the bots care about. It only gets dropped if inference
	// comes back empty too.
	bool inferred = false;
	const int goalClass = ClassifyEntity( ent, goalType, &inferred );
	if ( goalClass == FFGOALCLASS_UNKNOWN )
		return;

	CheckForMapChange();

	const int existing = FindGoalIndex( ent );
	if ( existing >= 0 )
	{
		FFBotLuaGoal &goal = s_goals[ existing ];

		// A later explicit declaration always displaces an earlier inference.
		if ( goal.goalType != goalType || goal.teamFlags != teamFlags ||
		     goal.goalClass != goalClass )
		{
			goal.goalType      = goalType;
			goal.teamFlags     = teamFlags;
			goal.goalClass     = goalClass;
			goal.classInferred = inferred;
			s_dirty = true;
		}
		RefreshGoalState( goal );
		return;
	}

	FFBotLuaGoal goal;
	goal.entity        = ent;
	goal.goalType      = goalType;
	goal.goalClass     = goalClass;
	goal.classInferred = inferred;
	goal.teamFlags     = teamFlags;
	goal.isTrigger     = isTrigger;
	goal.isLive        = true;
	goal.isCarried     = false;
	goal.isDropped     = false;
	goal.worldPos      = ent->GetAbsOrigin();
	goal.homePos       = goal.worldPos;

	RefreshGoalState( goal );

	s_goals.AddToTail( goal );
	s_dirty = true;

	if ( ff_bot_lua_objectives.GetInt() >= 2 )
	{
		Msg( "[FFBotLuaObjectives] + %s '%s' class=%s%s type=%s teamflags=0x%x %s\n",
			isTrigger ? "trigger" : "item",
			ent->GetEntityName() != NULL_STRING ? STRING( ent->GetEntityName() ) : "(unnamed)",
			FFBotLuaObjectives::GoalClassName( goalClass ),
			inferred ? " (inferred)" : "",
			GoalTypeName( goalType ), teamFlags,
			goal.isLive ? "live" : "INACTIVE" );
	}
}


//=============================================================================
// Event sinks.
//=============================================================================

void FFBotLuaObjectives::OnGoalInfo( CBaseEntity *ent, int goalType, int teamFlags )
{
	if ( ff_bot_lua_objectives.GetInt() <= 0 )
		return;
	if ( !ent )
		return;

	// This fires during entity spawn, i.e. during level load, i.e. before the
	// nav mesh has been activated. Recording is all that happens here; tagging
	// comes later from CFFBotTagger.
	const bool isTrigger = ( dynamic_cast< CFuncFFScript * >( ent ) != NULL );
	RecordGoal( ent, goalType, teamFlags, isTrigger );
}


void FFBotLuaObjectives::OnGoalStateChanged( CBaseEntity *ent )
{
	if ( ff_bot_lua_objectives.GetInt() <= 0 )
		return;

	const int index = FindGoalIndex( ent );
	if ( index < 0 )
		return;

	FFBotLuaGoal &goal = s_goals[ index ];

	const bool wasLive    = goal.isLive;
	const bool wasCarried = goal.isCarried;

	RefreshGoalState( goal );

	// Only a change in whether the thing is a valid objective needs a re-tag.
	// A flag changing hands moves a goal but doesn't change the set of tagged
	// areas, and re-deriving the whole mesh every time someone touches the flag
	// would be absurd.
	if ( goal.isLive != wasLive )
	{
		s_dirty = true;

		if ( ff_bot_lua_objectives.GetInt() >= 2 )
		{
			Msg( "[FFBotLuaObjectives] %s '%s' (%s) is now %s\n",
				goal.isTrigger ? "trigger" : "item",
				goal.entity.Get() && goal.entity.Get()->GetEntityName() != NULL_STRING
					? STRING( goal.entity.Get()->GetEntityName() ) : "(unnamed)",
				GoalTypeName( goal.goalType ),
				goal.isLive ? "LIVE" : "gone" );
		}
	}
	else if ( goal.isCarried != wasCarried && ff_bot_lua_objectives.GetInt() >= 2 )
	{
		Msg( "[FFBotLuaObjectives] item '%s' %s\n",
			goal.entity.Get() && goal.entity.Get()->GetEntityName() != NULL_STRING
				? STRING( goal.entity.Get()->GetEntityName() ) : "(unnamed)",
			goal.isCarried ? "picked up" : "released" );
	}
}


//=============================================================================
// Lifecycle.
//=============================================================================

void FFBotLuaObjectives::Rescan( void )
{
	if ( ff_bot_lua_objectives.GetInt() <= 0 )
		return;

	CheckForMapChange();

	// CFFInfoScript goals.
	//
	// Every one of them, not just the ones with a declared botgoaltype.
	// RecordGoal classifies and drops whatever it can't identify, which is how
	// rock2's gas suit — an info_ff_script with no botgoaltype anywhere in its
	// map script — becomes visible to the bots.
	CBaseEntity *ent = NULL;
	while ( ( ent = gEntList.FindEntityByClassT( ent, CLASS_INFOSCRIPT ) ) != NULL )
	{
		CFFInfoScript *script = static_cast< CFFInfoScript * >( ent );
		RecordGoal( ent, script->GetBotGoalType(), script->GetBotTeamFlags(), false );
	}

	// CFuncFFScript goals. These were invisible to every previous version of
	// the bot code, which only ever walked CLASS_INFOSCRIPT.
	ent = NULL;
	while ( ( ent = gEntList.FindEntityByClassT( ent, CLASS_TRIGGERSCRIPT ) ) != NULL )
	{
		CFuncFFScript *trigger = dynamic_cast< CFuncFFScript * >( ent );
		if ( !trigger )
			continue;
		const int goalType = trigger->GetBotGoalType();
		if ( goalType == Omnibot::kNone )
			continue;
		RecordGoal( ent, goalType, trigger->GetBotTeamFlags(), true );
	}
}


void FFBotLuaObjectives::OnMapLoad( void )
{
	if ( ff_bot_lua_objectives.GetInt() <= 0 )
		return;

	// Deliberately not a clear. By the time the nav mesh activates, Lua has
	// already spawned its entities and told us about every one of them through
	// OnGoalInfo; wiping here would throw all of that away. Map changes are
	// caught inside CheckForMapChange, which runs from the event sinks.
	CheckForMapChange();
	Rescan();

	const int live = [] () -> int {
		int n = 0;
		for ( int i = 0; i < s_goals.Count(); ++i )
		{
			if ( s_goals[ i ].isLive ) ++n;
		}
		return n;
	}();

	Msg( "[FFBotLuaObjectives] %d Lua goal entit%s (%d live) on %s.\n",
		s_goals.Count(), s_goals.Count() == 1 ? "y" : "ies", live, s_currentMap );

	// Cleared so the tagger pass that's about to run isn't immediately
	// followed by a redundant one.
	s_dirty = false;
}


//-----------------------------------------------------------------------------
void FFBotLuaObjectives::Tick( void )
{
	if ( ff_bot_lua_objectives.GetInt() <= 0 )
		return;

	if ( gpGlobals->curtime >= s_nextReconcileTime )
	{
		s_nextReconcileTime = gpGlobals->curtime + FFLUA_RECONCILE_INTERVAL;

		for ( int i = s_goals.Count() - 1; i >= 0; --i )
		{
			FFBotLuaGoal &goal = s_goals[ i ];

			if ( goal.entity.Get() == NULL )
			{
				// Entity genuinely gone, not merely Lua-removed. Lua-removed
				// entities still exist and come back on restore, so this is
				// the map ending or the entity being destroyed for real.
				s_goals.Remove( i );
				s_dirty = true;
				continue;
			}

			const bool wasLive = goal.isLive;
			RefreshGoalState( goal );

			// Catches state that arrives with no notification at all —
			// CFuncFFScript::SetActive / SetInactive have no Notify_ hook, so
			// trigger-based goals are only ever noticed here.
			if ( goal.isLive != wasLive )
				s_dirty = true;
		}
	}

	if ( gpGlobals->curtime >= s_nextRescanTime )
	{
		s_nextRescanTime = gpGlobals->curtime + FFLUA_RESCAN_INTERVAL;
		Rescan();
	}

	// The set of real objectives moving is also the only thing that can change
	// what kind of game this is — an attack/defend phase ending removes some
	// caps and restores others, and the mode has to be re-read from what is
	// live now rather than what was live at level init.
	if ( s_dirty )
		FFBotGameMode::InvalidateMode();

	// Re-derive nav tags when the set of real objectives has moved. Throttled,
	// because the derivation walks every area and re-runs the incursion
	// flood-fill.
	if ( s_dirty && gpGlobals->curtime >= s_nextRetagTime )
	{
		s_dirty = false;
		s_nextRetagTime = gpGlobals->curtime + FFLUA_RETAG_MIN_INTERVAL;

		CFFNavMesh *mesh = TheFFNavMesh();
		if ( mesh && mesh->IsLoaded() )
		{
			struct ClearTransient
			{
				bool operator()( CNavArea *a )
				{
					CFFNavArea *area = static_cast< CFFNavArea * >( a );
					area->ClearAttributeFF( ~FF_NAV_PERSISTENT_ATTRIBUTES );
					area->ClearAllAttributesFF2();
					return true;
				}
			} clearer;
			mesh->ForAllAreas( clearer );

			mesh->ClearTaggedAreaCaches();
			CFFBotTagger::TagAreasFromEntities( mesh );
			mesh->MarkDoorwayAreas();

			if ( ff_bot_lua_objectives.GetInt() >= 2 )
				Msg( "[FFBotLuaObjectives] objective set changed; nav re-tagged.\n" );
		}
	}
}


//=============================================================================
// Queries.
//=============================================================================

int FFBotLuaObjectives::Count( void )
{
	return s_goals.Count();
}


const FFBotLuaGoal *FFBotLuaObjectives::Get( int index )
{
	if ( index < 0 || index >= s_goals.Count() )
		return NULL;
	return &s_goals[ index ];
}


bool FFBotLuaObjectives::IsGoalLive( CBaseEntity *ent )
{
	const int index = FindGoalIndex( ent );
	if ( index < 0 )
		return false;
	return s_goals[ index ].isLive;
}


bool FFBotLuaObjectives::CanBotTouch( CBaseEntity *goalEnt, CBasePlayer *bot )
{
	if ( !goalEnt || !bot )
		return false;

	CFFInfoScript *script = dynamic_cast< CFFInfoScript * >( goalEnt );
	if ( !script )
	{
		// trigger_ff_script gates entry through a Lua allowed() callback. We
		// can't evaluate that from here without running Lua on a bot's behalf,
		// which would fire the script's side effects. Assume permitted; the
		// worst case is a wasted walk, which is what happened before anyway.
		return true;
	}

	return script->CanEntityTouch( bot );
}


CBaseEntity *FFBotLuaObjectives::FindNearestGoal( int goalType, int team,
	const Vector &from, CBasePlayer *forBot )
{
	CBaseEntity *best = NULL;
	float bestDistSq = FLT_MAX;

	for ( int i = 0; i < s_goals.Count(); ++i )
	{
		const FFBotLuaGoal &goal = s_goals[ i ];
		if ( !goal.isLive || goal.goalType != goalType )
			continue;
		if ( team >= 0 && !GoalIsForTeam( goal, team ) )
			continue;

		CBaseEntity *ent = goal.entity.Get();
		if ( !ent )
			continue;

		if ( forBot && !CanBotTouch( ent, forBot ) )
			continue;

		const float distSq = ( goal.worldPos - from ).LengthSqr();
		if ( distSq < bestDistSq )
		{
			bestDistSq = distSq;
			best = ent;
		}
	}

	return best;
}


void FFBotLuaObjectives::CollectGoals( int goalType, int team,
	CUtlVector< CBaseEntity * > *out )
{
	if ( !out )
		return;

	for ( int i = 0; i < s_goals.Count(); ++i )
	{
		const FFBotLuaGoal &goal = s_goals[ i ];
		if ( !goal.isLive || goal.goalType != goalType )
			continue;
		if ( team >= 0 && !GoalIsForTeam( goal, team ) )
			continue;

		CBaseEntity *ent = goal.entity.Get();
		if ( ent )
			out->AddToTail( ent );
	}
}


//-----------------------------------------------------------------------------
// Class-keyed variants. Use these for anything Omnibot's nine types can't
// name — hazard gear, keycards, balls.
//-----------------------------------------------------------------------------
CBaseEntity *FFBotLuaObjectives::FindNearestOfClass( int goalClass, int team,
	const Vector &from, CBasePlayer *forBot )
{
	CBaseEntity *best = NULL;
	float bestDistSq = FLT_MAX;

	for ( int i = 0; i < s_goals.Count(); ++i )
	{
		const FFBotLuaGoal &goal = s_goals[ i ];
		if ( !goal.isLive || goal.goalClass != goalClass )
			continue;
		if ( team >= 0 && !GoalIsForTeam( goal, team ) )
			continue;

		CBaseEntity *ent = goal.entity.Get();
		if ( !ent )
			continue;
		if ( forBot && !CanBotTouch( ent, forBot ) )
			continue;

		const float distSq = ( goal.worldPos - from ).LengthSqr();
		if ( distSq < bestDistSq )
		{
			bestDistSq = distSq;
			best = ent;
		}
	}

	return best;
}


void FFBotLuaObjectives::CollectOfClass( int goalClass, int team,
	CUtlVector< CBaseEntity * > *out )
{
	if ( !out )
		return;

	for ( int i = 0; i < s_goals.Count(); ++i )
	{
		const FFBotLuaGoal &goal = s_goals[ i ];
		if ( !goal.isLive || goal.goalClass != goalClass )
			continue;
		if ( team >= 0 && !GoalIsForTeam( goal, team ) )
			continue;

		CBaseEntity *ent = goal.entity.Get();
		if ( ent )
			out->AddToTail( ent );
	}
}


int FFBotLuaObjectives::CountOfClass( int goalClass )
{
	int count = 0;
	for ( int i = 0; i < s_goals.Count(); ++i )
	{
		if ( s_goals[ i ].goalClass == goalClass )
			++count;
	}
	return count;
}


//-----------------------------------------------------------------------------
// What the map is pointing this player at.
//
// Lua's UpdateObjectiveIcon / UpdateTeamObjectiveIcon set CFFPlayer's objective
// entity, and the HUD draws its arrow at whatever that is. It is per-player,
// per-team and phase-aware, maintained by the map script itself — which makes
// it strictly better information than anything we can derive, and makes a bot
// that follows it behave like a player following their own HUD.
//
// Not every map sets it. NULL means the map has no opinion, fall back to the
// derived logic.
//-----------------------------------------------------------------------------
CBaseEntity *FFBotLuaObjectives::GetScriptedObjective( CBasePlayer *player )
{
	if ( ff_bot_lua_objectives.GetInt() <= 0 )
		return NULL;

	CFFPlayer *ffPlayer = dynamic_cast< CFFPlayer * >( player );
	if ( !ffPlayer )
		return NULL;

	CBaseEntity *objective = ffPlayer->GetObjectiveEntity();
	if ( !objective )
		return NULL;

	// The map can leave a stale objective pointing at something Lua has since
	// removed. If we know the entity, trust our own liveness tracking over the
	// HUD handle; if we don't know it, the map is pointing at something outside
	// the goal system and we take its word for it.
	const int index = FindGoalIndex( objective );
	if ( index >= 0 && !s_goals[ index ].isLive )
		return NULL;

	return objective;
}


//=============================================================================
// Diagnostics.
//=============================================================================

void FFBotLuaObjectives::PrintReport( void )
{
	Msg( "==== FF Lua objectives ====\n" );
	Msg( "  tracking=%d  classify=%d  map=%s  entities=%d\n",
		ff_bot_lua_objectives.GetInt(), ff_bot_classify_entities.GetInt(),
		s_currentMap, s_goals.Count() );

	if ( s_goals.Count() == 0 )
	{
		Msg( "  NOTHING TRACKED.\n" );
		Msg( "  No entity on this map declares a 'botgoaltype', and none has a\n"
		     "  model or name we recognise. Author objectives by hand with\n"
		     "  ff_manual_nav_builder 1.\n" );
		Msg( "===========================\n" );
		return;
	}

	int live = 0;
	int inferredCount = 0;
	for ( int i = 0; i < s_goals.Count(); ++i )
	{
		const FFBotLuaGoal &goal = s_goals[ i ];
		if ( goal.isLive )
			++live;
		if ( goal.classInferred )
			++inferredCount;

		CBaseEntity *ent = goal.entity.Get();
		const char *name = ( ent && ent->GetEntityName() != NULL_STRING )
			? STRING( ent->GetEntityName() ) : "(unnamed)";

		Msg( "  %-7s %-13s %-9s %-20s teams=0x%02x %s%s%s (%.0f %.0f %.0f)\n",
			goal.isTrigger ? "trigger" : "item",
			GoalClassName( goal.goalClass ),
			goal.classInferred ? "INFERRED" : "declared",
			name,
			goal.teamFlags,
			goal.isLive ? "LIVE" : "gone",
			goal.isCarried ? " carried" : "",
			goal.isDropped ? " dropped" : "",
			goal.worldPos.x, goal.worldPos.y, goal.worldPos.z );
	}

	Msg( "  %d live of %d.  %d classified by inference rather than declaration.\n",
		live, s_goals.Count(), inferredCount );

	// Call out the classes Lua literally cannot declare, because seeing a
	// non-zero count here is the only confirmation that the classifier did
	// something no amount of map scripting would have.
	const int hazard  = CountOfClass( FFGOALCLASS_HAZARD_GEAR );
	const int keycard = CountOfClass( FFGOALCLASS_KEYCARD );
	const int ball    = CountOfClass( FFGOALCLASS_BALL );
	if ( hazard || keycard || ball )
	{
		Msg( "  beyond Lua's vocabulary: %d hazard-gear, %d keycard, %d ball\n",
			hazard, keycard, ball );
	}
	Msg( "===========================\n" );
}


//=============================================================================
// Console commands.
//=============================================================================

CON_COMMAND_F( ff_bot_lua_report,
	"List every Lua-declared goal entity the bots can see, with live state.",
	FCVAR_CHEAT )
{
	FFBotLuaObjectives::PrintReport();
}


CON_COMMAND_F( ff_bot_lua_rescan,
	"Force a rescan of Lua goal entities and re-tag the nav mesh.",
	FCVAR_CHEAT )
{
	FFBotLuaObjectives::Rescan();
	FFBotLuaObjectives::PrintReport();
}


CON_COMMAND_F( ff_bot_objective,
	"Show the objective the map's Lua is pointing you at (the HUD arrow target).",
	FCVAR_CHEAT )
{
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( !player )
	{
		Msg( "Must be issued by a player.\n" );
		return;
	}

	CBaseEntity *objective = FFBotLuaObjectives::GetScriptedObjective( player );
	if ( !objective )
	{
		Msg( "[ff_bot_objective] no scripted objective for you. This map's Lua "
		     "doesn't call UpdateObjectiveIcon, or it isn't set right now.\n" );
		return;
	}

	Msg( "[ff_bot_objective] '%s' (%s) at (%.0f %.0f %.0f), %.0fu away.\n",
		objective->GetEntityName() != NULL_STRING
			? STRING( objective->GetEntityName() ) : "(unnamed)",
		objective->GetClassname(),
		objective->GetAbsOrigin().x, objective->GetAbsOrigin().y,
		objective->GetAbsOrigin().z,
		( objective->GetAbsOrigin() - player->GetAbsOrigin() ).Length() );
}
