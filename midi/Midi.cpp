#include "Midi.h"
#include "MidiEvent.h"
#include "MidiTrack.h"
#include "MidiUtil.h"

#include <fstream>
#include <map>

#include <algorithm>

using namespace std;

Midi Midi::ReadFromFile(std::string filename)
{
	//fstream file(reinterpret_cast<const wchar_t*>((filename).c_str()), ios::in | ios::binary);
	fstream file(filename.c_str(), ios::in | ios::binary);
	if (!file.good()) throw MidiError(MidiError_BadFilename);

	Midi m;

	try
	{
		m = ReadFromStream(file);
	}
	catch (const MidiError &e)
	{
		// Close our file resource before handing the error up
		file.close();
		throw e;
	}

	return m;
}

Midi Midi::ReadFromStream(istream &stream)
{
	Midi m;

	// header_id is always "MThd" by definition
	const static string MidiFileHeader = "MThd";
	const static string RiffFileHeader = "RIFF";

	// I could use (MidiFileHeader.length() + 1), but then this has to be
	// dynamically allocated.  More hassle than it's worth.  MIDI is well
	// defined and will always have a 4-byte header.  We use 5 so we get
	// free null termination.
	char           header_id[5] = { 0, 0, 0, 0, 0 };
	unsigned int   header_length;
	unsigned short format;
	unsigned short track_count;
	unsigned short time_division;

	stream.read(header_id, static_cast<streamsize>(MidiFileHeader.length()));
	string header(header_id);
	if (header != MidiFileHeader)
	{
		if (header != RiffFileHeader) throw MidiError(MidiError_UnknownHeaderType);
		else
		{
			// We know how to support RIFF files
			unsigned int throw_away;
			stream.read(reinterpret_cast<char*>(&throw_away), sizeof(unsigned long)); // RIFF length
			stream.read(reinterpret_cast<char*>(&throw_away), sizeof(unsigned long)); // "RMID"
			stream.read(reinterpret_cast<char*>(&throw_away), sizeof(unsigned long)); // "data"
			stream.read(reinterpret_cast<char*>(&throw_away), sizeof(unsigned long)); // data size

			// Call this recursively, without the RIFF header this time
			return ReadFromStream(stream);
		}
	}

	stream.read(reinterpret_cast<char*>(&header_length), sizeof(unsigned int));
	stream.read(reinterpret_cast<char*>(&format), sizeof(unsigned short));
	stream.read(reinterpret_cast<char*>(&track_count), sizeof(unsigned short));
	stream.read(reinterpret_cast<char*>(&time_division), sizeof(unsigned short));

	if (stream.fail()) throw MidiError(MidiError_NoHeader);

	// Chunk Size is always 6 by definition
	const static unsigned int MidiFileHeaderChunkLength = 6;

	header_length = swap32(header_length);
	if (header_length != MidiFileHeaderChunkLength)
	{
		throw MidiError(MidiError_BadHeaderSize);
	}

	const static int MidiFormat0 = 0;
	const static int MidiFormat1 = 1;
	const static int MidiFormat2 = 2;

	format = swap16(format);
	if (format == MidiFormat2)
	{
		// MIDI 0: All information in 1 track
		// MIDI 1: Multiple tracks intended to be played simultaneously
		// MIDI 2: Multiple tracks intended to be played separately
		//
		// We do not support MIDI 2 at this time
		throw MidiError(MidiError_Type2MidiNotSupported);
	}

	track_count = swap16(track_count);
	if (format == 0 && track_count != 1)
	{
		// MIDI 0 has only 1 track by definition
		throw MidiError(MidiError_BadType0Midi);
	}

	// Time division can be encoded two ways based on a bit-flag:
	// - pulses per quarter note (15-bits)
	// - SMTPE frames per second (7-bits for SMPTE frame count and 8-bits for clock ticks per frame)
	time_division = swap16(time_division);
	bool in_smpte = ((time_division & 0x8000) != 0);

	if (in_smpte)
	{
		throw MidiError(MidiError_SMTPETimingNotImplemented);
	}
	m.m_time_division = time_division;

	// We ignore the possibility of SMPTE timing, so we can
	// use the time division value directly as PPQN.
	unsigned short pulses_per_quarter_note = time_division;

	// Read in our tracks
	for (int i = 0; i < track_count; ++i)
	{
		m.m_tracks.push_back(MidiTrack::ReadFromStream(stream));
	}


	m.TranslatePrivateInfo();

	m.BuildMeterTrack();
	m.BuildTempoTrack();

	m.BuildBarTimeList(pulses_per_quarter_note);

	m.TranslateRealTimeMeter(m.m_init_meter_amount, m.m_init_meter_unit);

	// Tell our tracks their IDs
	for (int i = 0; i < track_count; ++i)
	{
		m.m_tracks[i].SetTrackId(i);
		m.m_tracks[i].SetTrackName(m.GetTrackName(m.m_tracks[i].Events()));
	}

	unsigned long first_note_pulse = m.FindFirstNoteOnPulse();

	// of events into microseconds.
	for (MidiTrackList::iterator i = m.m_tracks.begin(); i != m.m_tracks.end(); ++i)
	{
		MidiEventMicrosecondList event_usecs;

		for (MidiEventPulsesList::const_iterator j = i->EventPulses().begin(); j != i->EventPulses().end(); ++j)
		{
			event_usecs.push_back(m.GetEventPulseInMicroseconds(*j, pulses_per_quarter_note));
		}

		i->SetEventUsecs(event_usecs);
	}

	// Translate each track's list of notes and list
	for (MidiTrackList::iterator i = m.m_tracks.begin(); i != m.m_tracks.end(); ++i)
	{
		i->Reset();

		m.TranslateNotes(i->Notes(), pulses_per_quarter_note);
		//m.TranslateNotes(i->Notes(), pulses_per_quarter_note, first_note_pulse);
	}

	m.m_initialized = true;

	// Just grab the end of the last note to find out how long the song is
	m.m_microsecond_base_song_length = m.m_translated_notes.empty() ? 0 : m.m_translated_notes.rbegin()->end;

	// Eat everything up until *just* before the first note event
	m.m_microsecond_dead_start_air = m.GetEventPulseInMicroseconds(m.FindFirstNoteOnPulse(), pulses_per_quarter_note) - 1;

	m.m_reserved_bars = m.GetSongReservedBarCount(first_note_pulse);

	m.m_microsecond_song_start = m.m_bar_usecs[m.m_reserved_bars];
	m.m_microsecond_song_end = m.m_bar_usecs[m.GetSongBarCount()];

	m.m_microsecond_init_running_tempo = m.GetSongRunningTempoMicroseconds();

	m.m_init_ticks = m.GetSongTicks(m.m_microsecond_init_running_tempo);

	return m;
}

