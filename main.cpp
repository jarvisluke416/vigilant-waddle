#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "winmm.lib")

// ============================================================
// SETTINGS
// ============================================================

const int SAMPLE_RATE = 44100;
const double PI = 3.14159265358979323846;

const int PLAYER_COUNT = 4;

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

// ============================================================
// GLOBAL STATE
// ============================================================

std::atomic<bool> playing[PLAYER_COUNT] = {};
std::atomic<bool> looping[PLAYER_COUNT] = {};
std::atomic<bool> stopRequested[PLAYER_COUNT] = {};

std::atomic<double> currentTempo[PLAYER_COUNT] = {};
std::atomic<int> currentPitch[PLAYER_COUNT] = {};

HWND playButton[PLAYER_COUNT] = {};
HWND loopButton[PLAYER_COUNT] = {};

HWND tempoMinusButton[PLAYER_COUNT] = {};
HWND tempoPlusButton[PLAYER_COUNT] = {};
HWND tempoLabel[PLAYER_COUNT] = {};

HWND pitchMinusButton[PLAYER_COUNT] = {};
HWND pitchPlusButton[PLAYER_COUNT] = {};
HWND pitchLabel[PLAYER_COUNT] = {};

HWND songEditor[PLAYER_COUNT] = {};

HWND universalPlayButton = nullptr;

HWND mainWindow = nullptr;

// ============================================================
// UNIVERSAL START SYNCHRONIZATION
// ============================================================

std::mutex universalMutex;
std::condition_variable universalCondition;

int universalWaitingPlayers = 0;

bool universalStartRequested = false;
bool universalCancelRequested = false;

std::atomic<bool> universalPlaying = false;

// ============================================================
// RANDOM
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
// NOTE FREQUENCIES
// ============================================================

double noteFrequency(const std::string& note)
{
    if (note == "C2")  return 65.41;
    if (note == "C#2") return 69.30;
    if (note == "D2")  return 73.42;
    if (note == "D#2") return 77.78;
    if (note == "E2")  return 82.41;
    if (note == "F2")  return 87.31;
    if (note == "F#2") return 92.50;
    if (note == "G2")  return 98.00;
    if (note == "G#2") return 103.83;
    if (note == "A2")  return 110.00;
    if (note == "A#2") return 116.54;
    if (note == "B2")  return 123.47;

    if (note == "C3")  return 130.81;
    if (note == "C#3") return 138.59;
    if (note == "D3")  return 146.83;
    if (note == "D#3") return 155.56;
    if (note == "E3")  return 164.81;
    if (note == "F3")  return 174.61;
    if (note == "F#3") return 185.00;
    if (note == "G3")  return 196.00;
    if (note == "G#3") return 207.65;
    if (note == "A3")  return 220.00;
    if (note == "A#3") return 233.08;
    if (note == "B3")  return 246.94;

    if (note == "C4")  return 261.63;
    if (note == "C#4") return 277.18;
    if (note == "D4")  return 293.66;
    if (note == "D#4") return 311.13;
    if (note == "E4")  return 329.63;
    if (note == "F4")  return 349.23;
    if (note == "F#4") return 369.99;
    if (note == "G4")  return 392.00;
    if (note == "G#4") return 415.30;
    if (note == "A4")  return 440.00;
    if (note == "A#4") return 466.16;
    if (note == "B4")  return 493.88;

    if (note == "C5")  return 523.25;
    if (note == "C#5") return 554.37;
    if (note == "D5")  return 587.33;
    if (note == "D#5") return 622.25;
    if (note == "E5")  return 659.25;
    if (note == "F5")  return 698.46;
    if (note == "F#5") return 739.99;
    if (note == "G5")  return 783.99;
    if (note == "G#5") return 830.61;
    if (note == "A5")  return 880.00;
    if (note == "A#5") return 932.33;
    if (note == "B5")  return 987.77;

    if (note == "C6")  return 1046.50;
    if (note == "C#6") return 1108.73;
    if (note == "D6")  return 1174.66;
    if (note == "D#6") return 1244.51;
    if (note == "E6")  return 1318.51;
    if (note == "F6")  return 1396.91;
    if (note == "F#6") return 1479.98;
    if (note == "G6")  return 1567.98;
    if (note == "G#6") return 1661.22;
    if (note == "A6")  return 1760.00;
    if (note == "A#6") return 1864.66;
    if (note == "B6")  return 1975.53;

    return 0.0;
}

// ============================================================
// INSTRUMENT SOUND
// ============================================================

