#include <JuceHeader.h>
#include "CrashLog.h"
#include "ui/MainComponent.h"
#include "ui/StartupSplash.h"
#include "Utf8.h"
#include "BinaryData.h"

class LiteHostApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "LiteHost"; }
    const juce::String getApplicationVersion() override { return "0.1.5"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        CrashLog::install();
        CrashLog::clear();
        CrashLog::write ("LiteHost start");

        // High priority can interact badly with SyncRoom's .NET process launch.
        juce::Process::setPriority (juce::Process::NormalPriority);
        juce::FloatVectorOperations::disableDenormalisedNumberSupport();

        juce::String projectPath;
        bool testSyncRoom = false;

        if (commandLine.isNotEmpty())
        {
            juce::ArgumentList args ("LiteHost", commandLine);
            for (const auto& arg : args.arguments)
            {
                const auto token = arg.text.unquoted();
                if (token.equalsIgnoreCase ("--test-syncroom"))
                {
                    testSyncRoom = true;
                    continue;
                }

                if (token.endsWithIgnoreCase (".litehost") || token.endsWithIgnoreCase (".xml"))
                {
                    projectPath = token;
                    continue;
                }

                const juce::File asFile (token);
                if (asFile.existsAsFile())
                    projectPath = asFile.getFullPathName();
            }
        }

        splash = std::make_unique<StartupSplashWindow>();
        splash->setStatus (jp (u8"起動しています..."), 0.04);
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), projectPath, testSyncRoom, splash.get());
        splash.reset();
    }

    void shutdown() override
    {
        CrashLog::write ("LiteHost shutdown");
        splash.reset();
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
        {
            if (auto* content = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
            {
                // requestQuit() returns false when a confirm dialog is shown (or cancelled).
                // It will call quit() itself if the user confirms.
                if (! content->requestQuit())
                    return;
            }
        }

        quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        juce::ignoreUnused (commandLine);
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name, juce::String projectPath, bool testSyncRoom, StartupProgress* progress)
            : DocumentWindow (name, juce::Colour (0xff10131a), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            auto icon = juce::ImageFileFormat::loadFrom (BinaryData::icon_png, BinaryData::icon_pngSize);
            if (icon.isValid())
                setIcon (icon);

            auto* content = new MainComponent (std::move (projectPath), progress);
            setContentOwned (content, true);
            setResizable (true, true);
            setResizeLimits (820, 560, 10000, 10000);
            if (! content->applySavedWindowState (*this))
                centreWithSize (980, 720);
            setVisible (true);

            if (testSyncRoom)
                content->runSyncRoomLoadTest();
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

private:
    std::unique_ptr<StartupSplashWindow> splash;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (LiteHostApplication)
