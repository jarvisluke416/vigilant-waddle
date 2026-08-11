#include <windows.h>
#include <mmsystem.h>
#include <gdiplus.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "gdi32.lib")

// ============================================================
// SETTINGS
// ============================================================

const int SAMPLE_RATE = 44100;
const double PI = 3.14159265358979323846;

const int PLAYER_COUNT = 16;

const int GRID_COLUMNS = 4;
const int GRID_ROWS = 4;

const double MIN_TEMPO = 40.0;
const double MAX_TEMPO = 240.0;
const double TEMPO_STEP = 5.0;

const int MIN_PITCH = -12;
const int MAX_PITCH = 12;
const int PITCH_STEP = 1;

const int AUDIO_BUFFER_COUNT = 2;

// ============================================================
// COLORS
// ============================================================

const COLORREF BACKGROUND = RGB(0, 207, 74);
const COLORREF COLUMN = RGB(32, 32, 38);
const COLORREF COLUMN_BORDER = RGB(55, 55, 65);

const COLORREF TEXT = RGB(0, 255, 0);
const COLORREF BUTTON_TEXT = RGB(0, 255, 0);

const COLORREF EDIT_BACKGROUND = RGB(15, 15, 20);
const COLORREF EDIT_TEXT = RGB(0, 255, 0);

// ============================================================
// GLOBAL STATE
// ============================================================

std::atomic<bool> playing[PLAYER_COUNT];
std::atomic<bool> looping[PLAYER_COUNT];
std::atomic<bool> stopRequested[PLAYER_COUNT];

std::atomic<double> currentTempo[PLAYER_COUNT];
std::atomic<int> currentPitch[PLAYER_COUNT];

HWND playButton[PLAYER_COUNT];
HWND loopButton[PLAYER_COUNT];

HWND tempoMinusButton[PLAYER_COUNT];
HWND tempoPlusButton[PLAYER_COUNT];
HWND tempoLabel[PLAYER_COUNT];

HWND pitchMinusButton[PLAYER_COUNT];
HWND pitchPlusButton[PLAYER_COUNT];
HWND pitchLabel[PLAYER_COUNT];

HWND songEditor[PLAYER_COUNT];

HWND mainWindow = nullptr;

// ============================================================
// PLAY ALL SYNCHRONIZATION
// ============================================================

std::mutex playAllMutex;

std::condition_variable playAllCondition;

int playAllWaiting = 0;

bool playAllActive = false;

std::chrono::steady_clock::time_point playAllStartTime;

// ============================================================
// RANDOM NOISE
// ============================================================

double noise()
{
    thread_local std::mt19937 generator(
        std::random_device{}()
    );

    thread_local std::uniform_real_distribution<double>
        distribution(-1.0, 1.0);

    return distribution(generator);
}

// ============================================================
// SONG DATA
// ============================================================

struct NoteEvent
{
    double startBeat;
    double durationBeats;
    double frequency;
    std::string instrument;
};

struct DrumEvent
{
    double startBeat;
    std::string type;
};

// ============================================================
// NOTE FREQUENCY
// ============================================================

double noteFrequency(const std::string& note)
{
    if (note.empty())
        return 0.0;

    int position = 0;

    int semitone = 0;

    char letter = note[position];

    switch (letter)
    {
        case 'C':
            semitone = 0;
            break;

        case 'D':
            semitone = 2;
            break;

        case 'E':
            semitone = 4;
            break;

        case 'F':
            semitone = 5;
            break;

        case 'G':
            semitone = 7;
            break;

        case 'A':
            semitone = 9;
            break;

        case 'B':
            semitone = 11;
            break;

        default:
            return 0.0;
    }

    position++;

    if (
        position < static_cast<int>(note.size())
        &&
        note[position] == '#'
    )
    {
        semitone++;
        position++;
    }

    if (position >= static_cast<int>(note.size()))
        return 0.0;

    int octave = 0;

    try
    {
        octave =
            std::stoi(
                note.substr(position)
            );
    }
    catch (...)
    {
        return 0.0;
    }

    int midi =
        (octave + 1) * 12 +
        semitone;

    return
        440.0 *
        std::pow(
            2.0,
            (midi - 69) / 12.0
        );
}

// ============================================================
// PIANO
// ============================================================

double piano(
    double t,
    double frequency)
{
    if (t < 0.0 || t >= 3.0)
        return 0.0;

    double envelope =
        std::exp(-2.8 * t);

    double fundamental =
        std::sin(
            2.0 *
            PI *
            frequency *
            t
        );

    double harmonic2 =
        std::sin(
            2.0 *
            PI *
            frequency *
            2.0 *
            t
        )
        * 0.35;

    double harmonic3 =
        std::sin(
            2.0 *
            PI *
            frequency *
            3.0 *
            t
        )
        * 0.15;

    return
        (
            fundamental +
            harmonic2 +
            harmonic3
        )
        *
        envelope
        *
        0.35;
}

// ============================================================
// BASS
// ============================================================

double bass(
    double t,
    double frequency)
{
    if (t < 0.0 || t >= 2.0)
        return 0.0;

    double envelope =
        std::exp(-3.5 * t);

    double fundamental =
        std::sin(
            2.0 *
            PI *
            frequency *
            t
        );

    double harmonic =
        std::sin(
            2.0 *
            PI *
            frequency *
            2.0 *
            t
        )
        * 0.2;

    return
        (
            fundamental +
            harmonic
        )
        *
        envelope
        *
        0.5;
}

// ============================================================
// GUITAR
// ============================================================

double guitar(
    double t,
    double frequency)
{
    if (t < 0.0 || t >= 2.5)
        return 0.0;

    double envelope =
        std::exp(-2.0 * t);

    double wave =
        std::sin(
            2.0 *
            PI *
            frequency *
            t
        );

    double harmonic =
        std::sin(
            2.0 *
            PI *
            frequency *
            2.0 *
            t
        )
        * 0.3;

    return
        (
            wave +
            harmonic
        )
        *
        envelope
        *
        0.32;
}

// ============================================================
// SYNTH
// ============================================================

double synth(
    double t,
    double frequency)
{
    if (t < 0.0 || t >= 3.0)
        return 0.0;

    double attack =
        std::min(
            1.0,
            t / 0.02
        );

    double envelope =
        std::exp(-1.2 * t) *
        attack;

    double wave =
        std::sin(
            2.0 *
            PI *
            frequency *
            t
        );

    double wave2 =
        std::sin(
            2.0 *
            PI *
            frequency *
            0.5 *
            t
        );

    return
        (
            wave * 0.75 +
            wave2 * 0.25
        )
        *
        envelope
        *
        0.3;
}

// ============================================================
// INSTRUMENT SELECTOR
// ============================================================

double makeInstrument(
    const std::string& instrument,
    double t,
    double frequency)
{
    if (instrument == "PIANO")
        return piano(t, frequency);

    if (instrument == "BASS")
        return bass(t, frequency);

    if (instrument == "GUITAR")
        return guitar(t, frequency);

    if (instrument == "SYNTH")
        return synth(t, frequency);

    // Default
    return piano(t, frequency);
}

// ============================================================
// DRUM SOUNDS
// ============================================================

double kick(double t)
{
    if (t < 0.0 || t >= 0.7)
        return 0.0;

    double frequency =
        150.0 *
        std::exp(-12.0 * t)
        +
        45.0;

    double envelope =
        std::exp(-7.0 * t);

    return
        std::sin(
            2.0 *
            PI *
            frequency *
            t
        )
        *
        envelope;
}

