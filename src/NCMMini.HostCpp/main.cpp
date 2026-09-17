#include "Host.h"
#include "Media.h"
#include "PipeServer.h"
#include "SettingsWindow.h"
#include "TaskbarPresenter.h"
#include "TaskbarBinding.h"

#include <objbase.h>
#include <shellapi.h>

#include <chrono>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <deque>

namespace ncmmini
{
class Application
{
public:
    explicit Application(AppOptions options, HANDLE showRequested)
        : options_(std::move(options)), player_(options_), settingsPath_(SettingsPath()),
          settings_(LoadSettings(settingsPath_)),
          settingsWindow_(settingsPath_, [this](const AppSettings& settings) { UpdateSettings(settings); }),
          showRequested_(showRequested)
    {
        if (!options_.showLyrics) settings_.showLyrics = false;
        if (!options_.closeCloudMusicOnExit) settings_.closeCloudMusicOnExit = false;
    }

    int Run()
    {
        const auto mode = SelectTaskbarMode(options_.taskbarMode, WindowsBuild(), HasModernTaskbar());
        const bool modern = mode == TaskbarMode::Win11;
        modern_ = modern;
        Log(modern ? L"Taskbar mode: win11" : L"Taskbar mode: deskband");
        runStarted_ = std::chrono::steady_clock::now();
        if (GetFileAttributesW(settingsPath_.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            SaveSettings(settingsPath_, settings_);
        }
        if (!settingsWindow_.Start())
        {
            Log(L"Settings window could not be initialized");
        }
        TaskbarPresenter taskbar;
        PipeServer pipe([this](BandCommand command) { QueueCommand(command); });
        if (modern)
        {
            if (!taskbar.Start([this](BandCommand command) { QueueCommand(command); }, options_.showBand))
            {
                Log(L"Win11 taskbar initialization failed");
                RequestShutdown();
                settingsWindow_.Stop();
                return 1;
            }
        }
        else pipe.Start();
        Log(L"Taskbar initialized; starting player polling");
        std::thread commands([this] { CommandLoop(); });
        const auto publish = [&](const BandState& state) {
            if (modern) taskbar.Publish(state);
            else pipe.Publish(state);
        };
        if (!modern && options_.showBand)
        {
            RunBandController(L"show");
        }
        if (options_.launchCloudMusic)
        {
            player_.TryLaunch();
        }

        const auto launchDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        bool playerSeen = false;
        std::wstring previousTitle;
        PlaybackState previousPlayback = PlaybackState::Unknown;
        std::wstring publishedTitle;
        std::wstring previousLyric;
        TrackInfo track;
        std::vector<LyricLine> lyrics;
        std::vector<std::uint8_t> cover;
        auto trackStarted = std::chrono::steady_clock::now();
        auto coverRetryAt = trackStarted;
        unsigned int coverRetryCount = 0;
        bool disconnectedPublished = false;
        bool forceRefresh = true;
        auto observedSettingsVersion = settingsVersion_.load();

        while (!ShouldStop())
        {
            const auto currentSettingsVersion = settingsVersion_.load();
            const auto settings = CurrentSettings();
            if (currentSettingsVersion != observedSettingsVersion)
            {
                observedSettingsVersion = currentSettingsVersion;
                previousTitle.clear();
                publishedTitle.clear();
                previousLyric.clear();
                forceRefresh = true;
            }
            const auto snapshot = player_.ReadSnapshot(modern);
            processId_ = snapshot.processId;
            if (!snapshot.running)
            {
                if (!disconnectedPublished)
                {
                    publish({false, L"", L"网易云音乐未连接", L"", {}});
                    disconnectedPublished = true;
                }
                previousPlayback = PlaybackState::Unknown;
                forceRefresh = true;
                if (!modern && (playerSeen || (options_.launchCloudMusic && std::chrono::steady_clock::now() >= launchDeadline)))
                {
                    break;
                }
                Wait(std::chrono::milliseconds(500));
                continue;
            }

            playerSeen = true;
            disconnectedPublished = false;
            bool stateChanged = false;
            const bool titleChanged = forceRefresh || snapshot.windowTitle != previousTitle;
            const bool playbackChanged = snapshot.playback != previousPlayback;
            if (titleChanged)
            {
                Log(L"Player state changed; running=" + std::to_wstring(snapshot.running)
                    + L" title_chars=" + std::to_wstring(snapshot.windowTitle.size()));
                forceRefresh = false;
                previousTitle = snapshot.windowTitle;
                // Publish the new title immediately; cache scans and cover download may take time.
                publish({true, snapshot.track.name, snapshot.track.artist, L"", {}, snapshot.playback});
                track = catalog_.Find(snapshot.windowTitle);
                if (track.name.empty()) track = snapshot.track;
                trackStarted = std::chrono::steady_clock::now();
                lyrics = settings.showLyrics ? lyricsStore_.Find(track) : std::vector<LyricLine>{};
                cover = LoadCover(track.coverUrl, options_.downloadCover);
                coverRetryCount = 0;
                coverRetryAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
                previousLyric.clear();
                publishedTitle.clear();
                stateChanged = true;
            }
            if (playbackChanged)
            {
                previousPlayback = snapshot.playback;
                stateChanged = true;
                Log(L"Player playback=" + std::to_wstring(static_cast<int>(snapshot.playback)));
            }

            const auto now = std::chrono::steady_clock::now();
            if (cover.empty() && now >= coverRetryAt && !snapshot.windowTitle.empty())
            {
                auto refreshed = catalog_.FindQueued(snapshot.windowTitle);
                if (refreshed.name.empty() && coverRetryCount > 0 && coverRetryCount % 6 == 0)
                {
                    refreshed = catalog_.Find(snapshot.windowTitle, true);
                }
                if (!refreshed.name.empty())
                {
                    const bool metadataChanged = refreshed.coverUrl != track.coverUrl
                        || refreshed.trackId != track.trackId || refreshed.lyricsId != track.lyricsId
                        || refreshed.name != track.name || refreshed.artist != track.artist;
                    track = refreshed;
                    stateChanged = stateChanged || metadataChanged;
                    if (metadataChanged && settings.showLyrics)
                    {
                        lyrics = lyricsStore_.Find(track);
                    }
                }
                if (!track.coverUrl.empty())
                {
                    cover = LoadCover(track.coverUrl, options_.downloadCover);
                    stateChanged = stateChanged || !cover.empty();
                }
                ++coverRetryCount;
                const auto delay = std::min(1000u, 100u * (coverRetryCount + 1));
                coverRetryAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay);
            }

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - trackStarted);
            const auto lyric = settings.showLyrics ? LyricsStore::Current(lyrics, elapsed) : std::wstring();
            if (stateChanged || lyric != previousLyric || snapshot.windowTitle != publishedTitle)
            {
                previousLyric = lyric;
                publishedTitle = snapshot.windowTitle;
                publish({
                    true,
                    track.name.empty() ? L"网易云音乐" : track.name,
                    track.artist,
                    lyric,
                    cover,
                    snapshot.playback
                });
            }
            Wait(std::chrono::milliseconds(settings.refreshIntervalMs));
        }

