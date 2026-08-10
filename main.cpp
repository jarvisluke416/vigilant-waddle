#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <random>
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

// ============================================================
// COLORS
// ============================================================

const COLORREF BACKGROUND = RGB(25, 25, 30);
const COLORREF COLUMN = RGB(32, 32, 38);
const COLORREF COLUMN_BORDER = RGB(55, 55, 65);
const COLORREF TEXT = RGB(240, 240, 245);

// ============================================================
// GLOBAL STATE
// ============================================================

std::atomic<bool> playing[PLAYER_COUNT] = {};
std::atomic<bool> looping[PLAYER_COUNT] = {};
std::atomic<bool> stopRequested[PLAYER_COUNT] = {};

std::atomic<double> currentTempo[PLAYER_COUNT] = {};

HWND playButton[PLAYER_COUNT] = {};
HWND loopButton[PLAYER_COUNT] = {};

HWND tempoMinusButton[PLAYER_COUNT] = {};
HWND tempoPlusButton[PLAYER_COUNT] = {};
HWND tempoLabel[PLAYER_COUNT] = {};

HWND mainWindow = nullptr;

// ============================================================
// RANDOM
// ============================================================

std::mt19937 randomGenerator(12345);

double noise()
{
    static std::uniform_real_distribution<double>
        distribution(-1.0, 1.0);

    return distribution(randomGenerator);
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

    return std::sin(
        2.0 * PI * frequency * t
    ) * envelope;
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
            2.0 * PI * 180.0 * t
        ) * std::exp(-20.0 * t);

    return noisePart * 0.8 +
           body * 0.2;
}

double closedHiHat(double t)
{
    if (t < 0 || t >= 0.12)
        return 0.0;

    return noise()
           * std::exp(-45.0 * t)
           * 0.7;
}

double openHiHat(double t)
{
    if (t < 0 || t >= 0.8)
        return 0.0;

    return noise()
           * std::exp(-5.0 * t)
           * 0.6;
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

    return noise()
           * (burst1 + burst2 + burst3)
           * envelope;
}

double tom(double t, double frequency)
{
    if (t < 0 || t >= 0.6)
        return 0.0;

    double pitch =
        frequency * std::exp(-3.0 * t)
        + frequency * 0.4;

    double envelope =
        std::exp(-7.0 * t);

    return std::sin(
        2.0 * PI * pitch * t
    ) * envelope;
}

double crash(double t)
{
    if (t < 0 || t >= 2.0)
        return 0.0;

    return noise()
           * std::exp(-2.5 * t)
           * 0.65;
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
            2.0 * PI * 3500.0 * t
        );

    return (
        metallic * 0.5 +
        tone * 0.5
    ) * envelope * 0.4;
}

double rimshot(double t)
{
    if (t < 0 || t >= 0.15)
        return 0.0;

    double envelope =
        std::exp(-40.0 * t);

    return
        std::sin(
            2.0 * PI * 1200.0 * t
        ) * envelope * 0.8
        +
        noise() * envelope * 0.3;
}

double cowbell(double t)
{
    if (t < 0 || t >= 0.3)
        return 0.0;

    double envelope =
        std::exp(-15.0 * t);

    double tone1 =
        std::sin(
            2.0 * PI * 540.0 * t
        );

    double tone2 =
        std::sin(
            2.0 * PI * 800.0 * t
        );

    return (
        tone1 + tone2
    ) * envelope * 0.4;
}

double shaker(double t)
{
    if (t < 0 || t >= 0.25)
        return 0.0;

    return noise()
           * std::exp(-18.0 * t)
           * 0.5;
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
            2.0 * PI * 4000.0 * t
        );

    return (
        metal * 0.7 +
        ring * 0.3
    ) * envelope * 0.5;
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
// LOAD SONG
// ============================================================