Midi Midi::LinkMidi(vector<std::string> files)
{
	Midi m;

	m = Midi::ReadFromFile(*files.begin());
	files.erase(files.begin());

	vector<Midi> list_m;
	for (auto f : files)
	{
		list_m.push_back(Midi::ReadFromFile(f));
	}

	std::string trackName = "drum";

	for (auto lm : list_m)
	{
		MidiTrack t = lm.FindTrack(trackName);
		m.LinkMidiTrack(trackName, t);
		m.m_microsecond_base_song_length += lm.m_microsecond_base_song_length;
		m.m_microsecond_song_end += lm.m_microsecond_song_end;

		unsigned long pulsesEnd = m.m_bar_pulses.back();
		lm.m_bar_pulses.erase(lm.m_bar_pulses.begin());
		for (auto pu : lm.m_bar_pulses)
		{
			m.m_bar_pulses.push_back(pulsesEnd + pu);
		}

		microseconds_t usecsEnd = m.m_bar_usecs.back();
		lm.m_bar_usecs.erase(lm.m_bar_usecs.begin());
		for (auto us : lm.m_bar_usecs)
		{
			m.m_bar_usecs.push_back(usecsEnd + us);
		}

		microseconds_t meterStart = m.m_meter_start_usecs.back().back().second;
		for (auto ms : lm.m_meter_start_usecs)
		{
			MeterMicrosecondList newMeterStart;
			for (auto mn : ms)
			{
				mn.second += meterStart;
				newMeterStart.push_back(mn);
			}
			m.m_meter_start_usecs.push_back(newMeterStart);
		}

		microseconds_t meterEnd = m.m_meter_end_usecs.back().back().second;
		for (auto me : lm.m_meter_end_usecs)
		{
			MeterMicrosecondList newMeterEnd;
			for (auto mn : me)
			{
				mn.second += meterEnd;
				newMeterEnd.push_back(mn);
			}
			m.m_meter_end_usecs.push_back(newMeterEnd);
		}
	}

	list_m.clear();

	return m;
}

bool Midi::LinkMidiTrack(std::string track_name, MidiTrack &track)
{
	MidiTrackList::iterator t = m_tracks.begin();

	while (t != m_tracks.end())
	{
		if (track_name == t->GetTrackName())
		{
			break;
		}

		++t;
	}

	if (t == m_tracks.end())
	{
		return false;
	}

	unsigned long deltaPulses = this->m_bar_pulses.back() - t->EventPulses().back();

	return t->LinkMidiTrack(track, deltaPulses, this->m_bar_pulses.back(), this->m_bar_usecs.back());
}

Midi Midi::ReadPrivateInfoFromFile(std::string filename)
{
	fstream file(filename.c_str(), ios::in | ios::binary);
	if (!file.good()) throw MidiError(MidiError_BadFilename);

	Midi m;

	try
	{
		istream &stream = file;

		const static string MidiFileHeader = "MThd";
		const static string RiffFileHeader = "RIFF";

		// I could use (MidiFileHeader.length() + 1), but then this has to be
		// dynamically allocated.  More hassle than it's worth.  MIDI is well
		// defined and will always have a 4-byte header.  We use 5 so we get
		// free null termination.
		char           header_id[5] = { 0, 0, 0, 0, 0 };
		unsigned int   header_length;
		unsigned short format;
		unsigned short track_count;
		unsigned short time_division;

		stream.read(header_id, static_cast<streamsize>(MidiFileHeader.length()));
		string header(header_id);
		if (header != MidiFileHeader)
		{
			if (header != RiffFileHeader) throw MidiError(MidiError_UnknownHeaderType);
			else
			{
				// We know how to support RIFF files
				unsigned int throw_away;
				stream.read(reinterpret_cast<char*>(&throw_away), sizeof(unsigned int)); // RIFF length
				stream.read(reinterpret_cast<char*>(&throw_away), sizeof(unsigned int)); // "RMID"
				stream.read(reinterpret_cast<char*>(&throw_away), sizeof(unsigned int)); // "data"
				stream.read(reinterpret_cast<char*>(&throw_away), sizeof(unsigned int)); // data size

																						  // Call this recursively, without the RIFF header this time
				return ReadFromStream(stream);
			}
		}

		stream.read(reinterpret_cast<char*>(&header_length), sizeof(unsigned int));
		stream.read(reinterpret_cast<char*>(&format), sizeof(unsigned short));
		stream.read(reinterpret_cast<char*>(&track_count), sizeof(unsigned short));
		stream.read(reinterpret_cast<char*>(&time_division), sizeof(unsigned short));

		if (stream.fail()) throw MidiError(MidiError_NoHeader);

		// Chunk Size is always 6 by definition
		const static unsigned int MidiFileHeaderChunkLength = 6;

		header_length = swap32(header_length);
		if (header_length != MidiFileHeaderChunkLength)
		{
			throw MidiError(MidiError_BadHeaderSize);
		}

		const static int MidiFormat0 = 0;
		const static int MidiFormat1 = 1;
		const static int MidiFormat2 = 2;

		format = swap16(format);
		if (format == MidiFormat2)
		{
			// MIDI 0: All information in 1 track
			// MIDI 1: Multiple tracks intended to be played simultaneously
			// MIDI 2: Multiple tracks intended to be played separately
			//
			// We do not support MIDI 2 at this time
			throw MidiError(MidiError_Type2MidiNotSupported);
		}

		track_count = swap16(track_count);
		if (format == 0 && track_count != 1)
		{
			// MIDI 0 has only 1 track by definition
			throw MidiError(MidiError_BadType0Midi);
		}

		// Time division can be encoded two ways based on a bit-flag:
		// - pulses per quarter note (15-bits)
		// - SMTPE frames per second (7-bits for SMPTE frame count and 8-bits for clock ticks per frame)
		time_division = swap16(time_division);
		bool in_smpte = ((time_division & 0x8000) != 0);

		if (in_smpte)
		{
			throw MidiError(MidiError_SMTPETimingNotImplemented);
		}

		// We ignore the possibility of SMPTE timing, so we can
		// use the time division value directly as PPQN.
		unsigned short pulses_per_quarter_note = time_division;

		// Read in our tracks
		m.m_tracks.push_back(MidiTrack::ReadFromStream(stream));

		m.TranslatePrivateInfo();
	}
	catch (const MidiError &e)
	{
		// Close our file resource before handing the error up
		file.close();
		throw e;
	}

	return m;
}


