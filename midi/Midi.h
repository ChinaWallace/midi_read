#ifndef __MIDI_H
#define __MIDI_H

#include <iostream>
#include <vector>

#include "Note.h"
#include "MidiTrack.h"
#include "MidiTypes.h"


class MidiError;
class MidiEvent;

typedef std::vector<MidiTrack> MidiTrackList;

typedef std::vector<MidiEvent> MidiEventList;
typedef std::vector<std::pair<size_t, MidiEvent> > MidiEventListWithTrackId;

typedef std::vector<std::pair<size_t, microseconds_t> > MeterMicrosecondList;

typedef std::vector<double> NoteArray;

enum LoopMode
{
	LoopBtoA,
	LoopEtoS,
	LoopNot,
};

struct PrivateData
{
	std::string tempo;
	std::string style;
	std::string difficulty;
};

// NOTE: This library's MIDI loading and handling is destructive.  Perfect
//       1:1 serialization routines will not be possible without quite a
//       bit of additional work.
//class Midi

/*
#ifdef DLL_MIDI
	class _declspec(dllexport) Midi
#else
	class _declspec(dllimport) Midi
#endif*/
class Midi
{
public:

	static Midi ReadFromFile(std::string filename);
	static Midi ReadFromStream(std::istream &stream);


	static Midi LinkMidi(vector<std::string> files);


	static Midi ReadPrivateInfoFromFile(std::string filename);


	PrivateData PrivateInfo() { return m_private_info; }


	const MidiTrack FindTrack(std::string track_name);


	const std::vector<MidiTrack> &Tracks() const { return m_tracks; }


	const std::vector<MidiTrack> &PlayTracks() const { return m_play_tracks; }


	const std::vector<MidiTrack> &MuteTracks() const { return m_mute_tracks; }


	const TranslatedNoteSet &Notes() const { return m_translated_notes; }


	TranslatedNoteSet FindNotes(std::string track_name);


	TranslatedNoteSet &PlayNotes() { return m_play_notes; }


	MidiEventListWithTrackId Update(microseconds_t delta);
	MidiEventListWithTrackId Update(microseconds_t delta, bool loop);


	void Reset(microseconds_t lead_in, microseconds_t lead_out);
	void Reset(microseconds_t lead_in, microseconds_t lead, microseconds_t defer, bool hide = false);


	void GetRealTimeMeter(unsigned int &meter_amount, unsigned int &meter_unit, microseconds_t song_position = -1);


	void GetSongBarIDAndMeterID(int &bar_id, int &meter_id, microseconds_t time = -1);


	microseconds_t GetSongPositionInMicroseconds() const { return m_microsecond_song_position; }


	microseconds_t GetSongLengthInMicroseconds() const;

	microseconds_t GetDeadAirStartOffsetMicroseconds() const { return m_microsecond_dead_start_air; }


	microseconds_t GetSongRunningTempoMicroseconds(microseconds_t song_position = 0) const;


	microseconds_t GetSongStartMicroseconds() const { return m_microsecond_song_start; }

	microseconds_t GetSongEndMicroseconds() const { return m_microsecond_song_end; }


	microseconds_t GetBarStartMicroseconds(int bar_id) const;

	microseconds_t GetBarForMeterStartMicroseconds(int bar_id, size_t meter_id) const;


	microseconds_t GetBarForMeterEndMicroseconds(int bar_id, size_t meter_id) const;


	unsigned int GetBarID(microseconds_t time) const;


	int GetSongReservedBarCount() const { return m_reserved_bars; }

	int GetSongBarCount() const;


	int GetSongPositionInBarID(bool defer = false) const;


	void TranslateRealTimeMeter(int &meter_amount, int &meter_unit, unsigned long event_pulses = 0);

	unsigned int GetSongTicks(microseconds_t running_tempo) const;

	unsigned int GetSongInitTicks() const { return m_init_ticks; }


	unsigned char Channel(unsigned char status);


	unsigned short GetMidiTimeDivision(void) { return m_time_division; }


	MidiEventMicrosecondList GetMidiBarStartUsecs(void) { return m_bar_usecs; }


	vector<MeterMicrosecondList> GetMidiMeterStartUsecs(void) { return m_meter_start_usecs; }


	vector<MeterMicrosecondList> GetMidiMeterEndUsecs(void) { return m_meter_end_usecs; }


	void addPlayTrack(std::string track);

	void addMuteTrack(std::string track);


