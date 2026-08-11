#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <atomic>
#include <cmath>
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

// GREEN TEXT
const COLORREF TEXT = RGB(0, 255, 0);
const COLORREF BUTTON_TEXT = RGB(0, 255, 0);

// Text input colors
const COLORREF INPUT_TEXT = RGB(0, 255, 0);
const COLORREF INPUT_BACKGROUND = RGB(15, 15, 20);

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

// NEW:
// Each player now has its own editable song text box.
HWND songTextBox[PLAYER_COUNT] = {};

HWND mainWindow = nullptr;

// ============================================================
// DEFAULT SONG TEXT
// ============================================================

const char* DEFAULT_SONG =
    "TEMPO 120\n"
    "LENGTH 4\n"
    "\n"
    "NOTE C4 0 1\n"
    "NOTE E4 1 1\n"
    "NOTE G4 2 1\n"
    "NOTE C5 3 1\n"
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
    if (note == "C3")  return 130.81;
    if (note == "D3")  return 146.83;
    if (note == "E3")  return 164.81;
    if (note == "F3")  return 174.61;
    if (note == "G3")  return 196.00;
    if (note == "A3")  return 220.00;
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

    return 0.0;
}

// ============================================================
// DRUMS
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
        noise() * envelope;

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
        std::exp(
            -14.0 *
            t
        );

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
            2.0 *
            PI *
            3500.0 *
            t
        );

    return
        (
            metallic * 0.5
            +
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
            2.0 *
            PI *
            4000.0 *
            t
        );

    return
        (
            metal * 0.7
            +
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

    if (type == "HIHAT")
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
// LOAD SONG FROM TEXT BOX
//
// THIS REPLACES THE OLD .TXT FILE SYSTEM.
//
// The function receives the exact text that the user typed
// into the interface and parses it the same way the old
// LoadSong() function parsed the text file.
// ============================================================

bool LoadSongFromText(
    const std::string& songText,
    std::vector<NoteEvent>& notes,
    std::vector<DrumEvent>& drums,
    double& tempo,
    double& loopLengthBeats)
{
    std::istringstream songFile(songText);

    tempo = 120.0;
    loopLengthBeats = 4.0;

    std::string command;

    while (songFile >> command)
    {
        if (command == "TEMPO")
        {
            songFile >> tempo;

            if (tempo < MIN_TEMPO)
                tempo = MIN_TEMPO;

            if (tempo > MAX_TEMPO)
                tempo = MAX_TEMPO;
        }

        else if (command == "LENGTH")
        {
            songFile >> loopLengthBeats;

            if (loopLengthBeats <= 0.0)
                loopLengthBeats = 4.0;
        }

        else if (command == "NOTE")
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
                        frequency
                    }
                );
            }
        }

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
//
// Pitch affects ONLY melody.
// Drums are NOT pitch shifted.
// ============================================================

