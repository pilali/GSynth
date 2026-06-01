#include "PluginProcessor.h"

namespace {
// Parameter IDs match the LV2 port symbols so state maps 1:1 with the LV2 build.
constexpr auto kGuitar      = "guitar";
constexpr auto kOctave      = "octave";
constexpr auto kSubOctave   = "sub_octave";
constexpr auto kSquare      = "square";
constexpr auto kAttackDelay = "attack_delay";
constexpr auto kStartFreq   = "start_freq";
constexpr auto kStopFreq    = "stop_freq";
constexpr auto kResonance   = "resonance";
constexpr auto kFilterRate  = "filter_rate";
constexpr auto kTrigger     = "trigger";
constexpr auto kFilterType  = "filter_type";
constexpr auto kPitchTrack  = "pitch_track";
constexpr auto kInputDrive  = "input_drive";

juce::NormalisableRange<float> unipolar()
{
    return { 0.0f, 1.0f };
}

juce::NormalisableRange<float> freqRange()
{
    juce::NormalisableRange<float> r{ 20.0f, 12000.0f };
    r.setSkewForCentre(1000.0f);   // logarithmic feel, matches lv2:logarithmic
    return r;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout
GSynthAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    const auto pid = [](const char* s) { return ParameterID{ s, 1 }; };

    layout.add(std::make_unique<AudioParameterFloat>(pid(kGuitar),    "Guitar",          unipolar(), 0.7f));
    layout.add(std::make_unique<AudioParameterFloat>(pid(kOctave),    "Octave (-1)",     unipolar(), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(pid(kSubOctave), "Sub Octave (-2)", unipolar(), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(pid(kSquare),    "Square Wave",     unipolar(), 0.0f));

    layout.add(std::make_unique<AudioParameterFloat>(
        pid(kAttackDelay), "Attack Delay",
        NormalisableRange<float>{ 0.0f, 2000.0f }, 0.0f,
        AudioParameterFloatAttributes{}.withLabel("ms")));

    layout.add(std::make_unique<AudioParameterFloat>(
        pid(kStartFreq), "Start Frequency", freqRange(), 200.0f,
        AudioParameterFloatAttributes{}.withLabel("Hz")));
    layout.add(std::make_unique<AudioParameterFloat>(
        pid(kStopFreq), "Stop Frequency", freqRange(), 2500.0f,
        AudioParameterFloatAttributes{}.withLabel("Hz")));

    layout.add(std::make_unique<AudioParameterFloat>(pid(kResonance),  "Resonance",           unipolar(), 0.3f));
    layout.add(std::make_unique<AudioParameterFloat>(pid(kFilterRate), "Filter Rate",         unipolar(), 0.5f));
    layout.add(std::make_unique<AudioParameterFloat>(pid(kTrigger),    "Trigger Sensitivity", unipolar(), 0.5f));

    layout.add(std::make_unique<AudioParameterChoice>(
        pid(kFilterType), "Filter Type",
        StringArray{ "SVF (clean)", "Moog Ladder" }, 1));
    layout.add(std::make_unique<AudioParameterChoice>(
        pid(kPitchTrack), "Pitch Track",
        StringArray{ "Schmitt + Flip-Flops", "YIN (CPU heavier)" }, 0));

    layout.add(std::make_unique<AudioParameterFloat>(pid(kInputDrive), "Input Drive", unipolar(), 0.25f));

    return layout;
}

GSynthAudioProcessor::GSynthAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createParameterLayout())
{
    pGuitar      = apvts.getRawParameterValue(kGuitar);
    pOctave      = apvts.getRawParameterValue(kOctave);
    pSubOctave   = apvts.getRawParameterValue(kSubOctave);
    pSquare      = apvts.getRawParameterValue(kSquare);
    pAttackDelay = apvts.getRawParameterValue(kAttackDelay);
    pStartFreq   = apvts.getRawParameterValue(kStartFreq);
    pStopFreq    = apvts.getRawParameterValue(kStopFreq);
    pResonance   = apvts.getRawParameterValue(kResonance);
    pFilterRate  = apvts.getRawParameterValue(kFilterRate);
    pTrigger     = apvts.getRawParameterValue(kTrigger);
    pFilterType  = apvts.getRawParameterValue(kFilterType);
    pPitchTrack  = apvts.getRawParameterValue(kPitchTrack);
    pInputDrive  = apvts.getRawParameterValue(kInputDrive);
}

GSynthAudioProcessor::~GSynthAudioProcessor()
{
    gsynth_dsp_free(dsp);
}

void GSynthAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    if (dsp == nullptr || sampleRate != currentSampleRate)
    {
        gsynth_dsp_free(dsp);
        dsp = gsynth_dsp_new(sampleRate);   // allocation lives outside the audio thread
        currentSampleRate = sampleRate;
    }
    if (dsp != nullptr)
        gsynth_dsp_reset(dsp);

    monoScratch.setSize(1, samplesPerBlock, false, false, true);
}

bool GSynthAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto mainOut = layouts.getMainOutputChannelSet();
    const auto mainIn  = layouts.getMainInputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono()
        && mainOut != juce::AudioChannelSet::stereo())
        return false;

    // Input must match the output, or be absent (instrument-style) — the engine
    // is mono internally either way.
    return mainIn == mainOut || mainIn.isDisabled();
}

void GSynthAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int totalIn    = getTotalNumInputChannels();
    const int totalOut   = getTotalNumOutputChannels();

    if (dsp == nullptr || numSamples == 0)
        return;

    const GSynthParams params {
        pGuitar->load(),      pOctave->load(),    pSubOctave->load(),
        pSquare->load(),      pAttackDelay->load(), pStartFreq->load(),
        pStopFreq->load(),    pResonance->load(), pFilterRate->load(),
        pTrigger->load(),     pFilterType->load(), pPitchTrack->load(),
        pInputDrive->load()
    };

    monoScratch.setSize(1, numSamples, false, false, true);
    float* mono = monoScratch.getWritePointer(0);

    // Sum the input down to mono (or silence if the plug-in has no input bus).
    if (totalIn > 0)
    {
        juce::FloatVectorOperations::copy(mono, buffer.getReadPointer(0), numSamples);
        for (int ch = 1; ch < totalIn; ++ch)
            juce::FloatVectorOperations::add(mono, buffer.getReadPointer(ch), numSamples);
        if (totalIn > 1)
            juce::FloatVectorOperations::multiply(mono, 1.0f / (float) totalIn, numSamples);
    }
    else
    {
        juce::FloatVectorOperations::clear(mono, numSamples);
    }

    gsynth_dsp_process(dsp, &params, mono, mono, (uint32_t) numSamples);

    // Fan the mono result out to every output channel.
    for (int ch = 0; ch < totalOut; ++ch)
        juce::FloatVectorOperations::copy(buffer.getWritePointer(ch), mono, numSamples);
}

juce::AudioProcessorEditor* GSynthAudioProcessor::createEditor()
{
    // Scaffold: auto-generated parameter editor. A custom fader UI mirroring the
    // MOD modgui is the next step.
    return new juce::GenericAudioProcessorEditor(*this);
}

void GSynthAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
}

void GSynthAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

// JUCE plug-in entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GSynthAudioProcessor();
}
