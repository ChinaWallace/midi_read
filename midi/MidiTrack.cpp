#include "MidiTrack.h"
#include "MidiEvent.h"
#include "MidiUtil.h"
#include "Midi.h"

#include <sstream>
#include <string>
#include <map>

using namespace std;

MidiTrack MidiTrack::ReadFromStream(std::istream &stream)
{
	// Verify the track header
	const static string MidiTrackHeader = "MTrk";

	// I could use (MidiTrackHeader.length() + 1), but then this has to be
	// dynamically allocated.  More hassle than it's worth.  MIDI is well
	// defined and will always have a 4-byte header.  We use 5 so we get
	// free null termination.
	char header_id[5] = { 0, 0, 0, 0, 0 };
	unsigned int track_length;

	stream.read(header_id, static_cast<streamsize>(MidiTrackHeader.length()));
	stream.read(reinterpret_cast<char*>(&track_length), sizeof(unsigned int));

	if (stream.fail()) throw MidiError(MidiError_TrackHeaderTooShort);

	string header(header_id);
	if (header != MidiTrackHeader) throw MidiError_BadTrackHeaderType;

	// Pull the full track out of the file all at once -- there is an
	// End-Of-Track event, but this allows us handle malformed MIDI a
	// little more gracefully.
	track_length = swap32(track_length);
	char *buffer = new char[track_length + 1];
	buffer[track_length] = 0;

	stream.read(buffer, track_length);
	if (stream.fail())
	{
		delete[] buffer;
		throw MidiError(MidiError_TrackTooShort);
	}

	// We have to jump through a couple hoops because istringstream
	// can't handle binary data unless constructed through an std::string. 
	string buffer_string(buffer, track_length);
	istringstream event_stream(buffer_string, ios::binary);
	delete[] buffer;

	MidiTrack t;

	// Read events until we run out of track
	char last_status = 0;
	unsigned long current_pulse_count = 0;
	while (event_stream.peek() != char_traits<char>::eof())
	{
		MidiEvent ev = MidiEvent::ReadFromStream(event_stream, last_status); 
		last_status = ev.StatusCode();

		t.m_events.push_back(ev);

		current_pulse_count += ev.GetDeltaPulses();

		t.m_event_pulses.push_back(current_pulse_count);
	}

	t.BuildNoteSet();
	t.DiscoverInstrument();

	return t;
}

bool MidiTrack::LinkMidiTrack(MidiTrack &track, unsigned long delta, unsigned long pulses, microseconds_t usecs)
{
	if (track.Events().empty())
	{
		return false;
	}

	MidiEventList list_events = track.Events();
	list_events.begin()->SetDeltaPulses(this->m_events.back().GetDeltaPulses() + delta);


	this->m_events.pop_back();
	for (auto e : list_events)
	{
		this->m_events.push_back(e);
	}


	if (pulses < this->m_event_pulses.back())
	{
		return false;
	}
	MidiEventPulsesList list_pulses = track.EventPulses();
	this->m_event_pulses.pop_back();
	for (auto p : list_pulses)
	{
		p += pulses;
		this->m_event_pulses.push_back(p);
	}


	if (usecs < this->m_event_usecs.back())
	{
		return false;
	}
	MidiEventMicrosecondList list_usecs = track.EventUsecs();
	this->m_event_usecs.pop_back();
	for (auto u : list_usecs)
	{
		u += usecs;
		this->m_event_usecs.push_back(u);
	}


	if (usecs < this->m_note_set.cbegin()->start)
	{
		return false;
	}
	NoteSet list_notes = track.Notes();
	for (auto n : list_notes)
	{
		n.start += pulses;
		n.end += pulses;
		n.track_id = this->m_note_set.begin()->track_id;
		this->m_note_set.insert(n);
	}


	this->m_notes_remaining = this->m_note_set.size();

	return true;
}

struct NoteInfo
{
	int velocity;
	unsigned char channel;
	unsigned long pulses;
};

