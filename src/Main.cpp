#include <JuceHeader.h>
#include "CrashLog.h"
#include "ui/MainComponent.h"

class LiteHostApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "LiteHost"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
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

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), projectPath, testSyncRoom);
    }

    void shutdown() override
    {
        CrashLog::write ("LiteHost shutdown");
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        juce::ignoreUnused (commandLine);
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name, juce::String projectPath, bool testSyncRoom)
            : DocumentWindow (name, juce::Colour (0xff10131a), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            auto* content = new MainComponent (std::move (projectPath));
            setContentOwned (content, true);
            setResizable (true, true);
            setResizeLimits (820, 560, 10000, 10000);
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
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (LiteHostApplication)
