#include "Metronome.h"
#include "MidiEvent.h"

Metronome::Metronome(void) : m_bInit(true), m_pFreeBeatSound(NULL), m_pFreeBeatFrames(NULL),
m_pSyncBeatSound(NULL), m_pSyncBeatFrames(NULL)
{}

Metronome::~Metronome(void)
{
	delete m_pFreeBeatFrames;
	delete m_pFreeBeatSound;
	delete m_pSyncBeatFrames;
	delete m_pSyncBeatSound;

	m_pFreeBeatFrames = NULL;
	m_pFreeBeatSound = NULL;
	m_pSyncBeatFrames = NULL;
	m_pSyncBeatSound = NULL;

	Colse();
}


void Metronome::Init(Midi &midi, microseconds_t defer_microseconds/* = 0*/)
{
	m_mDefer = defer_microseconds;
	m_mPrepareMeterPosition = 0;

	UpdateMeter(midi);

	m_tlLight = TL_Black;

	m_iTimer = 1;
	m_iMeterID = 0;

	m_bPlay = false;
	m_bSyncMidi = false;
	m_bKnockOnStick = false;
	m_bPlusMinus = false;

	m_bInit = false;

	return;
}

void Metronome::Init(unsigned int meter_amount /* = 4 */, unsigned int meter_unit /* = 4 */, microseconds_t ticks /* = 120 */, microseconds_t defer_microseconds /* = 0 */)
{
	m_mDefer = defer_microseconds;
	m_mPrepareMeterPosition = 0;

	microseconds_t tempo = 60000000 / ticks;
	UpdateMeter(meter_amount, meter_unit, tempo);

	m_tlLight = TL_Black;
	m_iTimer = 1;
	m_iMeterID = 0;

	m_bPlay = false;
	m_bSyncMidi = false;
	m_bKnockOnStick = false;
	m_bPlusMinus = false;

	m_bInit = false;

	return;
}


MidiEventList Metronome::Update(microseconds_t &delta_microseconds, Midi &midi, bool play, bool sync_midi /* = false */, bool prepare_meter /* = false */)
{
	MidiEventList evs;

	if (m_bInit)
	{
		return evs;
	}

	if (delta_microseconds <= 0)
	{
		return evs;
	}

	if (!play)
	{
		if (m_bPlay != play)
		{
			Reset();
			return Colse();
		}
		m_bPlay = play;
		return evs;
	}
	m_bPlay = play;

	if (m_bSyncMidi != sync_midi && !m_bSyncMidi && midi.GetSongPositionInMicroseconds() + m_mDefer < midi.GetSongEndMicroseconds())
	{
		Reset();
	}
	m_bSyncMidi = sync_midi;

	if (prepare_meter && m_mPrepareMeterPosition == 0)
	{
		m_bKnockOnStick = true;
	}

	UpdateMeter(midi);

	BeatStatus beat_status_frames = m_pFreeBeatFrames->Update(delta_microseconds);
	BeatStatus beat_status_sound = m_pFreeBeatSound->Update(delta_microseconds);


	switch (beat_status_frames)
	{
	case RestBeat:
		++m_iTimer;
		if (m_iTimer > 12)
		{
			m_tlLight = TL_Black;
			m_iTimer = 0;
		}
		break;
	case StrongBeat:
		m_tlLight = TL_Red;
		break;
	case SubsidiaryStrongBeat:
	case WeakBeat:
		m_tlLight = TL_Green;
		break;
	default:
		m_tlLight = TL_Black;
		break;
	}


	if (!m_bSyncMidi)
	{
		switch (beat_status_sound)
		{
		case StrongBeat:
			evs.clear();
			evs = StrongBeatEvents();
			break;
		case SubsidiaryStrongBeat:
		case WeakBeat:
			evs.clear();
			evs = WeakBeatEvents();
			break;
		case RestBeat:
		default:
			break;
		}
		return evs;
	}


	if (m_bKnockOnStick)
	{
		m_mPrepareMeterPosition += delta_microseconds;
		if (m_mPrepareMeterPosition >= m_mBarLength)
		{
			m_bKnockOnStick = false;
			delta_microseconds = m_mPrepareMeterPosition % m_mBarLength;
		}
		else
		{
			delta_microseconds = 0;

			switch (beat_status_sound)
			{
			case StrongBeat:
			case SubsidiaryStrongBeat:
			case WeakBeat:
				evs.clear();
				evs = PrepareBeatEvents();
				break;
			case RestBeat:
			default:
				break;
			}
			return evs;
		}
	}


	beat_status_frames = m_pSyncBeatFrames->Update(delta_microseconds);
	switch (beat_status_frames)
	{
	case RestBeat:
		++m_iTimer;
		if (m_iTimer > 12)
		{
			m_tlLight = TL_Black;
			m_iTimer = 0;
		}
		break;
	case StrongBeat:
		m_tlLight = TL_Red;
		break;
	case SubsidiaryStrongBeat:
	case WeakBeat:
		m_tlLight = TL_Green;
		break;
	default:
		m_tlLight = TL_Black;
		break;
	}


	beat_status_sound = m_pSyncBeatSound->Update(delta_microseconds);
	switch (beat_status_sound)
	{
	case StrongBeat:
		evs.clear();
		evs = StrongBeatEvents();
		break;
	case SubsidiaryStrongBeat:
	case WeakBeat:
		evs.clear();
		evs = WeakBeatEvents();
		break;
	case RestBeat:
	default:
		break;
	}

	return evs;
}

