//
// Created by GST49 on 2023/10/22.
//

#include "midi_file.h"

#include <utility>

#define REST_PITCH 128

namespace Midi {
    class MidiEvent {
    public:
        uint32_t deltaTime = 0;
        uint8_t flag = 0;
        std::vector<uint8_t> data;
    };

    class MidiNotes {
    public:
        explicit MidiNotes(uint8_t pitch, uint32_t duration) :
                pitch(pitch), duration(duration) {}
        uint8_t pitch;
        uint32_t duration;
    };

    class MidiTrackReader {
    public:
        explicit MidiTrackReader() : finished(false), crotchets_real_time(0), time_counter(0),
        track_duration(0), notes_size(0) {
            notes_flag.resize(128, {-1, 0});
        }

        std::string track_name;
        uint32_t crotchets_real_time;
        uint32_t track_duration;
        bool finished;
        std::vector<std::vector<MidiNotes>> tracks;
        std::priority_queue<int, std::vector<int>, std::greater<>> free_track;

        void endNotes(uint8_t pitch) {
            if(notes_flag[pitch].first == -1) {
                return;
            }
            auto [track_id, start_time] = notes_flag[pitch];
            if(tracks_last_time[track_id] < start_time) {
                tracks[track_id].emplace_back(REST_PITCH, start_time - tracks_last_time[track_id]);
            }
            tracks[track_id].emplace_back(pitch, time_counter - start_time);
            tracks_last_time[track_id] = time_counter;
            notes_flag[pitch] = {-1, 0};
            free_track.push(track_id);
        }

        void startNotes(uint8_t pitch) {
            if(notes_flag[pitch].first == -1) {
                if(free_track.empty()) {
                    tracks.emplace_back();
                    tracks_last_time.emplace_back(0);
                    free_track.push((int)tracks.size() - 1);
                }
                notes_flag[pitch] = {free_track.top(), time_counter};
                free_track.pop();
            }
        }

        void endTrack() {
            track_duration = time_counter;
            notes_flag.clear();
            tracks_last_time.clear();
            finished = true;
        }

        friend void operator<< (MidiTrackReader& track, MidiEvent& event) {
            if(track.finished) {
                return;
            }
            track.time_counter += event.deltaTime;
            if(event.flag == 0xFF) {
                if(event.data[0] == 0x03) {
                    track.track_name = std::string(event.data.begin() + 2, event.data.end());
                }
                else if(event.data[0] == 0x2F) {
                    track.endTrack();
                }
                else if(event.data[0] == 0x51){
                    uint32_t real_time_t = 0;
                    for(uint32_t i = 2; i < event.data.size(); i++) {
                        real_time_t = (real_time_t << 8) | event.data[i];
                    }
                    track.crotchets_real_time = real_time_t;
                }
            }
            else if((event.flag & 0xf0) == 0x90) {
                if(event.data[1] == 0x00) {
                    track.endNotes(event.data[0]);
                }
                else {
                    track.startNotes(event.data[0]);
                }
            }
            else if((event.flag & 0xf0) == 0x80) {
                track.endNotes(event.data[0]);
            }
        }
    private:
        uint32_t time_counter, notes_size;
        std::vector<std::pair<int, uint32_t>> notes_flag;
        std::vector<uint32_t> tracks_last_time;
    };

    class MidiFile {
    public:

        explicit MidiFile() = default;

        explicit MidiFile(std::string name) : file_name(std::move(name)) {}

        void ungetUint8(uint8_t byte) {
            midi_file.putback((char)byte);
            bytes_point--;
        }

        uint8_t getUint8() {
            uint8_t byte_t;
            midi_file.get((char&)byte_t);
            bytes_point++;
            return byte_t;
        }

        uint16_t getUint16() {
            return (getUint8() << 8) | getUint8();
        }

        uint32_t getUint32() {
            return (getUint8() << 24) | (getUint8() << 16) | (getUint8() << 8) | getUint8();
        }

        void getData(std::vector<uint8_t>& out, uint32_t cnt) {
            while(cnt--) {
                out.emplace_back(getUint8());
            }
        }

        uint32_t getDeltaTime() {
            uint32_t delta_time = 0;
            uint8_t data_t;
            do {
                data_t = getUint8();
                delta_time = (delta_time << 7) | (data_t & 0x7f);
            }
            while(data_t >> 7);
            return delta_time;
        }

        void friend operator>> (MidiFile &file, MidiEvent &midi_event) {
            midi_event.deltaTime = file.getDeltaTime();
            midi_event.flag = file.getUint8();
            midi_event.data.clear();
            if(midi_event.flag < 0x80) {
                file.ungetUint8(midi_event.flag);
                midi_event.flag = file.last_event_flag;
            }
            if(midi_event.flag == 0xFF) {
                file.getData(midi_event.data, 2);
                file.getData(midi_event.data, midi_event.data[1]);
            }
            else if(midi_event.flag >= 0xC0 && midi_event.flag <= 0xDF) {
                file.getData(midi_event.data, 1);
            }
            else if(midi_event.flag >= 0x80) {
                file.getData(midi_event.data, 2);
            }
            file.last_event_flag = midi_event.flag;
        }

        void loadHead() {
            if(getUint32() != 0x4d546864) {
                return;
            }
            getUint32();
            midi_mode = getUint16();
            track_size = getUint16();
            tick_time = getUint16();
        }

        void load() {
            midi_file.open(file_name, std::ios::in | std::ios::binary);
            if(!midi_file.is_open()) {
                return;
            }
            loadHead();
            for(int i = 0; i < track_size; i++) {
                midiTrack.emplace_back();
                *this >> midiTrack[i];
            }
        }

        void friend operator>> (MidiFile &file, MidiTrackReader &track) {
            MidiEvent event;
            if(file.getUint32() != 0x4d54726b) {
                return;
            }
            auto track_size = file.getUint32();
            while(!track.finished) {
                file >> event;
                track << event;
            }
        }

        std::string file_name;
        std::ifstream midi_file;
        std::vector<MidiTrackReader> midiTrack;
        uint16_t track_size = 0;
        uint16_t tick_time = 0;
        uint16_t midi_mode = 0;
        uint32_t bytes_point = 0;
        uint8_t last_event_flag = 0;

    private:
    };
}



