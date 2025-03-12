
#ifndef __MIDI_NOTE_H
#define __MIDI_NOTE_H

#include <set>
#include <vector>
#include "MidiTypes.h"

// Range of all 128 MIDI notes possible
typedef unsigned int NoteId;

// Arbitrary value outside the usual range
const static NoteId InvalidNoteId = 2048;

enum NoteState
{
	AutoPlayed,
	UserPlayable,
	UserUsed,
	UserMissed,
	UserRolling
};

template <class T>
struct GenericNote
{
	bool operator()(const GenericNote<T> &lhs, const GenericNote<T> &rhs)
	{
		if (lhs.start < rhs.start) return true;
		if (lhs.start > rhs.start) return false;

		if (lhs.end < rhs.end) return true;
		if (lhs.end > rhs.end) return false;

		if (lhs.note_id < rhs.note_id) return true;
		if (lhs.note_id > rhs.note_id) return false;

		if (lhs.track_id < rhs.track_id) return true;
		if (lhs.track_id > rhs.track_id) return false;

		return false;
	}

	T start;
	T end;
	NoteId note_id;
	size_t track_id;

	unsigned char channel;
	unsigned int bar_id;
	int velocity;
	microseconds_t time_unit;
	std::string track_name;

	NoteState state;
};


// Note keeps the internal pulses found in the MIDI file which are
// independent of tempo or playback speed.  TranslatedNote contains
// the exact (translated) microsecond that notes start and stop on
// based on a given playback speed, after dereferencing tempo changes.
namespace MidiLS
{
	typedef GenericNote<unsigned long> Note;
}
typedef GenericNote<microseconds_t> TranslatedNote;

typedef std::set<MidiLS::Note, MidiLS::Note> NoteSet;
typedef std::set<TranslatedNote, TranslatedNote> TranslatedNoteSet;

typedef std::vector<std::string> StrNoteSet;


#endif