double snare(double t)
{
    if (t < 0.0 || t >= 0.35)
        return 0.0;

    double envelope =
        std::exp(-15.0 * t);

    double noisePart =
        noise() *
        envelope;

    double body =
        std::sin(
            2.0 *
            PI *
            180.0 *
            t
        )
        *
        std::exp(-20.0 * t);

    return
        noisePart * 0.8 +
        body * 0.2;
}

double closedHiHat(double t)
{
    if (t < 0.0 || t >= 0.12)
        return 0.0;

    return
        noise()
        *
        std::exp(-45.0 * t)
        *
        0.7;
}

double openHiHat(double t)
{
    if (t < 0.0 || t >= 0.8)
        return 0.0;

    return
        noise()
        *
        std::exp(-5.0 * t)
        *
        0.6;
}

double clap(double t)
{
    if (t < 0.0 || t >= 0.3)
        return 0.0;

    double burst1 =
        std::exp(
            -80.0 *
            std::abs(t)
        );

    double burst2 =
        std::exp(
            -60.0 *
            std::abs(
                t - 0.025
            )
        );

    double burst3 =
        std::exp(
            -50.0 *
            std::abs(
                t - 0.050
            )
        );

    double envelope =
        std::exp(-14.0 * t);

    return
        noise()
        *
        (
            burst1 +
            burst2 +
            burst3
        )
        *
        envelope;
}

double tom(
    double t,
    double frequency)
{
    if (t < 0.0 || t >= 0.6)
        return 0.0;

    double pitch =
        frequency *
        std::exp(-3.0 * t)
        +
        frequency * 0.4;

    double envelope =
        std::exp(-7.0 * t);

    return
        std::sin(
            2.0 *
            PI *
            pitch *
            t
        )
        *
        envelope;
}

double crash(double t)
{
    if (t < 0.0 || t >= 2.0)
        return 0.0;

    return
        noise()
        *
        std::exp(-2.5 * t)
        *
        0.65;
}

double ride(double t)
{
    if (t < 0.0 || t >= 1.5)
        return 0.0;

    double envelope =
        std::exp(-2.0 * t);

    double metallic =
        noise();

    double tone =
        std::sin(
            2.0 *
            PI *
            3500.0 *
            t
        );

    return
        (
            metallic * 0.5 +
            tone * 0.5
        )
        *
        envelope
        *
        0.4;
}

double rimshot(double t)
{
    if (t < 0.0 || t >= 0.15)
        return 0.0;

    double envelope =
        std::exp(-40.0 * t);

    return
        std::sin(
            2.0 *
            PI *
            1200.0 *
            t
        )
        *
        envelope
        *
        0.8
        +
        noise()
        *
        envelope
        *
        0.3;
}

double cowbell(double t)
{
    if (t < 0.0 || t >= 0.3)
        return 0.0;

    double envelope =
        std::exp(-15.0 * t);

    double tone1 =
        std::sin(
            2.0 *
            PI *
            540.0 *
            t
        );

    double tone2 =
        std::sin(
            2.0 *
            PI *
            800.0 *
            t
        );

    return
        (
            tone1 +
            tone2
        )
        *
        envelope
        *
        0.4;
}

double shaker(double t)
{
    if (t < 0.0 || t >= 0.25)
        return 0.0;

    return
        noise()
        *
        std::exp(-18.0 * t)
        *
        0.5;
}

double tambourine(double t)
{
    if (t < 0.0 || t >= 0.7)
        return 0.0;

    double envelope =
        std::exp(-6.0 * t);

    double metal =
        noise();

    double ring =
        std::sin(
            2.0 *
            PI *
            4000.0 *
            t
        );

    return
        (
            metal * 0.7 +
            ring * 0.3
        )
        *
        envelope
        *
        0.5;
}

// ============================================================
// DRUM SELECTOR
// ============================================================

double makeDrum(
    const std::string& type,
    double t)
{
    if (
        type == "KICK" ||
        type == "BASS_DRUM"
    )
        return kick(t);

    if (type == "SNARE")
        return snare(t);

    if (type == "HIHAT" ||
        type == "CLOSED_HIHAT")
        return closedHiHat(t);

    if (
        type == "OPEN_HIHAT" ||
        type == "OPEN_HAT"
    )
        return openHiHat(t);

    if (type == "CLAP")
        return clap(t);

    if (
        type == "LOW_TOM" ||
        type == "TOM_LOW"
    )
        return tom(t, 110.0);

    if (
        type == "MID_TOM" ||
        type == "TOM_MID"
    )
        return tom(t, 180.0);

    if (
        type == "HIGH_TOM" ||
        type == "TOM_HIGH"
    )
        return tom(t, 280.0);

    if (type == "CRASH")
        return crash(t);

    if (type == "RIDE")
        return ride(t);

    if (type == "RIMSHOT")
        return rimshot(t);

    if (type == "COWBELL")
        return cowbell(t);

    if (type == "SHAKER")
        return shaker(t);

    if (type == "TAMBOURINE")
        return tambourine(t);

    return 0.0;
}

// ============================================================
// LOAD SONG FROM TEXT INPUT
// ============================================================

bool LoadSongFromText(
    const std::string& text,
    std::vector<NoteEvent>& notes,
    std::vector<DrumEvent>& drums,
    double& tempo,
    double& loopLengthBeats)
{
    notes.clear();
    drums.clear();

    tempo = 120.0;
    loopLengthBeats = 4.0;

    std::stringstream songFile(text);

    std::string command;

    while (songFile >> command)
    {
        // ----------------------------------------------------
        // TEMPO
        // ----------------------------------------------------

        if (command == "TEMPO")
        {
            songFile >> tempo;

            tempo =
                std::max(
                    MIN_TEMPO,
                    std::min(
                        MAX_TEMPO,
                        tempo
                    )
                );
        }

        // ----------------------------------------------------
        // LENGTH
        // ----------------------------------------------------

        else if (command == "LENGTH")
        {
            songFile >> loopLengthBeats;

            if (loopLengthBeats <= 0.0)
                loopLengthBeats = 4.0;
        }

        // ----------------------------------------------------
        // NOTE
        //
        // NOTE C4 0 1
        // ----------------------------------------------------

        else if (command == "NOTE")
{
    std::string instrument;
    std::string noteName;
    double startBeat;
    double durationBeats;

    songFile
        >> instrument
        >> noteName
        >> startBeat
        >> durationBeats;

    double frequency =
        noteFrequency(noteName);

    if (frequency > 0.0)
    {
        notes.push_back(
            {
                startBeat,
                durationBeats,
                frequency,
                instrument
            }
        );
    }
}

                

        // ----------------------------------------------------
        // PIANO
        //
        // PIANO C4 0 1
        // ----------------------------------------------------

        else if (command == "PIANO")
        {
            std::string noteName;

            double startBeat;
            double durationBeats;

            songFile
                >>
                noteName
                >>
                startBeat
                >>
                durationBeats;

            double frequency =
                noteFrequency(noteName);

            if (frequency > 0.0)
            {
                notes.push_back(
                    {
                        startBeat,
                        durationBeats,
                        frequency,
                        "PIANO"
                    }
                );
            }
        }

        // ----------------------------------------------------
        // BASS
        //
        // BASS C3 0 1
        // ----------------------------------------------------

        else if (command == "BASS")
        {
            std::string noteName;

            double startBeat;
            double durationBeats;

            songFile
                >>
                noteName
                >>
                startBeat
                >>
                durationBeats;

            double frequency =
                noteFrequency(noteName);

            if (frequency > 0.0)
            {
                notes.push_back(
                    {
                        startBeat,
                        durationBeats,
                        frequency,
                        "BASS"
                    }
                );
            }
        }

        // ----------------------------------------------------
        // GUITAR
        //
        // GUITAR E4 1 1
        // ----------------------------------------------------

        else if (command == "GUITAR")
        {
            std::string noteName;

            double startBeat;
            double durationBeats;

            songFile
                >>
                noteName
                >>
                startBeat
                >>
                durationBeats;

            double frequency =
                noteFrequency(noteName);

            if (frequency > 0.0)
            {
                notes.push_back(
                    {
                        startBeat,
                        durationBeats,
                        frequency,
                        "GUITAR"
                    }
                );
            }
        }

        // ----------------------------------------------------
        // SYNTH
        //
        // SYNTH G4 2 1
        // ----------------------------------------------------

        else if (command == "SYNTH")
        {
            std::string noteName;

            double startBeat;
            double durationBeats;

            songFile
                >>
                noteName
                >>
                startBeat
                >>
                durationBeats;

            double frequency =
                noteFrequency(noteName);

            if (frequency > 0.0)
            {
                notes.push_back(
                    {
                        startBeat,
                        durationBeats,
                        frequency,
                        "SYNTH"
                    }
                );
            }
        }

        // ----------------------------------------------------
        // DRUM
        //
        // DRUM KICK 0
        // ----------------------------------------------------

        else if (command == "DRUM")
        {
            std::string drumType;

            double startBeat;

            songFile
                >>
                drumType
                >>
                startBeat;

            drums.push_back(
                {
                    startBeat,
                    drumType
                }
            );
        }
    }

    return true;
}

