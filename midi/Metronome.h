#pragma once

#ifndef _METRONOME_H_
#define _METRONOME_H_

#include "Midi.h"
#include "MidiTypes.h"

enum MetronomeLight
{
	TL_Black,
	TL_Red,
	TL_Green
};

enum BeatStatus
{
	RestBeat,
	StrongBeat,
	SubsidiaryStrongBeat,
	WeakBeat,
};


class SimpleBeat
{
public:
	SimpleBeat(void);
	~SimpleBeat(void);


	void Init(int meter_amount, microseconds_t meter_microseconds, microseconds_t defer_microseconds = 0);


	void Reset(void);


	void Set(int meter_amount, microseconds_t meter_microseconds);
	void Set(int meter_amount, microseconds_t meter_microseconds, int meter_id, microseconds_t position);


	BeatStatus Update(microseconds_t &delta_microseconds);

	float GetProgress(void) { return float(m_mtPosition) / float(m_mtMeterLength); }


	microseconds_t GetPosition(void) { return m_mtPosition; }


	int GetMeterID(void) { return m_iMeterID; }

protected:
private:
	microseconds_t m_mtPosition;
	microseconds_t m_mtMeterLength;
	microseconds_t m_mtDefer;

	int m_iMeterID;
	int m_iMeterAmount;

	bool m_bfirst;
	bool m_bInit;
};


class Metronome
{
public:
	Metronome(void);
	~Metronome(void);


	void Init(Midi &midi, microseconds_t defer_microseconds = 0);

	void Init(unsigned int meter_amount = 4, unsigned int meter_unit = 4, microseconds_t ticks = 120, microseconds_t defer_microseconds = 0);


	MidiEventList Update(microseconds_t &delta_microseconds, Midi &midi, bool play, bool sync_midi = false,
		bool prepare_meter = false);

	MidiEventList Update(microseconds_t &delta_microseconds, bool play);


	MidiEventList Colse(void);


	void Reset(void);


	double GetMetronomeValue(void);


	double GetUpcastSpace(void);


	bool GetKnockOnStickState(void) { return m_bKnockOnStick; }


	MetronomeLight GetMetronomeLight(void) { return m_tlLight; }
	MetronomeLight GetMetronomeLight(void) const { return m_tlLight; }

protected:
private:

	void UpdateMeter(Midi &midi);

	void UpdateMeter(unsigned int &meter_amount, unsigned int &meter_unit, microseconds_t tempo);


	MidiEventList StrongBeatEvents(void);


	MidiEventList WeakBeatEvents(void);


	MidiEventList PrepareBeatEvents(void);

	SimpleBeat *m_pFreeBeatSound;
	SimpleBeat *m_pFreeBeatFrames;

	SimpleBeat *m_pSyncBeatSound;
	SimpleBeat *m_pSyncBeatFrames;

	MetronomeLight m_tlLight;

	microseconds_t m_mPrepareMeterPosition;
	microseconds_t m_mMeterLength;
	microseconds_t m_mBarLength;
	microseconds_t m_mDefer;

	int m_iTimer;
	int m_iMeterID;
	bool m_bPlay;
	bool m_bSyncMidi;
	bool m_bKnockOnStick;

	bool m_bPlusMinus;

	bool m_bInit;

};

#endif