void Midi::BuildMeterTrack()
{
	std::map<unsigned long, MidiEvent> meter_events;

	for (MidiTrackList::iterator t = m_tracks.begin(); t != m_tracks.end(); ++t)
	{
		for (size_t i = 0; i < t->Events().size(); ++i)
		{
			MidiEvent ev = t->Events()[i];
			unsigned long ev_pulses = t->EventPulses()[i];

			if (ev.Type() == MidiEventType_Meta && ev.MetaType() == MidiMetaEvent_TimeSignature)
			{
				MidiEventList::iterator event_to_erase = t->Events().begin();
				MidiEventPulsesList::iterator event_pulse_to_erase = t->EventPulses().begin();

				for (size_t j = 0; j < i; ++j)
				{
					++event_to_erase;
					++event_pulse_to_erase;
				}

				t->Events().erase(event_to_erase);
				t->EventPulses().erase(event_pulse_to_erase);

				if (t->Events().size() > i)
				{
					unsigned long next_dt = t->Events()[i].GetDeltaPulses();

					t->Events()[i].SetDeltaPulses(ev.GetDeltaPulses() + next_dt);
				}

				--i;

				meter_events[ev_pulses] = ev;
			}
		}
	}

	m_tracks.push_back(MidiTrack::CreateBlankTrack());

	MidiEventList &tempo_track_events = m_tracks[m_tracks.size() - 1].Events();
	MidiEventPulsesList &tempo_track_event_pulses = m_tracks[m_tracks.size() - 1].EventPulses();

	unsigned long previous_absolute_pulses = 0;
	for (std::map<unsigned long, MidiEvent>::const_iterator i = meter_events.begin(); i != meter_events.end(); ++i)
	{
		unsigned long absolute_pulses = i->first;
		MidiEvent ev = i->second;

		ev.SetDeltaPulses(absolute_pulses - previous_absolute_pulses);
		previous_absolute_pulses = absolute_pulses;

		tempo_track_event_pulses.push_back(absolute_pulses);
		tempo_track_events.push_back(ev);
	}

	return;
}

// NOTE: This is required for much of the other functionality provided
// by this class, however, this causes a destructive change in the way
// the MIDI is represented internally which means we can never save the
// file back out to disk exactly as we loaded it.
//
// This adds an extra track dedicated to tempo change events.  Tempo events
// are extracted from every other track and placed in the new one.
//
// This allows quick(er) calculation of wall-clock event times
void Midi::BuildTempoTrack()
{
	// This map will help us get rid of duplicate events if
	// the tempo is specified in every track (as is common).
	//
	// It also does sorting for us so we can just copy the
	// events right over to the new track.
	std::map<unsigned long, MidiEvent> tempo_events;

	// Run through each track looking for tempo events
	for (MidiTrackList::iterator t = m_tracks.begin(); t != m_tracks.end(); ++t)
	{
		for (size_t i = 0; i < t->Events().size(); ++i)
		{
			MidiEvent ev = t->Events()[i];
			unsigned long ev_pulses = t->EventPulses()[i];

			if (ev.Type() == MidiEventType_Meta && ev.MetaType() == MidiMetaEvent_TempoChange)
			{
				// Pull tempo event out of both lists
				//
				// Vector is kind of a hassle this way -- we have to
				// walk an iterator to that point in the list because
				// erase MUST take an iterator... but erasing from a
				// list invalidates iterators.  bleah.
				MidiEventList::iterator event_to_erase = t->Events().begin();
				MidiEventPulsesList::iterator event_pulse_to_erase = t->EventPulses().begin();

				for (size_t j = 0; j < i; ++j)
				{
					++event_to_erase;
					++event_pulse_to_erase;
				}

				t->Events().erase(event_to_erase);
				t->EventPulses().erase(event_pulse_to_erase);

				// Adjust next event's delta time
				if (t->Events().size() > i)
				{
					// (We just erased the element at i, so
					// now i is pointing to the next element)
					unsigned long next_dt = t->Events()[i].GetDeltaPulses();

					t->Events()[i].SetDeltaPulses(ev.GetDeltaPulses() + next_dt);
				}

				// We have to roll i back for the next loop around
				--i;

				// Insert our newly stolen event into the auto-sorting map
				tempo_events[ev_pulses] = ev;
			}
		}
	}

	// Create a new track (always the last track in the track list)
	m_tracks.push_back(MidiTrack::CreateBlankTrack());

	MidiEventList &tempo_track_events = m_tracks[m_tracks.size() - 1].Events();
	MidiEventPulsesList &tempo_track_event_pulses = m_tracks[m_tracks.size() - 1].EventPulses();

	// Copy over all our tempo events
	unsigned long previous_absolute_pulses = 0;
	for (std::map<unsigned long, MidiEvent>::const_iterator i = tempo_events.begin(); i != tempo_events.end(); ++i)
	{
		unsigned long absolute_pulses = i->first;
		MidiEvent ev = i->second;

		// Reset each of their delta times while we go
		ev.SetDeltaPulses(absolute_pulses - previous_absolute_pulses);
		previous_absolute_pulses = absolute_pulses;

		// Add them to the track
		tempo_track_event_pulses.push_back(absolute_pulses);
		tempo_track_events.push_back(ev);
	}
}

MidiEventMicrosecondList Midi::GetBarUsecs(){

	return m_bar_usecs;
}

