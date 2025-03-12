cmake_minimum_required(VERSION 3.30)
project(midi_read)

set(CMAKE_CXX_STANDARD 11)

set(CPP_SOURCE)
set(CPP_HEADER)

list(APPEND CPP_HEADER
        midi/Metronome.h
        midi/Midi.h
        midi/MidiEvent.h
        midi/MidiTrack.h
        midi/MidiTypes.h
        midi/MidiUtil.h
        midi/Note.h
)

list(APPEND CPP_SOURCE
        midi/Metronome.cpp
        midi/Midi.cpp
        midi/MidiEvent.cpp
        midi/MidiTrack.cpp
        midi/MidiUtil.cpp
        main.cpp
)

set(all_code_files
        ${CPP_HEADER}
        ${CPP_SOURCE}
)

#add_library(${APP_NAME} SHARED ${all_code_files})

add_executable(midi_read ${all_code_files})