// ============================================================
// GENERATE AUDIO
// ============================================================

bool GenerateAudio(
    const std::string& text,
    double tempoOverride,
    int pitchSemitones,
    std::vector<short>& samples)
{
    std::vector<NoteEvent> notes;

    std::vector<DrumEvent> drums;

    double tempo;
    double loopLengthBeats;

    if (!LoadSongFromText(
        text,
        notes,
        drums,
        tempo,
        loopLengthBeats))
    {
        return false;
    }

    tempo =
        std::max(
            MIN_TEMPO,
            std::min(
                MAX_TEMPO,
                tempoOverride
            )
        );

    int pitch =
        std::max(
            MIN_PITCH,
            std::min(
                MAX_PITCH,
                pitchSemitones
            )
        );

    double pitchMultiplier =
        std::pow(
            2.0,
            static_cast<double>(pitch) / 12.0
        );

    double secondsPerBeat =
        60.0 / tempo;

    double loopDuration =
        loopLengthBeats *
        secondsPerBeat;

    int totalSamples =
        static_cast<int>(
            std::round(
                loopDuration *
                SAMPLE_RATE
            )
        );

    if (totalSamples <= 0)
        return false;

    std::vector<double> audio(
        totalSamples,
        0.0
    );

    // ========================================================
    // MELODY / INSTRUMENTS
    // ========================================================

    for (const NoteEvent& note : notes)
    {
        double startSeconds =
            note.startBeat *
            secondsPerBeat;

        double durationSeconds =
            note.durationBeats *
            secondsPerBeat;

        int startSample =
            static_cast<int>(
                std::round(
                    startSeconds *
                    SAMPLE_RATE
                )
            );

        int noteSamples =
            static_cast<int>(
                std::round(
                    durationSeconds *
                    SAMPLE_RATE
                )
            );

        double shiftedFrequency =
            note.frequency *
            pitchMultiplier;

        for (
            int i = 0;
            i < noteSamples;
            ++i)
        {
            int index =
                startSample +
                i;

            if (index < 0)
                continue;

            if (index >= totalSamples)
                break;

            double time =
                static_cast<double>(i)
                /
                SAMPLE_RATE;

            double envelope = 1.0;

            if (time < 0.01)
            {
                envelope =
                    time / 0.01;
            }

            double remaining =
                durationSeconds -
                time;

            if (remaining < 0.05)
            {
                envelope =
                    std::min(
                        envelope,
                        std::max(
                            0.0,
                            remaining / 0.05
                        )
                    );
            }

            double wave =
                makeInstrument(
                    note.instrument,
                    time,
                    shiftedFrequency
                );

            audio[index] +=
                wave *
                envelope;
        }
    }

    // ========================================================
    // DRUMS
    //
    // DRUMS ARE NOT PITCH SHIFTED.
    // ========================================================

    for (const DrumEvent& drum : drums)
    {
        double startSeconds =
            drum.startBeat *
            secondsPerBeat;

        int startSample =
            static_cast<int>(
                std::round(
                    startSeconds *
                    SAMPLE_RATE
                )
            );

        int drumSamples =
            static_cast<int>(
                2.0 *
                SAMPLE_RATE
            );

        for (
            int i = 0;
            i < drumSamples;
            ++i)
        {
            int index =
                startSample +
                i;

            if (index < 0)
                continue;

            if (index >= totalSamples)
                break;

            double time =
                static_cast<double>(i)
                /
                SAMPLE_RATE;

            audio[index] +=
                makeDrum(
                    drum.type,
                    time
                )
                *
                0.5;
        }
    }

    // ========================================================
    // CONVERT TO 16-BIT PCM
    // ========================================================

    samples.resize(
        totalSamples
    );

    for (
        int i = 0;
        i < totalSamples;
        ++i)
    {
        double value =
            std::max(
                -1.0,
                std::min(
                    1.0,
                    audio[i]
                )
            );

        samples[i] =
            static_cast<short>(
                value *
                32767.0
            );
    }

    return true;
}

// ============================================================
// BUTTON IDs
// ============================================================

#define ID_PLAY_ALL 9000

#define ID_PLAY_BASE         1000
#define ID_LOOP_BASE         1100
#define ID_TEMPO_MINUS_BASE  1200
#define ID_TEMPO_PLUS_BASE   1300
#define ID_PITCH_MINUS_BASE  1400
#define ID_PITCH_PLUS_BASE   1500

// ============================================================
// EDITOR ID
// ============================================================

#define ID_EDITOR_BASE 2000

// ============================================================
// DISPLAY HELPERS
// ============================================================

void UpdateTempoDisplay(
    int playerIndex)
{
    if (
        playerIndex < 0 ||
        playerIndex >= PLAYER_COUNT)
        return;

    if (!tempoLabel[playerIndex])
        return;

    int tempo =
        static_cast<int>(
            std::round(
                currentTempo[playerIndex].load()
            )
        );

    char text[64];

    wsprintfA(
        text,
        "%d",
        tempo
    );

    SetWindowTextA(
        tempoLabel[playerIndex],
        text
    );

    InvalidateRect(
        tempoLabel[playerIndex],
        nullptr,
        TRUE
    );
}

void UpdatePitchDisplay(
    int playerIndex)
{
    if (
        playerIndex < 0 ||
        playerIndex >= PLAYER_COUNT)
        return;

    if (!pitchLabel[playerIndex])
        return;

    int pitch =
        currentPitch[playerIndex].load();

    char text[64];

    if (pitch > 0)
    {
        wsprintfA(
            text,
            "+%d",
            pitch
        );
    }
    else
    {
        wsprintfA(
            text,
            "%d",
            pitch
        );
    }

    SetWindowTextA(
        pitchLabel[playerIndex],
        text
    );

    InvalidateRect(
        pitchLabel[playerIndex],
        nullptr,
        TRUE
    );
}