void Midi::BuildBarTimeList(unsigned short pulses_per_quarter_note)
{
	if (m_tracks.size() <= 2)
	{
		return;
	}

	const MidiTrack &meterTrack = m_tracks[m_tracks.size() - 2];

	if (meterTrack.Events().size() <= 0)
	{
		return;
	}

	int meter_amount;
	int meter_unit;
	int bar_num = 0;

	unsigned long pulses_bar;
	unsigned long ev_pulses;
	unsigned long bar_pulses = 0;

	for (size_t i = 0; i < meterTrack.Events().size(); ++i)
	{

		size_t last_i = i + 1;
		if (last_i < meterTrack.Events().size())
		{
			if (meterTrack.Events()[i].BeatMember() == meterTrack.Events()[last_i].BeatMember()
				&& meterTrack.Events()[i].BeatDenominator() == meterTrack.Events()[last_i].BeatDenominator())
			{
				continue;
			}
		}

		meter_amount = meterTrack.Events()[i].BeatMember();
		meter_unit = meterTrack.Events()[i].BeatDenominator();

		pulses_bar = 4 * pulses_per_quarter_note * meter_amount / meter_unit;

		bar_pulses = (bar_num == 0) ? 0 : bar_pulses;// (bar_pulses + pulses_bar);
		if (last_i >= meterTrack.Events().size())
		{
			break;
		}
		ev_pulses = meterTrack.EventPulses()[last_i];

		while (bar_pulses < ev_pulses)
		{
			MeterMicrosecondList meters_start_list;
			MeterMicrosecondList meters_end_list;
			for (int j = 0; j < meter_amount; ++j)
			{
				unsigned long meter_start_pulses = 4 * pulses_per_quarter_note * j / meter_unit;
				meters_start_list.push_back(make_pair(j, GetEventPulseInMicroseconds(bar_pulses + meter_start_pulses, pulses_per_quarter_note)));
				unsigned long meter_end_pulses = 4 * pulses_per_quarter_note * (j+1) / meter_unit;
				meters_end_list.push_back(make_pair(j, GetEventPulseInMicroseconds(bar_pulses + meter_end_pulses, pulses_per_quarter_note)));
			}
			m_meter_start_usecs.push_back(meters_start_list);
			m_meter_end_usecs.push_back(meters_end_list);

			++bar_num;
			m_bar_pulses.push_back(bar_pulses);
			m_bar_usecs.push_back(GetEventPulseInMicroseconds(bar_pulses, pulses_per_quarter_note));
			bar_pulses = (bar_num == 0) ? 0 : (bar_pulses + pulses_bar);
		}
	}

	ev_pulses = FindLastNoteOffPulse();
	while (bar_pulses <= ev_pulses)
	{
		MeterMicrosecondList meters_start_list;
		MeterMicrosecondList meters_end_list;
		for (int j = 0; j < meter_amount; ++j)
		{
			unsigned long meter_start_pulses = 4 * pulses_per_quarter_note * j / meter_unit;
			meters_start_list.push_back(make_pair(j, GetEventPulseInMicroseconds(bar_pulses + meter_start_pulses, pulses_per_quarter_note)));
			unsigned long meter_end_pulses = 4 * pulses_per_quarter_note * (j + 1) / meter_unit;
			meters_end_list.push_back(make_pair(j, GetEventPulseInMicroseconds(bar_pulses + meter_end_pulses, pulses_per_quarter_note)));
		}
		m_meter_start_usecs.push_back(meters_start_list);
		m_meter_end_usecs.push_back(meters_end_list);

		++bar_num;
		m_bar_pulses.push_back(bar_pulses);
		m_bar_usecs.push_back(GetEventPulseInMicroseconds(bar_pulses, pulses_per_quarter_note));
		bar_pulses = (bar_num == 0) ? 0 : (bar_pulses + pulses_bar);
	}

	if (ev_pulses == m_bar_pulses.back())
	{
		return;
	}

	m_bar_pulses.push_back(bar_pulses);
	m_bar_usecs.push_back(GetEventPulseInMicroseconds(bar_pulses, pulses_per_quarter_note));

	return;
}

const MidiTrack Midi::FindTrack(std::string track_name)
{
	MidiTrackList::iterator t = m_tracks.begin();

	while (t != m_tracks.end())
	{
		if (track_name == GetTrackName(t->Events()))
		{
			break;
		}

		++t;
	}

	return *t;
}

TranslatedNoteSet Midi::FindNotes(std::string track_name)
{
	TranslatedNoteSet notes;

	for (TranslatedNoteSet::iterator i = m_translated_notes.begin(); i != m_translated_notes.end(); ++i)
	{
		if (track_name == i->track_name)
		{
			notes.insert(*i);
		}

		continue;
	}

	return notes;
}

int Midi::GetSongReservedBarCount(unsigned long first_note_pulses) const
{
	int bar_sum = -1;
	for (MidiEventPulsesList::const_iterator i = m_bar_pulses.begin(); i != m_bar_pulses.end(); ++i)
	{
		if ((*i) > first_note_pulses)
		{
			break;
		}

		++bar_sum;
	}

	return bar_sum;
}

void Midi::TranslateNotes(const NoteSet &notes, unsigned short pulses_per_quarter_note)
{
	for (NoteSet::const_iterator i = notes.begin(); i != notes.end(); ++i)
	{
		TranslatedNote trans;

		trans.note_id = i->note_id;
		trans.track_id = i->track_id;
		trans.channel = i->channel;
		trans.velocity = i->velocity;
		trans.start = GetEventPulseInMicroseconds(i->start, pulses_per_quarter_note);
		trans.end = GetEventPulseInMicroseconds(i->end, pulses_per_quarter_note);
		trans.time_unit = GetSongRunningTempoMicroseconds(trans.start);
		trans.bar_id = GetBarID(trans.start);
		trans.track_name = i->track_name;
		trans.state = UserPlayable;

		m_translated_notes.insert(trans);
	}
}

