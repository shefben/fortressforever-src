//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_squad.h
// Small groups of TFBot, managed as a unit
// Michael Booth, November 2009

#ifndef FF_BOT_SQUAD_H
#define FF_BOT_SQUAD_H

#include "NextBot/NextBotEventResponderInterface.h"

class CFFBot;

class CFFBotSquad : public INextBotEventResponder
{
public:
	CFFBotSquad( void );
	virtual ~CFFBotSquad() { }		

	// EventResponder ------
	virtual INextBotEventResponder *FirstContainedResponder( void ) const;
	virtual INextBotEventResponder *NextContainedResponder( INextBotEventResponder *current ) const;
	//----------------------

	bool IsMember( CFFBot *bot ) const;		// is the given bot in this squad?
	bool IsLeader( CFFBot *bot ) const;		// is the given bot the leader of this squad?

// 	CFFBot *GetMember( int i );
 	int GetMemberCount( void ) const;

	CFFBot *GetLeader( void ) const;

	class Iterator
	{
	public:
		Iterator( void )
		{
			m_bot = NULL;
			m_index = -1;
		}

		Iterator( CFFBot *bot, int index )
		{
			m_bot = bot;
			m_index = index;
		}

		CFFBot *operator() ( void )
		{
			return m_bot;
		}

		bool operator==( const Iterator &it ) const	{ return m_bot == it.m_bot && m_index == it.m_index; }
		bool operator!=( const Iterator &it ) const	{ return m_bot != it.m_bot || m_index != it.m_index; }

		CFFBot *m_bot;
		int m_index;
	};

	Iterator GetFirstMember( void ) const;
	Iterator GetNextMember( const Iterator &it ) const;
	Iterator InvalidIterator() const;

	void CollectMembers( CUtlVector< CFFBot * > *memberVector ) const;

	#define EXCLUDE_LEADER false
	float GetSlowestMemberSpeed( bool includeLeader = true ) const;
	float GetSlowestMemberIdealSpeed( bool includeLeader = true ) const;
	float GetMaxSquadFormationError( void ) const;

	bool ShouldSquadLeaderWaitForFormation( void ) const;		// return true if the squad leader needs to wait for members to catch up, ignoring those who have broken ranks
	bool IsInFormation( void ) const;						// return true if the squad is in formation (everyone is in or nearly in their desired positions)

	float GetFormationSize( void ) const;
	void SetFormationSize( float size );

	void DisbandAndDeleteSquad( void );

	void SetShouldPreserveSquad( bool bShouldPreserveSquad ) { m_bShouldPreserveSquad = bShouldPreserveSquad; }
	bool ShouldPreserveSquad() const { return m_bShouldPreserveSquad; }

private:
	friend class CFFBot;

	void Join( CFFBot *bot );
	void Leave( CFFBot *bot );

	CUtlVector< CHandle< CFFBot > > m_roster;
	CHandle< CFFBot > m_leader;

	float m_formationSize;
	bool m_bShouldPreserveSquad;
};

inline bool CFFBotSquad::IsMember( CFFBot *bot ) const
{
	return m_roster.HasElement( bot );
}

inline bool CFFBotSquad::IsLeader( CFFBot *bot ) const
{
	return m_leader == bot;
}

inline CFFBotSquad::Iterator CFFBotSquad::InvalidIterator() const
{
	return Iterator( NULL, -1 );
}

inline float CFFBotSquad::GetFormationSize( void ) const
{
	return m_formationSize;
}

inline void CFFBotSquad::SetFormationSize( float size )
{
	m_formationSize = size;
}


#endif // FF_BOT_SQUAD_H