double instrumentWave(
    const std::string& instrument,
    double frequency,
    double t)
{
    double phase =
        2.0 * PI * frequency * t;

    if (instrument == "BASS")
    {
        return
            std::sin(phase)
            * 0.75
            +
            std::sin(phase * 2.0)
            * 0.20;
    }

    if (instrument == "GUITAR")
    {
        return
            std::sin(phase)
            * 0.70
            +
            std::sin(phase * 2.0)
            * 0.20
            +
            std::sin(phase * 3.0)
            * 0.10;
    }

    if (instrument == "SYNTH")
    {
        double saw =
            2.0 *
            (frequency * t -
             std::floor(
                 frequency * t + 0.5
             ));

        return saw * 0.55;
    }

    if (instrument == "ORGAN")
    {
        return
            std::sin(phase)
            * 0.55
            +
            std::sin(phase * 2.0)
            * 0.25
            +
            std::sin(phase * 3.0)
            * 0.15;
    }

    if (instrument == "FLUTE")
    {
        return
            std::sin(phase)
            * 0.90
            +
            std::sin(phase * 2.0)
            * 0.08;
    }

    if (instrument == "BELL")
    {
        return
            std::sin(phase)
            * std::exp(-2.5 * t)
            +
            std::sin(phase * 2.01)
            * std::exp(-4.0 * t)
            * 0.35;
    }

    // Default = PIANO
    return
        std::sin(phase)
        * 0.80
        +
        std::sin(phase * 2.0)
        * 0.15
        +
        std::sin(phase * 3.0)
        * 0.05;
}

// ============================================================
// DRUMS
// ============================================================

double kick(double t)
{
    if (t < 0 || t >= 0.7)
        return 0.0;

    double frequency =
        150.0 *
        std::exp(-12.0 * t)
        + 45.0;

    double envelope =
        std::exp(-7.0 * t);

    return
        std::sin(
            2.0 * PI *
            frequency *
            t
        )
        * envelope;
}

double snare(double t)
{
    if (t < 0 || t >= 0.35)
        return 0.0;

    double envelope =
        std::exp(-15.0 * t);

    double noisePart =
        noise() * envelope;

    double body =
        std::sin(
            2.0 * PI *
            180.0 *
            t
        )
        *
        std::exp(-20.0 * t);

    return
        noisePart * 0.8
        +
        body * 0.2;
}

double closedHiHat(double t)
{
    if (t < 0 || t >= 0.12)
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
    if (t < 0 || t >= 0.8)
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
    if (t < 0 || t >= 0.3)
        return 0.0;

    double burst1 =
        std::exp(-80.0 * std::abs(t));

    double burst2 =
        std::exp(
            -60.0 *
            std::abs(t - 0.025)
        );

    double burst3 =
        std::exp(
            -50.0 *
            std::abs(t - 0.050)
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
    if (t < 0 || t >= 0.6)
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
            2.0 * PI *
            pitch *
            t
        )
        *
        envelope;
}

double crash(double t)
{
    if (t < 0 || t >= 2.0)
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
    if (t < 0 || t >= 1.5)
        return 0.0;

    double envelope =
        std::exp(-2.0 * t);

    double metallic =
        noise();

    double tone =
        std::sin(
            2.0 * PI *
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
    if (t < 0 || t >= 0.15)
        return 0.0;

    double envelope =
        std::exp(-40.0 * t);

    return
        std::sin(
            2.0 * PI *
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
    if (t < 0 || t >= 0.3)
        return 0.0;

    double envelope =
        std::exp(-15.0 * t);

    double tone1 =
        std::sin(
            2.0 * PI *
            540.0 *
            t
        );

    double tone2 =
        std::sin(
            2.0 * PI *
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
    if (t < 0 || t >= 0.25)
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
    if (t < 0 || t >= 0.7)
        return 0.0;

    double envelope =
        std::exp(-6.0 * t);

    double metal =
        noise();

    double ring =
        std::sin(
            2.0 * PI *
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
        type == "BASS_DRUM")
        return kick(t);

    if (type == "SNARE")
        return snare(t);

    if (type == "HIHAT" ||
        type == "CLOSED_HIHAT")
        return closedHiHat(t);

    if (type == "OPEN_HIHAT")
        return openHiHat(t);

    if (type == "CLAP")
        return clap(t);

    if (type == "LOW_TOM")
        return tom(t, 110.0);

    if (type == "MID_TOM")
        return tom(t, 180.0);

    if (type == "HIGH_TOM")
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
// READ TEXT FROM EDITOR
// ============================================================

std::string GetEditorText(int playerIndex)
{
    if (
        playerIndex < 0 ||
        playerIndex >= PLAYER_COUNT ||
        !songEditor[playerIndex])
    {
        return "";
    }

    int length =
        GetWindowTextLengthA(
            songEditor[playerIndex]
        );

    if (length <= 0)
        return "";

    std::vector<char> buffer(
        length + 1
    );

    GetWindowTextA(
        songEditor[playerIndex],
        buffer.data(),
        length + 1
    );

    return std::string(
        buffer.data()
    );
}

// ============================================================
// LOAD SONG FROM EDITOR TEXT
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

    std::istringstream songFile(text);

    std::string command;

    while (songFile >> command)
    {
        // ----------------------------------------------------
        // TEMPO
        // ----------------------------------------------------

        if (command == "TEMPO")
        {
            songFile >> tempo;

            if (tempo < MIN_TEMPO)
                tempo = MIN_TEMPO;

            if (tempo > MAX_TEMPO)
                tempo = MAX_TEMPO;
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
        //
        // or
        //
        // PIANO C4 0 1
        // BASS C3 0 1
        // GUITAR E4 1 1
        // SYNTH G4 2 1
        // ----------------------------------------------------

        else if (command == "NOTE")
        {
            std::string noteName;
            double startBeat;
            double durationBeats;

            songFile
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
                        "PIANO"
                    }
                );
            }
        }

        else if (
            command == "PIANO" ||
            command == "BASS" ||
            command == "GUITAR" ||
            command == "SYNTH" ||
            command == "ORGAN" ||
            command == "FLUTE" ||
            command == "BELL")
        {
            std::string noteName;
            double startBeat;
            double durationBeats;

            songFile
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
                        command
                    }
                );
            }
        }

        // ----------------------------------------------------
        // DRUM
        // ----------------------------------------------------

        else if (command == "DRUM")
        {
            std::string drumType;
            double startBeat;

            songFile
                >> drumType
                >> startBeat;

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

    double fileTempo;
    double loopLengthBeats;

    if (!LoadSongFromText(
        text,
        notes,
        drums,
        fileTempo,
        loopLengthBeats))
    {
        return false;
    }

    double tempo =
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
    // MELODY
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
                startSample + i;

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
                instrumentWave(
                    note.instrument,
                    shiftedFrequency,
                    time
                );

            audio[index] +=
                wave *
                envelope *
                0.25;
        }
    }

    // ========================================================
    // DRUMS
    //
    // Pitch does NOT affect drums.
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
                startSample + i;

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
// BUTTON IDS
// ============================================================