void Midi::TranslateNotes(const NoteSet &notes, unsigned short pulses_per_quarter_note, unsigned long first_note_pulses)
{
	int uiMember;
	int uiDenominator;

	unsigned long pulses_bar;

	for (NoteSet::const_iterator i = notes.begin(); i != notes.end(); ++i)
	{
		TranslateRealTimeMeter(uiMember, uiDenominator, i->start);
		pulses_bar = 4 * pulses_per_quarter_note * uiMember / uiDenominator;

		TranslatedNote trans;

		trans.note_id = i->note_id;
		trans.track_id = i->track_id;
		trans.channel = i->channel;
		trans.velocity = i->velocity;
		trans.start = GetEventPulseInMicroseconds(i->start, pulses_per_quarter_note);
		trans.end = GetEventPulseInMicroseconds(i->end, pulses_per_quarter_note);
		trans.time_unit = GetSongRunningTempoMicroseconds(trans.start);
		trans.bar_id = GetBarID(trans.start);
		trans.track_name = i->track_name;
		trans.state = UserPlayable;

		m_translated_notes.insert(trans);

		// --------------------------------------------------------------------------------------------------------------
	}
}


unsigned long Midi::FindFirstNoteOnPulse()
{
	unsigned long first_note_pulse = 0;

	// Find the very last value it could ever possibly be, to start with
	for (MidiTrackList::const_iterator t = m_tracks.begin(); t != m_tracks.end(); ++t)
	{
		if (t->EventPulses().size() == 0) continue;
		unsigned long pulses = t->EventPulses().back();

		if (pulses > first_note_pulse) first_note_pulse = pulses;
	}

	// Now run through each event in each track looking for the very
	// first note_on event
	for (MidiTrackList::const_iterator t = m_tracks.begin(); t != m_tracks.end(); ++t)
	{
		for (size_t ev_id = 0; ev_id < t->Events().size(); ++ev_id)
		{
			if (t->Events()[ev_id].Type() == MidiEventType_NoteOn)
			{
				unsigned long note_pulse = t->EventPulses()[ev_id];

				if (note_pulse < first_note_pulse) first_note_pulse = note_pulse;

				// We found the first note event in this
				// track.  No need to keep searching.
				break;
			}
		}
	}

	return first_note_pulse;
}

unsigned long Midi::FindLastNoteOffPulse()
{
	unsigned long last_note_pulse = 0;

	// Now run through each event in each track looking for the very
	// last note_off event
	for (MidiTrackList::const_iterator t = m_tracks.begin(); t != m_tracks.end(); ++t)
	{
		for (int ev_id = t->Events().size() - 1; ev_id >= 0; --ev_id)
		{
			if (t->Events()[ev_id].Type() == MidiEventType_NoteOff || (t->Events()[ev_id].Type() == MidiEventType_NoteOn && t->Events()[ev_id].NoteVelocity() == 0))
			{
				unsigned long note_pulse = t->EventPulses()[ev_id];

				if (note_pulse > last_note_pulse)
				{
					last_note_pulse = note_pulse;
				}

				// We found the last note event in this
				// track.  No need to keep searching.
				break;
			}
		}
	}

	return last_note_pulse;
}

microseconds_t Midi::ConvertPulsesToMicroseconds(unsigned long pulses, microseconds_t tempo, unsigned short pulses_per_quarter_note)
{
	// Here's what we have to work with:
	//   pulses is given
	//   tempo is given (units of microseconds/quarter_note)
	//   (pulses/quarter_note) is given as a constant in this object file
	const double quarter_notes = static_cast<double>(pulses) / static_cast<double>(pulses_per_quarter_note);
	const double microseconds = quarter_notes * static_cast<double>(tempo);

	return static_cast<microseconds_t>(microseconds);
}

microseconds_t Midi::GetEventPulseInMicroseconds(unsigned long event_pulses, unsigned short pulses_per_quarter_note) const
{
	if (m_tracks.size() == 0) return 0;
	const MidiTrack &tempo_track = m_tracks.back();

	microseconds_t running_result = 0;

	bool hit = false;
	unsigned long last_tempo_event_pulses = 0;
	microseconds_t running_tempo = DefaultUSTempo;
	for (size_t i = 0; i < tempo_track.Events().size(); ++i)
	{
		unsigned long tempo_event_pulses = tempo_track.EventPulses()[i];

		// If the time we're asking to convert is still beyond
		// this tempo event, just add the last time slice (at
		// the previous tempo) to the running wall-clock time.
		unsigned long delta_pulses = 0;
		if (event_pulses > tempo_event_pulses)
		{
			delta_pulses = tempo_event_pulses - last_tempo_event_pulses;
		}
		else
		{
			hit = true;
			delta_pulses = event_pulses - last_tempo_event_pulses;
		}

		running_result += ConvertPulsesToMicroseconds(delta_pulses, running_tempo, pulses_per_quarter_note);

		// If the time we're calculating is before the tempo event we're
		// looking at, we're done.
		if (hit) break;

		running_tempo = tempo_track.Events()[i].GetTempoInUsPerQn();
		last_tempo_event_pulses = tempo_event_pulses;
	}

	// The requested time may be after the very last tempo event
	if (!hit)
	{
		unsigned long remaining_pulses = event_pulses - last_tempo_event_pulses;
		running_result += ConvertPulsesToMicroseconds(remaining_pulses, running_tempo, pulses_per_quarter_note);
	}

	return running_result;
}

MidiEventListWithTrackId Midi::Update(microseconds_t delta)
{
	MidiEventListWithTrackId aggregated_events;
	if (!m_initialized) return aggregated_events;

	m_microsecond_song_position += delta;
	if (m_first_update_after_reset)
	{
		delta += m_microsecond_song_position;
		m_first_update_after_reset = false;
	}

	if (delta == 0) return aggregated_events;
	if (m_microsecond_song_position < 0) return aggregated_events;
	if (delta > m_microsecond_song_position) delta = m_microsecond_song_position;

	const size_t track_count = m_tracks.size();
	for (size_t i = 0; i < track_count; ++i)
	{
		const MidiEventList &track_events = m_tracks[i].Update(delta);

		const size_t event_count = track_events.size();
		for (size_t j = 0; j < event_count; ++j)
		{
			aggregated_events.insert(aggregated_events.end(), pair<size_t, MidiEvent>(i, track_events[j]));
		}
	}

	return aggregated_events;
}

