#include <JuceHeader.h>
#include "CrashLog.h"
#include "app/AppPaths.h"
#include "ui/MainComponent.h"
#include "ui/StartupSplash.h"
#include "Utf8.h"
#include "BinaryData.h"

class LiteHostApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "LiteHost"; }
    const juce::String getApplicationVersion() override { return "0.2.6"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        CrashLog::install();
        CrashLog::clear();
        CrashLog::write ("LiteHost start");

        AppPaths::preparePluginScannerForLaunch();

        // High priority can interact badly with SyncRoom's .NET process launch.
        juce::Process::setPriority (juce::Process::NormalPriority);
        juce::FloatVectorOperations::disableDenormalisedNumberSupport();

        const auto args = parseCommandLine (commandLine);

        splash = std::make_unique<StartupSplashWindow>();
        splash->setStatus (jp (u8"起動しています..."), 0.04);
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), args.projectPath, args.testSyncRoom, splash.get());
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
        if (mainWindow == nullptr)
            return;

        if (mainWindow->isMinimised())
            mainWindow->setMinimised (false);
        mainWindow->toFront (true);

        const auto args = parseCommandLine (commandLine);
        if (args.projectPath.isEmpty())
            return;

        if (auto* content = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
            content->openProjectFromOutside (juce::File (args.projectPath));
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
    struct LaunchArgs
    {
        juce::String projectPath;
        bool testSyncRoom = false;
    };

    static LaunchArgs parseCommandLine (const juce::String& commandLine)
    {
        LaunchArgs result;
        if (commandLine.isEmpty())
            return result;

        juce::ArgumentList args ("LiteHost", commandLine);
        for (const auto& arg : args.arguments)
        {
            const auto token = arg.text.unquoted();
            if (token.equalsIgnoreCase ("--test-syncroom"))
            {
                result.testSyncRoom = true;
                continue;
            }

            if (token.endsWithIgnoreCase (".litehost") || token.endsWithIgnoreCase (".xml"))
            {
                result.projectPath = token;
                continue;
            }

            const juce::File asFile (token);
            if (asFile.existsAsFile())
                result.projectPath = asFile.getFullPathName();
        }
        return result;
    }

    std::unique_ptr<StartupSplashWindow> splash;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (LiteHostApplication)
