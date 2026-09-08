#include "AppModalDialog.h"
#include "LookAndFeel.h"
#include "Utf8.h"
#include <algorithm>
#include <cmath>

namespace
{
    /**
     * In-app modal (child of MainComponent). Same HWND as the main window so
     * keyboard focus works; title-bar close button included.
     */
    class AppModalOverlay final : public juce::Component,
                                  private juce::ComponentListener
    {
    public:
        explicit AppModalOverlay (juce::DialogWindow::LaunchOptions& options)
            : titleText (options.dialogTitle),
              panelColour (options.dialogBackgroundColour),
              escapeCloses (options.escapeKeyTriggersCloseButton)
        {
            setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
            setWantsKeyboardFocus (true);
            setMouseClickGrabsKeyboardFocus (false);
            getProperties().set ("appModalOverlay", true);

            content.reset (options.content.release());
            jassert (content != nullptr);
            addAndMakeVisible (*content);
            prepareTabStops (*content);

            closeButton.setButtonText ("x");
            closeButton.setTooltip (jp (u8"閉じる"));
            closeButton.setWantsKeyboardFocus (false);
            closeButton.onClick = [this] { exitModalState (0); };
            addAndMakeVisible (closeButton);

            contentSize = { content->getWidth(), content->getHeight() };
            if (contentSize.x <= 0 || contentSize.y <= 0)
                contentSize = { 420, 320 };
        }

        ~AppModalOverlay() override
        {
            if (auto* p = getParentComponent())
                p->removeComponentListener (this);
        }

        void parentHierarchyChanged() override
        {
            if (auto* p = getParentComponent())
            {
                p->addComponentListener (this);
                setBounds (p->getLocalBounds());
            }
        }

        void componentMovedOrResized (juce::Component& c, bool, bool wasResized) override
        {
            if (wasResized && &c == getParentComponent())
                setBounds (c.getLocalBounds());
        }

        void componentBeingDeleted (juce::Component& c) override
        {
            if (&c == getParentComponent())
                c.removeComponentListener (this);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::black.withAlpha (0.55f));

            const auto panel = getPanelBounds().toFloat();
            g.setColour (panelColour);
            g.fillRoundedRectangle (panel, 8.0f);
            g.setColour (juce::Colour (0xff3a4254));
            g.drawRoundedRectangle (panel, 8.0f, 1.0f);

            auto titleArea = getPanelBounds().removeFromTop (titleBarH);
            g.setColour (juce::Colour (LiteLookAndFeel::text));
            g.setFont (LiteLookAndFeel::uiFont (15.0f, juce::Font::bold));
            g.drawText (titleText, titleArea.withTrimmedRight (titleBarH + 8).reduced (14, 0),
                        juce::Justification::centredLeft, true);
        }