#define ID_UNIVERSAL_PLAY 900

#define ID_PLAY1         101
#define ID_LOOP1         102
#define ID_TEMPO_MINUS1  103
#define ID_TEMPO_PLUS1   104
#define ID_PITCH_MINUS1  105
#define ID_PITCH_PLUS1   106

#define ID_PLAY2         201
#define ID_LOOP2         202
#define ID_TEMPO_MINUS2  203
#define ID_TEMPO_PLUS2   204
#define ID_PITCH_MINUS2  205
#define ID_PITCH_PLUS2   206

#define ID_PLAY3         301
#define ID_LOOP3         302
#define ID_TEMPO_MINUS3  303
#define ID_TEMPO_PLUS3   304
#define ID_PITCH_MINUS3  305
#define ID_PITCH_PLUS3   306

#define ID_PLAY4         401
#define ID_LOOP4         402
#define ID_TEMPO_MINUS4  403
#define ID_TEMPO_PLUS4   404
#define ID_PITCH_MINUS4  405
#define ID_PITCH_PLUS4   406

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
        "TEMPO: %d",
        tempo
    );

    SetWindowTextA(
        tempoLabel[playerIndex],
        text
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
            "PITCH: +%d",
            pitch
        );
    }
    else
    {
        wsprintfA(
            text,
            "PITCH: %d",
            pitch
        );
    }

    SetWindowTextA(
        pitchLabel[playerIndex],
        text
    );
}

// ============================================================
// UPDATE PLAY BUTTONS
// ============================================================

void UpdatePlayButtons()
{
    for (
        int i = 0;
        i < PLAYER_COUNT;
        ++i)
    {
        if (playButton[i])
        {
            SetWindowTextA(
                playButton[i],
                playing[i]
                ? "STOP"
                : "PLAY"
            );
        }
    }

    if (universalPlayButton)
    {
        SetWindowTextA(
            universalPlayButton,
            universalPlaying
            ? "STOP ALL"
            : "PLAY ALL"
        );
    }
}

// ============================================================
// RESIZE
// ============================================================

void ResizePlayerButtons(
    HWND window)
{
    RECT rect;

    GetClientRect(
        window,
        &rect
    );

    int windowWidth =
        rect.right;

    int columnWidth =
        windowWidth /
        PLAYER_COUNT;

    for (
        int i = 0;
        i < PLAYER_COUNT;
        ++i)
    {
        int x =
            i * columnWidth +
            15;

        int buttonWidth =
            columnWidth -
            30;

        if (buttonWidth < 100)
            buttonWidth = 100;

        MoveWindow(
            playButton[i],
            x,
            75,
            buttonWidth,
            45,
            TRUE
        );

        MoveWindow(
            loopButton[i],
            x,
            125,
            buttonWidth,
            35,
            TRUE
        );

        int smallWidth =
            (buttonWidth - 10) / 2;

        MoveWindow(
            tempoMinusButton[i],
            x,
            170,
            smallWidth,
            35,
            TRUE
        );

        MoveWindow(
            tempoLabel[i],
            x + smallWidth + 5,
            170,
            buttonWidth -
                smallWidth -
                5,
            35,
            TRUE
        );

        MoveWindow(
            tempoPlusButton[i],
            x,
            210,
            buttonWidth,
            35,
            TRUE
        );

        MoveWindow(
            pitchMinusButton[i],
            x,
            250,
            smallWidth,
            35,
            TRUE
        );

        MoveWindow(
            pitchLabel[i],
            x + smallWidth + 5,
            250,
            buttonWidth -
                smallWidth -
                5,
            35,
            TRUE
        );

        MoveWindow(
            pitchPlusButton[i],
            x,
            290,
            buttonWidth,
            35,
            TRUE
        );

        int editorHeight =
            std::max(
                150L,
                static_cast<LONG>(
                    rect.bottom - 340
                )
            );

        MoveWindow(
            songEditor[i],
            x,
            335,
            buttonWidth,
            editorHeight,
            TRUE
        );
    }

    if (universalPlayButton)
    {
        MoveWindow(
            universalPlayButton,
            15,
            5,
            windowWidth - 30,
            45,
            TRUE
        );
    }
}

