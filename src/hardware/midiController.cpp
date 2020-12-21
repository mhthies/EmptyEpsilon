
#include "midiController.h"
#include "logging.h"
#include "gameGlobalInfo.h"
#include "playerInfo.h"

#include <cmath>
#include <algorithm>

MidiController::MidiController()
{
    midiout = new RtMidiOut();
    midiin = new RtMidiIn();

    // Find X-Touch controller output port
    unsigned int nPorts = midiout->getPortCount();
    int portNumber = -1;
    for (unsigned int i=0; i<nPorts; i++) {
        std::string portName;
        try {
            portName = midiout->getPortName(i);
        } catch (RtMidiError &error) {
            LOG(WARNING) << "midi: Could not get port name: " << error.getMessage();
            break;
        }
        if (portName.find("X-TOUCH") != std::string::npos) {
            portNumber = i;
            break;
        }
    }
    if (portNumber != -1) {
        LOG(INFO) << "Opening port for X-Touch controller output port";
        midiout->openPort(portNumber);

        sendInitialization();
    } else {
        LOG(INFO) << "Could not find X-Touch MIDI controller output port";
    }

    // Find X-Touch controller input port
    nPorts = midiin->getPortCount();
    portNumber = -1;
    for (unsigned int i=0; i<nPorts; i++) {
        std::string portName;
        try {
            portName = midiin->getPortName(i);
        } catch (RtMidiError &error) {
            LOG(WARNING) << "midi: Could not get port name: " << error.getMessage();
            break;
        }
        if (portName.find("X-TOUCH") != std::string::npos) {
            portNumber = i;
            break;
        }
    }
    if (portNumber != -1) {
        LOG(INFO) << "Opening port for X-Touch controller input port";
        midiin->openPort(portNumber);
        midiin->ignoreTypes();
    } else {
        LOG(INFO) << "Could not find X-Touch MIDI controller input port";
    }
}

MidiController::~MidiController()
{
    delete midiout;
    delete midiin;
}


void MidiController::update(float delta)
{
    if (!bool(my_spaceship)) {
        return;
    }

    // Update system map
    size_t j = 0;
    for (int i=0; i < SYS_COUNT; ++i) {
        if (j == systemMap.size())
            break;
        if (my_spaceship->hasSystem(static_cast<ESystem>(i)))
            systemMap[j++] = static_cast<ESystem>(i);
    }

    if (midiin->isPortOpen()) {
        checkMidiIn();
    }

    if (midiout->isPortOpen()) {
        // Limit outgoing MIDI update rate to 300ms
        timeSinceUpdate += delta;
        if (timeSinceUpdate >= 0.3) {
            timeSinceUpdate = 0.0;
            sendMidiOut();
        }
    }
}

void MidiController::sendInitialization() {
    // Set encoders to "fan" mode
    std::vector<unsigned char> message(3);
    for (uint i = 10; i <= 25; ++i) {
        message[0] = 0b10110001;  // Control change , MIDI channel 1
        message[1] = i;  // control channel i
        message[2] = 2; // value 2
        midiout->sendMessage(&message);
    }
}

void MidiController::checkMidiIn() {
    P<PlayerSpaceship> ship = my_spaceship;

    std::array<float, SYS_COUNT> newPowerRequests;
    std::array<float, SYS_COUNT> newCoolantRequests;
    for (size_t i=0; i < systemMap.size(); ++i) {
        newPowerRequests[i] = std::numeric_limits<double>::quiet_NaN();
        newCoolantRequests[i] = std::numeric_limits<double>::quiet_NaN();
    }

    std::vector<unsigned char> message;
    while (true) {
        midiin->getMessage(&message);
        if (message.empty())
            break;
        MidiInputUpdate update = interpretReceivedMessage(message);
        if (update.type == InputUpdateType::POWER) {
            newPowerRequests[update.index] = update.value;
        } else if (update.type == InputUpdateType::COOLANT) {
            newCoolantRequests[update.index] = update.value;
        }
    }

    for (size_t i=0; i < systemMap.size(); ++i) {
        if (!isnan(newPowerRequests[i])) {
            ship->commandSetSystemPowerRequest(systemMap[i], newPowerRequests[i]);
        }
        if (!isnan(newCoolantRequests[i])) {
            ship->commandSetSystemCoolantRequest(systemMap[i], newCoolantRequests[i]);
        }
    }
}

void MidiController::sendMidiOut() {
    P<PlayerSpaceship> ship = my_spaceship;
    std::vector<unsigned char> message(3);

    LOG(DEBUG) << "Sending MIDI outputs";
    for (uint i = 0; i < systemMap.size(); ++i) {
        // Set faders to power_request
        message[0] = 0b10110000;  // Control change , MIDI channel 0
        message[1] = i+1;  // control channel i+1
        message[2] = round(ship->systems[systemMap[i]].power_request / 3.0 * 127);  // value (0..127)
        midiout->sendMessage(&message);

        // Set encoders to coolant_request
        message[0] = 0b10110000;  // Control change , MIDI channel 0
        message[1] = 10+i;  // control channel 10+i
        message[2] = round(ship->systems[systemMap[i]].coolant_request / 10.0 * 127);  // value (0..127)
        midiout->sendMessage(&message);

        // Set button leds according to heat
        std::array<float, 4> heatThresholds = {0.25, 0.5, 0.75, 0.9};
        for (size_t j = 0; j < 3; ++j) {
            bool ledOn = (ship->systems[systemMap[i]].heat_level >= heatThresholds[j]);
            bool blinking = (ship->systems[systemMap[i]].heat_level >= heatThresholds[3]);
            message[0] = 0b10010001;  // Note on, MIDI channel 2
            message[1] = (2-j)*8+i;  // Note no.
            message[2] = ledOn ? (blinking ? 3 : 2) : 0;
            midiout->sendMessage(&message);
        }

        // Set right encoder rings according to damage
        message[0] = 0b10110000;  // Control change , MIDI channel 0
        message[1] = 18+i;  // control channel
        message[2] = std::max(0, static_cast<int>(round((1.0 - ship->systems[systemMap[i]].health) * 127)));  // value
        midiout->sendMessage(&message);
    }
}

MidiInputUpdate MidiController::interpretReceivedMessage(const std::vector<unsigned char> &message) {
    std::array<float, 3> buttonPowerLevels = {0.30, 1.0, 1.5};

    switch (message[0] & (unsigned char)0xf0) {
        case 0b10010000:  // Note on
            if (message[1] >= 16 && message[1] <= 39) {  // Note is one from the button field
                unsigned int buttonIndex = message[1] - 16;
                return {InputUpdateType::POWER, buttonPowerLevels[2 - buttonIndex/8], buttonIndex % 8};
            }
            break;

        case 0b10110000:  // Control change
            if (message[1] >= 1 && message[1] <= 8) {  // Control channel is one of the faders
                return {InputUpdateType::POWER,
                        static_cast<float>(message[2] * 3.0 / 127),
                        static_cast<size_t>(message[1] - 1)};
            } else if (message[1] >= 10 && message[1] <= 17) {  // Control channel is one of the faders
                return {InputUpdateType::COOLANT,
                        static_cast<float>(message[2] * 10.0 / 127),
                        static_cast<size_t>(message[1] - 10)};
            }
            break;
    }

    return {InputUpdateType::NONE, 0.0, 0};
}
