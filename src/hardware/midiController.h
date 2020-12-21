//
// Created by michael on 24.11.20.
//

#ifndef MIDICONTROLLER_H
#define MIDICONTROLLER_H


#include <shipTemplate.h>
#include "engine.h"
#include "RtMidi.h"

enum class InputUpdateType {
    NONE,
    POWER,
    COOLANT
};

struct MidiInputUpdate {
    InputUpdateType type;
    float value;
    size_t index;
};

class MidiController: public Updatable
{
    private:
    RtMidiOut* midiout;
    RtMidiIn* midiin;
    float timeSinceUpdate = 0.0;
    std::array<ESystem, 8> systemMap = {SYS_None,SYS_None,SYS_None,SYS_None,SYS_None,SYS_None,SYS_None,SYS_None};

    public:
    MidiController();
    ~MidiController();

    virtual void update(float delta);

    private:
    void sendInitialization();
    void checkMidiIn();
    void sendMidiOut();
    static MidiInputUpdate interpretReceivedMessage(const std::vector<unsigned char>& message);
};


#endif //MIDICONTROLLER_H