// ============================================================
// UNIVERSAL START WAIT
// ============================================================

bool WaitForUniversalStart()
{
    std::unique_lock<std::mutex> lock(
        universalMutex
    );

    if (!universalStartRequested)
        return true;

    universalWaitingPlayers++;

    universalCondition.notify_all();

    universalCondition.wait(
        lock,
        []()
        {
            return
                !universalStartRequested;
        }
    );

    if (universalCancelRequested)
        return false;

    return true;
}

// ============================================================
// PLAY SONG
// ============================================================

void PlaySong(
    int playerIndex,
    bool synchronizedStart)
{
    if (
        playerIndex < 0 ||
        playerIndex >= PLAYER_COUNT)
        return;

    const std::string text =
        GetEditorText(playerIndex);

    if (text.empty())
    {
        MessageBoxA(
            mainWindow,
            "The song editor is empty.",
            "Song Error",
            MB_OK | MB_ICONERROR
        );

        playing[playerIndex] = false;

        PostMessage(
            mainWindow,
            WM_USER + playerIndex + 1,
            0,
            0
        );

        return;
    }

    std::vector<NoteEvent> tempNotes;
    std::vector<DrumEvent> tempDrums;

    double fileTempo = 120.0;
    double loopLengthBeats = 4.0;

    LoadSongFromText(
        text,
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

    UpdateTempoDisplay(
        playerIndex
    );

    UpdatePitchDisplay(
        playerIndex
    );

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
        MessageBoxA(
            mainWindow,
            "Could not open audio device.",
            "Audio Error",
            MB_OK | MB_ICONERROR
        );

        playing[playerIndex] = false;

        PostMessage(
            mainWindow,
            WM_USER + playerIndex + 1,
            0,
            0
        );

        return;
    }

    std::vector<short> audioSamples[
        AUDIO_BUFFER_COUNT
    ];

    WAVEHDR headers[
        AUDIO_BUFFER_COUNT
    ] = {};

    bool prepared[
        AUDIO_BUFFER_COUNT
    ] = {};

    bool queued[
        AUDIO_BUFFER_COUNT
    ] = {};

    auto generateBuffer =
        [&](int bufferIndex) -> bool
    {
        double tempo =
            currentTempo[playerIndex].load();

        int pitch =
            currentPitch[playerIndex].load();

        return GenerateAudio(
            text,
            tempo,
            pitch,
            audioSamples[bufferIndex]
        );
    };

    if (!generateBuffer(0) ||
        !generateBuffer(1))
    {
        waveOutClose(
            audioDevice
        );

        playing[playerIndex] = false;

        PostMessage(
            mainWindow,
            WM_USER + playerIndex + 1,
            0,
            0
        );

        return;
    }

    // ========================================================
    // PREPARE BOTH BUFFERS
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
                WM_USER + playerIndex + 1,
                0,
                0
            );

            return;
        }

        prepared[i] = true;
    }

    // ========================================================
    // UNIVERSAL PLAY:
    //
    // All four players reach this point and wait.
    // ========================================================

    if (synchronizedStart)
    {
        if (!WaitForUniversalStart())
        {
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
                }
            }

            waveOutClose(
                audioDevice
            );

            playing[playerIndex] = false;

            PostMessage(
                mainWindow,
                WM_USER + playerIndex + 1,
                0,
                0
            );

            return;
        }
    }

    // ========================================================
    // WRITE FIRST TWO BUFFERS
    // ========================================================

    for (
        int i = 0;
        i < AUDIO_BUFFER_COUNT;
        ++i)
    {
        if (stopRequested[playerIndex])
            break;

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
            stopRequested[playerIndex] = true;
            break;
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
    // CLEAN UP
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
        WM_USER + playerIndex + 1,
        0,
        0
    );
}

// ============================================================
// START ALL PLAYERS
// ============================================================

void StartAllPlayers()
{
    if (universalPlaying)
        return;

    universalPlaying = true;

    {
        std::lock_guard<std::mutex> lock(
            universalMutex
        );

        universalStartRequested = true;
        universalCancelRequested = false;
        universalWaitingPlayers = 0;
    }

    for (
        int i = 0;
        i < PLAYER_COUNT;
        ++i)
    {
        stopRequested[i] = false;
        playing[i] = true;
    }

    UpdatePlayButtons();

    // Start all four threads.

    for (
        int i = 0;
        i < PLAYER_COUNT;
        ++i)
    {
        std::thread(
            PlaySong,
            i,
            true
        ).detach();
    }

    // Give the threads time to reach
    // their synchronization point.

    std::thread(
        []()
        {
            std::unique_lock<std::mutex> lock(
                universalMutex
            );

            universalCondition.wait(
                lock,
                []()
                {
                    return
                        universalWaitingPlayers
                        >= PLAYER_COUNT
                        ||
                        universalCancelRequested;
                }
            );

            if (universalCancelRequested)
            {
                universalStartRequested = false;

                universalCondition.notify_all();

                return;
            }

            // =================================================
            // RELEASE ALL FOUR PLAYERS TOGETHER
            // =================================================

            universalStartRequested = false;

            universalCondition.notify_all();
        }
    ).detach();
}

// ============================================================
// STOP ALL PLAYERS
// ============================================================