MidiEventList Metronome::Update(microseconds_t &delta_microseconds, bool play)
{
	MidiEventList evs;

	if (m_bInit)
	{
		return evs;
	}

	if (delta_microseconds <= 0)
	{
		return evs;
	}

	if (!play)
	{
		if (m_bPlay != play)
		{
			Reset();
			return Colse();
		}
		m_bPlay = play;
		return evs;
	}
	m_bPlay = play;

	BeatStatus beat_status_frames = m_pFreeBeatFrames->Update(delta_microseconds);
	BeatStatus beat_status_sound = m_pFreeBeatSound->Update(delta_microseconds);


	switch (beat_status_frames)
	{
	case RestBeat:
		if (m_pFreeBeatFrames->GetProgress() >= 0.25)
		{
			m_tlLight = TL_Black;
		}
		break;
	case StrongBeat:
		m_tlLight = TL_Red;
		break;
	case SubsidiaryStrongBeat:
	case WeakBeat:
		m_tlLight = TL_Green;
		break;
	default:
		m_tlLight = TL_Black;
		break;
	}


	if (!m_bSyncMidi)
	{
		switch (beat_status_sound)
		{
		case StrongBeat:
			evs.clear();
			evs = StrongBeatEvents();
			break;
		case SubsidiaryStrongBeat:
		case WeakBeat:
			evs.clear();
			evs = WeakBeatEvents();
			break;
		case RestBeat:
		default:
			break;
		}
		return evs;
	}

	return evs;
}


void Metronome::Reset(void)
{
	m_pFreeBeatFrames->Reset();
	m_pFreeBeatSound->Reset();
	m_pSyncBeatFrames->Reset();
	m_pSyncBeatSound->Reset();

	m_mPrepareMeterPosition = 0;

	m_tlLight = TL_Black;

	m_iTimer = 0;	m_iMeterID = 0;

	m_bPlay = false;
	m_bSyncMidi = false;
	m_bKnockOnStick = false;
	m_bPlusMinus = false;
}


MidiEventList Metronome::Colse()
{
	MidiEventList evs;

	MidiEventSimple mes;
	mes.status = 0x89;
	mes.byte1 = 0x25;
	mes.byte2 = 0x7F;
	evs.push_back(MidiEvent::Build(mes));

	mes.status = 0x89;
	mes.byte1 = 0x51;
	mes.byte2 = 0x7F;
	evs.push_back(MidiEvent::Build(mes));

	mes.status = 0x89;
	mes.byte1 = 0x38;
	mes.byte2 = 0x7F;
	evs.push_back(MidiEvent::Build(mes));

	m_iTimer = 0;	m_tlLight = TL_Black;

	return evs;
}