// ============================================================
// GET TEXT FROM EDITOR
// ============================================================

std::string GetEditorText(
    int playerIndex)
{
    if (
        playerIndex < 0 ||
        playerIndex >= PLAYER_COUNT)
        return "";

    HWND editor =
        songEditor[playerIndex];

    if (!editor)
        return "";

    int length =
        GetWindowTextLengthA(
            editor
        );

    if (length <= 0)
        return "";

    std::string text(
        length + 1,
        '\0'
    );

    GetWindowTextA(
        editor,
        &text[0],
        length + 1
    );

    text.resize(
        length
    );

    return text;
}

// ============================================================
// PLAY ONE SONG
// ============================================================

void PlaySong(
    int playerIndex,
    std::string songText,
    bool synchronizeStart)
{
    if (
        playerIndex < 0 ||
        playerIndex >= PLAYER_COUNT)
        return;

    playing[playerIndex] = true;
    stopRequested[playerIndex] = false;

    // ========================================================
    // LOAD SONG
    // ========================================================

    std::vector<NoteEvent> tempNotes;

    std::vector<DrumEvent> tempDrums;

    double fileTempo = 120.0;

    double loopLengthBeats = 4.0;

    LoadSongFromText(
        songText,
        tempNotes,
        tempDrums,
        fileTempo,
        loopLengthBeats
    );

    if (
        currentTempo[playerIndex].load()
        <= 0.0)
    {
        currentTempo[playerIndex] =
            fileTempo;
    }

    // ========================================================
    // GENERATE FIRST BUFFERS
    // ========================================================

    std::vector<short> audioSamples[
        AUDIO_BUFFER_COUNT
    ];

    auto generateBuffer =
        [&](int bufferIndex) -> bool
    {
        double tempo =
            currentTempo[playerIndex].load();

        int pitch =
            currentPitch[playerIndex].load();

        return GenerateAudio(
            songText,
            tempo,
            pitch,
            audioSamples[bufferIndex]
        );
    };

    if (!generateBuffer(0))
    {
        playing[playerIndex] = false;

        PostMessage(
            mainWindow,
            WM_USER + 100 + playerIndex,
            0,
            0
        );

        return;
    }

    if (!generateBuffer(1))
    {
        playing[playerIndex] = false;

        PostMessage(
            mainWindow,
            WM_USER + 100 + playerIndex,
            0,
            0
        );

        return;
    }

    // ========================================================
    // OPEN AUDIO DEVICE
    // ========================================================

    WAVEFORMATEX format = {};

    format.wFormatTag =
        WAVE_FORMAT_PCM;

    format.nChannels =
        1;

    format.nSamplesPerSec =
        SAMPLE_RATE;

    format.wBitsPerSample =
        16;

    format.nBlockAlign =
        format.nChannels *
        format.wBitsPerSample /
        8;

    format.nAvgBytesPerSec =
        format.nSamplesPerSec *
        format.nBlockAlign;

    HWAVEOUT audioDevice =
        nullptr;

    MMRESULT result =
        waveOutOpen(
            &audioDevice,
            WAVE_MAPPER,
            &format,
            0,
            0,
            CALLBACK_NULL
        );

    if (
        result !=
        MMSYSERR_NOERROR)
    {
        playing[playerIndex] = false;

        PostMessage(
            mainWindow,
            WM_USER + 100 + playerIndex,
            0,
            0
        );

        return;
    }

    // ========================================================
    // SYNCHRONIZED START
    // ========================================================

    if (synchronizeStart)
    {
        std::unique_lock<std::mutex> lock(
            playAllMutex
        );

        playAllWaiting++;

        if (playAllWaiting >= PLAYER_COUNT)
        {
            playAllStartTime =
                std::chrono::steady_clock::now()
                +
                std::chrono::milliseconds(500);

            playAllActive = true;

            playAllCondition.notify_all();
        }
        else
        {
            playAllCondition.wait(
                lock,
                []()
                {
                    return playAllActive;
                }
            );
        }

        auto startTime =
            playAllStartTime;

        lock.unlock();

        while (
            std::chrono::steady_clock::now()
            <
            startTime)
        {
            std::this_thread::yield();
        }
    }

    // ========================================================
    // WAVE HEADERS
    // ========================================================

    WAVEHDR headers[
        AUDIO_BUFFER_COUNT
    ] = {};

    bool prepared[
        AUDIO_BUFFER_COUNT
    ] = {};

    bool queued[
        AUDIO_BUFFER_COUNT
    ] = {};

    // ========================================================
    // QUEUE INITIAL BUFFERS
    // ========================================================

    for (
        int i = 0;
        i < AUDIO_BUFFER_COUNT;
        ++i)
    {
        headers[i] = {};

        headers[i].lpData =
            reinterpret_cast<LPSTR>(
                audioSamples[i].data()
            );

        headers[i].dwBufferLength =
            static_cast<DWORD>(
                audioSamples[i].size()
                *
                sizeof(short)
            );

        result =
            waveOutPrepareHeader(
                audioDevice,
                &headers[i],
                sizeof(WAVEHDR)
            );

        if (
            result !=
            MMSYSERR_NOERROR)
        {
            waveOutReset(
                audioDevice
            );

            waveOutClose(
                audioDevice
            );

            playing[playerIndex] = false;

            PostMessage(
                mainWindow,
                WM_USER + 100 + playerIndex,
                0,
                0
            );

            return;
        }

        prepared[i] = true;

        result =
            waveOutWrite(
                audioDevice,
                &headers[i],
                sizeof(WAVEHDR)
            );

        if (
            result !=
            MMSYSERR_NOERROR)
        {
            waveOutReset(
                audioDevice
            );

            for (
                int j = 0;
                j < AUDIO_BUFFER_COUNT;
                ++j)
            {
                if (prepared[j])
                {
                    waveOutUnprepareHeader(
                        audioDevice,
                        &headers[j],
                        sizeof(WAVEHDR)
                    );
                }
            }

            waveOutClose(
                audioDevice
            );

            playing[playerIndex] = false;

            PostMessage(
                mainWindow,
                WM_USER + 100 + playerIndex,
                0,
                0
            );

            return;
        }

        queued[i] = true;
    }

    // ========================================================
    // PLAYBACK LOOP
    // ========================================================

    while (
        !stopRequested[playerIndex])
    {
        bool didSomething =
            false;

        for (
            int i = 0;
            i < AUDIO_BUFFER_COUNT;
            ++i)
        {
            if (!queued[i])
                continue;

            if (
                !(headers[i].dwFlags &
                  WHDR_DONE))
            {
                continue;
            }

            didSomething = true;

            waveOutUnprepareHeader(
                audioDevice,
                &headers[i],
                sizeof(WAVEHDR)
            );

            prepared[i] = false;
            queued[i] = false;

            if (stopRequested[playerIndex])
                continue;

            if (!looping[playerIndex])
                continue;

            if (!generateBuffer(i))
            {
                stopRequested[playerIndex] = true;
                continue;
            }

            headers[i] = {};

            headers[i].lpData =
                reinterpret_cast<LPSTR>(
                    audioSamples[i].data()
                );

            headers[i].dwBufferLength =
                static_cast<DWORD>(
                    audioSamples[i].size()
                    *
                    sizeof(short)
                );

            result =
                waveOutPrepareHeader(
                    audioDevice,
                    &headers[i],
                    sizeof(WAVEHDR)
                );

            if (
                result !=
                MMSYSERR_NOERROR)
            {
                stopRequested[playerIndex] = true;
                continue;
            }

            prepared[i] = true;

            result =
                waveOutWrite(
                    audioDevice,
                    &headers[i],
                    sizeof(WAVEHDR)
                );

            if (
                result !=
                MMSYSERR_NOERROR)
            {
                waveOutUnprepareHeader(
                    audioDevice,
                    &headers[i],
                    sizeof(WAVEHDR)
                );

                prepared[i] = false;

                stopRequested[playerIndex] = true;

                continue;
            }

            queued[i] = true;
        }

        // ----------------------------------------------------
        // NON-LOOPING SONG FINISHED
        // ----------------------------------------------------

        if (!looping[playerIndex])
        {
            bool anythingQueued =
                false;

            for (
                int i = 0;
                i < AUDIO_BUFFER_COUNT;
                ++i)
            {
                if (queued[i])
                {
                    anythingQueued = true;
                    break;
                }
            }

            if (!anythingQueued)
                break;
        }

        if (!didSomething)
        {
            Sleep(1);
        }
    }

    // ========================================================
    // STOP AUDIO
    // ========================================================

    waveOutReset(
        audioDevice
    );

    for (
        int i = 0;
        i < AUDIO_BUFFER_COUNT;
        ++i)
    {
        if (prepared[i])
        {
            waveOutUnprepareHeader(
                audioDevice,
                &headers[i],
                sizeof(WAVEHDR)
            );

            prepared[i] = false;
        }

        queued[i] = false;
    }

    waveOutClose(
        audioDevice
    );

    playing[playerIndex] = false;

    PostMessage(
        mainWindow,
        WM_USER + 100 + playerIndex,
        0,
        0
    );
}