void StopAllPlayers()
{
    universalPlaying = false;

    for (
        int i = 0;
        i < PLAYER_COUNT;
        ++i)
    {
        stopRequested[i] = true;
    }

    {
        std::lock_guard<std::mutex> lock(
            universalMutex
        );

        universalCancelRequested = true;
        universalStartRequested = false;
    }

    universalCondition.notify_all();

    UpdatePlayButtons();
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
        // BUTTON TEXT COLOR
        // ====================================================

        case WM_CTLCOLORBTN:
        {
            HDC dc =
                (HDC)wParam;

            SetTextColor(
                dc,
                BUTTON_TEXT
            );

            SetBkMode(
                dc,
                TRANSPARENT
            );

            return (LRESULT)GetStockObject(
                NULL_BRUSH
            );
        }

        // ====================================================
        // STATIC TEXT
        //
        // This also prevents the tempo/pitch digits
        // from ghosting/blurring.
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

            static HBRUSH columnBrush =
                CreateSolidBrush(
                    COLUMN
                );

            return (LRESULT)columnBrush;
        }

        // ====================================================
        // BUTTONS
        // ====================================================

        case WM_COMMAND:
        {
            int button =
                LOWORD(wParam);

            // =================================================
            // UNIVERSAL PLAY
            // =================================================

            if (button == ID_UNIVERSAL_PLAY)
            {
                if (!universalPlaying)
                {
                    StartAllPlayers();
                }
                else
                {
                    StopAllPlayers();
                }

                break;
            }

            // =================================================
            // PLAYER 1
            // =================================================

            if (button == ID_PLAY1)
            {
                int p = 0;

                if (!playing[p])
                {
                    stopRequested[p] = false;
                    playing[p] = true;

                    UpdatePlayButtons();

                    std::thread(
                        PlaySong,
                        p,
                        false
                    ).detach();
                }
                else
                {
                    stopRequested[p] = true;

                    UpdatePlayButtons();
                }
            }

            else if (button == ID_LOOP1)
            {
                int p = 0;

                looping[p] =
                    !looping[p];

                SetWindowTextA(
                    loopButton[p],
                    looping[p]
                    ? "LOOP: ON"
                    : "LOOP: OFF"
                );
            }

            else if (button == ID_TEMPO_MINUS1)
            {
                int p = 0;

                double tempo =
                    currentTempo[p].load();

                tempo -= TEMPO_STEP;

                if (tempo < MIN_TEMPO)
                    tempo = MIN_TEMPO;

                currentTempo[p] = tempo;

                UpdateTempoDisplay(p);
            }

            else if (button == ID_TEMPO_PLUS1)
            {
                int p = 0;

                double tempo =
                    currentTempo[p].load();

                tempo += TEMPO_STEP;

                if (tempo > MAX_TEMPO)
                    tempo = MAX_TEMPO;

                currentTempo[p] = tempo;

                UpdateTempoDisplay(p);
            }

            else if (button == ID_PITCH_MINUS1)
            {
                int p = 0;

                int pitch =
                    currentPitch[p].load();

                pitch -= PITCH_STEP;

                if (pitch < MIN_PITCH)
                    pitch = MIN_PITCH;

                currentPitch[p] = pitch;

                UpdatePitchDisplay(p);
            }

            else if (button == ID_PITCH_PLUS1)
            {
                int p = 0;

                int pitch =
                    currentPitch[p].load();

                pitch += PITCH_STEP;

                if (pitch > MAX_PITCH)
                    pitch = MAX_PITCH;

                currentPitch[p] = pitch;

                UpdatePitchDisplay(p);
            }

            // =================================================
            // PLAYER 2
            // =================================================

            else if (button == ID_PLAY2)
            {
                int p = 1;

                if (!playing[p])
                {
                    stopRequested[p] = false;
                    playing[p] = true;

                    UpdatePlayButtons();

                    std::thread(
                        PlaySong,
                        p,
                        false
                    ).detach();
                }
                else
                {
                    stopRequested[p] = true;

                    UpdatePlayButtons();
                }
            }

            else if (button == ID_LOOP2)
            {
                int p = 1;

                looping[p] =
                    !looping[p];

                SetWindowTextA(
                    loopButton[p],
                    looping[p]
                    ? "LOOP: ON"
                    : "LOOP: OFF"
                );
            }

            else if (button == ID_TEMPO_MINUS2)
            {
                int p = 1;

                double tempo =
                    currentTempo[p].load();

                tempo -= TEMPO_STEP;

                if (tempo < MIN_TEMPO)
                    tempo = MIN_TEMPO;

                currentTempo[p] = tempo;

                UpdateTempoDisplay(p);
            }

            else if (button == ID_TEMPO_PLUS2)
            {
                int p = 1;

                double tempo =
                    currentTempo[p].load();

                tempo += TEMPO_STEP;

                if (tempo > MAX_TEMPO)
                    tempo = MAX_TEMPO;

                currentTempo[p] = tempo;

                UpdateTempoDisplay(p);
            }

            else if (button == ID_PITCH_MINUS2)
            {
                int p = 1;

                int pitch =
                    currentPitch[p].load();

                pitch -= PITCH_STEP;

                if (pitch < MIN_PITCH)
                    pitch = MIN_PITCH;

                currentPitch[p] = pitch;

                UpdatePitchDisplay(p);
            }

            else if (button == ID_PITCH_PLUS2)
            {
                int p = 1;

                int pitch =
                    currentPitch[p].load();

                pitch += PITCH_STEP;

                if (pitch > MAX_PITCH)
                    pitch = MAX_PITCH;

                currentPitch[p] = pitch;

                UpdatePitchDisplay(p);
            }

            // =================================================
            // PLAYER 3
            // =================================================

            else if (button == ID_PLAY3)
            {
                int p = 2;

                if (!playing[p])
                {
                    stopRequested[p] = false;
                    playing[p] = true;

                    UpdatePlayButtons();

                    std::thread(
                        PlaySong,
                        p,
                        false
                    ).detach();
                }
                else
                {
                    stopRequested[p] = true;

                    UpdatePlayButtons();
                }
            }

            else if (button == ID_LOOP3)
            {
                int p = 2;

                looping[p] =
                    !looping[p];

                SetWindowTextA(
                    loopButton[p],
                    looping[p]
                    ? "LOOP: ON"
                    : "LOOP: OFF"
                );
            }

            else if (button == ID_TEMPO_MINUS3)
            {
                int p = 2;

                double tempo =
                    currentTempo[p].load();

                tempo -= TEMPO_STEP;

                if (tempo < MIN_TEMPO)
                    tempo = MIN_TEMPO;

                currentTempo[p] = tempo;

                UpdateTempoDisplay(p);
            }

            else if (button == ID_TEMPO_PLUS3)
            {
                int p = 2;

                double tempo =
                    currentTempo[p].load();

                tempo += TEMPO_STEP;

                if (tempo > MAX_TEMPO)
                    tempo = MAX_TEMPO;

                currentTempo[p] = tempo;

                UpdateTempoDisplay(p);
            }

            else if (button == ID_PITCH_MINUS3)
            {
                int p = 2;

                int pitch =
                    currentPitch[p].load();

                pitch -= PITCH_STEP;

                if (pitch < MIN_PITCH)
                    pitch = MIN_PITCH;

                currentPitch[p] = pitch;

                UpdatePitchDisplay(p);
            }

            else if (button == ID_PITCH_PLUS3)
            {
                int p = 2;

                int pitch =
                    currentPitch[p].load();

                pitch += PITCH_STEP;

                if (pitch > MAX_PITCH)
                    pitch = MAX_PITCH;

                currentPitch[p] = pitch;

                UpdatePitchDisplay(p);
            }

            // =================================================
            // PLAYER 4
            // =================================================

            else if (button == ID_PLAY4)
            {
                int p = 3;

                if (!playing[p])
                {
                    stopRequested[p] = false;
                    playing[p] = true;

                    UpdatePlayButtons();

                    std::thread(
                        PlaySong,
                        p,
                        false
                    ).detach();
                }
                else
                {
                    stopRequested[p] = true;

                    UpdatePlayButtons();
                }
            }

            else if (button == ID_LOOP4)
            {
                int p = 3;

                looping[p] =
                    !looping[p];

                SetWindowTextA(
                    loopButton[p],
                    looping[p]
                    ? "LOOP: ON"
                    : "LOOP: OFF"
                );
            }

            else if (button == ID_TEMPO_MINUS4)
            {
                int p = 3;

                double tempo =
                    currentTempo[p].load();

                tempo -= TEMPO_STEP;

                if (tempo < MIN_TEMPO)
                    tempo = MIN_TEMPO;

                currentTempo[p] = tempo;

                UpdateTempoDisplay(p);
            }

            else if (button == ID_TEMPO_PLUS4)
            {
                int p = 3;

                double tempo =
                    currentTempo[p].load();

                tempo += TEMPO_STEP;

                if (tempo > MAX_TEMPO)
                    tempo = MAX_TEMPO;

                currentTempo[p] = tempo;

                UpdateTempoDisplay(p);
            }

            else if (button == ID_PITCH_MINUS4)
            {
                int p = 3;

                int pitch =
                    currentPitch[p].load();

                pitch -= PITCH_STEP;

                if (pitch < MIN_PITCH)
                    pitch = MIN_PITCH;

                currentPitch[p] = pitch;

                UpdatePitchDisplay(p);
            }

            else if (button == ID_PITCH_PLUS4)
            {
                int p = 3;

                int pitch =
                    currentPitch[p].load();

                pitch += PITCH_STEP;

                if (pitch > MAX_PITCH)
                    pitch = MAX_PITCH;

                currentPitch[p] = pitch;

                UpdatePitchDisplay(p);
            }

            break;
        }

        // ====================================================
        // PLAYBACK FINISHED
        // ====================================================

        case WM_USER + 1:
        case WM_USER + 2:
        case WM_USER + 3:
        case WM_USER + 4:
        {
            int playerIndex =
                static_cast<int>(
                    message -
                    (WM_USER + 1)
                );

            if (
                playerIndex >= 0 &&
                playerIndex < PLAYER_COUNT)
            {
                playing[playerIndex] =
                    false;

                SetWindowTextA(
                    playButton[playerIndex],
                    "PLAY"
                );
            }

            bool anyonePlaying = false;

            for (
                int i = 0;
                i < PLAYER_COUNT;
                ++i)
            {
                if (playing[i])
                {
                    anyonePlaying = true;
                    break;
                }
            }

            if (!anyonePlaying)
            {
                universalPlaying = false;

                if (universalPlayButton)
                {
                    SetWindowTextA(
                        universalPlayButton,
                        "PLAY ALL"
                    );
                }
            }

            break;
        }

        // ====================================================
        // RESIZE
        // ====================================================

        case WM_SIZE:
        {
            ResizePlayerButtons(
                window
            );

            InvalidateRect(
                window,
                nullptr,
                TRUE
            );

            break;
        }

        // ====================================================
        // PAINT
        // ====================================================

        case WM_PAINT:
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

            int columnWidth =
                rect.right /
                PLAYER_COUNT;

            for (
                int i = 0;
                i < PLAYER_COUNT;
                ++i)
            {
                RECT columnRect =
                {
                    i * columnWidth + 5,
                    55,
                    (i + 1) *
                        columnWidth - 5,
                    rect.bottom - 5
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

                HBRUSH oldBrush =
                    (HBRUSH)SelectObject(
                        dc,
                        GetStockObject(
                            NULL_BRUSH
                        )
                    );

                Rectangle(
                    dc,
                    columnRect.left,
                    columnRect.top,
                    columnRect.right,
                    columnRect.bottom
                );

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
            }

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
                    26,
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

            const char* titles[4] =
            {
                "I",
                "II",
                "III",
                "IV"
            };

            for (
                int i = 0;
                i < PLAYER_COUNT;
                ++i)
            {
                RECT titleRect =
                {
                    i * columnWidth,
                    60,
                    (i + 1) *
                        columnWidth,
                    100
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

            break;
        }

        // ====================================================
        // DESTROY
        // ====================================================

        case WM_DESTROY:
        {
            for (
                int i = 0;
                i < PLAYER_COUNT;
                ++i)
            {
                stopRequested[i] = true;
            }

            {
                std::lock_guard<std::mutex> lock(
                    universalMutex
                );

                universalCancelRequested = true;
                universalStartRequested = false;
            }

            universalCondition.notify_all();

            PostQuitMessage(0);

            break;
        }

        default:
        {
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
        "CppSongMaker";

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
            "C++ Song Maker",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1200,
            800,
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
    // INITIAL VALUES
    // ========================================================

    for (
        int i = 0;
        i < PLAYER_COUNT;
        ++i)
    {
        currentTempo[i] =
            120.0;

        currentPitch[i] =
            0;

        playing[i] =
            false;

        looping[i] =
            false;

        stopRequested[i] =
            false;
    }

    // ========================================================
    // UNIVERSAL PLAY BUTTON
    // ========================================================

    universalPlayButton =
        CreateWindowA(
            "BUTTON",
            "PLAY ALL",
            WS_VISIBLE |
            WS_CHILD |
            BS_PUSHBUTTON,
            15,
            5,
            100,
            45,
            window,
            (HMENU)(INT_PTR)
                ID_UNIVERSAL_PLAY,
            instance,
            nullptr
        );

    // ========================================================
    // PLAYER CONTROLS
    // ========================================================

    for (
        int i = 0;
        i < PLAYER_COUNT;
        ++i)
    {
        int playID;
        int loopID;
        int tempoMinusID;
        int tempoPlusID;
        int pitchMinusID;
        int pitchPlusID;

        if (i == 0)
        {
            playID =
                ID_PLAY1;

            loopID =
                ID_LOOP1;

            tempoMinusID =
                ID_TEMPO_MINUS1;

            tempoPlusID =
                ID_TEMPO_PLUS1;

            pitchMinusID =
                ID_PITCH_MINUS1;

            pitchPlusID =
                ID_PITCH_PLUS1;
        }
        else if (i == 1)
        {
            playID =
                ID_PLAY2;

            loopID =
                ID_LOOP2;

            tempoMinusID =
                ID_TEMPO_MINUS2;

            tempoPlusID =
                ID_TEMPO_PLUS2;

            pitchMinusID =
                ID_PITCH_MINUS2;

            pitchPlusID =
                ID_PITCH_PLUS2;
        }
        else if (i == 2)
        {
            playID =
                ID_PLAY3;

            loopID =
                ID_LOOP3;

            tempoMinusID =
                ID_TEMPO_MINUS3;

            tempoPlusID =
                ID_TEMPO_PLUS3;

            pitchMinusID =
                ID_PITCH_MINUS3;

            pitchPlusID =
                ID_PITCH_PLUS3;
        }
        else
        {
            playID =
                ID_PLAY4;

            loopID =
                ID_LOOP4;

            tempoMinusID =
                ID_TEMPO_MINUS4;

            tempoPlusID =
                ID_TEMPO_PLUS4;

            pitchMinusID =
                ID_PITCH_MINUS4;

            pitchPlusID =
                ID_PITCH_PLUS4;
        }

        // ====================================================
        // PLAY
        // ====================================================

        playButton[i] =
            CreateWindowA(
                "BUTTON",
                "PLAY",
                WS_VISIBLE |
                WS_CHILD |
                BS_PUSHBUTTON,
                0,
                75,
                100,
                45,
                window,
                (HMENU)(INT_PTR)
                    playID,
                instance,
                nullptr
            );

        // ====================================================
        // LOOP
        // ====================================================

        loopButton[i] =
            CreateWindowA(
                "BUTTON",
                "LOOP: OFF",
                WS_VISIBLE |
                WS_CHILD |
                BS_PUSHBUTTON,
                0,
                125,
                100,
                35,
                window,
                (HMENU)(INT_PTR)
                    loopID,
                instance,
                nullptr
            );

        // ====================================================
        // TEMPO MINUS
        // ====================================================

        tempoMinusButton[i] =
            CreateWindowA(
                "BUTTON",
                "-",
                WS_VISIBLE |
                WS_CHILD |
                BS_PUSHBUTTON,
                0,
                170,
                40,
                35,
                window,
                (HMENU)(INT_PTR)
                    tempoMinusID,
                instance,
                nullptr
            );

        // ====================================================
        // TEMPO LABEL
        // ====================================================

        tempoLabel[i] =
            CreateWindowA(
                "STATIC",
                "TEMPO: 120",
                WS_VISIBLE |
                WS_CHILD |
                SS_CENTER |
                SS_CENTERIMAGE,
                0,
                170,
                100,
                35,
                window,
                nullptr,
                instance,
                nullptr
            );

        // ====================================================
        // TEMPO PLUS
        // ====================================================

        tempoPlusButton[i] =
            CreateWindowA(
                "BUTTON",
                "+",
                WS_VISIBLE |
                WS_CHILD |
                BS_PUSHBUTTON,
                0,
                210,
                100,
                35,
                window,
                (HMENU)(INT_PTR)
                    tempoPlusID,
                instance,
                nullptr
            );

        // ====================================================
        // PITCH MINUS
        // ====================================================

        pitchMinusButton[i] =
            CreateWindowA(
                "BUTTON",
                "-",
                WS_VISIBLE |
                WS_CHILD |
                BS_PUSHBUTTON,
                0,
                250,
                40,
                35,
                window,
                (HMENU)(INT_PTR)
                    pitchMinusID,
                instance,
                nullptr
            );

        // ====================================================
        // PITCH LABEL
        // ====================================================

        pitchLabel[i] =
            CreateWindowA(
                "STATIC",
                "PITCH: 0",
                WS_VISIBLE |
                WS_CHILD |
                SS_CENTER |
                SS_CENTERIMAGE,
                0,
                250,
                100,
                35,
                window,
                nullptr,
                instance,
                nullptr
            );

        // ====================================================
        // PITCH PLUS
        // ====================================================

        pitchPlusButton[i] =
            CreateWindowA(
                "BUTTON",
                "+",
                WS_VISIBLE |
                WS_CHILD |
                BS_PUSHBUTTON,
                0,
                290,
                100,
                35,
                window,
                (HMENU)(INT_PTR)
                    pitchPlusID,
                instance,
                nullptr
            );

        // ====================================================
        // SONG TEXT EDITOR
        //
        // ES_MULTILINE makes the song appear vertically,
        // one command per line.
        //
        // WS_VSCROLL gives you a vertical scrollbar.
        //
        // ES_AUTOVSCROLL keeps scrolling downward as
        // additional lines are entered.
        // ====================================================

        const char* defaultSong =
            "TEMPO 120\r\n"
            "LENGTH 4\r\n"
            "\r\n"
            "PIANO C4 0 1\r\n"
            "PIANO E4 1 1\r\n"
            "PIANO G4 2 1\r\n"
            "PIANO C5 3 1\r\n"
            "\r\n"
            "DRUM KICK 0\r\n"
            "DRUM HIHAT 0\r\n"
            "DRUM HIHAT 0.5\r\n"
            "DRUM KICK 1\r\n"
            "DRUM HIHAT 1\r\n"
            "DRUM HIHAT 1.5\r\n"
            "DRUM KICK 2\r\n"
            "DRUM HIHAT 2\r\n"
            "DRUM HIHAT 2.5\r\n"
            "DRUM KICK 3\r\n"
            "DRUM HIHAT 3\r\n"
            "DRUM HIHAT 3.5\r\n";

        songEditor[i] =
            CreateWindowExA(
                WS_EX_CLIENTEDGE,
                "EDIT",
                defaultSong,
                WS_VISIBLE |
                WS_CHILD |
                WS_VSCROLL |
                ES_MULTILINE |
                ES_AUTOVSCROLL |
                ES_WANTRETURN,
                0,
                335,
                100,
                200,
                window,
                nullptr,
                instance,
                nullptr
            );

        // ====================================================
        // EDITOR FONT
        // ====================================================

        HFONT editorFont =
            CreateFontA(
                16,
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
                FIXED_PITCH,
                "Consolas"
            );

        SendMessage(
            songEditor[i],
            WM_SETFONT,
            (WPARAM)editorFont,
            TRUE
        );

        // ====================================================
        // SET SOME MARGINS
        // ====================================================

        SendMessage(
            songEditor[i],
            EM_SETMARGINS,
            EC_LEFTMARGIN |
            EC_RIGHTMARGIN,
            MAKELONG(8, 8)
        );
    }

    // ========================================================
    // POSITION EVERYTHING
    // ========================================================

    ResizePlayerButtons(
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
