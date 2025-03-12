Midi* m_midi;
Metronome* m_tempo;

void playMidi() {
    std::this_thread::sleep_for(std::chrono::microseconds(1));

    if (m_midi->GetSongEndMicroseconds()<= m_midi->GetSongPositionInMicroseconds())
    {
        return;
    }

    microseconds_t mtDelta = 1000000;
    auto midiEvents = m_midi->Update(mtDelta,false);
    const size_t stLength = midiEvents.size();
 for (size_t i = 0; i < stLength; ++i)
 {
  MidiEvent &ev = midiEvents[i].second;
  std::string trackName = ev.getTrackName();


  MidiEventSimple simple;
  if (ev.GetSimpleEvent(&simple) == false) {

   continue;
  }
        //自己处理后续逻辑

 }
}

int main() {
    std::string midiPath = "/Volumes/cai/8-02.mid";
    m_midi = new Midi(Midi::ReadFromFile(midiPath));

    //获取小节时间
    auto bar_time = m_midi->GetBarStartMicroseconds(1) - m_midi->GetBarStartMicroseconds(0);
    //获取midi速度
    auto baseTicks = m_midi->GetSongInitTicks();
    unsigned int m_meterSum; //节拍分子
    unsigned int m_meterUnit;//节拍分母
    m_midi->GetRealTimeMeter(m_meterSum, m_meterUnit);

    int delayTime = 0;
     //midi设置延迟
    m_midi->Reset(0, 0, delayTime);

    //节拍器
    m_tempo = new Metronome();
    m_tempo->Init(*m_midi, delayTime);

    //获取音轨
    auto tracks = m_midi->Tracks();
    //从音轨上获取音符列表
    auto m_notes = m_midi->FindNotes(tracks[0].GetTrackName());
    //设置播放循环
    m_midi->SetLoop(0, m_midi->GetSongEndMicroseconds());

    // 异步执行任务
    std::future<void> result = std::async(std::launch::async, playMidi);

    //简单midi事件
    MidiEvent midiEvent = MidiEvent::Build(MidiEventSimple(0x80, 55, 100));

    return 0;
}