bool GenerateAudio(
    const std::string& songText,
    double tempoOverride,
    int pitchSemitones,
    std::vector<short>& samples)
{
    std::vector<NoteEvent> notes;
    std::vector<DrumEvent> drums;

    double fileTempo;
    double loopLengthBeats;

    if (!LoadSongFromText(
        songText,
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
                std::sin(
                    2.0 *
                    PI *
                    shiftedFrequency *
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
    // NO PITCH MULTIPLIER.
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
// GET TEXT FROM A PLAYER'S TEXT BOX
// ============================================================

std::string GetSongText(
    int playerIndex)
{
    if (
        playerIndex < 0 ||
        playerIndex >= PLAYER_COUNT)
        return "";

    if (!songTextBox[playerIndex])
        return "";

    int length =
        GetWindowTextLengthA(
            songTextBox[playerIndex]
        );

    if (length <= 0)
        return "";

    std::string text(
        length + 1,
        '\0'
    );

    GetWindowTextA(
        songTextBox[playerIndex],
        &text[0],
        length + 1
    );

    text.resize(length);

    return text;
}

// ============================================================
// RESIZE PLAYER CONTROLS
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

    int windowHeight =
        rect.bottom;

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
            50,
            TRUE
        );

        MoveWindow(
            loopButton[i],
            x,
            130,
            buttonWidth,
            40,
            TRUE
        );

        int smallWidth =
            (buttonWidth - 10) / 2;

        MoveWindow(
            tempoMinusButton[i],
            x,
            180,
            smallWidth,
            40,
            TRUE
        );

        MoveWindow(
            tempoLabel[i],
            x + smallWidth + 5,
            180,
            buttonWidth -
                smallWidth -
                5,
            40,
            TRUE
        );

        MoveWindow(
            tempoPlusButton[i],
            x,
            225,
            buttonWidth,
            40,
            TRUE
        );

        MoveWindow(
            pitchMinusButton[i],
            x,
            275,
            smallWidth,
            40,
            TRUE
        );

        MoveWindow(
            pitchLabel[i],
            x + smallWidth + 5,
            275,
            buttonWidth -
                smallWidth -
                5,
            40,
            TRUE
        );

        MoveWindow(
            pitchPlusButton[i],
            x,
            320,
            buttonWidth,
            40,
            TRUE
        );

        // ====================================================
        // SONG TEXT INPUT
        // ====================================================

        int textTop = 370;
        int textBottom = windowHeight - 15;

        int textHeight =
            textBottom -
            textTop;

        if (textHeight < 80)
            textHeight = 80;

        MoveWindow(
            songTextBox[i],
            x,
            textTop,
            buttonWidth,
            textHeight,
            TRUE
        );
    }
}

// ============================================================
// PLAY ONE PLAYER
// ============================================================

void PlaySong(
    int playerIndex,
    std::string songText)
{
    if (
        playerIndex < 0 ||
        playerIndex >= PLAYER_COUNT)
        return;

    if (playing[playerIndex])
        return;

    playing[playerIndex] = true;
    stopRequested[playerIndex] = false;

    // --------------------------------------------------------
    // Make sure the text box isn't empty.
    // --------------------------------------------------------

    if (songText.empty())
    {
        MessageBoxA(
            mainWindow,
            "The song text box is empty.",
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

    // --------------------------------------------------------
    // Read the song text.
    // --------------------------------------------------------

    std::vector<NoteEvent> tempNotes;
    std::vector<DrumEvent> tempDrums;

    double fileTempo = 120.0;
    double loopLengthBeats = 4.0;

    if (!LoadSongFromText(
        songText,
        tempNotes,
        tempDrums,
        fileTempo,
        loopLengthBeats))
    {
        MessageBoxA(
            mainWindow,
            "Could not read the song text.",
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

    // ========================================================
    // AUDIO BUFFERS
    // ========================================================

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
            songText,
            tempo,
            pitch,
            audioSamples[bufferIndex]
        );
    };

    // ========================================================
    // GENERATE FIRST TWO BUFFERS
    // ========================================================

    if (!generateBuffer(0))
    {
        waveOutClose(audioDevice);

        playing[playerIndex] = false;

        PostMessage(
            mainWindow,
            WM_USER + playerIndex + 1,
            0,
            0
        );

        return;
    }

    if (!generateBuffer(1))
    {
        waveOutClose(audioDevice);

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
            for (
                int j = 0;
                j < i;
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

            // ------------------------------------------------
            // If LOOP is OFF, don't create another buffer.
            // ------------------------------------------------

            if (!looping[playerIndex])
                continue;

            // ------------------------------------------------
            // LOOP is ON.
            //
            // Generate the same song text again using the
            // current tempo and pitch settings.
            // ------------------------------------------------

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
        // If looping is OFF, wait until both queued buffers
        // have finished.
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
        WM_USER + playerIndex + 1,
        0,
        0
    );
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
        // BUTTON / EDIT TEXT COLORS
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

        case WM_CTLCOLOREDIT:
        {
            HDC dc =
                (HDC)wParam;

            SetTextColor(
                dc,
                INPUT_TEXT
            );

            SetBkColor(
                dc,
                INPUT_BACKGROUND
            );

            static HBRUSH inputBrush =
                CreateSolidBrush(
                    INPUT_BACKGROUND
                );

            return (LRESULT)inputBrush;
        }

        // ====================================================
        // STATIC TEXT
        // ====================================================

        case WM_CTLCOLORSTATIC:
        {
            HDC dc =
                (HDC)wParam;

            SetTextColor(
                dc,
                TEXT
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
        // BUTTONS
        // ====================================================

        case WM_COMMAND:
        {
            int button =
                LOWORD(wParam);

            // =================================================
            // PLAYER 1
            // =================================================

            if (button == ID_PLAY1)
            {
                int p = 0;

                if (!playing[p])
                {
                    std::string songText =
                        GetSongText(p);

                    stopRequested[p] = false;

                    SetWindowTextA(
                        playButton[p],
                        "STOP"
                    );

                    std::thread(
                        PlaySong,
                        p,
                        songText
                    ).detach();
                }
                else
                {
                    stopRequested[p] = true;

                    SetWindowTextA(
                        playButton[p],
                        "PLAY"
                    );
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
                    std::string songText =
                        GetSongText(p);

                    stopRequested[p] = false;

                    SetWindowTextA(
                        playButton[p],
                        "STOP"
                    );

                    std::thread(
                        PlaySong,
                        p,
                        songText
                    ).detach();
                }
                else
                {
                    stopRequested[p] = true;

                    SetWindowTextA(
                        playButton[p],
                        "PLAY"
                    );
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
                    std::string songText =
                        GetSongText(p);

                    stopRequested[p] = false;

                    SetWindowTextA(
                        playButton[p],
                        "STOP"
                    );

                    std::thread(
                        PlaySong,
                        p,
                        songText
                    ).detach();
                }
                else
                {
                    stopRequested[p] = true;

                    SetWindowTextA(
                        playButton[p],
                        "PLAY"
                    );
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
                    std::string songText =
                        GetSongText(p);

                    stopRequested[p] = false;

                    SetWindowTextA(
                        playButton[p],
                        "STOP"
                    );

                    std::thread(
                        PlaySong,
                        p,
                        songText
                    ).detach();
                }
                else
                {
                    stopRequested[p] = true;

                    SetWindowTextA(
                        playButton[p],
                        "PLAY"
                    );
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
        {
            SetWindowTextA(
                playButton[0],
                "PLAY"
            );

            break;
        }

        case WM_USER + 2:
        {
            SetWindowTextA(
                playButton[1],
                "PLAY"
            );

            break;
        }

        case WM_USER + 3:
        {
            SetWindowTextA(
                playButton[2],
                "PLAY"
            );

            break;
        }

        case WM_USER + 4:
        {
            SetWindowTextA(
                playButton[3],
                "PLAY"
            );

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
            // COLUMNS
            // ------------------------------------------------

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
                    5,
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

            // ------------------------------------------------
            // TITLES
            // ------------------------------------------------

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
                    20,
                    (i + 1) *
                        columnWidth,
                    60
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
            1100,
            750,
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
    // CREATE CONTROLS
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
            playID = ID_PLAY1;
            loopID = ID_LOOP1;
            tempoMinusID = ID_TEMPO_MINUS1;
            tempoPlusID = ID_TEMPO_PLUS1;
            pitchMinusID = ID_PITCH_MINUS1;
            pitchPlusID = ID_PITCH_PLUS1;
        }
        else if (i == 1)
        {
            playID = ID_PLAY2;
            loopID = ID_LOOP2;
            tempoMinusID = ID_TEMPO_MINUS2;
            tempoPlusID = ID_TEMPO_PLUS2;
            pitchMinusID = ID_PITCH_MINUS2;
            pitchPlusID = ID_PITCH_PLUS2;
        }
        else if (i == 2)
        {
            playID = ID_PLAY3;
            loopID = ID_LOOP3;
            tempoMinusID = ID_TEMPO_MINUS3;
            tempoPlusID = ID_TEMPO_PLUS3;
            pitchMinusID = ID_PITCH_MINUS3;
            pitchPlusID = ID_PITCH_PLUS3;
        }
        else
        {
            playID = ID_PLAY4;
            loopID = ID_LOOP4;
            tempoMinusID = ID_TEMPO_MINUS4;
            tempoPlusID = ID_TEMPO_PLUS4;
            pitchMinusID = ID_PITCH_MINUS4;
            pitchPlusID = ID_PITCH_PLUS4;
        }

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
                75,
                100,
                50,
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
                130,
                100,
                40,
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
                180,
                40,
                40,
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
                "TEMPO: 120",
                WS_VISIBLE |
                WS_CHILD |
                SS_CENTER |
                SS_CENTERIMAGE,
                0,
                180,
                100,
                40,
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
                225,
                100,
                40,
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
                275,
                40,
                40,
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
                "PITCH: 0",
                WS_VISIBLE |
                WS_CHILD |
                SS_CENTER |
                SS_CENTERIMAGE,
                0,
                275,
                100,
                40,
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
                320,
                100,
                40,
                window,
                (HMENU)(INT_PTR)pitchPlusID,
                instance,
                nullptr
            );

        // ----------------------------------------------------
        // SONG TEXT INPUT
        // ----------------------------------------------------

        songTextBox[i] =
            CreateWindowExA(
                WS_EX_CLIENTEDGE,
                "EDIT",
                DEFAULT_SONG,
                WS_VISIBLE |
                WS_CHILD |
                WS_VSCROLL |
                WS_HSCROLL |
                ES_MULTILINE |
                ES_AUTOVSCROLL |
                ES_AUTOHSCROLL |
                ES_WANTRETURN,
                0,
                370,
                100,
                200,
                window,
                nullptr,
                instance,
                nullptr
            );

        // ----------------------------------------------------
        // SET FONT FOR TEXT INPUT
        // ----------------------------------------------------

        HFONT editFont =
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
                DEFAULT_PITCH,
                "Consolas"
            );

        SendMessage(
            songTextBox[i],
            WM_SETFONT,
            (WPARAM)editFont,
            TRUE
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
