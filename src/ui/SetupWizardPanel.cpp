#include "SetupWizardPanel.h"
#include "LookAndFeel.h"
#include "Utf8.h"

SetupWizardPanel::SetupWizardPanel (juce::AudioDeviceManager& devices,
                                    juce::StringArray defaultsIn,
                                    juce::StringArray extrasIn)
    : deviceSelector (devices, 0, 64, 2, 8, true, false, true, false),
      folderList (std::move (defaultsIn), std::move (extrasIn))
{
    stepTitle.setFont (LiteLookAndFeel::uiFont (18.0f, juce::Font::bold));
    stepTitle.setColour (juce::Label::textColourId, juce::Colour (LiteLookAndFeel::text));
    addAndMakeVisible (stepTitle);

    stepBody.setFont (LiteLookAndFeel::uiFont (14.0f));
    stepBody.setColour (juce::Label::textColourId, juce::Colour (LiteLookAndFeel::muted));
    stepBody.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (stepBody);

    progress.setColour (juce::Label::textColourId, juce::Colour (LiteLookAndFeel::muted));
    progress.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (progress);

    addAndMakeVisible (deviceSelector);
    deviceSelector.setVisible (false);

    addAndMakeVisible (folderList);
    folderList.setVisible (false);

    addFolder.setButtonText (jp (u8"フォルダを追加"));
    addFolder.onClick = [this] { folderList.browseAndAdd(); };
    addAndMakeVisible (addFolder);

    removeFolder.setButtonText (jp (u8"選択を削除"));
    removeFolder.onClick = [this] { folderList.removeSelected(); };
    addAndMakeVisible (removeFolder);

    scanNow.setButtonText (jp (u8"スキャンして次へ"));
    scanNow.onClick = [this] { leaveVstPage (true); };
    addAndMakeVisible (scanNow);

    insertSyncRoom.setButtonText (jp (u8"探してメインに挿す"));
    insertSyncRoom.setClickingTogglesState (false);
    insertSyncRoom.onClick = [this] {
        wantSyncRoom = true;
        wantSyncRoomToggle.setToggleState (true, juce::dontSendNotification);
        if (onTryInsertSyncRoom && onTryInsertSyncRoom())
            goTo (4);
    };
    addAndMakeVisible (insertSyncRoom);

    wantSyncRoomToggle.setButtonText (jp (u8"メインアウトに SyncRoom を挿す"));
    wantSyncRoomToggle.setClickingTogglesState (true);
    wantSyncRoomToggle.setToggleState (true, juce::dontSendNotification);
    wantSyncRoomToggle.onClick = [this] {
        wantSyncRoom = wantSyncRoomToggle.getToggleState();
    };
    addAndMakeVisible (wantSyncRoomToggle);

    back.setButtonText (jp (u8"戻る"));
    back.onClick = [this] { goTo (page - 1); };
    addAndMakeVisible (back);

    next.setButtonText (jp (u8"次へ"));
    next.onClick = [this] {
        if (page == 2)
        {
            leaveVstPage (false);
            return;
        }

        if (page == 3)
        {
            wantSyncRoom = wantSyncRoomToggle.getToggleState();
            if (wantSyncRoom)
            {
                if (onTryInsertSyncRoom)
                    onTryInsertSyncRoom();
            }
            goTo (4);
            return;
        }

        if (page >= 4)
            finishWizard();
        else
            goTo (page + 1);
    };
    addAndMakeVisible (next);

    skip.setButtonText (jp (u8"スキップ"));
    skip.onClick = [this] { finishWizard(); };
    addAndMakeVisible (skip);

    goTo (0);
}

SetupWizardPanel::~SetupWizardPanel()
{
    if (! finished && onAbandoned)
        onAbandoned();
}

