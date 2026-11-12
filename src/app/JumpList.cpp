#include "JumpList.h"
#include "Utf8.h"

#if JUCE_WINDOWS

 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <objbase.h>
 #include <objectarray.h>
 #include <propsys.h>
 #include <shobjidl.h>
 #include <wrl/client.h>
 #include <cstring>
 #include <cwchar>
 #include <iterator>

namespace
{
    using Microsoft::WRL::ComPtr;

    // PKEY_Title, declared here so propsys.lib isn't needed.
    constexpr PROPERTYKEY titleKey { { 0xF29F85E0, 0x4FF9, 0x1068, { 0xAB, 0x91, 0x08, 0x00, 0x2B, 0x27, 0xB3, 0xD9 } }, 2 };

    juce::String quotedArgument (const juce::File& file)
    {
        return "\"" + file.getFullPathName() + "\"";
    }

    bool setLinkTitle (IShellLinkW& link, const juce::String& title)
    {
        ComPtr<IPropertyStore> store;
        if (FAILED (link.QueryInterface (IID_PPV_ARGS (&store))))
            return false;

        const auto wide = title.toWideCharPointer();
        const auto bytes = (std::wcslen (wide) + 1) * sizeof (wchar_t);

        PROPVARIANT value;
        PropVariantInit (&value);
        value.vt = VT_LPWSTR;
        value.pwszVal = static_cast<LPWSTR> (CoTaskMemAlloc (bytes));
        if (value.pwszVal == nullptr)
            return false;
        std::memcpy (value.pwszVal, wide, bytes);

        const bool ok = SUCCEEDED (store->SetValue (titleKey, value)) && SUCCEEDED (store->Commit());
        PropVariantClear (&value);
        return ok;
    }

    ComPtr<IShellLinkW> makeProjectLink (const juce::File& exe, const juce::File& project)
    {
        ComPtr<IShellLinkW> link;
        if (FAILED (CoCreateInstance (CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS (&link))))
            return {};

        link->SetPath (exe.getFullPathName().toWideCharPointer());
        link->SetArguments (quotedArgument (project).toWideCharPointer());
        link->SetWorkingDirectory (exe.getParentDirectory().getFullPathName().toWideCharPointer());
        link->SetDescription (project.getFullPathName().toWideCharPointer());
        link->SetIconLocation (exe.getFullPathName().toWideCharPointer(), 0);

        if (! setLinkTitle (*link.Get(), project.getFileNameWithoutExtension()))
            return {};

        return link;
    }

    /** Arguments of items the user removed from the Jump List; re-adding them makes AppendCategory fail. */
    juce::StringArray removedArguments (IObjectArray* removed)
    {
        juce::StringArray result;
        UINT count = 0;
        if (removed == nullptr || FAILED (removed->GetCount (&count)))
            return result;

        for (UINT i = 0; i < count; ++i)
        {
            ComPtr<IShellLinkW> link;
            if (FAILED (removed->GetAt (i, IID_PPV_ARGS (&link))))
                continue;

            wchar_t args[MAX_PATH * 2] {};
            if (SUCCEEDED (link->GetArguments (args, (int) std::size (args))))
                result.add (juce::String (args));
        }
        return result;
    }

    struct ComScope
    {
        ComScope() : hr (CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED)) {}
        ~ComScope()
        {
            if (SUCCEEDED (hr))
                CoUninitialize();
        }
        HRESULT hr;
    };
}

void JumpList::setRecentProjects (const juce::Array<juce::File>& projects)
{
    const ComScope com;

    ComPtr<ICustomDestinationList> list;
    if (FAILED (CoCreateInstance (CLSID_DestinationList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS (&list))))
        return;

    UINT maxSlots = 0;
    ComPtr<IObjectArray> removed;
    if (FAILED (list->BeginList (&maxSlots, IID_PPV_ARGS (&removed))))
        return;

    const auto removedArgs = removedArguments (removed.Get());
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

    ComPtr<IObjectCollection> items;
    if (FAILED (CoCreateInstance (CLSID_EnumerableObjectCollection, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS (&items))))
    {
        list->AbortList();
        return;
    }

    UINT added = 0;
    for (const auto& project : projects)
    {
        if (added >= maxSlots)
            break;
        if (! project.existsAsFile() || removedArgs.contains (quotedArgument (project)))
            continue;

        if (auto link = makeProjectLink (exe, project))
            if (SUCCEEDED (items->AddObject (link.Get())))
                ++added;
    }

    if (added > 0)
    {
        ComPtr<IObjectArray> array;
        // E_ACCESSDENIED here means the user turned off "recently opened items"; commit an empty list then.
        if (SUCCEEDED (items.As (&array)))
            list->AppendCategory (jp (u8"最近使ったプロジェクト").toWideCharPointer(), array.Get());
    }

    list->CommitList();
}

#else

void JumpList::setRecentProjects (const juce::Array<juce::File>&) {}

#endif