void Metronome::UpdateMeter(Midi &midi)
{
	unsigned int meter_amount;
	unsigned int meter_unit;

	midi.GetRealTimeMeter(meter_amount, meter_unit, midi.GetSongPositionInMicroseconds() + m_mDefer);
	m_mMeterLength = 4 * midi.GetSongRunningTempoMicroseconds(midi.GetSongPositionInMicroseconds() + m_mDefer) / meter_unit;
	m_mBarLength = 4 * m_mMeterLength * meter_amount / meter_unit;

	int bar_id, meter_id;
	microseconds_t now_time = midi.GetSongPositionInMicroseconds() + m_mDefer;
	midi.GetSongBarIDAndMeterID(bar_id, meter_id, now_time);
	microseconds_t position = now_time - midi.GetBarForMeterStartMicroseconds(bar_id, meter_id);
	if (position > 0)
	{
		meter_id++;
		meter_id %= meter_amount;
	}

	if (m_bSyncMidi && now_time >= midi.GetSongEndMicroseconds())
	{
		m_bSyncMidi = false;
	}

	if (m_pFreeBeatSound != NULL)
	{
		if (m_bSyncMidi && !m_bKnockOnStick)
		{
			m_pFreeBeatSound->Set(meter_amount, m_mMeterLength, meter_id, position);
		}
		else
		{
			m_pFreeBeatSound->Set(meter_amount, m_mMeterLength);
		}
	}
	else
	{
		m_pFreeBeatSound = new SimpleBeat();
		m_pFreeBeatSound->Init(meter_amount, m_mMeterLength, 0);
	}


	if (m_pSyncBeatSound != NULL)
	{
		m_pSyncBeatSound->Set(meter_amount, m_mMeterLength, meter_id, position);
	}
	else
	{
		m_pSyncBeatSound = new SimpleBeat();
		m_pSyncBeatSound->Init(meter_amount, m_mMeterLength, 0);
	}

	midi.GetRealTimeMeter(meter_amount, meter_unit, midi.GetSongPositionInMicroseconds());
	m_mMeterLength = 4 * midi.GetSongRunningTempoMicroseconds(midi.GetSongPositionInMicroseconds()) / meter_unit;

	now_time = midi.GetSongPositionInMicroseconds();
	midi.GetSongBarIDAndMeterID(bar_id, meter_id, now_time);
	position = now_time - midi.GetBarForMeterStartMicroseconds(bar_id, meter_id);
	if (position > 0)
	{
		meter_id++;
		meter_id %= meter_amount;
	}


	if (m_pFreeBeatFrames != NULL)
	{
		if (m_bSyncMidi && !m_bKnockOnStick)
		{
			m_pFreeBeatFrames->Set(meter_amount, m_mMeterLength, meter_id, position);
		}
		else
		{
			m_pFreeBeatFrames->Set(meter_amount, m_mMeterLength);
		}
	}
	else
	{
		m_pFreeBeatFrames = new SimpleBeat();
		m_pFreeBeatFrames->Init(meter_amount, m_mMeterLength, m_mDefer);
	}


	if (m_pSyncBeatFrames != NULL)
	{
		m_pSyncBeatFrames->Set(meter_amount, m_mMeterLength, meter_id, position);
	}
	else
	{
		m_pSyncBeatFrames = new SimpleBeat();
		m_pSyncBeatFrames->Init(meter_amount, m_mMeterLength, 0);
	}
}

void Metronome::UpdateMeter(unsigned int &meter_amount, unsigned int &meter_unit, microseconds_t tempo)
{
	m_mMeterLength = 4 * tempo / meter_unit;
	m_mBarLength = 4 * m_mMeterLength * meter_amount / meter_unit;


	if (m_pFreeBeatSound != NULL)
	{
		m_pFreeBeatSound->Set(meter_amount, m_mMeterLength);
	}
	else
	{
		m_pFreeBeatSound = new SimpleBeat();
		m_pFreeBeatSound->Init(meter_amount, m_mMeterLength, 0);
	}


	if (m_pFreeBeatFrames != NULL)
	{
		m_pFreeBeatFrames->Set(meter_amount, m_mMeterLength);
	}
	else
	{
		m_pFreeBeatFrames = new SimpleBeat();
		m_pFreeBeatFrames->Init(meter_amount, m_mMeterLength, m_mDefer);
	}
}

MidiEventList Metronome::StrongBeatEvents(void)
{
	MidiEventList evs;
	MidiEventSimple mes;

	mes.status = 0x89;
	mes.byte1 = 0x38;
	mes.byte2 = 0x7F;
	evs.push_back(MidiEvent::Build(mes));

	mes.status = 0x89;
	mes.byte1 = 0x51;
	mes.byte2 = 0x7F;
	evs.push_back(MidiEvent::Build(mes));

	mes.status = 0x99;
	mes.byte1 = 0x51;
	mes.byte2 = 0x7F;
	evs.push_back(MidiEvent::Build(mes));

	return evs;
}

MidiEventList Metronome::WeakBeatEvents(void)
{
	MidiEventList evs;
	MidiEventSimple mes;

	mes.status = 0x89;
	mes.byte1 = 0x32;
	mes.byte2 = 0x7F;
	evs.push_back(MidiEvent::Build(mes));

	mes.status = 0x99;
	mes.byte1 = 0x32;
	mes.byte2 = 0x7F;
	evs.push_back(MidiEvent::Build(mes));

	return evs;
}