// ============================================================
// START PLAYER
// ============================================================

void StartPlayer(
    int playerIndex,
    bool synchronizeStart)
{
    if (
        playerIndex < 0 ||
        playerIndex >= PLAYER_COUNT)
        return;

    if (playing[playerIndex])
        return;

    std::string text =
        GetEditorText(
            playerIndex
        );

    stopRequested[playerIndex] =
        false;

    SetWindowTextA(
        playButton[playerIndex],
        "STOP"
    );

    std::thread(
        PlaySong,
        playerIndex,
        text,
        synchronizeStart
    ).detach();
}

// ============================================================
// RESIZE GRID
// ============================================================

void ResizePlayerControls(
    HWND window)
{
    RECT rect;

    GetClientRect(
        window,
        &rect
    );

    int windowWidth =
        rect.right;

    int windowHeight =
        rect.bottom;

    // Leave room for PLAY ALL
    int topArea = 55;

    int gridWidth =
        windowWidth;

    int gridHeight =
        windowHeight -
        topArea;

    if (gridHeight < 200)
        gridHeight = 200;

    int columnWidth =
        gridWidth /
        GRID_COLUMNS;

    int rowHeight =
        gridHeight /
        GRID_ROWS;

    for (
        int i = 0;
        i < PLAYER_COUNT;
        ++i)
    {
        int column =
            i % GRID_COLUMNS;

        int row =
            i / GRID_COLUMNS;

        int cellX =
            column *
            columnWidth;

        int cellY =
            topArea +
            row *
            rowHeight;

        int x =
            cellX + 8;

        int y =
            cellY + 8;

        int buttonWidth =
            columnWidth - 16;

        if (buttonWidth < 80)
            buttonWidth = 80;

        // ----------------------------------------------------
        // PLAY
        // ----------------------------------------------------

        MoveWindow(
            playButton[i],
            x,
            y + 25,
            buttonWidth,
            30,
            TRUE
        );

        // ----------------------------------------------------
        // LOOP
        // ----------------------------------------------------

        MoveWindow(
            loopButton[i],
            x,
            y + 58,
            buttonWidth,
            25,
            TRUE
        );

        // ----------------------------------------------------
        // TEMPO
        // ----------------------------------------------------

        int smallWidth =
            (buttonWidth - 6) / 3;

        MoveWindow(
            tempoMinusButton[i],
            x,
            y + 88,
            smallWidth,
            27,
            TRUE
        );

        MoveWindow(
            tempoLabel[i],
            x + smallWidth + 3,
            y + 88,
            smallWidth,
            27,
            TRUE
        );

        MoveWindow(
            tempoPlusButton[i],
            x +
            (smallWidth + 3) * 2,
            y + 88,
            buttonWidth -
            (smallWidth + 3) * 2,
            27,
            TRUE
        );

        // ----------------------------------------------------
        // PITCH
        // ----------------------------------------------------

        MoveWindow(
            pitchMinusButton[i],
            x,
            y + 118,
            smallWidth,
            27,
            TRUE
        );

        MoveWindow(
            pitchLabel[i],
            x + smallWidth + 3,
            y + 118,
            smallWidth,
            27,
            TRUE
        );

        MoveWindow(
            pitchPlusButton[i],
            x +
            (smallWidth + 3) * 2,
            y + 118,
            buttonWidth -
            (smallWidth + 3) * 2,
            27,
            TRUE
        );

        // ----------------------------------------------------
        // TEXT EDITOR
        // ----------------------------------------------------

        int editorY =
            y + 150;

        int editorHeight =
            rowHeight -
            160;

        if (editorHeight < 50)
            editorHeight = 50;

        MoveWindow(
            songEditor[i],
            x,
            editorY,
            buttonWidth,
            editorHeight,
            TRUE
        );
    }
}

// ============================================================
// WINDOW PROCEDURE
// ============================================================