bool LoadSong(
    const char* filename,
    std::vector<NoteEvent>& notes,
    std::vector<DrumEvent>& drums,
    double& tempo,
    double& loopLengthBeats)
{
    std::ifstream songFile(filename);

    if (!songFile)
        return false;

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

            songFile >>
                noteName >>
                startBeat >>
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

            songFile >>
                drumType >>
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
    const char* filename,
    double tempoOverride,
    std::vector<short>& samples)
{
    std::vector<NoteEvent> notes;
    std::vector<DrumEvent> drums;

    double fileTempo;
    double loopLengthBeats;

    if (!LoadSong(
        filename,
        notes,
        drums,
        fileTempo,
        loopLengthBeats))
    {
        return false;
    }

    double tempo =
        tempoOverride;

    if (tempo < MIN_TEMPO)
        tempo = MIN_TEMPO;

    if (tempo > MAX_TEMPO)
        tempo = MAX_TEMPO;

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
                startSeconds *
                SAMPLE_RATE
            );

        int noteSamples =
            static_cast<int>(
                durationSeconds *
                SAMPLE_RATE
            );

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
                / SAMPLE_RATE;

            double envelope = 1.0;

            // Attack
            if (time < 0.01)
            {
                envelope =
                    time / 0.01;
            }

            // Release
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
                    note.frequency *
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
    // ========================================================

    for (const DrumEvent& drum : drums)
    {
        double startSeconds =
            drum.startBeat *
            secondsPerBeat;

        int startSample =
            static_cast<int>(
                startSeconds *
                SAMPLE_RATE
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
                / SAMPLE_RATE;

            audio[index] +=
                makeDrum(
                    drum.type,
                    time
                ) * 0.5;
        }
    }

    // ========================================================
    // CONVERT TO 16-BIT
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
                value * 32767.0
            );
    }

    return true;
}

// ============================================================
// BUTTON IDs
// ============================================================

#define ID_PLAY1        101
#define ID_LOOP1        102
#define ID_TEMPO_MINUS1 103
#define ID_TEMPO_PLUS1  104

#define ID_PLAY2        201
#define ID_LOOP2        202
#define ID_TEMPO_MINUS2 203
#define ID_TEMPO_PLUS2  204

#define ID_PLAY3        301
#define ID_LOOP3        302
#define ID_TEMPO_MINUS3 303
#define ID_TEMPO_PLUS3  304

#define ID_PLAY4        401
#define ID_LOOP4        402
#define ID_TEMPO_MINUS4 403
#define ID_TEMPO_PLUS4  404

// ============================================================
// TEMPO DISPLAY
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
            currentTempo[playerIndex].load()
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
// RESIZE BUTTONS
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
            i * columnWidth + 20;

        int buttonWidth =
            columnWidth - 40;

        if (buttonWidth < 80)
            buttonWidth = 80;

        int smallButtonWidth =
            (buttonWidth - 10) / 2;

        if (smallButtonWidth < 40)
            smallButtonWidth = 40;

        MoveWindow(
            playButton[i],
            x,
            80,
            buttonWidth,
            55,
            TRUE
        );

        MoveWindow(
            loopButton[i],
            x,
            145,
            buttonWidth,
            45,
            TRUE
        );

        MoveWindow(
            tempoMinusButton[i],
            x,
            200,
            smallButtonWidth,
            40,
            TRUE
        );

        MoveWindow(
            tempoLabel[i],
            x + smallButtonWidth + 5,
            200,
            buttonWidth -
                smallButtonWidth -
                10,
            40,
            TRUE
        );

        MoveWindow(
            tempoPlusButton[i],
            x,
            245,
            buttonWidth,
            40,
            TRUE
        );
    }
}

// ============================================================
// PLAY ONE PLAYER
//
// IMPORTANT:
// The audio device stays open for the entire playback.
//
// TWO buffers are queued:
//     Buffer 0 -> current loop
//     Buffer 1 -> next loop
//
// This prevents the device from becoming empty at
// the loop boundary.
// ============================================================