MidiEventListWithTrackId Midi::Update(microseconds_t delta, bool loop)
{
	MidiEventListWithTrackId aggregated_events;
	if (!m_initialized)
	{
		return aggregated_events;
	}

	if (delta < 0)
	{
		return aggregated_events;
	}
	m_microsecond_song_position += delta;
	if (m_first_update_after_reset)
	{
		delta += (m_microsecond_song_position + m_microsecond_defer);
		m_first_update_after_reset = false;
	}

	if (delta == 0)
	{
		return aggregated_events;
	}
	if (m_microsecond_song_position + m_microsecond_defer < 0)
	{
		return aggregated_events;
	}
	if (delta > m_microsecond_song_position + m_microsecond_defer)
	{
		delta = m_microsecond_song_position + m_microsecond_defer;
	}

	if (loop)
	{
		// ----ѭ���Ľ������ڿ�ʼ��֮��
		if (m_microsecond_loop_end > m_microsecond_loop_start)
		{
			if (m_microsecond_song_position >= m_microsecond_loop_end)
			{
				m_microsecond_song_position = m_microsecond_song_position - m_microsecond_loop_end + m_microsecond_loop_start;
			}
		}
		// ----ѭ���Ľ������ڿ�ʼ��֮ǰ
		if (m_microsecond_loop_end < m_microsecond_loop_start)
		{
			if (m_microsecond_song_position >= m_microsecond_song_end)
			{
				m_microsecond_song_position = m_microsecond_song_position - m_microsecond_song_end + m_microsecond_song_start;
			}
			else
			{
				if (m_microsecond_song_position >= m_microsecond_loop_end && m_microsecond_song_position < m_microsecond_loop_start)
				{
					m_microsecond_song_position = m_microsecond_song_position - m_microsecond_loop_end + m_microsecond_loop_start;
				}
			}
		}
	}

	/*if (m_microsecond_song_position > m_microsecond_song_end)
	{
	m_microsecond_song_position = m_microsecond_song_end;
	}*/

	const size_t track_count = m_tracks.size();
	for (size_t i = 0; i < track_count; ++i)
	{
		const MidiEventList &track_events = m_tracks[i].Update(delta, loop);

		/*std::string name = GetTrackName(m_tracks[i].Events());

		// ���ε�����Ҫ�����Ĺ���ڵ���Ϣ
		if (isMuteNote(name))
		{
			continue;
		}*/

		const size_t event_count = track_events.size();
		for (size_t j = 0; j < event_count; ++j)
		{
			//track_events[j].GetTempoInUsPerQn();
			aggregated_events.insert(aggregated_events.end(), pair<size_t, MidiEvent>(i, track_events[j]));
		}
	}

	return aggregated_events;
}

void Midi::Reset(microseconds_t lead_in, microseconds_t lead_out)
{
	m_microsecond_lead_out = lead_out;
	//m_microsecond_song_position = m_microsecond_dead_start_air - lead_in;
	m_microsecond_song_position = m_microsecond_song_start - lead_in;
	m_first_update_after_reset = true;

	for (MidiTrackList::iterator i = m_tracks.begin(); i != m_tracks.end(); ++i) { i->Reset(); /*i->Reset(m_microsecond_song_position, m_microsecond_song_end);*/ }
}

void Midi::Reset(microseconds_t lead_in, microseconds_t lead_out, microseconds_t defer, bool hide/* = false*/)
{
	m_microsecond_lead_out = lead_out;
	m_microsecond_song_position = hide ? m_microsecond_song_start - lead_in : -lead_in;
	m_first_update_after_reset = true;

	for (MidiTrackList::iterator i = m_tracks.begin(); i != m_tracks.end(); ++i)
	{
		i->Reset();
		/*i->Reset(m_microsecond_song_position, m_microsecond_song_end);*/
	}

	if (m_first_set)
	{
		m_first_set = false;
		m_microsecond_song_end -= defer;
		m_microsecond_song_start -= defer;
		m_microsecond_defer = defer;
		m_microsecond_song_position -= defer;
	}
}

void Midi::GetRealTimeMeter(unsigned int &meter_amount, unsigned int &meter_unit, microseconds_t song_position/* = -1*/)
{
	if (m_tracks.size() <= 2)
	{
		meter_amount = 0;
		meter_unit = 0;
		return;
	}

	if (song_position == -1)
	{
		song_position = m_microsecond_song_position;
	}

	const MidiTrack &meterTrack = m_tracks[m_tracks.size() - 2];

	if (meterTrack.Events().size() > 0)
	{
		meter_amount = meterTrack.Events().begin()->BeatMember();
		meter_unit = meterTrack.Events().begin()->BeatDenominator();
	}

	for (size_t i = 0; i < meterTrack.Events().size(); ++i)
	{
		MidiEvent ev = meterTrack.Events()[i];
		microseconds_t ev_usecs = meterTrack.EventUsecs()[i];

		if (ev_usecs > song_position)
		{
			break;
		}

		meter_amount = ev.BeatMember();
		meter_unit = ev.BeatDenominator();
	}

	return;
}

void Midi::GetSongBarIDAndMeterID(int &bar_id, int &meter_id, microseconds_t time/* = -1*/)
{
	bar_id = -1;
	meter_id = 0;

	if (time == -1)
	{
		time = m_microsecond_song_position;
	}

	if (time >= m_microsecond_song_end)
	{
		bar_id = m_bar_usecs.size() - 2;
		meter_id = m_meter_start_usecs[bar_id].back().first;

		return;
	}

	for (MidiEventMicrosecondList::const_iterator i = m_bar_usecs.begin(); i != m_bar_usecs.end(); ++i)
	{
		if ((*i) > time)
		{
			break;
		}

		++bar_id;
	}
	bar_id = (bar_id < 0) ? 0 : bar_id;

	MeterMicrosecondList meter_list = m_meter_start_usecs[bar_id];
	for (MeterMicrosecondList::const_iterator i = meter_list.begin(); i != meter_list.end(); ++i)
	{
		if (i->second > time)
		{
			break;
		}

		meter_id = i->first;
	}
}

microseconds_t Midi::GetSongLengthInMicroseconds() const
{
	if (!m_initialized) return 0;
	return m_microsecond_base_song_length - m_microsecond_dead_start_air;
}

microseconds_t Midi::GetSongRunningTempoMicroseconds(microseconds_t song_position /* = 0 */) const
{
	const MidiTrack &tempoTrack = m_tracks.back();

	microseconds_t runningTempo = DefaultUSTempo;
	if (tempoTrack.Events().size() > 0)
	{
		runningTempo = tempoTrack.Events().begin()->GetTempoInUsPerQn();
	}

	for (size_t i = 0; i < tempoTrack.Events().size(); ++i)
	{
		if (tempoTrack.EventUsecs()[i] > song_position)
		{
			break;
		}
		runningTempo = tempoTrack.Events()[i].GetTempoInUsPerQn();
	}

	return runningTempo;
}