        RequestShutdown();
        taskbar.Stop();
        pipe.Stop();
        commands.join();
        settingsWindow_.Stop();
        if (CurrentSettings().closeCloudMusicOnExit)
        {
            player_.Close();
        }
        if (!modern) RunBandController(L"hide");
        return 0;
    }

private:
    void RequestShutdown()
    {
        {
            std::lock_guard lock(commandMutex_);
            shutdown_ = true;
        }
        commandWake_.notify_all();
    }

    void QueueCommand(BandCommand command)
    {
        if (command == BandCommand::Exit) { RequestShutdown(); return; }
        std::lock_guard lock(commandMutex_);
        if (shutdown_) return;
        if (commandQueue_.size() >= 32) return;
        commandQueue_.push_back(command);
        commandWake_.notify_one();
    }

    void CommandLoop()
    {
        while (!shutdown_)
        {
            if (WaitForSingleObject(showRequested_, 0) == WAIT_OBJECT_0)
            {
                if (modern_)
                {
                    const auto window = FindWindowW(Win11ControllerClass, nullptr);
                    if (window) PostMessageW(window, Win11ShowMessage, 0, 0);
                }
                else RunBandController(L"show");
            }
            BandCommand command;
            {
                std::unique_lock lock(commandMutex_);
                commandWake_.wait_for(lock, std::chrono::milliseconds(100),
                    [this] { return shutdown_ || !commandQueue_.empty(); });
                if (shutdown_) break;
                if (commandQueue_.empty()) continue;
                command = commandQueue_.front();
                commandQueue_.pop_front();
            }
            HandleCommand(command);
        }
    }