void MidiTrack::BuildNoteSet()
{
	m_note_set.clear();

	// Keep a list of all the notes currently "on" (and the pulse that
	// it was started).  On a note_on event, we create an element.  On
	// a note_off event we check that an element exists, make a "Note",
	// and remove the element from the list.  If there is already an
	// element on a note_on we both cap off the previous "Note" and
	// begin a new one.
	//
	// A note_on with velocity 0 is a note_off
	std::map<NoteId, NoteInfo> m_active_notes;

	for (size_t i = 0; i < m_events.size(); ++i)
	{
		const MidiEvent &ev = m_events[i];
		if (ev.Type() != MidiEventType_NoteOn && ev.Type() != MidiEventType_NoteOff) continue;

		bool on = (ev.Type() == MidiEventType_NoteOn && ev.NoteVelocity() > 0);
		NoteId id = ev.NoteNumber();

		// Check for an active note
		std::map<NoteId, NoteInfo>::iterator find_ret = m_active_notes.find(id);
		bool active_event = (find_ret !=  m_active_notes.end());

		// Close off the last event if there was one
		if (active_event)
		{
			MidiLS::Note n;
			n.start = find_ret->second.pulses;
			n.end = m_event_pulses[i];
			n.note_id = id;
			n.channel = find_ret->second.channel;
			n.velocity = find_ret->second.velocity;

			// NOTE: This must be set at the next level up.  The track
			// itself has no idea what its index is.
			n.track_id = 0;

			// Add a note and remove this NoteId from the active list
			m_note_set.insert(n);
			m_active_notes.erase(find_ret);
		}

		// We've handled any active events.  If this was a note_off we're done.
		if (!on) continue;

		// Add a new active event
		NoteInfo info;
		info.channel = ev.Channel();
		info.velocity = ev.NoteVelocity();
		info.pulses = m_event_pulses[i];

		m_active_notes[id] = info;
	}

	// NOTE: No reason to report this error. It's non-critical
	// so there is no reason we need to shut down for it.
	// That would be needlessly restrictive against promiscuous
	// MIDI files.  As-is, a note just won't be inserted if
	// it isn't closed properly.
	/*
	if (m_active_notes.size() > 0)
	{
	throw MidiError(MidiError_UnresolvedNoteEvents);
	}
	*/
}

void MidiTrack::DiscoverInstrument()
{
	// Default to Program 0 per the MIDI Standard
	m_instrument_id = 0;
	bool instrument_found = false;

	// These are actually 10 and 16 in the MIDI standard.  However, MIDI
	// channels are 1-based facing the user.  They're stored 0-based.
	const static int PercussionChannel1 = 9;
	const static int PercussionChannel2 = 15;

	// Check to see if any/all of the notes
	// in this track use Channel 10.
	bool any_note_uses_percussion = false;
	bool any_note_does_not_use_percussion = false;

	for (size_t i = 0; i < m_events.size(); ++i)
	{
		const MidiEvent &ev = m_events[i];
		if (ev.Type() != MidiEventType_NoteOn) continue;

		if (ev.Channel() == PercussionChannel1 || ev.Channel() == PercussionChannel2) any_note_uses_percussion = true;
		if (ev.Channel() != PercussionChannel1 && ev.Channel() != PercussionChannel2) any_note_does_not_use_percussion = true;
	}

	if (any_note_uses_percussion && !any_note_does_not_use_percussion)
	{
		m_instrument_id = InstrumentIdPercussion;
		return;
	}

	if (any_note_uses_percussion && any_note_does_not_use_percussion)
	{
		m_instrument_id = InstrumentIdVarious;
		return;
	}

	for (size_t i = 0; i < m_events.size(); ++i)
	{
		const MidiEvent &ev = m_events[i];
		if (ev.Type() != MidiEventType_ProgramChange) continue;

		// If we've already hit a different instrument in this
		// same track, just tag it as "various" and exit early
		//
		// Also check that the same instrument isn't just set
		// multiple times in the same track
		if (instrument_found && m_instrument_id != ev.ProgramNumber())
		{
			m_instrument_id = InstrumentIdVarious;
			return;
		}

		m_instrument_id = ev.ProgramNumber();
		instrument_found = true;
	}
}

void MidiTrack::SetTrackId(size_t track_id)
{
	MidiLS::Note n;
	NoteSet::iterator label;

	for (NoteSet::iterator i = m_note_set.begin(); i != m_note_set.end(); )
	{
		n = *i;
		n.track_id = track_id;
		label = i++;
		m_note_set.erase(label);
		m_note_set.insert(n);
	}
	return;
}


void MidiTrack::SetTrackName(std::string track_name)
{
	MidiLS::Note n;
	NoteSet::iterator label;

	for (NoteSet::iterator i = m_note_set.begin(); i != m_note_set.end(); )
	{
		n = *i;
		n.track_name = track_name;
		label = i++;
		m_note_set.erase(label);
		m_note_set.insert(n);
	}
	
	for (MidiEventList::iterator i = m_events.begin(); i != m_events.end(); ++i)
	{
		i->setTrackName(track_name);
	}

	m_track_name = track_name;

	return;
}

void MidiTrack::Reset()
{
	m_initial_microseconds = 0;
	m_end_microseconds = 0;

	m_loop_start_microseconds = 0;
	m_loop_end_microseconds = 0;

	m_running_microseconds = 0;
	m_last_event = -1;

	m_notes_remaining = static_cast<unsigned int>(m_note_set.size());
}