unsigned int Midi::AggregateEventsRemain() const
{
	if (!m_initialized) return 0;

	unsigned int aggregate = 0;
	for (MidiTrackList::const_iterator i = m_tracks.begin(); i != m_tracks.end(); ++i)
	{
		aggregate += i->AggregateEventsRemain();
	}
	return aggregate;
}

unsigned int Midi::AggregateNotesRemain() const
{
	if (!m_initialized) return 0;

	unsigned int aggregate = 0;
	for (MidiTrackList::const_iterator i = m_tracks.begin(); i != m_tracks.end(); ++i)
	{
		aggregate += i->AggregateNotesRemain();
	}
	return aggregate;
}

unsigned int Midi::AggregateEventCount() const
{
	if (!m_initialized) return 0;

	unsigned int aggregate = 0;
	for (MidiTrackList::const_iterator i = m_tracks.begin(); i != m_tracks.end(); ++i)
	{
		aggregate += i->AggregateEventCount();
	}
	return aggregate;
}

unsigned int Midi::AggregateNoteCount() const
{
	if (!m_initialized) return 0;

	unsigned int aggregate = 0;
	for (MidiTrackList::const_iterator i = m_tracks.begin(); i != m_tracks.end(); ++i)
	{
		aggregate += i->AggregateNoteCount();
	}
	return aggregate;
}

double Midi::GetSongPercentageComplete() const
{
	if (!m_initialized) return 0.0;

	const double pos = static_cast<double>(m_microsecond_song_position - m_microsecond_dead_start_air);
	const double len = static_cast<double>(GetSongLengthInMicroseconds());

	if (pos < 0) return 0.0;
	if (len == 0) return 1.0;

	return min((pos / len), 1.0);
}

/*
bool Midi::IsSongOver() const
{
   if (!m_initialized) return true;
   return (m_microsecond_song_position - m_microsecond_dead_start_air) >= GetSongLengthInMicroseconds() + m_microsecond_lead_out;
}*/


bool Midi::IsSongOver() const
{
	if (!m_initialized) return true;

	return m_microsecond_song_position >= m_microsecond_song_end + m_microsecond_lead_out;
}


MidiEventListWithTrackId Midi::LoadControlEvent()
{
	MidiEventListWithTrackId aggregated_events;

	const size_t track_count = m_tracks.size();
	for (size_t i = 0; i < track_count; ++i)
	{
		const MidiEventList &track_events = m_tracks[i].LoadControlEvent();

		const size_t event_count = track_events.size();
		for (size_t j = 0; j < event_count; ++j)
		{
			aggregated_events.insert(aggregated_events.end(), std::pair<size_t, MidiEvent>(i, track_events[j]));
		}
	}

	return aggregated_events;
}

microseconds_t Midi::GetBarStartMicroseconds(int bar_id) const
{
	return (bar_id < m_bar_usecs.size()) ? m_bar_usecs[bar_id] : -99999;
}

microseconds_t Midi::GetBarForMeterStartMicroseconds(int bar_id, size_t meter_id) const
{
	microseconds_t start = -99999;

	if (m_meter_start_usecs.size() <= bar_id)
	{
		start = m_meter_end_usecs[m_meter_end_usecs.size() - 1].back().second;
		return start;
	}

	MeterMicrosecondList list = m_meter_start_usecs[bar_id];
	for (int i = 0; i < list.size(); ++i)
	{
		if (meter_id == list[i].first)
		{
			start = list[i].second;
			break;
		}
	}

	return start;
}

microseconds_t Midi::GetBarForMeterEndMicroseconds(int bar_id, size_t meter_id) const
{
	microseconds_t end = -99999;

	if (m_meter_end_usecs.size() <= bar_id)
	{
		MeterMicrosecondList list = m_meter_end_usecs[m_meter_end_usecs.size() - 1];
		end = list.back().second + list[list.size()-1].second - list[list.size() - 2].second;
		return end;
	}

	MeterMicrosecondList list = m_meter_end_usecs[bar_id];
	for (int i = 0; i < list.size(); ++i)
	{
		if (meter_id == list[i].first)
		{
			end = list[i].second;
			break;
		}
	}

	return end;
}

unsigned int Midi::GetBarID(microseconds_t time) const
{
	unsigned int id = 0;

	for (int i = 0; i < m_bar_usecs.size(); ++i)
	{
		if (time < m_bar_usecs[i])
		{
			break;
		}

		id = i;
	}

	return id;
}

int Midi::GetSongBarCount() const
{
	return (m_bar_usecs.size() > 1) ? m_bar_usecs.size() - 1 : 0;
}

int Midi::GetSongPositionInBarID(bool defer /* = false */) const
{
	int bar_id = -1;

	for (MidiEventMicrosecondList::const_iterator i = m_bar_usecs.begin(); i != m_bar_usecs.end(); ++i)
	{
		if ((*i) > m_microsecond_song_position)
		{
			break;
		}

		++bar_id;
	}

	return bar_id;
}

unsigned int Midi::GetSongTicks(microseconds_t running_tempo) const
{
	unsigned int ticks;

	if (OneMinuteInMicroseconds % running_tempo != 0)
	{
		if (m_microsecond_init_running_tempo / (OneMinuteInMicroseconds % running_tempo) > 2)
		{
			ticks = static_cast<int>(OneMinuteInMicroseconds / running_tempo);
		}
		else
		{
			ticks = static_cast<int>(OneMinuteInMicroseconds / running_tempo) + 1;
		}
	}
	else
	{
		ticks = static_cast<int>(OneMinuteInMicroseconds / running_tempo);
	}

	return ticks;
}