MidiEventList Metronome::PrepareBeatEvents(void)
{
	MidiEventList evs;
	MidiEventSimple mes;

	mes.status = 0x89;
	mes.byte1 = 0x25;
	mes.byte2 = 0x7F;
	evs.push_back(MidiEvent::Build(mes));

	mes.status = 0x99;
	mes.byte1 = 0x25;
	mes.byte2 = 0x7F;
	evs.push_back(MidiEvent::Build(mes));

	return evs;
}

double Metronome::GetUpcastSpace()
{
	microseconds_t position;
	if (m_bSyncMidi && !m_bKnockOnStick)
	{
		position = m_pSyncBeatFrames->GetPosition();
	}
	else
	{
		position = m_pFreeBeatFrames->GetPosition();
	}


	microseconds_t mMeterPosition = ((position > 0) ? position : 0) % m_mMeterLength;

	double acceleration = 60.0 / static_cast<long double>((m_mMeterLength / 2) * (m_mMeterLength / 2));

	double dVelocity = acceleration * static_cast<long double>(m_mMeterLength) * 0.5;

	double dUpcastSpace = static_cast<double>(mMeterPosition * mMeterPosition) * acceleration * 0.5 - dVelocity * static_cast<double>(mMeterPosition);

	return dUpcastSpace;
}

double Metronome::GetMetronomeValue()
{
	microseconds_t position;
	int meter_id;
	if (m_bSyncMidi && !m_bKnockOnStick)
	{
		position = m_pSyncBeatFrames->GetPosition();
		meter_id = m_pSyncBeatFrames->GetMeterID();
	}
	else
	{
		position = m_pFreeBeatFrames->GetPosition();
		meter_id = m_pFreeBeatFrames->GetMeterID();
	}
	//CCLOG("MeterID: %d", meter_id);

	if (m_iMeterID != meter_id)
	{
		m_bPlusMinus = !m_bPlusMinus;
		m_iMeterID = meter_id;
	}

	microseconds_t mMeterPosition = ((position > 0) ? position : 0) % m_mMeterLength;
	microseconds_t mHalfMeter = m_mMeterLength / 2;

	if (!m_bPlusMinus)
	{
		return static_cast<double>(mHalfMeter - mMeterPosition) / static_cast<double>(mHalfMeter);
	}
	else
	{
		return -static_cast<double>(mHalfMeter - mMeterPosition) / static_cast<double>(mHalfMeter);
	}
}



SimpleBeat::SimpleBeat(void) : m_bInit(true)
{}

SimpleBeat::~SimpleBeat(void)
{}

void SimpleBeat::Init(int meter_amount, microseconds_t meter_microseconds, microseconds_t defer_microseconds /* = 0 */)
{
	m_mtPosition = -defer_microseconds;
	m_mtMeterLength = meter_microseconds;
	m_mtDefer = defer_microseconds;

	m_iMeterID = 0;
	m_iMeterAmount = meter_amount;

	m_bfirst = true;
	m_bInit = false;
}

BeatStatus SimpleBeat::Update(microseconds_t &delta_microseconds)
{
	if (m_bInit)
	{
		return RestBeat;
	}

	if (delta_microseconds <= 0)
	{
		return RestBeat;
	}

	m_mtPosition += delta_microseconds;

	if (m_mtPosition >= m_mtMeterLength || m_bfirst)
	{
		m_bfirst = false;
		m_mtPosition %= m_mtMeterLength;

		if (m_iMeterID++ == 0)
		{
			m_iMeterID = m_iMeterID % m_iMeterAmount;
			return StrongBeat;
		}
		else
		{
			m_iMeterID = m_iMeterID % m_iMeterAmount;
			return WeakBeat;
		}
	}

	return RestBeat;
}

void SimpleBeat::Reset(void)
{
	m_mtPosition = -m_mtDefer;
	m_iMeterID = 0;
	m_bfirst = true;
}

void SimpleBeat::Set(int meter_amount, microseconds_t meter_microseconds)
{
	m_mtMeterLength = meter_microseconds;
	m_iMeterAmount = meter_amount;
}

void SimpleBeat::Set(int meter_amount, microseconds_t meter_microseconds, int meter_id, microseconds_t position)
{
	m_mtMeterLength = meter_microseconds;
	m_iMeterAmount = meter_amount;
	m_iMeterID = meter_id;
	m_mtPosition = position;
}