        void resized() override
        {
            const auto panel = getPanelBounds();
            closeButton.setBounds (panel.getRight() - titleBarH + 4, panel.getY() + 4,
                                   titleBarH - 8, titleBarH - 8);
            if (content != nullptr)
                content->setBounds (panel.withTrimmedTop (titleBarH));
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (escapeCloses && ! getPanelBounds().contains (e.getPosition()))
                exitModalState (0);
        }

        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key.isKeyCode (juce::KeyPress::tabKey))
                return cycleFocus (! key.getModifiers().isShiftDown());

            if (escapeCloses && key.isKeyCode (juce::KeyPress::escapeKey))
            {
                exitModalState (0);
                return true;
            }

            return false;
        }

        bool cycleFocus (bool forward)
        {
            // KEYDOWN + WM_CHAR can deliver Tab twice; ignore the duplicate.
            const auto now = juce::Time::getMillisecondCounter();
            if (now - lastTabMs < 80)
                return true;
            lastTabMs = now;

            juce::Array<juce::Component*> focusable;
            collectFocusable (content.get(), focusable);
            if (focusable.isEmpty())
                return false;

            auto* focused = juce::Component::getCurrentlyFocusedComponent();
            int index = -1;
            if (focused != nullptr)
            {
                for (int i = 0; i < focusable.size(); ++i)
                {
                    auto* c = focusable.getUnchecked (i);
                    if (c == focused || c->isParentOf (focused))
                    {
                        index = i;
                        break;
                    }
                }
            }

            const int n = focusable.size();
            const int next = index < 0 ? 0
                                       : (forward ? (index + 1) % n : (index - 1 + n) % n);
            auto* target = focusable.getUnchecked (next);
            target->grabKeyboardFocus();
            target->repaint();
            if (focused != nullptr)
                focused->repaint();
            return true;
        }

    private:
        static constexpr int titleBarH = 40;
        static constexpr int panelPad = 24;

        juce::Rectangle<int> getPanelBounds() const
        {
            const int w = juce::jmin (contentSize.x, juce::jmax (200, getWidth() - panelPad * 2));
            const int h = juce::jmin (contentSize.y + titleBarH,
                                     juce::jmax (160, getHeight() - panelPad * 2));
            return { (getWidth() - w) / 2, (getHeight() - h) / 2, w, h };
        }

        static void prepareTabStops (juce::Component& root)
        {
            for (int i = 0; i < root.getNumChildComponents(); ++i)
            {
                auto* c = root.getChildComponent (i);
                if (c == nullptr)
                    continue;

                if (dynamic_cast<juce::ComboBox*> (c) != nullptr
                    || dynamic_cast<juce::Button*> (c) != nullptr
                    || dynamic_cast<juce::ListBox*> (c) != nullptr
                    || dynamic_cast<juce::TreeView*> (c) != nullptr
                    || dynamic_cast<juce::TextEditor*> (c) != nullptr
                    || dynamic_cast<juce::FilenameComponent*> (c) != nullptr)
                {
                    c->setWantsKeyboardFocus (true);
                }
                else if (auto* slider = dynamic_cast<juce::Slider*> (c))
                {
                    // Prefer the numeric text box for Tab; keep the track mouse-only.
                    slider->setWantsKeyboardFocus (slider->getTextBoxPosition() == juce::Slider::NoTextBox);
                }
                else if (auto* label = dynamic_cast<juce::Label*> (c))
                {
                    const bool sliderTextBox = dynamic_cast<juce::Slider*> (label->getParentComponent()) != nullptr;
                    label->setWantsKeyboardFocus (sliderTextBox || label->isEditable());
                }

                prepareTabStops (*c);
            }
        }

        static bool isTabStop (juce::Component& c)
        {
            if (! c.isVisible() || ! c.isEnabled() || c.getWidth() <= 0 || c.getHeight() <= 0)
                return false;

            if (dynamic_cast<juce::Button*> (&c) != nullptr
                || dynamic_cast<juce::ComboBox*> (&c) != nullptr
                || dynamic_cast<juce::ListBox*> (&c) != nullptr
                || dynamic_cast<juce::TreeView*> (&c) != nullptr
                || dynamic_cast<juce::TextEditor*> (&c) != nullptr
                || dynamic_cast<juce::FilenameComponent*> (&c) != nullptr)
            {
                return c.getWantsKeyboardFocus();
            }

            if (auto* slider = dynamic_cast<juce::Slider*> (&c))
                return slider->getWantsKeyboardFocus()
                    && slider->getTextBoxPosition() == juce::Slider::NoTextBox;

            if (auto* label = dynamic_cast<juce::Label*> (&c))
            {
                if (dynamic_cast<juce::Slider*> (label->getParentComponent()) != nullptr)
                    return true;
                return label->isEditable() && label->getWantsKeyboardFocus();
            }

            return false;
        }

        static void collectFocusableRecursive (juce::Component* root, juce::Array<juce::Component*>& out)
        {
            if (root == nullptr)
                return;

            for (int i = 0; i < root->getNumChildComponents(); ++i)
            {
                auto* c = root->getChildComponent (i);
                if (c == nullptr || ! c->isVisible() || ! c->isEnabled())
                    continue;

                if (isTabStop (*c))
                    out.add (c);

                collectFocusableRecursive (c, out);
            }
        }

        static void collectFocusable (juce::Component* root, juce::Array<juce::Component*>& out)
        {
            collectFocusableRecursive (root, out);

            // Reading order: top → bottom, then left → right (same row ≈ 10px).
            struct RowCol
            {
                juce::Component* c;
                int row;
                int x;
            };

            juce::Array<RowCol> keyed;
            keyed.ensureStorageAllocated (out.size());
            for (auto* c : out)
            {
                const auto screen = c->getScreenBounds();
                keyed.add ({ c, screen.getY(), screen.getX() });
            }

            std::sort (keyed.begin(), keyed.end(), [] (const RowCol& a, const RowCol& b) {
                if (std::abs (a.row - b.row) > 10)
                    return a.row < b.row;
                if (a.x != b.x)
                    return a.x < b.x;
                return a.row < b.row;
            });

            out.clearQuick();
            for (auto& item : keyed)
                out.add (item.c);
        }

        juce::String titleText;
        juce::Colour panelColour;
        bool escapeCloses = true;
        juce::uint32 lastTabMs = 0;
        juce::Point<int> contentSize;
        std::unique_ptr<juce::Component> content;
        juce::TextButton closeButton;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppModalOverlay)
    };

}

