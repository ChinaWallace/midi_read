#ifndef __MIDI_EVENT_H
#define __MIDI_EVENT_H

#include <string>
#include <iostream>

#include "MidiUtil.h"
#include "Note.h"


struct MidiEventSimple
{
	MidiEventSimple() : status(0), byte1(0), byte2(0) { }
	MidiEventSimple(unsigned char s, unsigned char b1, unsigned char b2) : status(s), byte1(b1), byte2(b2) { }

	unsigned char status;
	unsigned char byte1;
	unsigned char byte2;
};


using namespace std;

class MidiEvent
{
public:
	static MidiEvent ReadFromStream(std::istream &stream, unsigned char last_status);
	static MidiEvent Build(const MidiEventSimple &simple);
	static MidiEvent NullEvent();

	// NOTE: There is a VERY good chance you don't want to use this directly.
	// The only reason it's not private is because the standard containers
	// require a default constructor.
	MidiEvent() : m_status(0), m_data1(0), m_data2(0), m_tempo_uspqn(0) { }

	// Returns true if the event could be expressed in a simple event.  (So, this will
	// return false for Meta and SysEx events.)
	bool GetSimpleEvent(MidiEventSimple *simple) const;

	MidiEventType Type() const;
	unsigned long GetDeltaPulses() const { return m_delta_pulses; }

	// This is generally for internal Midi library use only.
	void SetDeltaPulses(unsigned long delta_pulses) { m_delta_pulses = delta_pulses; }

	NoteId NoteNumber() const;

	// Returns a friendly name for this particular Note-On or Note-
	// Off event. (e.g. "A#2")  Returns empty string on other types
	// of events.
	static std::string NoteName(NoteId note_number);

	// Returns the "Program to change to" value if this is a Program
	// Change event, 0 otherwise.
	int ProgramNumber() const;

	// Returns the "velocity" of a Note-On (or 0 if this is a Note-
	// Off event).  Returns -1 for other event types.
	int NoteVelocity() const;

	unsigned int BeatMember() const;							// ��ȡMidi�еĽ��ķ����¼�
	unsigned int BeatDenominator() const;						// ��ȡMidi�еĽ��ķ�ĸ�¼�

	void SetVelocity(int velocity);

	// Returns which type of meta event this is (or
	// MetaEvent_Unknown if type() is not EventType_Meta).
	MidiMetaEventType MetaType() const;

	// Retrieve the tempo from a tempo meta event in microseconds
	// per quarter note.  (Non-meta-tempo events will throw an error).
	unsigned long GetTempoInUsPerQn() const;

	// Convenience function: Is this the special End-Of-Track event
	bool IsEnd() const;

	// Returns which channel this event operates on.  This is
	// only defined for standard MIDI events that require a
	// channel argument.
	unsigned char Channel() const;

	void SetChannel(unsigned char channel);

	// Does this event type allow arbitrary text
	bool HasText() const;

	// Returns the text content of the event (or empty-string if
	// this isn't a text event.)
	std::string Text() const;

	// Returns the status code of the MIDI event
	unsigned char StatusCode() const { return m_status; }

	unsigned char GetEventData1() const { return m_data1; }		// �Զ���
	unsigned char GetEventData2() const { return m_data2; }		// �Զ���

	vector<unsigned char> &OtharData(void) { return m_other_data; }

	void setTrackName(std::string name) { m_strTrackName = name; }
	std::string getTrackName(void) const { return m_strTrackName; }

	bool operator()(const MidiEvent &lhs, const MidiEvent &rhs)
	{
		if (lhs.m_status < rhs.m_status) return true;
		if (lhs.m_status > rhs.m_status) return false;

		if (lhs.m_data1 < rhs.m_data1) return true;
		if (lhs.m_data1 > rhs.m_data1) return false;

		if (lhs.m_data2 < rhs.m_data2) return true;
		if (lhs.m_data2 > rhs.m_data2) return false;

		return false;
	}

private:
	void ReadMeta(std::istream &stream);
	void ReadSysEx(std::istream &stream);
	void ReadStandard(std::istream &stream);

	unsigned char m_status;
	unsigned char m_data1;
	unsigned char m_data2;
	unsigned long m_delta_pulses;

	vector<unsigned char> m_other_data;

	unsigned char m_meta_type;

	unsigned long m_tempo_uspqn;
	std::string m_text;

	std::string m_strTrackName;
};

#endif __MIDI_EVENT_H