LRESULT CALLBACK WindowProcedure(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (message)
    {
        // ====================================================
        // BUTTON COLOR
        // ====================================================

        case WM_CTLCOLORBTN:
        {
            HDC dc =
                (HDC)wParam;

            SetTextColor(
                dc,
                BUTTON_TEXT
            );

            SetBkColor(
                dc,
                COLUMN
            );

            SetBkMode(
                dc,
                OPAQUE
            );

            static HBRUSH buttonBrush =
                CreateSolidBrush(
                    COLUMN
                );

            return (LRESULT)
                buttonBrush;
        }

        // ====================================================
        // EDITOR COLOR
        // ====================================================

        case WM_CTLCOLOREDIT:
        {
            HDC dc =
                (HDC)wParam;

            SetTextColor(
                dc,
                EDIT_TEXT
            );

            SetBkColor(
                dc,
                EDIT_BACKGROUND
            );

            static HBRUSH editBrush =
                CreateSolidBrush(
                    EDIT_BACKGROUND
                );

            return (LRESULT)
                editBrush;
        }

        // ====================================================
        // STATIC LABEL COLOR
        // ====================================================

        case WM_CTLCOLORSTATIC:
        {
            HDC dc =
                (HDC)wParam;

            SetTextColor(
                dc,
                TEXT
            );

            SetBkColor(
                dc,
                COLUMN
            );

            SetBkMode(
                dc,
                OPAQUE
            );

            static HBRUSH staticBrush =
                CreateSolidBrush(
                    COLUMN
                );

            return (LRESULT)
                staticBrush;
        }

        // ====================================================
        // COMMANDS
        // ====================================================

        case WM_COMMAND:
        {
            int button =
                LOWORD(wParam);

            // ------------------------------------------------
            // PLAY ALL
            // ------------------------------------------------

            if (button == ID_PLAY_ALL)
            {
                bool anyPlaying = false;

                for (
                    int i = 0;
                    i < PLAYER_COUNT;
                    ++i)
                {
                    if (playing[i])
                    {
                        anyPlaying = true;
                        break;
                    }
                }

                if (anyPlaying)
                {
                    for (
                        int i = 0;
                        i < PLAYER_COUNT;
                        ++i)
                    {
                        stopRequested[i] = true;
                    }

                    SetWindowTextA(
                        FindWindowExA(
                            window,
                            nullptr,
                            "BUTTON",
                            "STOP ALL"
                        ),
                        "PLAY ALL"
                    );
                }
                else
                {
                    {
                        std::lock_guard<std::mutex>
                            lock(playAllMutex);

                        playAllWaiting = 0;
                        playAllActive = false;
                    }

                    for (
                        int i = 0;
                        i < PLAYER_COUNT;
                        ++i)
                    {
                        if (!playing[i])
                        {
                            std::string text =
                                GetEditorText(i);

                            stopRequested[i] = false;

                            SetWindowTextA(
                                playButton[i],
                                "STOP"
                            );

                            std::thread(
                                PlaySong,
                                i,
                                text,
                                true
                            ).detach();
                        }
                    }

                    HWND playAllButton =
                        GetDlgItem(
                            window,
                            ID_PLAY_ALL
                        );

                    SetWindowTextA(
                        playAllButton,
                        "STOP ALL"
                    );
                }

                break;
            }

            // ------------------------------------------------
            // PLAYER BUTTONS
            // ------------------------------------------------

            if (
                button >= ID_PLAY_BASE &&
                button <
                ID_PLAY_BASE + PLAYER_COUNT)
            {
                int p =
                    button -
                    ID_PLAY_BASE;

                if (!playing[p])
                {
                    StartPlayer(
                        p,
                        false
                    );
                }
                else
                {
                    stopRequested[p] =
                        true;

                    SetWindowTextA(
                        playButton[p],
                        "PLAY"
                    );
                }

                break;
            }

            // ------------------------------------------------
            // LOOP
            // ------------------------------------------------

            if (
                button >= ID_LOOP_BASE &&
                button <
                ID_LOOP_BASE + PLAYER_COUNT)
            {
                int p =
                    button -
                    ID_LOOP_BASE;

                looping[p] =
                    !looping[p];

                SetWindowTextA(
                    loopButton[p],
                    looping[p]
                    ? "LOOP: ON"
                    : "LOOP: OFF"
                );

                break;
            }

            // ------------------------------------------------
            // TEMPO MINUS
            // ------------------------------------------------

            if (
                button >= ID_TEMPO_MINUS_BASE &&
                button <
                ID_TEMPO_MINUS_BASE +
                PLAYER_COUNT)
            {
                int p =
                    button -
                    ID_TEMPO_MINUS_BASE;

                double tempo =
                    currentTempo[p].load();

                tempo -=
                    TEMPO_STEP;

                if (tempo < MIN_TEMPO)
                    tempo = MIN_TEMPO;

                currentTempo[p] =
                    tempo;

                UpdateTempoDisplay(p);

                break;
            }

            // ------------------------------------------------
            // TEMPO PLUS
            // ------------------------------------------------

            if (
                button >= ID_TEMPO_PLUS_BASE &&
                button <
                ID_TEMPO_PLUS_BASE +
                PLAYER_COUNT)
            {
                int p =
                    button -
                    ID_TEMPO_PLUS_BASE;

                double tempo =
                    currentTempo[p].load();

                tempo +=
                    TEMPO_STEP;

                if (tempo > MAX_TEMPO)
                    tempo = MAX_TEMPO;

                currentTempo[p] =
                    tempo;

                UpdateTempoDisplay(p);

                break;
            }

            // ------------------------------------------------
            // PITCH MINUS
            // ------------------------------------------------

            if (
                button >= ID_PITCH_MINUS_BASE &&
                button <
                ID_PITCH_MINUS_BASE +
                PLAYER_COUNT)
            {
                int p =
                    button -
                    ID_PITCH_MINUS_BASE;

                int pitch =
                    currentPitch[p].load();

                pitch -=
                    PITCH_STEP;

                if (pitch < MIN_PITCH)
                    pitch = MIN_PITCH;

                currentPitch[p] =
                    pitch;

                UpdatePitchDisplay(p);

                break;
            }

            // ------------------------------------------------
            // PITCH PLUS
            // ------------------------------------------------

            if (
                button >= ID_PITCH_PLUS_BASE &&
                button <
                ID_PITCH_PLUS_BASE +
                PLAYER_COUNT)
            {
                int p =
                    button -
                    ID_PITCH_PLUS_BASE;

                int pitch =
                    currentPitch[p].load();

                pitch +=
                    PITCH_STEP;

                if (pitch > MAX_PITCH)
                    pitch = MAX_PITCH;

                currentPitch[p] =
                    pitch;

                UpdatePitchDisplay(p);

                break;
            }

            break;
        }

        // ====================================================
        // PLAYBACK FINISHED
        // ====================================================

        default:
        {
            if (
                message >= WM_USER + 100 &&
                message <
                WM_USER + 100 +
                PLAYER_COUNT)
            {
                int p =
                    static_cast<int>(
                        message -
                        (WM_USER + 100)
                    );

                if (
                    p >= 0 &&
                    p < PLAYER_COUNT)
                {
                    SetWindowTextA(
                        playButton[p],
                        "PLAY"
                    );
                }

                bool anyPlaying = false;

                for (
                    int i = 0;
                    i < PLAYER_COUNT;
                    ++i)
                {
                    if (playing[i])
                    {
                        anyPlaying = true;
                        break;
                    }
                }

                if (!anyPlaying)
                {
                    SetWindowTextA(
                        GetDlgItem(
                            window,
                            ID_PLAY_ALL
                        ),
                        "PLAY ALL"
                    );
                }

                return 0;
            }

            // ================================================
            // RESIZE
            // ================================================

            if (message == WM_SIZE)
            {
                ResizePlayerControls(
                    window
                );

                InvalidateRect(
                    window,
                    nullptr,
                    TRUE
                );

                return 0;
            }

            // ================================================
            // PAINT
            // ================================================

            if (message == WM_PAINT)
            {
                PAINTSTRUCT ps;

                HDC dc =
                    BeginPaint(
                        window,
                        &ps
                    );

                RECT rect;

                GetClientRect(
                    window,
                    &rect
                );

                HBRUSH backgroundBrush =
                    CreateSolidBrush(
                        BACKGROUND
                    );

                FillRect(
                    dc,
                    &rect,
                    backgroundBrush
                );

                DeleteObject(
                    backgroundBrush
                );

                // --------------------------------------------
                // GRID
                // --------------------------------------------

                int gridTop = 55;

                int columnWidth =
                    rect.right /
                    GRID_COLUMNS;

                int gridHeight =
                    rect.bottom -
                    gridTop;

                int rowHeight =
                    gridHeight /
                    GRID_ROWS;

                HPEN borderPen =
                    CreatePen(
                        PS_SOLID,
                        1,
                        COLUMN_BORDER
                    );

                HPEN oldPen =
                    (HPEN)SelectObject(
                        dc,
                        borderPen
                    );

                HBRUSH nullBrush =
                    (HBRUSH)GetStockObject(
                        NULL_BRUSH
                    );

                HBRUSH oldBrush =
                    (HBRUSH)SelectObject(
                        dc,
                        nullBrush
                    );

                for (
                    int i = 0;
                    i < PLAYER_COUNT;
                    ++i)
                {
                    int column =
                        i % GRID_COLUMNS;

                    int row =
                        i / GRID_COLUMNS;

                    RECT columnRect =
                    {
                        column *
                            columnWidth + 4,

                        gridTop +
                            row *
                            rowHeight + 4,

                        (column + 1) *
                            columnWidth - 4,

                        gridTop +
                            (row + 1) *
                            rowHeight - 4
                    };

                    HBRUSH columnBrush =
                        CreateSolidBrush(
                            COLUMN
                        );

                    FillRect(
                        dc,
                        &columnRect,
                        columnBrush
                    );

                    DeleteObject(
                        columnBrush
                    );

                    Rectangle(
                        dc,
                        columnRect.left,
                        columnRect.top,
                        columnRect.right,
                        columnRect.bottom
                    );
                }

                SelectObject(
                    dc,
                    oldBrush
                );

                SelectObject(
                    dc,
                    oldPen
                );

                DeleteObject(
                    borderPen
                );

                // --------------------------------------------
                // COLUMN TITLES
                // --------------------------------------------

                SetTextColor(
                    dc,
                    TEXT
                );

                SetBkMode(
                    dc,
                    TRANSPARENT
                );

                HFONT titleFont =
                    CreateFontA(
                        16,
                        0,
                        0,
                        0,
                        FW_BOLD,
                        FALSE,
                        FALSE,
                        FALSE,
                        DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS,
                        DEFAULT_QUALITY,
                        DEFAULT_PITCH,
                        "Segoe UI"
                    );

                HFONT oldFont =
                    (HFONT)SelectObject(
                        dc,
                        titleFont
                    );

                const char* titles[16] =
                {
                    "I",
                    "II",
                    "III",
                    "IV",
                    "V",
                    "VI",
                    "VII",
                    "VIII",
                    "IX",
                    "X",
                    "XI",
                    "XII",
                    "XIII",
                    "XIV",
                    "XV",
                    "XVI"
                };

                for (
                    int i = 0;
                    i < PLAYER_COUNT;
                    ++i)
                {
                    int column =
                        i % GRID_COLUMNS;

                    int row =
                        i / GRID_COLUMNS;

                    RECT titleRect =
                    {
                        column *
                            columnWidth,

                        gridTop +
                            row *
                            rowHeight +
                            7,

                        (column + 1) *
                            columnWidth,

                        gridTop +
                            row *
                            rowHeight +
                            28
                    };

                    DrawTextA(
                        dc,
                        titles[i],
                        -1,
                        &titleRect,
                        DT_CENTER |
                        DT_SINGLELINE
                    );
                }

                SelectObject(
                    dc,
                    oldFont
                );

                DeleteObject(
                    titleFont
                );

                EndPaint(
                    window,
                    &ps
                );

                return 0;
            }

            // ================================================
            // DESTROY
            // ================================================

            if (message == WM_DESTROY)
            {
                for (
                    int i = 0;
                    i < PLAYER_COUNT;
                    ++i)
                {
                    stopRequested[i] =
                        true;
                }

                PostQuitMessage(0);

                return 0;
            }

            return DefWindowProcA(
                window,
                message,
                wParam,
                lParam
            );
        }
    }

    return 0;
}