namespace AppModalDialog
{
juce::Component* launch (juce::DialogWindow::LaunchOptions& options)
{
    auto* host = options.componentToCentreAround;
    if (host == nullptr)
        return nullptr;

    auto* overlay = new AppModalOverlay (options);
    host->addAndMakeVisible (overlay);
    overlay->setBounds (host->getLocalBounds());
    overlay->toFront (true);
    overlay->enterModalState (true, nullptr, true);

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<AppModalOverlay> (overlay)] {
        if (safe == nullptr)
            return;
        safe->toFront (true);
        safe->cycleFocus (true);
    });

    return overlay;
}


juce::Component* launchPanel (juce::Component* centreAround,
                              std::unique_ptr<juce::Component> content,
                              const juce::String& title,
                              bool resizable)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (content.release());
    options.dialogTitle = title;
    options.dialogBackgroundColour = juce::Colour (LiteLookAndFeel::surface);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.componentToCentreAround = centreAround;
    options.resizable = resizable;
    return launch (options);
}

bool forwardTabToAppModal (juce::Component& host, const juce::KeyPress& key)
{
    if (! key.isKeyCode (juce::KeyPress::tabKey))
        return false;

    for (int i = 0; i < host.getNumChildComponents(); ++i)
        if (auto* overlay = dynamic_cast<AppModalOverlay*> (host.getChildComponent (i)))
            return overlay->cycleFocus (! key.getModifiers().isShiftDown());

    return false;
}

bool nudgeFocusedSlider (const juce::KeyPress& key)
{
    const bool up = key.isKeyCode (juce::KeyPress::upKey) || key.isKeyCode (juce::KeyPress::rightKey);
    const bool down = key.isKeyCode (juce::KeyPress::downKey) || key.isKeyCode (juce::KeyPress::leftKey);
    if (! up && ! down)
        return false;

    if (key.getModifiers().isCommandDown() || key.getModifiers().isAltDown())
        return false;

    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (focused == nullptr)
        return false;

    auto* slider = dynamic_cast<juce::Slider*> (focused);
    if (slider == nullptr)
        slider = focused->findParentComponentOfClass<juce::Slider>();
    if (slider == nullptr)
        return false;

    double step = slider->getInterval();
    if (step <= 0.0)
        step = 0.1;
    if (key.getModifiers().isShiftDown())
        step *= 10.0;

    slider->setValue (slider->getValue() + (up ? step : -step), juce::sendNotificationSync);
    return true;
}

/** Ensure Enter-default (or first) Alert button actually has keyboard focus. */
void focusDefaultAlertButton (juce::AlertWindow& aw)
{
    juce::Button* fallback = nullptr;

    for (int i = 0; i < aw.getNumButtons(); ++i)
    {
        auto* b = aw.getButton (i);
        if (b == nullptr)
            continue;

        if (fallback == nullptr)
            fallback = b;

        if (b->isRegisteredForShortcut (juce::KeyPress (juce::KeyPress::returnKey)))
        {
            b->grabKeyboardFocus();
            return;
        }
    }

    if (fallback != nullptr)
        fallback->grabKeyboardFocus();
}

void showAlertAndFocusDefault (juce::AlertWindow* aw, juce::ModalComponentManager::Callback* callback)
{
    aw->enterModalState (true, callback, true);
    // Peer / modal focus transfer may run after this call — re-assert on next tick.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::AlertWindow> (aw)] {
        if (safe != nullptr)
            focusDefaultAlertButton (*safe);
    });
}

int nearestBufferSize (const juce::Array<int>& sizes, int preferred)
{
    if (sizes.isEmpty())
        return preferred;

    int best = sizes.getUnchecked (0);
    int bestDist = std::abs (best - preferred);

    for (auto size : sizes)
    {
        const int dist = std::abs (size - preferred);
        if (dist < bestDist || (dist == bestDist && size < best))
        {
            best = size;
            bestDist = dist;
        }
    }

    return best;
}
}
