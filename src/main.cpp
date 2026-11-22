#include <Geode/Geode.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <cmath>

// Cooked platform-specific definitions //
#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#elif defined(GEODE_IS_MACOS)
bool isGameFocused();
#endif

using namespace geode::prelude;

class MusicFocusEffect {
public:
    // Geode node fields don't work here //
    inline static float musicVolume = 0.f;
    inline static bool muted = false;
    inline static FMOD::DSP* lowPass = nullptr;
    inline static FMOD::ChannelGroup* filteredGroup = nullptr;

    static void removeLowPass() {
        if (lowPass) {
            filteredGroup->removeDSP(lowPass);
            lowPass->release();
            lowPass = nullptr;
            filteredGroup = nullptr;
        }
    }

    static void applyLowPass(FMODAudioEngine* audio) {
        removeLowPass();
        if (!audio->m_system || !audio->m_backgroundMusicChannel) return;

        FMOD::DSP* dsp = nullptr;
        auto cutoffMultiplier = Mod::get()->getSettingValue<double>("cutoff-multiplier");
        float cutoff = static_cast<float>(powf(cutoffMultiplier, 3.f) * 22000.0);
        if (cutoff < 1.f) cutoff = 1.f;

        auto result = audio->m_system->createDSPByType(FMOD_DSP_TYPE_MULTIBAND_EQ, &dsp);
        if (result == FMOD_OK) {
            result = dsp->setParameterInt(FMOD_DSP_MULTIBAND_EQ_A_FILTER, FMOD_DSP_MULTIBAND_EQ_FILTER_LOWPASS_24DB);
            if (result == FMOD_OK) result = dsp->setParameterFloat(FMOD_DSP_MULTIBAND_EQ_A_FREQUENCY, cutoff);
            if (result == FMOD_OK) result = audio->m_backgroundMusicChannel->addDSP(FMOD_CHANNELCONTROL_DSP_HEAD, dsp);
        }
        if (result != FMOD_OK) {
            if (dsp) dsp->release();
            log::warn("could not apply music low-pass filter: {}", static_cast<int>(result));
            return;
        }
        lowPass = dsp;
        filteredGroup = audio->m_backgroundMusicChannel;
    }

    static void unfocus() {
        if (muted) return;
        auto audio = FMODAudioEngine::sharedEngine();
        if (!muted) {
            musicVolume = audio->m_musicVolume;
            muted = true;
        }
        auto volumeMultiplier = Mod::get()->getSettingValue<double>("volume-multiplier");
        audio->setBackgroundMusicVolume(musicVolume * static_cast<float>(volumeMultiplier));
        applyLowPass(audio);
    }

    static void focus() {
        removeLowPass();

        if (muted) {
            FMODAudioEngine::sharedEngine()->setBackgroundMusicVolume(musicVolume);
            muted = false;
        }
    }
};

// AppDelegate does not appear to work properly, notably with borderless fullscreen "fix" applied //
class WindowObserver {
public:
    void reflectWindowFocus() {
        bool focused = true;
#ifdef GEODE_IS_WINDOWS
        DWORD foregroundProcess = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
        focused = foregroundProcess == GetCurrentProcessId();
#elif defined(GEODE_IS_MACOS)
        focused = isGameFocused();
#endif
        if (focused) {
            MusicFocusEffect::focus();
        } else {
            MusicFocusEffect::unfocus();
        }
    }
};
$on_game(Loaded) {
    GameManager::get()->schedule(schedule_selector(WindowObserver::reflectWindowFocus), .125f);
}