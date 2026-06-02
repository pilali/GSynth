/*
 * GSynth — JUCE AudioProcessor.
 *
 * Format-agnostic wrapper (VST3 / AU / Standalone) around the shared DSP core
 * in ../src/gsynth_dsp.{c,h}. The synth engine is monophonic (guitar in), so
 * audio is summed to mono, processed once, and fanned out to every output
 * channel. Parameters mirror the LV2 control ports one-for-one.
 */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "gsynth_dsp.h"

class GSynthAudioProcessor : public juce::AudioProcessor
{
public:
    GSynthAudioProcessor();
    ~GSynthAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    GSynthDsp* dsp = nullptr;
    double currentSampleRate = 0.0;

    juce::AudioBuffer<float> monoScratch;

    // cached raw parameter pointers (atomic, read on the audio thread)
    std::atomic<float>* pGuitar      = nullptr;
    std::atomic<float>* pOctave      = nullptr;
    std::atomic<float>* pSubOctave   = nullptr;
    std::atomic<float>* pSquare      = nullptr;
    std::atomic<float>* pAttackDelay = nullptr;
    std::atomic<float>* pStartFreq   = nullptr;
    std::atomic<float>* pStopFreq    = nullptr;
    std::atomic<float>* pResonance   = nullptr;
    std::atomic<float>* pFilterRate  = nullptr;
    std::atomic<float>* pTrigger     = nullptr;
    std::atomic<float>* pFilterType  = nullptr;
    std::atomic<float>* pPitchTrack  = nullptr;
    std::atomic<float>* pInputDrive  = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GSynthAudioProcessor)
};