MidiEventList MidiTrack::Update(microseconds_t delta_microseconds)
{
	m_running_microseconds += delta_microseconds;

	MidiEventList evs;
	for (size_t i = m_last_event + 1; i < m_events.size(); ++i)
	{
		if (m_event_usecs[i] <= m_running_microseconds)
		{
			evs.push_back(m_events[i]);
			m_last_event = static_cast<long>(i);

			if (m_events[i].Type() == MidiEventType_NoteOn &&
				m_events[i].NoteVelocity() > 0) m_notes_remaining--;
		}
		else break;
	}

	return evs;
}


void MidiTrack::Reset(microseconds_t start_time, microseconds_t end_time)																				// ����
{
	m_initial_microseconds = start_time;
	m_end_microseconds = end_time;

	m_running_microseconds = start_time;
	m_last_event = -1;

	m_loop_start_microseconds = 0;
	m_loop_end_microseconds = 0;

	m_notes_remaining = static_cast<unsigned int>(m_note_set.size());
}

MidiEventList MidiTrack::Update(microseconds_t delta_microseconds, bool loop)			// ����
{
	m_running_microseconds += delta_microseconds;

	MidiEventList evs;

	if (loop)
	{

		if (m_loop_end_microseconds > m_loop_start_microseconds)
		{
			if (m_running_microseconds > m_loop_end_microseconds)
			{
				for (size_t i = 0; i < m_event_usecs.size(); ++i)
				{
					if (m_event_usecs[i] >= m_loop_start_microseconds)
					{
						m_last_event = i - 1;
						break;
					}
				}
				m_running_microseconds = m_running_microseconds - m_loop_end_microseconds + m_loop_start_microseconds;

				m_change_play = true;
			}
		} 

		if (m_loop_end_microseconds < m_loop_start_microseconds)
		{
			if (m_running_microseconds > m_end_microseconds)
			{
				m_last_event = - 1;
				m_running_microseconds = m_running_microseconds - m_end_microseconds + m_initial_microseconds;
			}
			else
			{
				if (m_running_microseconds > m_loop_end_microseconds && m_running_microseconds < m_loop_start_microseconds)
				{
					for (size_t i = 0; i < m_event_usecs.size(); ++i)
					{
						if (m_event_usecs[i] >= m_loop_start_microseconds)
						{
							m_last_event = i - 1;
							break;
						}
					}
					m_running_microseconds = m_running_microseconds - m_loop_end_microseconds + m_loop_start_microseconds;

					m_change_play = true;
				}
			}
		}
	}


	if (m_change_play)
	{
		evs = LoadControlEvent(evs);
		m_change_play = false;
	}

	for (size_t i = m_last_event + 1; i < m_events.size(); ++i)
	{
		if (m_event_usecs[i] <= m_running_microseconds)
		{

			evs.push_back(m_events[i]);
			m_last_event = static_cast<long>(i);

			/*else
			{
			if (m_events[i].Type() == MidiEventType_NoteOn &&
			m_events[i].NoteVelocity() > 0) m_notes_remaining--;
			}*/
		}
		else break;
	}

	return evs;
}


void MidiTrack::SetLoop(microseconds_t start_time, microseconds_t enf_time)
{
	m_loop_start_microseconds = start_time;
	m_loop_end_microseconds = enf_time;
	return;
}



MidiEventList MidiTrack::LoadControlEvent()
{
	MidiEventList evs;
	for (size_t i = 0; i < m_events.size(); ++i)
	{
		if (m_event_usecs[i] <= m_running_microseconds)
		{
			if (m_events[i].Type() != MidiEventType_NoteOn && m_events[i].Type() != MidiEventType_NoteOff)
			{
				evs.push_back(m_events[i]);
			}
			continue;
		}
		else break;
	}

	return evs;
}


MidiEventList MidiTrack::LoadControlEvent(MidiEventList &evs)
{
	for (size_t i = 0; i < m_events.size(); ++i)
	{
		if (m_event_usecs[i] <= m_running_microseconds)
		{
			if (m_events[i].Type() != MidiEventType_NoteOn && m_events[i].Type() != MidiEventType_NoteOff)
			{
				evs.push_back(m_events[i]);
			}
			continue;
		}
		else break;
	}

	return evs;
}


void MidiTrack::SetPlayStart(microseconds_t start_microseconds)
{
	for (size_t i = 0; i < m_event_usecs.size(); ++i)
	{
		if (m_event_usecs[i] >= start_microseconds)
		{
			m_last_event = i - 1;
			break;
		}
	}

	m_running_microseconds = start_microseconds;
	m_change_play = true;
}