    bool ShouldStop() const
    {
        return shutdown_ || (options_.durationSeconds && std::chrono::steady_clock::now() - runStarted_
            >= std::chrono::seconds(options_.durationSeconds));
    }

    void HandleCommand(BandCommand command)
    {
        if (command == BandCommand::Options)
        {
            settingsWindow_.Show();
            return;
        }
        if (command == BandCommand::Exit)
        {
            RequestShutdown();
            return;
        }
        const auto processId = processId_.load();
        if (processId != 0)
        {
            const bool sent = player_.Send(command, processId);
            Log(L"CloudMusic command=" + std::to_wstring(static_cast<std::uint32_t>(command))
                + L" queued=" + (sent ? L"1" : L"0"));
        }
    }

    void Wait(std::chrono::milliseconds duration) const
    {
        const auto deadline = std::chrono::steady_clock::now() + duration;
        while (!ShouldStop() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    }

    AppSettings CurrentSettings() const
    {
        std::lock_guard lock(settingsMutex_);
        return settings_;
    }

    void UpdateSettings(const AppSettings& settings)
    {
        {
            std::lock_guard lock(settingsMutex_);
            settings_ = settings;
            if (!options_.closeCloudMusicOnExit) settings_.closeCloudMusicOnExit = false;
        }
        ++settingsVersion_;
    }

    AppOptions options_;
    PlayerController player_;
    std::wstring settingsPath_;
    mutable std::mutex settingsMutex_;
    AppSettings settings_;
    SettingsWindow settingsWindow_;
    TrackCatalog catalog_;
    LyricsStore lyricsStore_;
    std::atomic_bool shutdown_{false};
    std::atomic<DWORD> processId_{0};
    std::atomic<std::uint64_t> settingsVersion_{0};
    std::chrono::steady_clock::time_point runStarted_;
    std::mutex commandMutex_;
    std::condition_variable commandWake_;
    std::deque<BandCommand> commandQueue_;
    HANDLE showRequested_ = nullptr;
    bool modern_ = false;
};
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    int argumentCount = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    const auto options = ncmmini::ParseOptions(argumentCount, arguments);
    if (arguments != nullptr) LocalFree(arguments);
    if (!options.error.empty())
    {
        ncmmini::Log(options.error);
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 2;
    }
    if (options.taskbarStatus)
    {
        const auto window = FindWindowW(ncmmini::Win11ControllerClass, nullptr);
        DWORD_PTR status = 0;
        const bool queried = window && SendMessageTimeoutW(window, ncmmini::Win11StatusMessage,
            0, 0, SMTO_ABORTIFHUNG, 1000, &status);
        if (SUCCEEDED(comResult)) CoUninitialize();
        return queried ? (status == 1 ? 0 : 3) : 1;
    }

    HANDLE showRequested = CreateEventW(nullptr, FALSE, FALSE, L"Local\\NCMMini.ShowRequested");
    if (!showRequested)
    {
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 1;
    }
    HANDLE instance = CreateMutexW(nullptr, TRUE, L"Local\\NCMMini.Host");
    if (instance == nullptr)
    {
        CloseHandle(showRequested);
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (options.launchCloudMusic)
        {
            ncmmini::PlayerController(options).TryLaunch();
        }
        if (options.showBand)
        {
            SetEvent(showRequested);
        }
        CloseHandle(instance);
        CloseHandle(showRequested);
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 0;
    }

    const int result = ncmmini::Application(options, showRequested).Run();
    ReleaseMutex(instance);
    CloseHandle(instance);
    CloseHandle(showRequested);
    if (SUCCEEDED(comResult)) CoUninitialize();
    return result;
}