// ============================================================
// MAIN
// ============================================================

int WINAPI WinMain(
    HINSTANCE instance,
    HINSTANCE,
    LPSTR,
    int)
{
    const char CLASS_NAME[] =
        "CppSongMaker16";

    // ========================================================
    // INITIALIZE ARRAYS
    // ========================================================

    for (
        int i = 0;
        i < PLAYER_COUNT;
        ++i)
    {
        playing[i] = false;

        looping[i] = false;

        stopRequested[i] = false;

        currentTempo[i] =
            120.0;

        currentPitch[i] =
            0;

        playButton[i] = nullptr;
        loopButton[i] = nullptr;

        tempoMinusButton[i] = nullptr;
        tempoPlusButton[i] = nullptr;
        tempoLabel[i] = nullptr;

        pitchMinusButton[i] = nullptr;
        pitchPlusButton[i] = nullptr;
        pitchLabel[i] = nullptr;

        songEditor[i] = nullptr;
    }

    // ========================================================
    // WINDOW CLASS
    // ========================================================

    WNDCLASSA windowClass = {};

    windowClass.lpfnWndProc =
        WindowProcedure;

    windowClass.hInstance =
        instance;

    windowClass.lpszClassName =
        CLASS_NAME;

    windowClass.hbrBackground =
        CreateSolidBrush(
            BACKGROUND
        );

    windowClass.hCursor =
        LoadCursorA(
            nullptr,
            IDC_ARROW
        );

    RegisterClassA(
        &windowClass
    );

    // ========================================================
    // WINDOW
    // ========================================================

    HWND window =
        CreateWindowExA(
            0,
            CLASS_NAME,
            "C++ Audio - 16 Players",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1200,
            900,
            nullptr,
            nullptr,
            instance,
            nullptr
        );

    if (!window)
        return 0;

    mainWindow =
        window;

    // ========================================================
    // PLAY ALL BUTTON
    // ========================================================

    HWND playAllButton =
        CreateWindowA(
            "BUTTON",
            "PLAY ALL",
            WS_VISIBLE |
            WS_CHILD |
            BS_PUSHBUTTON,
            10,
            10,
            150,
            35,
            window,
            (HMENU)(INT_PTR)ID_PLAY_ALL,
            instance,
            nullptr
        );

    // ========================================================
    // DEFAULT SONG
    // ========================================================

    const char* defaultSong =
        "TEMPO 120\n"
        "LENGTH 4\n"
        "\n"
        "PIANO C4 0 1\n"
        "PIANO E4 1 1\n"
        "PIANO G4 2 1\n"
        "PIANO C5 3 1\n"
        "\n"
        "BASS C3 0 1\n"
        "BASS C3 2 1\n"
        "\n"
        "GUITAR E4 1 1\n"
        "GUITAR G4 3 1\n"
        "\n"
        "SYNTH G4 2 1\n"
        "\n"
        "DRUM KICK 0\n"
        "DRUM HIHAT 0\n"
        "DRUM HIHAT 0.5\n"
        "DRUM KICK 1\n"
        "DRUM HIHAT 1\n"
        "DRUM HIHAT 1.5\n"
        "DRUM KICK 2\n"
        "DRUM HIHAT 2\n"
        "DRUM HIHAT 2.5\n"
        "DRUM KICK 3\n"
        "DRUM HIHAT 3\n"
        "DRUM HIHAT 3.5\n";

    // ========================================================
    // CREATE 16 PLAYERS
    // ========================================================

    for (
        int i = 0;
        i < PLAYER_COUNT;
        ++i)
    {
        int playID =
            ID_PLAY_BASE + i;

        int loopID =
            ID_LOOP_BASE + i;

        int tempoMinusID =
            ID_TEMPO_MINUS_BASE + i;

        int tempoPlusID =
            ID_TEMPO_PLUS_BASE + i;

        int pitchMinusID =
            ID_PITCH_MINUS_BASE + i;

        int pitchPlusID =
            ID_PITCH_PLUS_BASE + i;

        int editorID =
            ID_EDITOR_BASE + i;

        // ----------------------------------------------------
        // PLAY
        // ----------------------------------------------------

        playButton[i] =
            CreateWindowA(
                "BUTTON",
                "PLAY",
                WS_VISIBLE |
                WS_CHILD |
                BS_PUSHBUTTON,
                0,
                0,
                100,
                30,
                window,
                (HMENU)(INT_PTR)playID,
                instance,
                nullptr
            );

        // ----------------------------------------------------
        // LOOP
        // ----------------------------------------------------

        loopButton[i] =
            CreateWindowA(
                "BUTTON",
                "LOOP: OFF",
                WS_VISIBLE |
                WS_CHILD |
                BS_PUSHBUTTON,
                0,
                0,
                100,
                25,
                window,
                (HMENU)(INT_PTR)loopID,
                instance,
                nullptr
            );

        // ----------------------------------------------------
        // TEMPO MINUS
        // ----------------------------------------------------

        tempoMinusButton[i] =
            CreateWindowA(
                "BUTTON",
                "-",
                WS_VISIBLE |
                WS_CHILD |
                BS_PUSHBUTTON,
                0,
                0,
                30,
                27,
                window,
                (HMENU)(INT_PTR)tempoMinusID,
                instance,
                nullptr
            );

        // ----------------------------------------------------
        // TEMPO LABEL
        // ----------------------------------------------------

        tempoLabel[i] =
            CreateWindowA(
                "STATIC",
                "120",
                WS_VISIBLE |
                WS_CHILD |
                SS_CENTER |
                SS_CENTERIMAGE,
                0,
                0,
                40,
                27,
                window,
                nullptr,
                instance,
                nullptr
            );

        // ----------------------------------------------------
        // TEMPO PLUS
        // ----------------------------------------------------

        tempoPlusButton[i] =
            CreateWindowA(
                "BUTTON",
                "+",
                WS_VISIBLE |
                WS_CHILD |
                BS_PUSHBUTTON,
                0,
                0,
                30,
                27,
                window,
                (HMENU)(INT_PTR)tempoPlusID,
                instance,
                nullptr
            );

        // ----------------------------------------------------
        // PITCH MINUS
        // ----------------------------------------------------

        pitchMinusButton[i] =
            CreateWindowA(
                "BUTTON",
                "-",
                WS_VISIBLE |
                WS_CHILD |
                BS_PUSHBUTTON,
                0,
                0,
                30,
                27,
                window,
                (HMENU)(INT_PTR)pitchMinusID,
                instance,
                nullptr
            );

        // ----------------------------------------------------
        // PITCH LABEL
        // ----------------------------------------------------

        pitchLabel[i] =
            CreateWindowA(
                "STATIC",
                "0",
                WS_VISIBLE |
                WS_CHILD |
                SS_CENTER |
                SS_CENTERIMAGE,
                0,
                0,
                40,
                27,
                window,
                nullptr,
                instance,
                nullptr
            );

        // ----------------------------------------------------
        // PITCH PLUS
        // ----------------------------------------------------

        pitchPlusButton[i] =
            CreateWindowA(
                "BUTTON",
                "+",
                WS_VISIBLE |
                WS_CHILD |
                BS_PUSHBUTTON,
                0,
                0,
                30,
                27,
                window,
                (HMENU)(INT_PTR)pitchPlusID,
                instance,
                nullptr
            );

        // ----------------------------------------------------
        // SONG EDITOR
        //
        // MULTILINE
        // WORD WRAP
        // VERTICAL SCROLL
        // ----------------------------------------------------

        songEditor[i] =
            CreateWindowExA(
                WS_EX_CLIENTEDGE,
                "EDIT",
                defaultSong,
                WS_VISIBLE |
                WS_CHILD |
                ES_MULTILINE |
                ES_AUTOVSCROLL |
                ES_WANTRETURN |
                WS_VSCROLL |
                WS_TABSTOP,
                0,
                0,
                100,
                100,
                window,
                (HMENU)(INT_PTR)editorID,
                instance,
                nullptr
            );

        // ----------------------------------------------------
        // FONT FOR EDITOR
        // ----------------------------------------------------

        HFONT editorFont =
            CreateFontA(
                14,
                0,
                0,
                0,
                FW_NORMAL,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY,
                FIXED_PITCH |
                FF_MODERN,
                "Consolas"
            );

        SendMessage(
            songEditor[i],
            WM_SETFONT,
            (WPARAM)editorFont,
            TRUE
        );

        // ----------------------------------------------------
        // FONT FOR BUTTONS
        // ----------------------------------------------------

        HFONT buttonFont =
            CreateFontA(
                13,
                0,
                0,
                0,
                FW_BOLD,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY,
                DEFAULT_PITCH,
                "Segoe UI"
            );

        SendMessage(
            playButton[i],
            WM_SETFONT,
            (WPARAM)buttonFont,
            TRUE
        );

        SendMessage(
            loopButton[i],
            WM_SETFONT,
            (WPARAM)buttonFont,
            TRUE
        );

        SendMessage(
            tempoMinusButton[i],
            WM_SETFONT,
            (WPARAM)buttonFont,
            TRUE
        );

        SendMessage(
            tempoPlusButton[i],
            WM_SETFONT,
            (WPARAM)buttonFont,
            TRUE
        );

        SendMessage(
            pitchMinusButton[i],
            WM_SETFONT,
            (WPARAM)buttonFont,
            TRUE
        );

        SendMessage(
            pitchPlusButton[i],
            WM_SETFONT,
            (WPARAM)buttonFont,
            TRUE
        );

        // ----------------------------------------------------
        // FONT FOR NUMBERS
        //
        // Larger fixed-width font helps prevent
        // the tempo/pitch digits from visually shifting.
        // ----------------------------------------------------

        HFONT numberFont =
            CreateFontA(
                15,
                0,
                0,
                0,
                FW_BOLD,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                FIXED_PITCH |
                FF_MODERN,
                "Consolas"
            );

        SendMessage(
            tempoLabel[i],
            WM_SETFONT,
            (WPARAM)numberFont,
            TRUE
        );

        SendMessage(
            pitchLabel[i],
            WM_SETFONT,
            (WPARAM)numberFont,
            TRUE
        );
    }

    // ========================================================
    // PLAY ALL FONT
    // ========================================================

    HFONT playAllFont =
        CreateFontA(
            15,
            0,
            0,
            0,
            FW_BOLD,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY,
            DEFAULT_PITCH,
            "Segoe UI"
        );

    SendMessage(
        playAllButton,
        WM_SETFONT,
        (WPARAM)playAllFont,
        TRUE
    );

    // ========================================================
    // INITIAL POSITION
    // ========================================================

    ResizePlayerControls(
        window
    );

    // ========================================================
    // SHOW WINDOW
    // ========================================================

    ShowWindow(
        window,
        SW_SHOW
    );

    UpdateWindow(
        window
    );

    // ========================================================
    // MESSAGE LOOP
    // ========================================================

    MSG message = {};

    while (
        GetMessageA(
            &message,
            nullptr,
            0,
            0
        )
    )
    {
        TranslateMessage(
            &message
        );

        DispatchMessageA(
            &message
        );
    }

    return 0;
}
