#include "PluginEditor.h"

namespace {
struct FaderDef { const char* id; const char* label; };

// Same controls, order and grouping as the MOD modgui.
const std::vector<std::vector<FaderDef>> kGroups = {
    { { "guitar", "GTR" }, { "octave", "OCT" }, { "sub_octave", "SUB" }, { "square", "SQR" } },
    { { "trigger", "TRIG" }, { "attack_delay", "ATCK" }, { "start_freq", "STRT" },
      { "stop_freq", "STOP" }, { "resonance", "RES" }, { "filter_rate", "RATE" } },
    { { "input_drive", "DRV" }, { "filter_type", "FILT" }, { "pitch_track", "PTCH" } },
};
const char* kGroupNames[3] = { "VOICE MIX", "FILTER", "MISC" };

const juce::Colour kPedal      { 0xff0d0d0d };
const juce::Colour kText       { 0xffc8c8c8 };
const juce::Colour kBrand      { 0xffe0e0e0 };
const juce::Colour kGroove     { 0xff1c1c1c };

constexpr int kHeaderH    = 46;
constexpr int kFooterH    = 30;
constexpr int kGroupLblH  = 22;
constexpr int kShortLblH  = 16;
constexpr int kGroupGap   = 26;
constexpr int kSidePad    = 18;
} // namespace

// ===========================================================================
//  LookAndFeel
// ===========================================================================
GSynthLookAndFeel::GSynthLookAndFeel()
{
    setColour(juce::Label::textColourId, kText);
}

void GSynthLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float, float,
                                         juce::Slider::SliderStyle, juce::Slider&)
{
    const float cx     = (float) x + (float) width * 0.5f;
    const float top    = (float) y + 8.0f;
    const float bottom = (float) y + (float) height - 8.0f;

    // --- grooved track with graduation ticks ---
    const float grooveW = 6.0f;
    juce::Rectangle<float> groove(cx - grooveW * 0.5f, top, grooveW, bottom - top);
    g.setColour(kGroove);
    g.fillRoundedRectangle(groove, 2.0f);

    g.setColour(juce::Colour::fromRGBA(210, 210, 210, 26));
    const int ticks = 12;
    const float tickW = 16.0f;
    for (int i = 0; i <= ticks; ++i)
    {
        const float ty = top + (bottom - top) * (float) i / (float) ticks;
        g.fillRect(cx - tickW * 0.5f, ty, tickW, 1.0f);
    }

    // --- glossy silver cap (mirrors slider.png: bright bevel, dark body
    //     gradient, bright central grip stripe, dark borders, soft shadow) ---
    const float capW = juce::jmin((float) width * 0.84f, 46.0f);
    const float capH = 22.0f;
    const float capX = cx - capW * 0.5f;
    const float capY = sliderPos - capH * 0.5f;
    juce::Rectangle<float> cap(capX, capY, capW, capH);

    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillRoundedRectangle(cap.translated(0.0f, 2.0f), 4.0f);

    juce::ColourGradient grad(juce::Colour(0xffbebebe), 0.0f, capY,
                              juce::Colour(0xff2a2a2a), 0.0f, capY + capH, false);
    grad.addColour(0.18, juce::Colour(0xff707070));
    grad.addColour(0.55, juce::Colour(0xff5a5a5a));
    g.setGradientFill(grad);
    g.fillRoundedRectangle(cap, 4.0f);

    g.setColour(juce::Colour(0xffe1e1e1));
    g.fillRect(capX + 1.5f, capY + capH * 0.5f - 1.0f, capW - 3.0f, 2.0f);

    g.setColour(juce::Colour(0xff141414));
    g.drawRoundedRectangle(cap, 4.0f, 1.0f);
}

