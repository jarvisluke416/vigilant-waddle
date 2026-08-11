#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <atomic>
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

const COLORREF TEXT_COLOR = RGB(255, 255, 255);
const COLORREF BUTTON_TEXT = RGB(0, 255, 0);

const COLORREF EDIT_BACKGROUND = RGB(15, 15, 20);
const COLORREF EDIT_TEXT = RGB(255, 255, 255);

// ============================================================
// PLAYER STATE
// ============================================================

std::atomic<bool> playing[PLAYER_COUNT];
std::atomic<bool> looping[PLAYER_COUNT];
std::atomic<bool> stopRequested[PLAYER_COUNT];

std::atomic<double> currentTempo[PLAYER_COUNT];
std::atomic<int> currentPitch[PLAYER_COUNT];

// ============================================================
// WINDOWS CONTROLS
// ============================================================

HWND playButton[PLAYER_COUNT] = {};
HWND loopButton[PLAYER_COUNT] = {};

HWND tempoMinusButton[PLAYER_COUNT] = {};
HWND tempoPlusButton[PLAYER_COUNT] = {};
HWND tempoLabel[PLAYER_COUNT] = {};

HWND pitchMinusButton[PLAYER_COUNT] = {};
HWND pitchPlusButton[PLAYER_COUNT] = {};
HWND pitchLabel[PLAYER_COUNT] = {};

HWND songEditor[PLAYER_COUNT] = {};

HWND mainWindow = nullptr;
HWND scrollWindow = nullptr;

// ============================================================
// SCROLLING
// ============================================================

int scrollY = 0;
int contentHeight = 0;

const int TOP_BAR_HEIGHT = 75;

const int SECTION_WIDTH = 260;
const int SECTION_HEIGHT = 520;

const int GRID_GAP = 10;

const int EDIT_HEIGHT = 270;

// ============================================================
// UNIVERSAL PLAY SYNCHRONIZATION
// ============================================================

std::mutex startMutex;
std::condition_variable startCondition;

int universalWaiting = 0;
bool universalStarting = false;

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
// INSTRUMENT WAVEFORM
// ============================================================

double instrumentWave(
    const std::string& instrument,
    double frequency,
    double t)
{
    double phase =
        2.0 *
        PI *
        frequency *
        t;

    if (instrument == "PIANO")
    {
        double fundamental =
            std::sin(phase);

        double harmonic2 =
            std::sin(phase * 2.0) * 0.35;

        double harmonic3 =
            std::sin(phase * 3.0) * 0.15;

        return
            fundamental +
            harmonic2 +
            harmonic3;
    }

    if (instrument == "BASS")
    {
        return
            std::sin(phase) * 0.8 +
            std::sin(phase * 2.0) * 0.2;
    }

    if (instrument == "GUITAR")
    {
        return
            std::sin(phase) * 0.65 +
            std::sin(phase * 2.0) * 0.25 +
            std::sin(phase * 3.0) * 0.10;
    }

    if (instrument == "SYNTH")
    {
        double saw =
            2.0 *
            (
                frequency * t -
                std::floor(
                    frequency * t +
                    0.5
                )
            );

        return saw;
    }

    if (instrument == "ORGAN")
    {
        return
            std::sin(phase) * 0.7 +
            std::sin(phase * 2.0) * 0.25 +
            std::sin(phase * 4.0) * 0.10;
    }

    if (instrument == "FLUTE")
    {
        return
            std::sin(phase) * 0.85 +
            std::sin(phase * 2.0) * 0.10;
    }

    if (instrument == "TRUMPET")
    {
        return
            std::sin(phase) * 0.55 +
            std::sin(phase * 2.0) * 0.30 +
            std::sin(phase * 3.0) * 0.20;
    }

    if (instrument == "BELL")
    {
        return
            std::sin(phase) +
            std::sin(phase * 2.71) * 0.4 +
            std::sin(phase * 4.13) * 0.2;
    }

    // Default instrument
    return std::sin(phase);
}

// ============================================================
// DRUM SOUNDS
// ============================================================

double kick(double t)
{
    if (t < 0 || t >= 0.7)
        return 0.0;

    double frequency =
        150.0 * std::exp(-12.0 * t) + 45.0;

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
    if (t < 0 || t >= 0.35)
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
    if (t < 0 || t >= 0.12)
        return 0.0;

    return
        noise() *
        std::exp(-45.0 * t) *
        0.7;
}

