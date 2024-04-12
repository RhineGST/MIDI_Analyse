cmake_minimum_required(VERSION 3.25)
project(MIDI_Analyse)

set(CMAKE_CXX_STANDARD 17)

add_executable(MIDI_Analyse main.cpp midi_file.cpp midi_file.h)
