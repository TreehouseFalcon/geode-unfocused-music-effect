#include <Geode/Geode.hpp>
#include <cmath>
#include <optional>

// Cooked platform-specific definitions //
#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#elif defined(GEODE_IS_MACOS)
bool isGameFocused();
#endif

using namespace geode::prelude;

class MusicFocusEffect {
    using DSPClock = unsigned long long;

    struct Transition {
        DSPClock start;
        DSPClock end;
        bool filterFading;
    };

    inline static std::optional<Transition> transition;
    inline static bool focused = true;
    inline static FMOD::ChannelGroup* musicGroup = nullptr;
    inline static FMOD::DSP* lowPass = nullptr;

    static FMOD_RESULT scheduleTransition(FMOD::System* system, bool windowFocused) {
        int sampleRate = 0;
        auto result = system->getSoftwareFormat(&sampleRate, nullptr, nullptr);
        if (result != FMOD_OK) return result;

        auto durationMs = Mod::get()->getSettingValue<int64_t>("transition-time");
        auto duration = static_cast<DSPClock>(sampleRate * durationMs / 1000);
        float reducedGain = static_cast<float>(Mod::get()->getSettingValue<double>("volume-multiplier"));
        float startGain = windowFocused ? reducedGain : 1.f;
        float endGain = windowFocused ? 1.f : reducedGain;

        result = system->lockDSP();
        if (result != FMOD_OK) return result;

        // Clear the fade //
        DSPClock now = 0;
        result = musicGroup->getDSPClock(nullptr, &now);
        if (result == FMOD_OK && transition) {
            result = musicGroup->removeFadePoints(transition->start, transition->start);
            if (result == FMOD_OK && transition->end != transition->start) {
                result = musicGroup->removeFadePoints(transition->end, transition->end);
            }
        }

        // Initialize the fade //
        if (result == FMOD_OK) {
            result = musicGroup->addFadePoint(now, duration == 0 ? endGain : startGain);
            if (result == FMOD_OK) {
                if (duration != 0) result = musicGroup->addFadePoint(now + duration, endGain);
                bool scheduled = result == FMOD_OK;
                transition = Transition{now, scheduled ? now + duration : now, scheduled};
            }
        }

        auto unlockResult = system->unlockDSP();
        return result == FMOD_OK ? unlockResult : result;
    }

    static void removeLowPass() {
        if (lowPass) {
            musicGroup->removeDSP(lowPass);
            lowPass->release();
            lowPass = nullptr;
        }
    }

    static void applyLowPass(FMOD::System* system, float wet) {
        FMOD::DSP* dsp = nullptr;
        auto cutoffMultiplier = Mod::get()->getSettingValue<double>("cutoff-multiplier");
        float cutoff = static_cast<float>(powf(cutoffMultiplier, 3.f) * 22000.0);
        if (cutoff < 1.f) cutoff = 1.f;

        auto result = system->createDSPByType(FMOD_DSP_TYPE_MULTIBAND_EQ, &dsp);
        if (result == FMOD_OK) {
            result = dsp->setParameterInt(FMOD_DSP_MULTIBAND_EQ_A_FILTER, FMOD_DSP_MULTIBAND_EQ_FILTER_LOWPASS_24DB);
            if (result == FMOD_OK) result = dsp->setParameterFloat(FMOD_DSP_MULTIBAND_EQ_A_FREQUENCY, cutoff);
            if (result == FMOD_OK) result = dsp->setWetDryMix(1.f, wet, 1.f - wet);
            if (result == FMOD_OK) result = musicGroup->addDSP(FMOD_CHANNELCONTROL_DSP_HEAD, dsp);
        }
        if (result != FMOD_OK) {
            if (dsp) dsp->release();
            log::warn("could not apply music low-pass filter: {}", static_cast<int>(result));
            return;
        }
        lowPass = dsp;
    }

public:
    static void setFocused(bool windowFocused) {
        auto audio = FMODAudioEngine::sharedEngine();

        if (musicGroup != audio->m_backgroundMusicChannel) {
            removeLowPass();
            musicGroup = audio->m_backgroundMusicChannel;
            transition.reset();
            focused = true;
        }
        if (focused == windowFocused || !audio->m_system || !musicGroup) return;

        auto result = scheduleTransition(audio->m_system, windowFocused);
        if (result != FMOD_OK) {
            log::warn("could not schedule music focus fade: {}", static_cast<int>(result));
            return;
        }

        focused = windowFocused;
        if (!focused && !lowPass) {
            applyLowPass(audio->m_system, transition->start == transition->end ? 1.f : 0.f);
        }
        updateLowPass();
    }

    static void updateLowPass() {
        if (!lowPass || !transition || !transition->filterFading) return;
        if (FMODAudioEngine::sharedEngine()->m_backgroundMusicChannel != musicGroup) return;

        auto& fade = *transition;
        DSPClock now = fade.end;
        if (fade.start != fade.end && musicGroup->getDSPClock(nullptr, &now) != FMOD_OK) return;

        bool finished = now >= fade.end;
        float progress = 1.f;
        if (!finished) {
            progress = now <= fade.start ? 0.f : static_cast<float>(now - fade.start) / static_cast<float>(fade.end - fade.start);
        }
        float wet = focused ? 1.f - progress : progress;
        if (finished && focused) {
            removeLowPass();
        } else if (lowPass->setWetDryMix(1.f, wet, 1.f - wet) == FMOD_OK && finished) {
            fade.filterFading = false;
        }
    }
};

// AppDelegate does not appear to work properly, notably with borderless fullscreen "fix" applied //
class WindowObserver {
public:
    void reflectWindowFocus(float) {
        bool focused = true;
#ifdef GEODE_IS_WINDOWS
        DWORD foregroundProcess = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
        focused = foregroundProcess == GetCurrentProcessId();
#elif defined(GEODE_IS_MACOS)
        focused = isGameFocused();
#endif
        MusicFocusEffect::setFocused(focused);
    }

    void updateLowPass(float) {
        MusicFocusEffect::updateLowPass();
    }
};
$on_game(Loaded) {
    GameManager::get()->schedule(schedule_selector(WindowObserver::reflectWindowFocus), .125f);
    GameManager::get()->schedule(schedule_selector(WindowObserver::updateLowPass));
}