void SetupWizardPanel::resized()
{
    auto r = getLocalBounds().reduced (20);
    auto header = r.removeFromTop (28);
    progress.setBounds (header.removeFromRight (90));
    stepTitle.setBounds (header);

    auto footer = r.removeFromBottom (40);
    skip.setBounds (footer.removeFromLeft (100).reduced (2));
    next.setBounds (footer.removeFromRight (110).reduced (2));
    back.setBounds (footer.removeFromRight (100).reduced (2));
    r.removeFromBottom (8);

    if (page == 1)
    {
        stepBody.setBounds (r.removeFromTop (48));
        r.removeFromTop (6);
        deviceSelector.setBounds (r);
    }
    else if (page == 2)
    {
        stepBody.setBounds (r.removeFromTop (52));
        r.removeFromTop (6);
        auto folderButtons = r.removeFromBottom (36);
        addFolder.setBounds (folderButtons.removeFromLeft (130).reduced (2));
        removeFolder.setBounds (folderButtons.removeFromLeft (110).reduced (2));
        scanNow.setBounds (folderButtons.removeFromRight (140).reduced (2));
        folderList.setBounds (r);
    }
    else if (page == 3)
    {
        stepBody.setBounds (r.removeFromTop (100));
        r.removeFromTop (10);
        wantSyncRoomToggle.setBounds (r.removeFromTop (28));
        r.removeFromTop (8);
        insertSyncRoom.setBounds (r.removeFromTop (36).removeFromLeft (200));
    }
    else
    {
        stepBody.setBounds (r);
    }
}

void SetupWizardPanel::finishWizard()
{
    if (finished)
        return;
    finished = true;
    if (onFinished)
        onFinished();
}

void SetupWizardPanel::leaveVstPage (bool startScan)
{
    if (onCommitVstFolders)
        onCommitVstFolders (folderList.getExtras(), startScan);
    goTo (3);
}

void SetupWizardPanel::goTo (int newPage)
{
    page = juce::jlimit (0, 4, newPage);

    switch (page)
    {
        case 0:
            stepTitle.setText (jp (u8"LiteHost へようこそ"), juce::dontSendNotification);
            stepBody.setText (jp (u8"DAW ほど大げさではなく、入力を選んで VST を挿して鳴らすだけのホストです。\n"
                                  u8"最初にオーディオ機器とプラグイン一覧を整えておきましょう（あとからいつでも変更できます）。"),
                              juce::dontSendNotification);
            break;
        case 1:
            stepTitle.setText (jp (u8"オーディオと MIDI"), juce::dontSendNotification);
            stepBody.setText (jp (u8"Windows では ASIO ドライバとバッファ 128 前後がおすすめです。\n"
                                  u8"途切れるときはバッファを 256 などに上げてください。演奏用 MIDI 入力もここで有効にできます。"),
                              juce::dontSendNotification);
            break;
        case 2:
            stepTitle.setText (jp (u8"VST3 フォルダ"), juce::dontSendNotification);
            stepBody.setText (jp (u8"標準は Common Files\\VST3 のみです。必要なフォルダを追加してからスキャンしてください。\n"
                                  u8"「次へ」だけ進むとスキャンはしません（あとからヘッダーの VST3 スキャンでも可）。"),
                              juce::dontSendNotification);
            break;
        case 3:
            stepTitle.setText (jp (u8"SyncRoom"), juce::dontSendNotification);
            stepBody.setText (jp (u8"オンラインセッション用に、メインアウトへ SyncRoom の VST を挿せます。\n"
                                  u8"直前で指定したフォルダ（と標準パス）から探します。見つからない場合は挿さず、メッセージだけ出します。"),
                              juce::dontSendNotification);
            wantSyncRoomToggle.setToggleState (wantSyncRoom, juce::dontSendNotification);
            break;
        default:
            stepTitle.setText (jp (u8"準備完了"), juce::dontSendNotification);
            stepBody.setText (jp (u8"使い方の要点:\n"
                                  u8"・トラックで入力（オーディオ ch または MIDI）を選ぶ\n"
                                  u8"・+ VST でエフェクト／音源を挿す（ダブルクリックでエディタ）\n"
                                  u8"・メインアウトに SyncRoom などを挿せる\n"
                                  u8"・サーフェス／MIDI 学習はヘッダーから設定\n\n"
                                  u8"このウィザードは「ヘルプ」メニューから再度開けます。"),
                              juce::dontSendNotification);
            break;
    }

    progress.setText (juce::String (page + 1) + " / 5", juce::dontSendNotification);

    deviceSelector.setVisible (page == 1);
    folderList.setVisible (page == 2);
    addFolder.setVisible (page == 2);
    removeFolder.setVisible (page == 2);
    scanNow.setVisible (page == 2);
    wantSyncRoomToggle.setVisible (page == 3);
    insertSyncRoom.setVisible (page == 3);

    back.setEnabled (page > 0);
    next.setButtonText (page >= 4 ? jp (u8"完了") : jp (u8"次へ"));
    skip.setVisible (page < 4);

    resized();
    repaint();
}