void PlaySong(
    int playerIndex)
{
    if (
        playerIndex < 0 ||
        playerIndex >= PLAYER_COUNT)
        return;

    if (playing[playerIndex])
        return;

    playing[playerIndex] = true;
    stopRequested[playerIndex] = false;

    const char* filenames[PLAYER_COUNT] =
    {
        "song1.txt",
        "song2.txt",
        "song3.txt",
        "song4.txt"
    };

    // ========================================================
    // READ INITIAL TEMPO
    // ========================================================

    std::vector<NoteEvent> tempNotes;
    std::vector<DrumEvent> tempDrums;

    double fileTempo = 120.0;
    double loopLengthBeats = 4.0;

    if (!LoadSong(
        filenames[playerIndex],
        tempNotes,
        tempDrums,
        fileTempo,
        loopLengthBeats))
    {
        MessageBoxA(
            mainWindow,
            "Could not open or read the song file.",
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

    // If the tempo has not been initialized,
    // use the tempo from the song file.
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

    // ========================================================
    // OPEN DEVICE ONCE
    // ========================================================

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
        result != MMSYSERR_NOERROR)
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
    // GENERATE FIRST TWO BUFFERS
    //
    // Both are generated before playback begins.
    // This gives waveOut two buffers to work with.
    // ========================================================

    std::vector<short> audioBuffers[2];

    double initialTempo =
        currentTempo[playerIndex].load();

    if (!GenerateAudio(
        filenames[playerIndex],
        initialTempo,
        audioBuffers[0]))
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

    if (looping[playerIndex])
    {
        if (!GenerateAudio(
            filenames[playerIndex],
            currentTempo[playerIndex].load(),
            audioBuffers[1]))
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
    }
    else
    {
        // Just make an empty buffer.
        // It will not be submitted unless needed.
        audioBuffers[1].clear();
    }

    // ========================================================
    // WAVE HEADERS
    // ========================================================

    WAVEHDR headers[2] = {};

    headers[0].lpData =
        reinterpret_cast<LPSTR>(
            audioBuffers[0].data()
        );

    headers[0].dwBufferLength =
        static_cast<DWORD>(
            audioBuffers[0].size()
            * sizeof(short)
        );

    // ========================================================
    // PREPARE FIRST BUFFER
    // ========================================================

    result =
        waveOutPrepareHeader(
            audioDevice,
            &headers[0],
            sizeof(WAVEHDR)
        );

    if (
        result != MMSYSERR_NOERROR)
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
    // PLAY FIRST BUFFER
    // ========================================================

    result =
        waveOutWrite(
            audioDevice,
            &headers[0],
            sizeof(WAVEHDR)
        );

    if (
        result != MMSYSERR_NOERROR)
    {
        waveOutUnprepareHeader(
            audioDevice,
            &headers[0],
            sizeof(WAVEHDR)
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

    // ========================================================
    // SECOND BUFFER
    //
    // If looping is already enabled, queue it immediately.
    // ========================================================

    bool secondBufferQueued =
        false;

    if (
        looping[playerIndex] &&
        !audioBuffers[1].empty())
    {
        headers[1].lpData =
            reinterpret_cast<LPSTR>(
                audioBuffers[1].data()
            );

        headers[1].dwBufferLength =
            static_cast<DWORD>(
                audioBuffers[1].size()
                * sizeof(short)
            );

        result =
            waveOutPrepareHeader(
                audioDevice,
                &headers[1],
                sizeof(WAVEHDR)
            );

        if (
            result == MMSYSERR_NOERROR)
        {
            result =
                waveOutWrite(
                    audioDevice,
                    &headers[1],
                    sizeof(WAVEHDR)
                );

            if (
                result == MMSYSERR_NOERROR)
            {
                secondBufferQueued = true;
            }
            else
            {
                waveOutUnprepareHeader(
                    audioDevice,
                    &headers[1],
                    sizeof(WAVEHDR)
                );
            }
        }
    }

    // ========================================================
    // PLAYBACK MANAGEMENT
    // ========================================================

    int currentBuffer = 0;

    while (!stopRequested[playerIndex])
    {
        // Wait until the current buffer finishes.
        while (
            !(headers[currentBuffer].dwFlags
              & WHDR_DONE))
        {
            if (
                stopRequested[playerIndex])
            {
                break;
            }

            Sleep(1);
        }

        if (stopRequested[playerIndex])
            break;

        // ====================================================
        // LOOP OFF
        // ====================================================

        if (!looping[playerIndex])
        {
            break;
        }

        // ====================================================
        // THE OTHER BUFFER SHOULD ALREADY BE PLAYING
        // ====================================================

        int finishedBuffer =
            currentBuffer;

        int nextBuffer =
            1 - currentBuffer;

        // ====================================================
        // If the second buffer was not successfully queued,
        // we need to prepare it now.
        //
        // Normally this is already playing.
        // ====================================================

        if (
            finishedBuffer == 0 &&
            !secondBufferQueued)
        {
            break;
        }

        // ====================================================
        // Wait for the next buffer to finish if necessary.
        //
        // This case normally doesn't occur because buffer 1
        // was queued before buffer 0 finished.
        // ====================================================

        if (
            nextBuffer == 1 &&
            secondBufferQueued)
        {
            // Buffer 1 is already queued.
        }

        // ====================================================
        // Wait until the finished buffer is completely done.
        // ====================================================

        while (
            !(headers[finishedBuffer].dwFlags
              & WHDR_DONE))
        {
            if (
                stopRequested[playerIndex])
                break;

            Sleep(1);
        }

        if (stopRequested[playerIndex])
            break;

        // ====================================================
        // GENERATE THE NEXT VERSION OF THE FINISHED BUFFER
        //
        // This happens while the other buffer is playing.
        //
        // Tempo changes therefore apply to a future loop.
        // ====================================================

        std::vector<short> newSamples;

        double tempo =
            currentTempo[playerIndex].load();

        if (!GenerateAudio(
            filenames[playerIndex],
            tempo,
            newSamples))
        {
            break;
        }

        // ====================================================
        // UNPREPARE THE FINISHED BUFFER
        // ====================================================

        waveOutUnprepareHeader(
            audioDevice,
            &headers[finishedBuffer],
            sizeof(WAVEHDR)
        );

        // ====================================================
        // REPLACE ITS AUDIO
        // ====================================================

        audioBuffers[finishedBuffer] =
            std::move(newSamples);

        headers[finishedBuffer] = {};

        headers[finishedBuffer].lpData =
            reinterpret_cast<LPSTR>(
                audioBuffers[finishedBuffer].data()
            );

        headers[finishedBuffer].dwBufferLength =
            static_cast<DWORD>(
                audioBuffers[finishedBuffer].size()
                * sizeof(short)
            );

        // ====================================================
        // PREPARE IT AGAIN
        // ====================================================

        result =
            waveOutPrepareHeader(
                audioDevice,
                &headers[finishedBuffer],
                sizeof(WAVEHDR)
            );

        if (
            result != MMSYSERR_NOERROR)
        {
            break;
        }

        // ====================================================
        // QUEUE IT AGAIN
        //
        // The other buffer is already playing, so this one
        // becomes the buffer after it.
        // ====================================================

        result =
            waveOutWrite(
                audioDevice,
                &headers[finishedBuffer],
                sizeof(WAVEHDR)
            );

        if (
            result != MMSYSERR_NOERROR)
        {
            break;
        }

        currentBuffer =
            finishedBuffer;
    }

    // ========================================================
    // STOP PLAYBACK
    // ========================================================

    waveOutReset(
        audioDevice
    );

    // ========================================================
    // CLEAN UP HEADER 0
    // ========================================================

    if (
        headers[0].dwFlags &
        WHDR_PREPARED)
    {
        waveOutUnprepareHeader(
            audioDevice,
            &headers[0],
            sizeof(WAVEHDR)
        );
    }

    // ========================================================
    // CLEAN UP HEADER 1
    // ========================================================

    if (
        headers[1].dwFlags &
        WHDR_PREPARED)
    {
        waveOutUnprepareHeader(
            audioDevice,
            &headers[1],
            sizeof(WAVEHDR)
        );
    }

    // ========================================================
    // CLOSE DEVICE ONLY ON ACTUAL STOP
    // ========================================================

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
                    stopRequested[p] = false;

                    SetWindowTextA(
                        playButton[p],
                        "STOP"
                    );

                    std::thread(
                        PlaySong,
                        p
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

                currentTempo[p] =
                    tempo;

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

                currentTempo[p] =
                    tempo;

                UpdateTempoDisplay(p);
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

                    SetWindowTextA(
                        playButton[p],
                        "STOP"
                    );

                    std::thread(
                        PlaySong,
                        p
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

                currentTempo[p] =
                    tempo;

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

                currentTempo[p] =
                    tempo;

                UpdateTempoDisplay(p);
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

                    SetWindowTextA(
                        playButton[p],
                        "STOP"
                    );

                    std::thread(
                        PlaySong,
                        p
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

                currentTempo[p] =
                    tempo;

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

                currentTempo[p] =
                    tempo;

                UpdateTempoDisplay(p);
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

                    SetWindowTextA(
                        playButton[p],
                        "STOP"
                    );

                    std::thread(
                        PlaySong,
                        p
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

                currentTempo[p] =
                    tempo;

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

                currentTempo[p] =
                    tempo;

                UpdateTempoDisplay(p);
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
            // FOUR COLUMNS
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
                    (i + 1) * columnWidth - 5,
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
                    (i + 1) * columnWidth,
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
        LoadCursor(
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
            900,
            600,
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
    // INITIAL TEMPOS
    // ========================================================

    for (
        int i = 0;
        i < PLAYER_COUNT;
        ++i)
    {
        currentTempo[i] =
            120.0;
    }

    // ========================================================
    // CREATE BUTTONS
    // ========================================================

    for (
        int i = 0;
        i < PLAYER_COUNT;
        ++i)
    {
        int playID = 0;
        int loopID = 0;
        int tempoMinusID = 0;
        int tempoPlusID = 0;

        if (i == 0)
        {
            playID = ID_PLAY1;
            loopID = ID_LOOP1;
            tempoMinusID = ID_TEMPO_MINUS1;
            tempoPlusID = ID_TEMPO_PLUS1;
        }
        else if (i == 1)
        {
            playID = ID_PLAY2;
            loopID = ID_LOOP2;
            tempoMinusID = ID_TEMPO_MINUS2;
            tempoPlusID = ID_TEMPO_PLUS2;
        }
        else if (i == 2)
        {
            playID = ID_PLAY3;
            loopID = ID_LOOP3;
            tempoMinusID = ID_TEMPO_MINUS3;
            tempoPlusID = ID_TEMPO_PLUS3;
        }
        else
        {
            playID = ID_PLAY4;
            loopID = ID_LOOP4;
            tempoMinusID = ID_TEMPO_MINUS4;
            tempoPlusID = ID_TEMPO_PLUS4;
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
                80,
                100,
                55,
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
                145,
                100,
                45,
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
                200,
                40,
                40,
                window,
                (HMENU)(INT_PTR)tempoMinusID,
                instance,
                nullptr
            );

        // ----------------------------------------------------
        // TEMPO DISPLAY
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
                200,
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
                245,
                100,
                40,
                window,
                (HMENU)(INT_PTR)tempoPlusID,
                instance,
                nullptr
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
        GetMessage(
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

        DispatchMessage(
            &message
        );
    }

    return 0;
}
