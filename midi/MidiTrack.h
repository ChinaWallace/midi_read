#ifndef __MIDI_TRACK_H
#define __MIDI_TRACK_H

#include <vector>
#include <iostream>

#include "Note.h"
#include "MidiEvent.h"
#include "MidiUtil.h"


class MidiEvent;

typedef std::vector<MidiEvent> MidiEventList;
typedef std::vector<unsigned long> MidiEventPulsesList;
typedef std::vector<microseconds_t> MidiEventMicrosecondList;


class MidiTrack
{
public:
	static MidiTrack ReadFromStream(std::istream &stream);
	static MidiTrack CreateBlankTrack() { return MidiTrack(); }


	bool LinkMidiTrack(MidiTrack &track, unsigned long delta, unsigned long pulses, microseconds_t usecs);

	MidiEventList &Events() { return m_events; }
	MidiEventPulsesList &EventPulses() { return m_event_pulses; }
	MidiEventMicrosecondList &EventUsecs() { return m_event_usecs; }

	const MidiEventList &Events() const { return m_events; }
	const MidiEventPulsesList &EventPulses() const { return m_event_pulses; }
	const MidiEventMicrosecondList &EventUsecs() const { return m_event_usecs; }

	void SetEventUsecs(const MidiEventMicrosecondList &event_usecs) { m_event_usecs = event_usecs; }

	const std::wstring InstrumentName() const { return InstrumentNames[m_instrument_id]; }
	bool IsPercussion() const { return m_instrument_id == InstrumentIdPercussion; }

	const NoteSet &Notes() const { return m_note_set; }

	void SetTrackId(size_t track_id);
	void SetTrackName(std::string track_name);																				// ������������


	std::string GetTrackName(void) { return m_track_name; }


	bool hasNotes() const { return (m_note_set.size() > 0); }

	void Reset();
	void Reset(microseconds_t start_time, microseconds_t end_time);															// ����
	MidiEventList Update(microseconds_t delta_microseconds);
	MidiEventList Update(microseconds_t delta_microseconds, bool loop);														// ����

	void SetPlayStart(microseconds_t start_microseconds);																	// ����ĳһʱ�俪ʼ����

	void SetLoop(microseconds_t start_time, microseconds_t enf_time);														// ����ѭ��ʱ��

	MidiEventList LoadControlEvent();																						// �������¼�
	MidiEventList LoadControlEvent(MidiEventList &evs);																		// �������¼�

	unsigned int AggregateEventsRemain() const { return static_cast<unsigned int>(m_events.size() - (m_last_event + 1)); }
	unsigned int AggregateEventCount() const { return static_cast<unsigned int>(m_events.size()); }

	unsigned int AggregateNotesRemain() const { return m_notes_remaining; }
	unsigned int AggregateNoteCount() const { return static_cast<unsigned int>(m_note_set.size()); }

private:
	MidiTrack() : m_instrument_id(0), m_change_play(false)  { Reset(); }

	void BuildNoteSet();
	void DiscoverInstrument();

	MidiEventList m_events;
	MidiEventPulsesList m_event_pulses;
	MidiEventMicrosecondList m_event_usecs;

	bool m_change_play;

	microseconds_t m_initial_microseconds;
	microseconds_t m_end_microseconds;

	microseconds_t m_loop_start_microseconds;
	microseconds_t m_loop_end_microseconds;

	std::string m_track_name;

	NoteSet m_note_set;

	int m_instrument_id;

	microseconds_t m_running_microseconds;
	long m_last_event;

	unsigned int m_notes_remaining;
};

#endif
