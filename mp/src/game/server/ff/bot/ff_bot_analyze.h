//========= Fortress Forever Bot =============================================//
//
// CFFBotAnalyzer — automatic derivation of gameplay knowledge.
//
// THE PROBLEM
//
// The bots have three ways of learning what a map means:
//
//   1. Entity tagging (CFFBotTagger) — spawn rooms, flags, caps. Exact, but it
//      only knows what an entity told it.
//   2. Shape heuristics (CFFBotAutoTagger) — high ground, water, chokes. Cheap,
//      local, and crude: "this area is a chokepoint" was literally "this area
//      is between 32 and 96 units wide", which tags every corridor and doorway
//      in the map whether or not any of them matter.
//   3. Hand authoring (FFNavBuilder) — accurate, and somebody has to do it for
//      every map, forever.
//
// Everything the heuristics could not derive fell to (3), and the list was
// long: which corridor is worth a sentry, which wall opens a shortcut, where a
// defender should stand, which way to look from there.
//
// WHAT THIS DOES INSTEAD
//
// Most of that list is not actually map knowledge. It is a property of the nav
// graph, or of the visibility sets nav_generate already computes, or of world
// entities nobody was reading — and all three are available at map load for
// free.
//
//   PASS 1  Topology.       Articulation points (Tarjan). An area whose removal
//                           disconnects the graph is a chokepoint by
//                           definition, which is a much stronger claim than
//                           "narrow" and does not depend on a width tuning
//                           constant at all.
//
//   PASS 2  Traffic.        Run the pathfinder over every spawn-threshold to
//                           objective pair and count how often each area
//                           appears. "Where does everyone walk" answers most of
//                           what an author answers by hand.
//
//   PASS 3  Visibility.     nav_generate's analyze phase already computes, for
//                           every area, the set of areas it can see, and writes
//                           it into the .nav. Nothing was reading it. An area
//                           that sees a lot of high-traffic ground from a safe
//                           distance is a sniper perch; the direction of the
//                           busiest thing it sees is an aim hint.
//
//   PASS 4  Defense.        Cut points on the enemy's approach, with sight of
//                           it, near enough to the thing being defended.
//                           Stamped into the same FF_NAV2_DEFEND_* bits the
//                           hand-authored posts use, so the existing consumer
//                           picks them up unchanged.
//
//   PASS 5  Entities.       func_breakable, trigger_teleport, trigger_push,
//                           func_button — none of which the bot layer read.
//                           The breakable test is the interesting one: a
//                           breakable is only a demoman target if destroying it
//                           shortens a route, and that is a graph query rather
//                           than a guess.
//
// HAND AUTHORING STILL WINS
//
// Every pass here checks whether a stronger source already spoke. A derived
// sentry hint never overwrites a placed one, a derived aim yaw never overwrites
// an authored one. The analyzer fills the map in; it does not argue with you.
//
//===========================================================================//

#ifndef FF_BOT_ANALYZE_H
#define FF_BOT_ANALYZE_H
#ifdef _WIN32
#pragma once
#endif

class CFFNavMesh;

namespace CFFBotAnalyzer
{
	// Run every pass. Called from CFFBotTagger::TagAreasFromEntities after the
	// entity pass, the manual markers and the incursion flood fill — all three
	// are inputs here — and from ff_nav_analyze for iteration without a reload.
	void AnalyzeAll( CFFNavMesh *mesh );

	// Clear only the bits this module owns, so a re-run doesn't accumulate.
	// Deliberately does NOT clear anything the manual builder can also set.
	void ClearDerived( CFFNavMesh *mesh );

	// Last run's numbers, for ff_nav_analyze_report.
	void PrintReport( void );
}

#endif // FF_BOT_ANALYZE_H