// ===========================================================================
//  Editor
// ===========================================================================
GSynthAudioProcessorEditor::GSynthAudioProcessorEditor(GSynthAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lnf);

    for (const auto& group : kGroups)
    {
        for (const auto& def : group)
        {
            auto slider = std::make_unique<juce::Slider>(juce::Slider::LinearVertical,
                                                         juce::Slider::NoTextBox);
            slider->setDoubleClickReturnValue(true, 0.0);
            addAndMakeVisible(*slider);
            attachments.push_back(
                std::make_unique<SliderAttachment>(processorRef.apvts, def.id, *slider));

            auto label = std::make_unique<juce::Label>(juce::String(), def.label);
            label->setJustificationType(juce::Justification::centred);
            label->setFont(juce::Font(juce::FontOptions().withHeight(11.0f).withStyle("Bold")));
            label->setColour(juce::Label::textColourId, kText.withAlpha(0.55f));
            label->setInterceptsMouseClicks(false, false);
            addAndMakeVisible(*label);

            sliders.push_back(std::move(slider));
            labels.push_back(std::move(label));
        }
    }

    setSize(730, 344);
}

GSynthAudioProcessorEditor::~GSynthAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void GSynthAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    headerArea = area.removeFromTop(kHeaderH);
    footerArea = area.removeFromBottom(kFooterH);

    auto body = area.reduced(kSidePad, 6);
    auto groupLabelRow = body.removeFromBottom(kGroupLblH);
    faderArea = body;

    const int nFaders   = (int) sliders.size();   // 13
    const int nGaps     = (int) kGroups.size() - 1;
    const int cellW     = (body.getWidth() - nGaps * kGroupGap) / nFaders;

    int runX = body.getX();
    int idx  = 0;
    for (size_t gi = 0; gi < kGroups.size(); ++gi)
    {
        const int groupStartX = runX;
        for (size_t ci = 0; ci < kGroups[gi].size(); ++ci, ++idx)
        {
            juce::Rectangle<int> cell(runX, body.getY(), cellW, body.getHeight());
            auto labelCell = cell.removeFromBottom(kShortLblH);
            sliders[(size_t) idx]->setBounds(cell.reduced(2, 0));
            labels[(size_t) idx]->setBounds(labelCell);
            runX += cellW;
        }

        groupLabelBounds[gi] = juce::Rectangle<int>(groupStartX, groupLabelRow.getY(),
                                                    runX - groupStartX, kGroupLblH);

        if (gi < kGroups.size() - 1)
        {
            separatorX[gi] = runX + kGroupGap / 2;
            runX += kGroupGap;
        }
    }
}

void GSynthAudioProcessorEditor::paint(juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();

    // pedal body
    g.setColour(kPedal);
    g.fillRoundedRectangle(full, 12.0f);
    g.setColour(juce::Colours::white.withAlpha(0.04f));
    g.drawRoundedRectangle(full.reduced(0.5f), 12.0f, 1.0f);

    // header
    auto header = headerArea.reduced(kSidePad, 0);
    g.setColour(kBrand);
    g.setFont(juce::Font(juce::FontOptions().withHeight(22.0f).withStyle("Bold Italic")));
    const int brandW = 96;
    g.drawText("GSynth", header.removeFromLeft(brandW),
               juce::Justification::centredLeft, false);
    g.setColour(kText.withAlpha(0.40f));
    g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f).withStyle("Bold")));
    g.drawText("GUITAR SYNTHESIZER", header,
               juce::Justification::centredLeft, false);
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.fillRect((float) kSidePad, (float) headerArea.getBottom(),
               (float) getWidth() - 2.0f * kSidePad, 1.0f);

    // group separators
    g.setColour(juce::Colours::white.withAlpha(0.07f));
    for (int sx : separatorX)
        g.fillRect((float) sx, (float) faderArea.getY() + 4.0f,
                   1.0f, (float) faderArea.getHeight() - 8.0f);

    // group labels
    g.setColour(kText.withAlpha(0.32f));
    g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f).withStyle("Bold")));
    for (size_t gi = 0; gi < groupLabelBounds.size(); ++gi)
        g.drawText(kGroupNames[gi], groupLabelBounds[gi],
                   juce::Justification::centred, false);

    // footer
    g.setColour(kText.withAlpha(0.30f));
    g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f).withStyle("Bold")));
    g.drawText("VOICE + FILTER", footerArea, juce::Justification::centred, false);
}