double openHiHat(double t)
{
    if (t < 0 || t >= 0.8)
        return 0.0;

    return
        noise() *
        std::exp(-5.0 * t) *
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
        noise() *
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
        std::exp(-3.0 * t) +
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
    if (t < 0 || t >= 2.0)
        return 0.0;

    return
        noise() *
        std::exp(-2.5 * t) *
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
        envelope *
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
            2.0 *
            PI *
            1200.0 *
            t
        )
        *
        envelope *
        0.8
        +
        noise() *
        envelope *
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
        envelope *
        0.4;
}

double shaker(double t)
{
    if (t < 0 || t >= 0.25)
        return 0.0;

    return
        noise() *
        std::exp(-18.0 * t) *
        0.5;
}

double tambourine(double t)
{
    if (t < 0 || t >= 0.7)
        return 0.0;

    double envelope =
        std::exp(-6.0 * t);

    return
        noise() *
        envelope *
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

    if (
        type == "HIHAT" ||
        type == "CLOSED_HIHAT"
    )
        return closedHiHat(t);

    if (
        type == "OPEN_HIHAT" ||
        type == "OPENHIHAT"
    )
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
// UPPERCASE
// ============================================================

std::string upper(
    std::string value)
{
    for (char& c : value)
    {
        if (c >= 'a' && c <= 'z')
            c =
                static_cast<char>(
                    c - 'a' + 'A'
                );
    }

    return value;
}

// ============================================================
// LOAD SONG FROM EDITOR TEXT
// ============================================================

bool LoadSongText(
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

    std::istringstream input(text);

    std::string command;

    while (input >> command)
    {
        command = upper(command);

        if (command == "TEMPO")
        {
            input >> tempo;

            tempo =
                std::max(
                    MIN_TEMPO,
                    std::min(
                        MAX_TEMPO,
                        tempo
                    )
                );
        }

        else if (command == "LENGTH")
        {
            input >> loopLengthBeats;

            if (loopLengthBeats <= 0.0)
                loopLengthBeats = 4.0;
        }

        else if (command == "NOTE")
        {
            std::string noteName;
            double startBeat;
            double durationBeats;

            input
                >> noteName
                >> startBeat
                >> durationBeats;

            double frequency =
                noteFrequency(
                    upper(noteName)
                );

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

        else if (command == "PIANO" ||
         command == "BASS" ||
         command == "GUITAR" ||
         command == "SYNTH" ||
         command == "ORGAN" ||
         command == "FLUTE" ||
         command == "TRUMPET" ||
         command == "BELL")
{
    std::string noteName;
    double startBeat;
    double durationBeats;

    input
        >> noteName
        >> startBeat
        >> durationBeats;

    double frequency =
        noteFrequency(
            upper(noteName)
        );

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

        else if (command == "DRUM")
        {
            std::string drumType;
            double startBeat;

            input
                >> drumType
                >> startBeat;

            drums.push_back(
                {
                    startBeat,
                    upper(drumType)
                }
            );
        }
    }

    return true;
}

// ============================================================
// GET EDITOR TEXT
// ============================================================

std::string GetEditorText(
    int playerIndex)
{
    if (
        playerIndex < 0 ||
        playerIndex >= PLAYER_COUNT)
    {
        return "";
    }

    HWND editor =
        songEditor[playerIndex];

    if (!editor)
        return "";

    int length =
        GetWindowTextLengthA(editor);

    if (length <= 0)
        return "";

    std::string text(
        length,
        '\0'
    );

    GetWindowTextA(
        editor,
        &text[0],
        length + 1
    );

    return text;
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

    if (!LoadSongText(
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
            static_cast<double>(pitch) /
            12.0
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
                static_cast<double>(i) /
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
                0.18;
        }
    }

    // ========================================================
    // DRUMS
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
                static_cast<double>(i) /
                SAMPLE_RATE;

            audio[index] +=
                makeDrum(
                    drum.type,
                    time
                )
                *
                0.45;
        }
    }

    // ========================================================
    // CONVERT TO PCM
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

#define ID_UNIVERSAL_PLAY 9000

#define ID_PLAY_BASE         1000
#define ID_LOOP_BASE         1100
#define ID_TEMPO_MINUS_BASE  1200
#define ID_TEMPO_PLUS_BASE   1300
#define ID_PITCH_MINUS_BASE  1400
#define ID_PITCH_PLUS_BASE   1500

// ============================================================
// EDIT CONTROL COLORS
// ============================================================

HBRUSH editBrush = nullptr;

// ============================================================
// UPDATE TEMPO
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

// ============================================================
// UPDATE PITCH
// ============================================================

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
// CREATE BUTTON ID
// ============================================================

int PlayerButtonID(
    int base,
    int player)
{
    return base + player;
}

// ============================================================
// CONTENT HEIGHT
// ============================================================

int CalculateContentHeight(
    RECT rect)
{
    int rowHeight =
        SECTION_HEIGHT +
        GRID_GAP;

    int height =
        TOP_BAR_HEIGHT +
        GRID_ROWS *
        rowHeight +
        30;

    return (height > rect.bottom)
    ? height
    : rect.bottom;
}

// ============================================================
// UPDATE SCROLL BAR
// ============================================================

void UpdateScrollBar(
    HWND window)
{
    RECT rect;

    GetClientRect(
        window,
        &rect
    );

    contentHeight =
        CalculateContentHeight(
            rect
        );

    int visibleHeight =
        rect.bottom -
        TOP_BAR_HEIGHT;

    if (visibleHeight < 1)
        visibleHeight = 1;

    SCROLLINFO si = {};

    si.cbSize =
        sizeof(SCROLLINFO);

    si.fMask =
        SIF_RANGE |
        SIF_PAGE |
        SIF_POS;

    si.nMin = 0;

    si.nMax =
        std::max(
            0,
            contentHeight -
            TOP_BAR_HEIGHT -
            1
        );

    si.nPage =
        static_cast<UINT>(
            visibleHeight
        );

    si.nPos =
        scrollY;

    SetScrollInfo(
        window,
        SB_VERT,
        &si,
        TRUE
    );
}

// ============================================================
// MOVE PLAYER CONTROLS
// ============================================================

void ResizePlayerControls(
    HWND window)
{
    RECT rect;

    GetClientRect(
        window,
        &rect
    );

    int availableWidth =
        rect.right;

    int columnWidth =
        availableWidth /
        GRID_COLUMNS;

    if (columnWidth < SECTION_WIDTH)
        columnWidth = SECTION_WIDTH;

    for (
        int i = 0;
        i < PLAYER_COUNT;
        ++i)
    {
        int row =
            i / GRID_COLUMNS;

        int column =
            i % GRID_COLUMNS;

        int x =
            column *
            columnWidth +
            GRID_GAP;

        int y =
            TOP_BAR_HEIGHT +
            row *
            (SECTION_HEIGHT + GRID_GAP) -
            scrollY;

        int sectionWidth =
            columnWidth -
            GRID_GAP * 2;

        if (sectionWidth < 180)
            sectionWidth = 180;

        int buttonWidth =
            sectionWidth -
            20;

        // ----------------------------------------------------
        // PLAY
        // ----------------------------------------------------

        MoveWindow(
            playButton[i],
            x + 10,
            y + 45,
            buttonWidth,
            42,
            TRUE
        );

        // ----------------------------------------------------
        // LOOP
        // ----------------------------------------------------

        MoveWindow(
            loopButton[i],
            x + 10,
            y + 92,
            buttonWidth,
            35,
            TRUE
        );

        // ----------------------------------------------------
        // TEMPO MINUS
        // ----------------------------------------------------

        MoveWindow(
            tempoMinusButton[i],
            x + 10,
            y + 135,
            40,
            35,
            TRUE
        );

        // ----------------------------------------------------
        // TEMPO LABEL
        // ----------------------------------------------------

        MoveWindow(
            tempoLabel[i],
            x + 55,
            y + 135,
            buttonWidth - 45,
            35,
            TRUE
        );

        // ----------------------------------------------------
        // TEMPO PLUS
        // ----------------------------------------------------

        MoveWindow(
            tempoPlusButton[i],
            x + 10,
            y + 175,
            buttonWidth,
            35,
            TRUE
        );

        // ----------------------------------------------------
        // PITCH MINUS
        // ----------------------------------------------------

        MoveWindow(
            pitchMinusButton[i],
            x + 10,
            y + 217,
            40,
            35,
            TRUE
        );

        // ----------------------------------------------------
        // PITCH LABEL
        // ----------------------------------------------------

        MoveWindow(
            pitchLabel[i],
            x + 55,
            y + 217,
            buttonWidth - 45,
            35,
            TRUE
        );

        // ----------------------------------------------------
        // PITCH PLUS
        // ----------------------------------------------------

        MoveWindow(
            pitchPlusButton[i],
            x + 10,
            y + 257,
            buttonWidth,
            35,
            TRUE
        );

        // ----------------------------------------------------
        // TEXT EDITOR
        // ----------------------------------------------------

        MoveWindow(
            songEditor[i],
            x + 10,
            y + 300,
            buttonWidth,
            EDIT_HEIGHT,
            TRUE
        );
    }

    UpdateScrollBar(
        window
    );
}

// ============================================================
// DRAW PLAYER SECTIONS
// ============================================================

void DrawPlayerSections(
    HDC dc,
    RECT rect)
{
    int columnWidth =
        rect.right /
        GRID_COLUMNS;

    for (
        int i = 0;
        i < PLAYER_COUNT;
        ++i)
    {
        int row =
            i / GRID_COLUMNS;

        int column =
            i % GRID_COLUMNS;

        int x =
            column *
            columnWidth +
            GRID_GAP;

        int y =
            TOP_BAR_HEIGHT +
            row *
            (SECTION_HEIGHT + GRID_GAP) -
            scrollY;

        RECT section =
        {
            x,
            y,
            x + columnWidth -
                GRID_GAP * 2,
            y + SECTION_HEIGHT
        };

        // Skip completely invisible sections
        if (
            section.bottom < TOP_BAR_HEIGHT ||
            section.top > rect.bottom)
        {
            continue;
        }

        HBRUSH brush =
            CreateSolidBrush(
                COLUMN
            );

        FillRect(
            dc,
            &section,
            brush
        );

        DeleteObject(
            brush
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
            section.left,
            section.top,
            section.right,
            section.bottom
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
}

// ============================================================
// PLAY ONE PLAYER
// ============================================================

void PlaySong(
    int playerIndex,
    bool synchronizeStart = false)
{
    if (
        playerIndex < 0 ||
        playerIndex >= PLAYER_COUNT)
        return;

    if (playing[playerIndex])
        return;

    playing[playerIndex] = true;
    stopRequested[playerIndex] = false;

    std::string songText =
        GetEditorText(
            playerIndex
        );

    if (songText.empty())
    {
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
    // SYNCHRONIZE UNIVERSAL PLAY
    // ========================================================

    if (synchronizeStart)
    {
        std::unique_lock<std::mutex> lock(
            startMutex
        );

        ++universalWaiting;

        if (universalWaiting >= PLAYER_COUNT)
        {
            universalStarting = true;

            startCondition.notify_all();
        }
        else
        {
            startCondition.wait(
                lock,
                []()
                {
                    return universalStarting;
                }
            );
        }
    }

    // ========================================================
    // LOAD SONG DATA
    // ========================================================

    std::vector<NoteEvent> notes;
    std::vector<DrumEvent> drums;

    double fileTempo = 120.0;
    double loopLengthBeats = 4.0;

    LoadSongText(
        songText,
        notes,
        drums,
        fileTempo,
        loopLengthBeats
    );

    double tempo =
        currentTempo[playerIndex].load();

    if (tempo <= 0.0)
        tempo = fileTempo;

    currentTempo[playerIndex] =
        tempo;

    UpdateTempoDisplay(
        playerIndex
    );

    UpdatePitchDisplay(
        playerIndex
    );

    // ========================================================
    // AUDIO FORMAT
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
        double currentT =
            currentTempo[playerIndex].load();

        int currentP =
            currentPitch[playerIndex].load();

        return GenerateAudio(
            songText,
            currentT,
            currentP,
            audioSamples[
                bufferIndex
            ]
        );
    };

    // ========================================================
    // FIRST BUFFER
    // ========================================================

    if (!generateBuffer(0))
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
    // SECOND BUFFER
    // ========================================================

    if (!generateBuffer(1))
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
                audioSamples[i].size() *
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
                WM_USER + playerIndex + 1,
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
                WM_USER + playerIndex + 1,
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
                stopRequested[playerIndex] =
                    true;

                continue;
            }

            headers[i] = {};

            headers[i].lpData =
                reinterpret_cast<LPSTR>(
                    audioSamples[i].data()
                );

            headers[i].dwBufferLength =
                static_cast<DWORD>(
                    audioSamples[i].size() *
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
                stopRequested[playerIndex] =
                    true;

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

                stopRequested[playerIndex] =
                    true;

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
            Sleep(1);
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
        WM_USER + playerIndex + 1,
        0,
        0
    );
}

// ============================================================
// START ALL 16
// ============================================================

void StartUniversalPlay()
{
    {
        std::lock_guard<std::mutex> lock(
            startMutex
        );

        universalWaiting = 0;
        universalStarting = false;
    }

    for (
        int i = 0;
        i < PLAYER_COUNT;
        ++i)
    {
        if (!playing[i])
        {
            stopRequested[i] = false;

            SetWindowTextA(
                playButton[i],
                "STOP"
            );

            std::thread(
                PlaySong,
                i,
                true
            ).detach();
        }
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

            SetBkMode(
                dc,
                TRANSPARENT
            );

            return (LRESULT)
                GetStockObject(
                    NULL_BRUSH
                );
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

            return (LRESULT)
                editBrush;
        }

        // ====================================================
        // STATIC COLOR
        // ====================================================

        case WM_CTLCOLORSTATIC:
        {
            HDC dc =
                (HDC)wParam;

            SetTextColor(
                dc,
                TEXT_COLOR
            );

            SetBkMode(
                dc,
                TRANSPARENT
            );

            return (LRESULT)
                GetStockObject(
                    NULL_BRUSH
                );
        }

        // ====================================================
        // COMMAND
        // ====================================================

        case WM_COMMAND:
        {
            int id =
                LOWORD(wParam);

            // ------------------------------------------------
            // UNIVERSAL PLAY
            // ------------------------------------------------

            if (id == ID_UNIVERSAL_PLAY)
            {
                StartUniversalPlay();

                break;
            }

            // ------------------------------------------------
            // PLAYER CONTROLS
            // ------------------------------------------------

            for (
                int p = 0;
                p < PLAYER_COUNT;
                ++p)
            {
                if (
                    id ==
                    PlayerButtonID(
                        ID_PLAY_BASE,
                        p
                    ))
                {
                    if (!playing[p])
                    {
                        stopRequested[p] =
                            false;

                        SetWindowTextA(
                            playButton[p],
                            "STOP"
                        );

                        std::thread(
                            PlaySong,
                            p,
                            false
                        ).detach();
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

                if (
                    id ==
                    PlayerButtonID(
                        ID_LOOP_BASE,
                        p
                    ))
                {
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

                if (
                    id ==
                    PlayerButtonID(
                        ID_TEMPO_MINUS_BASE,
                        p
                    ))
                {
                    double tempo =
                        currentTempo[p].load();

                    tempo -=
                        TEMPO_STEP;

                    tempo =
                        std::max(
                            MIN_TEMPO,
                            tempo
                        );

                    currentTempo[p] =
                        tempo;

                    UpdateTempoDisplay(p);

                    break;
                }

                if (
                    id ==
                    PlayerButtonID(
                        ID_TEMPO_PLUS_BASE,
                        p
                    ))
                {
                    double tempo =
                        currentTempo[p].load();

                    tempo +=
                        TEMPO_STEP;

                    tempo =
                        std::min(
                            MAX_TEMPO,
                            tempo
                        );

                    currentTempo[p] =
                        tempo;

                    UpdateTempoDisplay(p);

                    break;
                }

                if (
                    id ==
                    PlayerButtonID(
                        ID_PITCH_MINUS_BASE,
                        p
                    ))
                {
                    int pitch =
                        currentPitch[p].load();

                    pitch -=
                        PITCH_STEP;

                    pitch =
                        std::max(
                            MIN_PITCH,
                            pitch
                        );

                    currentPitch[p] =
                        pitch;

                    UpdatePitchDisplay(p);

                    break;
                }

                if (
                    id ==
                    PlayerButtonID(
                        ID_PITCH_PLUS_BASE,
                        p
                    ))
                {
                    int pitch =
                        currentPitch[p].load();

                    pitch +=
                        PITCH_STEP;

                    pitch =
                        std::min(
                            MAX_PITCH,
                            pitch
                        );

                    currentPitch[p] =
                        pitch;

                    UpdatePitchDisplay(p);

                    break;
                }
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
        case WM_USER + 5:
        case WM_USER + 6:
        case WM_USER + 7:
        case WM_USER + 8:
        case WM_USER + 9:
        case WM_USER + 10:
        case WM_USER + 11:
        case WM_USER + 12:
        case WM_USER + 13:
        case WM_USER + 14:
        case WM_USER + 15:
        case WM_USER + 16:
        {
            int player =
                static_cast<int>(
                    message -
                    WM_USER -
                    1
                );

            if (
                player >= 0 &&
                player < PLAYER_COUNT)
            {
                SetWindowTextA(
                    playButton[player],
                    "PLAY"
                );
            }

            break;
        }

        // ====================================================
        // VERTICAL SCROLL
        // ====================================================

        case WM_VSCROLL:
        {
            SCROLLINFO si = {};

            si.cbSize =
                sizeof(SCROLLINFO);

            si.fMask =
                SIF_ALL;

            GetScrollInfo(
                window,
                SB_VERT,
                &si
            );

            int oldPos =
                si.nPos;

            int newPos =
                oldPos;

            switch (
                LOWORD(wParam))
            {
                case SB_LINEUP:
                    newPos -= 40;
                    break;

                case SB_LINEDOWN:
                    newPos += 40;
                    break;

                case SB_PAGEUP:
                    newPos -=
                        static_cast<int>(
                            si.nPage
                        );
                    break;

                case SB_PAGEDOWN:
                    newPos +=
                        static_cast<int>(
                            si.nPage
                        );
                    break;

                case SB_THUMBTRACK:
                    newPos =
                        si.nTrackPos;
                    break;

                case SB_TOP:
                    newPos =
                        si.nMin;
                    break;

                case SB_BOTTOM:
                    newPos =
                        si.nMax;
                    break;

                default:
                    break;
            }

            newPos =
                std::max(
                    si.nMin,
                    std::min(
                        newPos,
                        si.nMax -
                        static_cast<int>(
                            si.nPage
                        ) +
                        1
                    )
                );

            if (newPos != oldPos)
            {
                scrollY =
                    newPos;

                SetScrollPos(
                    window,
                    SB_VERT,
                    scrollY,
                    TRUE
                );

                ResizePlayerControls(
                    window
                );

                InvalidateRect(
                    window,
                    nullptr,
                    TRUE
                );
            }

            break;
        }

        // ====================================================
        // MOUSE WHEEL
        // ====================================================

        case WM_MOUSEWHEEL:
        {
            int delta =
                GET_WHEEL_DELTA_WPARAM(
                    wParam
                );

            SCROLLINFO si = {};

            si.cbSize =
                sizeof(SCROLLINFO);

            si.fMask =
                SIF_ALL;

            GetScrollInfo(
                window,
                SB_VERT,
                &si
            );

            int amount =
                delta > 0
                ? -80
                : 80;

            int newPos =
                si.nPos +
                amount;

            int maxPos =
                si.nMax -
                static_cast<int>(
                    si.nPage
                ) +
                1;

            if (maxPos < 0)
                maxPos = 0;

            newPos =
                std::max(
                    0,
                    std::min(
                        newPos,
                        maxPos
                    )
                );

            scrollY =
                newPos;

            SetScrollPos(
                window,
                SB_VERT,
                scrollY,
                TRUE
            );

            ResizePlayerControls(
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
        // RESIZE
        // ====================================================

        case WM_SIZE:
        {
            ResizePlayerControls(
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

            // ------------------------------------------------
            // BACKGROUND
            // ------------------------------------------------

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

            // ------------------------------------------------
            // PLAYER SECTIONS
            // ------------------------------------------------

            DrawPlayerSections(
                dc,
                rect
            );

            // ------------------------------------------------
            // TOP BAR
            // ------------------------------------------------

            RECT topBar =
            {
                0,
                0,
                rect.right,
                TOP_BAR_HEIGHT
            };

            HBRUSH topBrush =
                CreateSolidBrush(
                    RGB(10, 10, 14)
                );

            FillRect(
                dc,
                &topBar,
                topBrush
            );

            DeleteObject(
                topBrush
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
                stopRequested[i] =
                    true;
            }

            if (editBrush)
            {
                DeleteObject(
                    editBrush
                );

                editBrush =
                    nullptr;
            }

            PostQuitMessage(
                0
            );

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
        "CppSongMaker16";

    // ========================================================
    // INITIAL STATE
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
    }

    // ========================================================
    // EDIT BRUSH
    // ========================================================

    editBrush =
        CreateSolidBrush(
            EDIT_BACKGROUND
        );

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
    // MAIN WINDOW
    // ========================================================

    HWND window =
        CreateWindowExA(
            WS_EX_COMPOSITED,
            CLASS_NAME,
            "C++ Song Maker - 16 Players",
            WS_OVERLAPPEDWINDOW |
            WS_VSCROLL,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1150,
            850,
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
    // UNIVERSAL PLAY BUTTON
    // ========================================================

    CreateWindowA(
        "BUTTON",
        "UNIVERSAL PLAY",
        WS_VISIBLE |
        WS_CHILD |
        BS_PUSHBUTTON,
        10,
        15,
        180,
        42,
        window,
        (HMENU)(INT_PTR)
            ID_UNIVERSAL_PLAY,
        instance,
        nullptr
    );

    // ========================================================
    // CREATE 16 PLAYERS
    // ========================================================

    for (
        int i = 0;
        i < PLAYER_COUNT;
        ++i)
    {
        int playID =
            PlayerButtonID(
                ID_PLAY_BASE,
                i
            );

        int loopID =
            PlayerButtonID(
                ID_LOOP_BASE,
                i
            );

        int tempoMinusID =
            PlayerButtonID(
                ID_TEMPO_MINUS_BASE,
                i
            );

        int tempoPlusID =
            PlayerButtonID(
                ID_TEMPO_PLUS_BASE,
                i
            );

        int pitchMinusID =
            PlayerButtonID(
                ID_PITCH_MINUS_BASE,
                i
            );

        int pitchPlusID =
            PlayerButtonID(
                ID_PITCH_PLUS_BASE,
                i
            );

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
                40,
                window,
                (HMENU)(INT_PTR)
                    playID,
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
                35,
                window,
                (HMENU)(INT_PTR)
                    loopID,
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
                40,
                35,
                window,
                (HMENU)(INT_PTR)
                    tempoMinusID,
                instance,
                nullptr
            );

        // ----------------------------------------------------
        // TEMPO LABEL
        // ----------------------------------------------------

        tempoLabel[i] =
            CreateWindowA(
                "STATIC",
                "TEMPO: 120",
                WS_VISIBLE |
                WS_CHILD |
                SS_CENTER |
                SS_CENTERIMAGE,
                0,
                0,
                100,
                35,
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
                100,
                35,
                window,
                (HMENU)(INT_PTR)
                    tempoPlusID,
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
                40,
                35,
                window,
                (HMENU)(INT_PTR)
                    pitchMinusID,
                instance,
                nullptr
            );

        // ----------------------------------------------------
        // PITCH LABEL
        // ----------------------------------------------------

        pitchLabel[i] =
            CreateWindowA(
                "STATIC",
                "PITCH: 0",
                WS_VISIBLE |
                WS_CHILD |
                SS_CENTER |
                SS_CENTERIMAGE,
                0,
                0,
                100,
                35,
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
                100,
                35,
                window,
                (HMENU)(INT_PTR)
                    pitchPlusID,
                instance,
                nullptr
            );

        // ----------------------------------------------------
        // TEXT INPUT
        //
        // IMPORTANT:
        // It starts EMPTY.
        // Nothing is prefilled.
        //
        // ES_MULTILINE + ES_AUTOVSCROLL allows the user
        // to enter many lines.
        // ES_WANTRETURN allows Enter.
        //
        // The edit control automatically wraps text to
        // the width of each individual player column.
        // ----------------------------------------------------

        songEditor[i] =
            CreateWindowExA(
                WS_EX_CLIENTEDGE,
                "EDIT",
                "",
                WS_VISIBLE |
                WS_CHILD |
                WS_VSCROLL |
                ES_MULTILINE |
                ES_AUTOVSCROLL |
                ES_WANTRETURN |
                ES_NOHIDESEL,
                0,
                0,
                200,
                EDIT_HEIGHT,
                window,
                nullptr,
                instance,
                nullptr
            );

        // Make the editor easier to read.
        SendMessageA(
            songEditor[i],
            WM_SETFONT,
            (WPARAM)GetStockObject(
                DEFAULT_GUI_FONT
            ),
            TRUE
        );
    }

    // ========================================================
    // INITIAL LAYOUT
    // ========================================================

    ResizePlayerControls(
        window
    );

    // ========================================================
    // SHOW
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
        ))
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