	bool isPlayNote(const std::string track);

	bool isMuteNote(const std::string track);


	bool isPercussion(unsigned char channel);


	bool IsSongOver() const;


	LoopMode GetLoopMode(microseconds_t delta_microseconds) const;


	void SetLoop(microseconds_t start_microseconds, microseconds_t end_microseconds);

	MidiEventListWithTrackId SetPlayStart(microseconds_t start_microseconds);

	// This doesn't include lead-in (so it's perfect for a progress bar).
	// (It is also clamped to [0.0, 1.0], so lead-in and lead-out won't give any
	// unexpected results.)
	double GetSongPercentageComplete() const;

	/*// This will report when the lead-out period is complete.
	bool IsSongOver() const;*/

	unsigned int AggregateEventsRemain() const;
	unsigned int AggregateEventCount() const;

	unsigned int AggregateNotesRemain() const;
	unsigned int AggregateNoteCount() const;

	MidiEventMicrosecondList GetBarUsecs();
private:
	const static unsigned long DefaultBPM = 120;
	const static microseconds_t OneMinuteInMicroseconds = 60000000;
	const static microseconds_t DefaultUSTempo = OneMinuteInMicroseconds / DefaultBPM;

	static microseconds_t ConvertPulsesToMicroseconds(unsigned long pulses, microseconds_t tempo, unsigned short pulses_per_quarter_note);

	Midi(): m_initialized(false), m_microsecond_dead_start_air(0), m_microsecond_song_start(0), m_init_meter_amount(0), m_init_meter_unit(0),
		m_microsecond_init_running_tempo(0), m_microsecond_defer(0), m_reserved_bars(0), m_first_set(true) { Reset(0, 0); }

	// This is O(n) where n is the number of tempo changes (across all tracks) in
	// the song up to the specified time.  Tempo changes are usually a small number.
	// (Almost always 0 or 1, going up to maybe 30-100 in rare cases.)
	microseconds_t GetEventPulseInMicroseconds(unsigned long event_pulses, unsigned short pulses_per_quarter_note) const;


	bool LinkMidiTrack(std::string track_name, MidiTrack &track);

	MidiEventListWithTrackId LoadControlEvent();


	void TranslatePrivateInfo(void);


	std::string GetTrackName(const MidiEventList &list) const;


	unsigned long FindFirstNoteOnPulse();

	unsigned long FindLastNoteOffPulse();

	void BuildMeterTrack();

	void BuildTempoTrack();

	void BuildBarTimeList(unsigned short pulses_per_quarter_note);


	int GetSongReservedBarCount(unsigned long first_note_pulses) const;

	void TranslateNotes(const NoteSet &notes, unsigned short pulses_per_quarter_note);
	void TranslateNotes(const NoteSet &notes, unsigned short pulses_per_quarter_note, unsigned long first_note_pulses);

	NoteId StandardizingDrumNoteId(NoteId id);														// ��׼�����ӹĵ�������

	bool m_initialized;

	PrivateData m_private_info;

	vector<std::string> m_stlPlayTrack;
	vector<std::string> m_stlMuteTrack;

	MidiEventPulsesList m_bar_pulses;
	MidiEventMicrosecondList m_bar_usecs;

	vector<MeterMicrosecondList> m_meter_start_usecs;
	vector<MeterMicrosecondList> m_meter_end_usecs;

	TranslatedNoteSet m_translated_notes;
	TranslatedNoteSet m_play_notes;

	// Position can be negative (for lead-in).
	microseconds_t m_microsecond_song_position;
	microseconds_t m_microsecond_base_song_length;

	microseconds_t m_microsecond_lead_out;
	microseconds_t m_microsecond_dead_start_air;

	microseconds_t m_microsecond_song_start;
	microseconds_t m_microsecond_song_end;

	microseconds_t m_microsecond_loop_start;
	microseconds_t m_microsecond_loop_end;

	microseconds_t m_microsecond_init_running_tempo;

	microseconds_t m_microsecond_defer;

	unsigned int m_reserved_bars;
	unsigned int m_init_ticks;
	int m_init_meter_amount;
	int m_init_meter_unit;

	unsigned short m_time_division;

	bool m_first_set;

	bool m_first_update_after_reset;
	double m_playback_speed;
	MidiTrackList m_tracks;
	MidiTrackList m_play_tracks;
	MidiTrackList m_mute_tracks;
};

#endif