void Midi::TranslateRealTimeMeter(int &meter_amount, int &meter_unit, unsigned long event_pulses /* = 0 */)
{
	if (m_tracks.size() <= 2)
	{
		meter_amount = 0;
		meter_unit = 0;
		return;
	}

	const MidiTrack &meterTrack = m_tracks[m_tracks.size() - 2];

	if (meterTrack.Events().size() > 0)
	{
		meter_amount = meterTrack.Events().begin()->BeatMember();
		meter_unit = meterTrack.Events().begin()->BeatDenominator();
	}

	for (size_t i = 0; i < meterTrack.Events().size(); ++i)
	{
		MidiEvent ev = meterTrack.Events()[i];
		unsigned long ev_pulses = meterTrack.EventPulses()[i];

		if (ev_pulses > event_pulses)
		{
			break;
		}

		meter_amount = ev.BeatMember();
		meter_unit = ev.BeatDenominator();
	}

	return;
}

void Midi::TranslatePrivateInfo(void)
{
	if (m_tracks.size() == 0)
	{
		return;
	}

	const MidiTrack &private_track = m_tracks.front();
	for (size_t i = 0; i < private_track.Events().size(); ++i)
	{
		const MidiEvent &ev = private_track.Events()[i];

		if (ev.MetaType() != MidiMetaEvent_Text || !ev.HasText())
		{
			continue;
		}

		std::string text = ev.Text();
		text.erase(text.find('\n'), text.substr(text.find('\n')).size());

		std::string tempo = "Speed*";
		std::string difficulty = "Level*";
		std::string style = "Style*";

		if (text.find(tempo) != std::string::npos
			&& text.find(difficulty) != std::string::npos
			&& text.find(style) != std::string::npos)
		{
			std::string temp = text;

			// ��ȡ��Ŀ�Ѷ�
			std::size_t pos = temp.find_last_of('_');
			if (string::npos != pos)
			{
				temp.erase(0, tempo.size() + pos + 1);
				text.erase(pos, text.substr(pos).size());
			}
			m_private_info.difficulty = temp;

			// ��ȡ��Ŀ���
			temp = text;
			pos = temp.find_last_of('_');
			if (string::npos != pos)
			{
				temp.erase(0, style.size() + pos + 1);
				text.erase(pos, text.substr(pos).size());
			}
			m_private_info.style = temp;

			// ��ȡ��Ŀ�ٶ�
			temp = text;
			temp.erase(0, difficulty.size());
			m_private_info.tempo = temp;

			break;
		}
	}

	return;
}

std::string Midi::GetTrackName(const MidiEventList &list) const
{
	std::string strTrackName;
	for (MidiEventList::const_iterator i = list.begin(); i != list.end(); ++i)
	{
		if (i->MetaType() == MidiMetaEvent_TrackName)
		{
			strTrackName = i->Text();
			break;
		}
		continue;
	}

	return strTrackName;
}

LoopMode Midi::GetLoopMode(microseconds_t delta_microseconds) const
{
	if (m_microsecond_loop_end > m_microsecond_loop_start)
	{
		if (m_microsecond_song_position + delta_microseconds > m_microsecond_loop_end)
		{
			return LoopBtoA;
		}
		else
		{
			return LoopNot;
		}
	}
	else if (m_microsecond_loop_end < m_microsecond_loop_start)
	{
		if (m_microsecond_song_position + delta_microseconds > m_microsecond_loop_end
			&& m_microsecond_song_position + delta_microseconds < m_microsecond_loop_start)
		{
			return LoopBtoA;
		}
		else if (m_microsecond_song_position + delta_microseconds > m_microsecond_song_end)
		{
			return LoopEtoS;
		}
		else
		{
			return LoopNot;
		}
	}
	else
	{
		return LoopNot;
	}
}

void Midi::SetLoop(microseconds_t start_microseconds, microseconds_t end_microseconds)
{
	m_microsecond_loop_start = start_microseconds;
	m_microsecond_loop_end = end_microseconds;

	for (MidiTrackList::iterator i = m_tracks.begin(); i != m_tracks.end(); ++i)
	{
		i->SetLoop(m_microsecond_loop_start, m_microsecond_loop_end);
	}

	m_microsecond_loop_start -= m_microsecond_defer;
	m_microsecond_loop_end -= m_microsecond_defer;
	return;
}

MidiEventListWithTrackId Midi::SetPlayStart(microseconds_t start_microseconds)
{
	MidiEventListWithTrackId aggregated_events;

	const size_t track_count = m_tracks.size();
	for (size_t i = 0; i < track_count; ++i)
	{
		m_tracks[i].SetPlayStart(start_microseconds);
		const MidiEventList &track_events = m_tracks[i].LoadControlEvent();

		m_microsecond_song_position = start_microseconds - m_microsecond_defer;

		/*if (isMuteNote(GetTrackName(track_events)))
		{
			continue;
		}*/

		const size_t event_count = track_events.size();
		for (size_t j = 0; j < event_count; ++j)
		{
			aggregated_events.insert(aggregated_events.end(), pair<size_t, MidiEvent>(i, track_events[j]));
		}
	}

	return aggregated_events;
}

unsigned char Midi::Channel(unsigned char status)
{
	return (status & 0x0F);
}

void Midi::addPlayTrack(std::string track)
{
	if (!isPlayNote(track))
	{
		m_stlPlayTrack.push_back(track);

		m_play_tracks.push_back(FindTrack(track));

		m_play_notes = FindNotes(track);
	}
}

void Midi::addMuteTrack(std::string track)
{
	if (!isMuteNote(track))
	{
		m_stlMuteTrack.push_back(track);

		for (MidiTrackList::iterator i = m_tracks.begin(); i != m_tracks.end(); )
		{
			MidiTrackList::iterator t = i++;

			std::string name = GetTrackName(t->Events());
			if (track == name)
			{
				MidiTrack mute_track = *t;
				m_mute_tracks.push_back(mute_track);
				m_tracks.erase(t);

				break;
			}
		}
	}
}

bool Midi::isPlayNote(const std::string track)
{
	bool bPlay = false;

	for (int i = 0; i < m_stlPlayTrack.size(); ++i)
	{
		bPlay = (track == m_stlPlayTrack[i]) ? true : bPlay;
	}

	return bPlay;
}

bool Midi::isMuteNote(const std::string track)
{
	bool bMute = false;

	for (int i = 0; i < m_stlMuteTrack.size(); ++i)
	{
		bMute = (track == m_stlMuteTrack[i]) ? true : bMute;
	}

	return bMute;
}

bool Midi::isPercussion(unsigned char channel)
{
	if (channel == 0x9)
		return true;
	else
		return false;
}